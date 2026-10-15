// L3 实机：双通道并发收发（风险 2）—— 句柄串行化下的通道归属
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVariantList>
#include <cstdio>
#include <cstring>

#include "core/CanDevice.h"
#include "core/RxManager.h"
#include "model/FrameListModel.h"

namespace {

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool sendCan(CanDevice &dev, quint32 port, quint32 id)
{
    ZCAN_20_MSG tx;
    std::memset(&tx, 0, sizeof(tx));
    tx.hdr.inf.txm = ZCAN_SR_NORM;   // 自发自收
    tx.hdr.inf.echo = 1;
    tx.hdr.id = id;
    tx.hdr.chn = static_cast<U8>(port);
    tx.hdr.len = 4;
    for (int b = 0; b < 4; ++b)
        tx.dat[b] = static_cast<quint8>(port * 16 + b);
    return dev.transmitCan(port, tx);
}

// 统计 model 中指定 ID 且通道匹配的行数
int countIdOnChannel(const QAbstractItemModel *m, const QString &idText, int channel)
{
    if (!m)
        return -1;
    int n = 0;
    for (int r = 0; r < m->rowCount(); ++r) {
        const QModelIndex idx = m->index(r, 0);
        if (m->data(idx, FrameListModel::IdRole).toString() != idText)
            continue;
        if (m->data(idx, FrameListModel::ChannelRole).toInt() == channel)
            ++n;
    }
    return n;
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
    if (dev.channels() < 2) {
        std::fprintf(stderr, "设备通道数不足 2（channels=%d）\n", dev.channels());
        dev.close();
        return 3;
    }

    const bool cfg0 = !dev.configureChannel(0, 500000, 2000000, 87.5, false).isEmpty();
    const bool cfg1 = !dev.configureChannel(1, 500000, 2000000, 87.5, false).isEmpty();
    dev.setTerminalResistor(0, true);
    dev.setTerminalResistor(1, true);
    const bool start0 = dev.startChannel(0);
    const bool start1 = dev.startChannel(1);
    std::printf("通道配置: ch0=%s ch1=%s 启动: ch0=%s ch1=%s\n",
                cfg0 ? "成功" : "失败", cfg1 ? "成功" : "失败",
                start0 ? "成功" : "失败", start1 ? "成功" : "失败");
    if (!cfg0 || !cfg1 || !start0 || !start1) {
        dev.close();
        return 4;
    }

    RxManager mgr(&dev);
    if (!mgr.start(QVariantList{0, 1}, false)) {
        std::fprintf(stderr, "双通道接收管线启动失败\n");
        dev.close();
        return 5;
    }
    wait(300);
    dev.clearBuffer(0);
    dev.clearBuffer(1);
    mgr.clear();

    const bool s0 = sendCan(dev, 0, 0x400);
    const bool s1 = sendCan(dev, 1, 0x401);
    // 分多次轮询，规避 VCI_Transmit 在并发接收时的阻塞
    int ch0Frames = 0;
    int ch1Frames = 0;
    for (int i = 0; i < 20; ++i) {
        wait(300);
        QAbstractItemModel *m = mgr.frameModel();
        ch0Frames = countIdOnChannel(m, QStringLiteral("0x400"), 0);
        ch1Frames = countIdOnChannel(m, QStringLiteral("0x401"), 1);
        if (ch0Frames > 0 && ch1Frames > 0)
            break;
    }

    QAbstractItemModel *m = mgr.frameModel();
    const int total = m ? m->rowCount() : -1;
    std::printf("并发收发: 发送ch0=%s ch1=%s | 0x400@ch0=%d 0x401@ch1=%d 模型行=%d\n",
                s0 ? "成功" : "失败", s1 ? "成功" : "失败", ch0Frames, ch1Frames, total);

    mgr.stop();
    dev.resetChannel(0);
    dev.resetChannel(1);
    dev.close();

    const bool ok = s0 && s1 && ch0Frames > 0 && ch1Frames > 0;
    std::printf("结论: %s\n", ok ? "通过" : "未通过");
    return ok ? 0 : 6;
}
