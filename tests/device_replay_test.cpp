// 步骤 10/11 实机验证（可选）：记录 → 回放，经自发自收回环比对序列
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

#include "core/CanDevice.h"
#include "core/Recorder.h"
#include "core/Replayer.h"
#include "core/RxManager.h"
#include "model/FrameListModel.h"

namespace {

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

FrameData mkFrame(quint32 id, bool extended, bool fd, bool brs, const QByteArray &data, qint64 wallUs)
{
    FrameData f;
    f.wallUs = wallUs;
    f.hostUs = wallUs;
    f.id = id;
    f.extended = extended;
    f.fd = fd;
    f.brs = brs;
    f.len = static_cast<quint8>(data.size());
    f.data = data;
    f.tx = true;
    f.echo = true;
    return f;
}

bool modelHas(const QAbstractItemModel *model, const QString &idText, const QString &dataText)
{
    for (int r = 0; r < model->rowCount(); ++r) {
        const QModelIndex idx = model->index(r, 0);
        if (model->data(idx, FrameListModel::IdRole).toString() == idText
            && model->data(idx, FrameListModel::DataRole).toString() == dataText)
            return true;
    }
    return false;
}

// 本机 VCI_Transmit 在接收线程并发时可能阻塞数秒，故轮询等待自收帧出现
bool waitForFrame(const QAbstractItemModel *model, const QString &idText,
                  const QString &dataText, int timeoutMs)
{
    const int step = 300;
    for (int t = 0; t < timeoutMs; t += step) {
        if (modelHas(model, idText, dataText))
            return true;
        wait(step);
    }
    return modelHas(model, idText, dataText);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    CanDevice dev;
    if (!dev.open()) {
        std::fprintf(stderr, "打开设备失败（需 USB 权限）\n");
        return 2;
    }
    if (dev.configureChannel(0, 500000, 2000000, 87.5, false).isEmpty()) {
        std::fprintf(stderr, "通道配置失败\n");
        dev.close();
        return 3;
    }
    dev.setTerminalResistor(0, true);
    if (!dev.startChannel(0)) {
        std::fprintf(stderr, "启动通道失败\n");
        dev.close();
        return 4;
    }

    RxManager mgr(&dev);
    if (!mgr.start(QVariantList{0}, false)) {
        std::fprintf(stderr, "接收管线启动失败\n");
        dev.close();
        return 5;
    }
    wait(200);
    mgr.clear();

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::fprintf(stderr, "临时目录不可用\n");
        mgr.stop();
        dev.close();
        return 6;
    }
    const QString ascPath = dir.filePath(QStringLiteral("replay.asc"));
    const QString csvPath = dir.filePath(QStringLiteral("replay.csv"));

    // ---- 记录一份已知序列 ----
    const QByteArray canData = QByteArray::fromHex("0102030405060708");
    const QByteArray fdData = QByteArray::fromHex("112233445566778899AABBCC");
    const QString canDataText = QStringLiteral("01 02 03 04 05 06 07 08");
    const QString fdDataText = QStringLiteral("11 22 33 44 55 66 77 88 99 AA BB CC");

    Recorder rec;
    const bool recStarted = rec.startWithPaths(ascPath, csvPath);
    const qint64 base = QDateTime::currentMSecsSinceEpoch() * 1000;
    rec.enqueueFrame(mkFrame(0x123, false, false, false, canData, base + 1000));
    rec.enqueueFrame(mkFrame(0x1ABCDEF0, true, true, true, fdData, base + 11000));
    rec.stop();
    std::printf("记录: 启动=%s 写入=%llu 丢弃=%llu 错误=%s\n",
                recStarted ? "成功" : "失败",
                static_cast<unsigned long long>(rec.writtenCount()),
                static_cast<unsigned long long>(rec.droppedCount()),
                rec.errorString().toUtf8().constData());

    QAbstractItemModel *model = mgr.frameModel();

    // ---- 回放 ASC ----
    Replayer rp(&dev);
    const bool ascLoaded = rp.load(ascPath);
    const bool ascPlayed = ascLoaded && rp.play(0);
    const bool canSeen = waitForFrame(model, QStringLiteral("0x123"), canDataText, 20000);
    const bool fdSeen = waitForFrame(model, QStringLiteral("0x1ABCDEF0"), fdDataText, 20000);
    std::printf("回放(ASC): 加载=%s 帧数=%d 播放=%s CAN=%s FD=%s 错误=%s\n",
                ascLoaded ? "成功" : "失败", rp.frameCount(), ascPlayed ? "成功" : "失败",
                canSeen ? "收到" : "未收到", fdSeen ? "收到" : "未收到",
                rp.errorString().toUtf8().constData());

    // ---- 回放 CSV ----
    mgr.clear();
    Replayer rp2(&dev);
    const bool csvLoaded = rp2.load(csvPath);
    const bool csvPlayed = csvLoaded && rp2.play(0);
    const bool canSeen2 = waitForFrame(model, QStringLiteral("0x123"), canDataText, 20000);
    const bool fdSeen2 = waitForFrame(model, QStringLiteral("0x1ABCDEF0"), fdDataText, 20000);
    std::printf("回放(CSV): 加载=%s 帧数=%d 播放=%s CAN=%s FD=%s\n",
                csvLoaded ? "成功" : "失败", rp2.frameCount(), csvPlayed ? "成功" : "失败",
                canSeen2 ? "收到" : "未收到", fdSeen2 ? "收到" : "未收到");

    mgr.stop();
    dev.resetChannel(0);
    dev.close();

    const bool ok = recStarted && rec.writtenCount() == 2 && ascLoaded && ascPlayed && canSeen
                    && fdSeen && csvLoaded && csvPlayed && canSeen2 && fdSeen2;
    std::printf("结论: %s\n", ok ? "通过" : "未通过");
    return ok ? 0 : 7;
}
