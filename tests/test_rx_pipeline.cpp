// 步骤 2/3 实机管线验证：设备 + 接收线程 + 环形缓冲 + 模型
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <cstring>

#include "core/CanDevice.h"
#include "core/RxManager.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    CanDevice dev;
    if (!dev.open()) {
        std::fprintf(stderr, "打开设备失败（需 USB 权限）\n");
        return 2;
    }

    const QString msg = dev.configureChannel(0, 500000, 2000000, 87.5, false);
    if (msg.isEmpty()) {
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

    const int kFrames = 50;
    int sent = 0;
    for (int i = 0; i < kFrames; ++i) {
        ZCAN_20_MSG tx;
        std::memset(&tx, 0, sizeof(tx));
        tx.hdr.inf.txm = ZCAN_SR_NORM;   // 自发自收
        tx.hdr.inf.fmt = 0;
        tx.hdr.inf.echo = 1;
        tx.hdr.id = 0x300 + static_cast<quint32>(i);
        tx.hdr.chn = 0;
        tx.hdr.len = 8;
        for (int b = 0; b < 8; ++b)
            tx.dat[b] = static_cast<quint8>(b);
        if (dev.transmitCan(0, tx))
            ++sent;
    }
    std::printf("发送 %d/%d 帧\n", sent, kFrames);

    QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    app.exec();

    QAbstractItemModel *model = mgr.frameModel();
    const qulonglong received = mgr.totalReceived();
    const int rows = model ? model->rowCount() : -1;
    const qulonglong dropped = mgr.totalDropped();

    std::printf("管线统计: 接收=%llu 丢弃=%llu 模型行=%d\n",
                static_cast<unsigned long long>(received),
                static_cast<unsigned long long>(dropped), rows);

    mgr.stop();
    dev.resetChannel(0);
    dev.close();

    const bool ok = received >= static_cast<qulonglong>(kFrames) && rows >= kFrames;
    std::printf("结论: %s\n", ok ? "通过" : "未通过");
    return ok ? 0 : 6;
}
