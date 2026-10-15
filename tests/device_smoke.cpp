// 步骤 1 冒烟测试：打开设备、读取信息、按标准公式配置并启动通道、短暂接收
#include <QCoreApplication>
#include <QString>
#include <cstdio>
#include <cstring>

#include "core/BitTiming.h"
#include "core/CanDevice.h"

namespace {

void printInfo(const CanDeviceInfo &info)
{
    std::printf("设备信息:\n");
    std::printf("  SN      : %s\n", qPrintable(info.sn));
    std::printf("  ID      : %s\n", qPrintable(info.id));
    std::printf("  硬件版本: 0x%04x\n", info.hwv);
    std::printf("  固件版本: 0x%04x\n", info.fwv);
    std::printf("  驱动版本: 0x%04x\n", info.drv);
    std::printf("  API版本 : 0x%04x\n", info.api);
    std::printf("  通道数  : %u\n", info.channels);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    CanDevice dev;
    if (!dev.open()) {
        std::fprintf(stderr, "打开设备失败：请确认 USB 权限（需安装 udev 规则）\n");
        return 2;
    }
    std::printf("打开设备成功\n");

    const CanDeviceInfo info = dev.deviceInfo();
    printInfo(info);

    int channelCount = info.channels > 0 ? info.channels : 2;
    if (channelCount > 2)
        channelCount = 2;

    for (int ch = 0; ch < channelCount; ++ch) {
        ChannelConfig cfg;
        cfg.clock = BitTiming::DefaultClock;   // clk 即真实时钟（60MHz，V1.01）

        const auto arb = BitTiming::calculate(cfg.clock, 500000, 87.5);
        const auto dat = BitTiming::calculate(cfg.clock, 2000000, 87.5);
        if (!arb.ok || !dat.ok) {
            std::fprintf(stderr, "位定时计算失败\n");
            dev.close();
            return 3;
        }
        cfg.arbitration = arb.params;
        cfg.data = dat.params;

        std::printf("通道%d 仲裁: %s\n", ch, qPrintable(arb.message));
        std::printf("通道%d 数据: %s\n", ch, qPrintable(dat.message));

        if (!dev.initChannel(ch, cfg)) {
            std::fprintf(stderr, "初始化通道 %d 失败\n", ch);
            dev.close();
            return 4;
        }
        dev.setTerminalResistor(ch, true);
        if (!dev.startChannel(ch)) {
            std::fprintf(stderr, "启动通道 %d 失败\n", ch);
            dev.close();
            return 5;
        }
        std::printf("通道%d 启动成功\n", ch);
    }

    // 短暂接收，确认接口可用
    ZCAN_20_MSG canBuf[256];
    ZCAN_FD_MSG fdBuf[256];
    int canTotal = 0;
    int fdTotal = 0;
    for (int i = 0; i < 100; ++i) {
        for (int ch = 0; ch < channelCount; ++ch) {
            const int rc = dev.receiveCan(ch, canBuf, 256, 5);
            if (rc > 0)
                canTotal += rc;
            const int rf = dev.receiveFd(ch, fdBuf, 256, 5);
            if (rf > 0)
                fdTotal += rf;
        }
        QCoreApplication::processEvents();
    }
    std::printf("接收样本: CAN=%d, CANFD=%d\n", canTotal, fdTotal);

    // 自发自收验证：验证发送与接收通路
    if (channelCount > 0) {
        ZCAN_20_MSG tx;
        std::memset(&tx, 0, sizeof(tx));
        tx.hdr.inf.txm = ZCAN_SR_NORM;   // 自发自收
        tx.hdr.inf.fmt = 0;
        tx.hdr.inf.sef = 0;
        tx.hdr.inf.echo = 1;
        tx.hdr.id = 0x123;
        tx.hdr.chn = 0;
        tx.hdr.len = 8;
        for (int i = 0; i < 8; ++i)
            tx.dat[i] = static_cast<quint8>(i);
        const bool sent = dev.transmitCan(0, tx);
        std::printf("自发自收发送 CAN 0x123: %s\n", sent ? "成功" : "失败");

        int echoCount = 0;
        for (int i = 0; i < 200 && echoCount == 0; ++i) {
            const int rc = dev.receiveCan(0, canBuf, 256, 5);
            for (int k = 0; k < rc; ++k) {
                if ((canBuf[k].hdr.id & 0x7FFu) == 0x123u)
                    ++echoCount;
            }
            QCoreApplication::processEvents();
        }
        std::printf("自发自收接收计数: %d\n", echoCount);
    }

    for (int ch = 0; ch < channelCount; ++ch)
        dev.resetChannel(ch);

    dev.close();
    std::printf("关闭设备成功\n");
    return 0;
}
