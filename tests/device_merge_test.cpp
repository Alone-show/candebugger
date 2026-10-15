// L3 实机：合并接收模式（风险 3）—— setRecvMerge + VCI_ReceiveData 全链路
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

bool sendCan(CanDevice &dev, quint32 id, bool extended = false)
{
    ZCAN_20_MSG tx;
    std::memset(&tx, 0, sizeof(tx));
    tx.hdr.inf.txm = ZCAN_SR_NORM;   // 自发自收
    tx.hdr.inf.echo = 1;
    tx.hdr.inf.sef = extended ? 1 : 0;
    tx.hdr.id = id;
    tx.hdr.chn = 0;
    tx.hdr.len = 8;
    for (int b = 0; b < 8; ++b)
        tx.dat[b] = static_cast<quint8>(b);
    return dev.transmitCan(0, tx);
}

bool sendFd(CanDevice &dev, quint32 id, bool extended, bool brs)
{
    ZCAN_FD_MSG tx;
    std::memset(&tx, 0, sizeof(tx));
    tx.hdr.inf.txm = ZCAN_SR_NORM;
    tx.hdr.inf.echo = 1;
    tx.hdr.inf.fmt = 1;
    tx.hdr.inf.sef = extended ? 1 : 0;
    tx.hdr.inf.brs = brs ? 1 : 0;
    tx.hdr.id = id;
    tx.hdr.chn = 0;
    tx.hdr.len = 12;
    for (int b = 0; b < 12; ++b)
        tx.dat[b] = static_cast<quint8>(0x10 + b);
    return dev.transmitFd(0, tx);
}

bool modelHasId(const QAbstractItemModel *m, const QString &idText)
{
    if (!m)
        return false;
    for (int r = 0; r < m->rowCount(); ++r) {
        if (m->data(m->index(r, 0), FrameListModel::IdRole).toString() == idText)
            return true;
    }
    return false;
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

    // ---- A. 合并接收模式 ----
    const bool mergeSet = dev.setRecvMerge(0, true);
    RxManager merged(&dev);
    if (!merged.start(QVariantList{0}, /*mergeMode=*/true)) {
        std::fprintf(stderr, "合并接收管线启动失败\n");
        dev.close();
        return 5;
    }
    wait(300);
    dev.clearBuffer(0);
    merged.clear();

    const bool canSent = sendCan(dev, 0x300);
    const bool fdSent = sendFd(dev, 0x1ABCDEF0, true, true);
    wait(900);

    QAbstractItemModel *m = merged.frameModel();
    const bool mergeCan = modelHasId(m, QStringLiteral("0x300"));
    const bool mergeFd = modelHasId(m, QStringLiteral("0x1ABCDEF0"));
    std::printf("A 合并接收: 下发=%s 发送CAN=%s FD=%s 收CAN=%s 收FD=%s 接收=%llu 丢弃=%llu 模型行=%d\n",
                mergeSet ? "成功" : "失败", canSent ? "成功" : "失败", fdSent ? "成功" : "失败",
                mergeCan ? "是" : "否", mergeFd ? "是" : "否",
                static_cast<unsigned long long>(merged.totalReceived()),
                static_cast<unsigned long long>(merged.totalDropped()),
                m ? m->rowCount() : -1);
    merged.stop();

    // ---- B. 取消合并，逐通道接收 ----
    const bool unmerge = dev.setRecvMerge(0, false);
    RxManager perCh(&dev);
    if (!perCh.start(QVariantList{0}, /*mergeMode=*/false)) {
        std::fprintf(stderr, "逐通道接收管线启动失败\n");
        dev.close();
        return 6;
    }
    wait(300);
    dev.clearBuffer(0);
    perCh.clear();

    const bool canSent2 = sendCan(dev, 0x301);
    wait(700);
    QAbstractItemModel *m2 = perCh.frameModel();
    const bool perChSeen = modelHasId(m2, QStringLiteral("0x301"));
    std::printf("B 逐通道接收: 取消合并=%s 发送=%s 收0x301=%s 模型行=%d\n",
                unmerge ? "成功" : "失败", canSent2 ? "成功" : "失败",
                perChSeen ? "是" : "否", m2 ? m2->rowCount() : -1);

    perCh.stop();
    dev.resetChannel(0);
    dev.close();

    const bool ok = mergeSet && canSent && fdSent && mergeCan && mergeFd && unmerge && canSent2
                    && perChSeen;
    std::printf("结论: %s\n", ok ? "通过" : "未通过");
    return ok ? 0 : 7;
}
