// L3 实机：正常模式发送不应因等待总线 ACK 而阻塞数秒
// 回归点：驱动 VCI_Transmit 内部 wait(tx) 默认超时 ~1500ms，必须由
// CanDevice::startChannel 下发的 CMD_CAN_TX_TIMEOUT(0x44) 压到很小。
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>
#include <cstring>

#include "core/CanDevice.h"
#include "core/FrameBuilder.h"

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

    ZCAN_20_MSG tx;
    std::memset(&tx, 0, sizeof(tx));
    tx.hdr.inf.txm = ZCAN_TX_NORM;   // 正常模式：驱动会等待总线 ACK
    tx.hdr.inf.fmt = 0;
    tx.hdr.inf.echo = 1;
    tx.hdr.id = 0x123;
    tx.hdr.chn = 0;
    tx.hdr.len = 8;
    for (int b = 0; b < 8; ++b)
        tx.dat[b] = static_cast<quint8>(b);

    qint64 worst = 0;
    std::printf("超时设定 txTimeoutMs=%d\n", dev.txTimeoutMs());
    for (int i = 0; i < 5; ++i) {
        QElapsedTimer t;
        t.start();
        dev.transmitCan(0, tx);
        const qint64 ms = t.elapsed();
        std::printf("  第 %d 次: %lld ms\n", i + 1, static_cast<long long>(ms));
        worst = qMax(worst, ms);
    }

    dev.resetChannel(0);
    dev.close();

    std::printf("正常模式单帧发送最坏耗时: %lld ms (驱动默认 ACK 超时 ~1500ms)\n",
                static_cast<long long>(worst));
    const bool ok = worst < 300;
    std::printf("结论: %s\n", ok ? "通过" : "未通过");
    return ok ? 0 : 5;
}
