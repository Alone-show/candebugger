// 位定时标定工具：对给定波特率实测出可用参数并写入标定表（与 App 共用 QSettings）
//
// SDK 的实际分频不能可靠地用公式外推（实测 250k 与 500k 落在不同名义时钟档，
// 且 brp*tq 相同的组合表现不同），故用实测方式标定。
//
// 用法（需实机 + 对端在总线上持续发送）：
//   ./build/device_calibrate <目标bps> [通道] [每个候选监听ms] [探针次数]
//   例： ./build/device_calibrate 250000 0
//        ./build/device_calibrate 500000 0
//
// 评分：拿到 ACK 权重最高（逐帧二值、受对端阵发影响小），其次是被动收到的有效帧数。
// 选出的参数会写入标定表；App 中该波特率即优先使用标定值（界面显示"已标定"）。
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/BitCalibration.h"
#include "core/BitTiming.h"
#include "core/CanDevice.h"

namespace {

void buildProbe(ZCAN_20_MSG *msg, quint32 id, quint32 chn)
{
    std::memset(msg, 0, sizeof(*msg));
    msg->hdr.inf.txm = 0;   // 正常发送（需要对端 ACK）
    msg->hdr.inf.fmt = 0;
    msg->hdr.inf.sdf = 0;
    msg->hdr.inf.sef = 0;
    msg->hdr.id = id & 0x7FFu;
    msg->hdr.chn = static_cast<U8>(chn);
    msg->hdr.len = 8;
    for (int i = 0; i < 8; ++i)
        msg->dat[i] = static_cast<U8>(i);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CanDebugger"));
    QCoreApplication::setApplicationName(QStringLiteral("USBCANFD 调试器"));

    const quint32 bps = argc > 1 ? static_cast<quint32>(QString(argv[1]).toUInt()) : 250000u;
    const int channel = argc > 2 ? QString(argv[2]).toInt() : 0;
    const int listenMs = argc > 3 ? QString(argv[3]).toInt() : 700;
    const int probes = argc > 4 ? QString(argv[4]).toInt() : 8;

    CanDevice dev;
    if (!dev.open()) {
        std::fprintf(stderr, "打开设备失败：请确认 USB 权限，且 App 未占用设备\n");
        return 2;
    }
    const CanDeviceInfo info = dev.deviceInfo();
    std::printf("设备: %s HW=0x%04x FW=0x%04x 通道=%u\n",
                qPrintable(info.id), info.hwv, info.fwv, info.channels);
    std::printf("标定目标 %u bps，通道 %d（监听 %dms，探针 %d 次）\n\n",
                bps, channel, listenMs, probes);

    dev.setTerminalResistor(static_cast<quint32>(channel), true);

    struct Cand
    {
        quint32 clock;
        int brp;
        BitTimingParams params;
        int frames;
        int ack;
        int score;
    };
    QList<Cand> results;

    const quint32 clocks[] = {60000000u, 80000000u};
    const int tqs[] = {16, 12, 20, 8, 24};

    for (quint32 clk : clocks) {
        const quint32 canClk = BitTiming::canClockForSdkClock(clk);
        for (int tq : tqs) {
            const int est = qMax(1, static_cast<int>(canClk / (static_cast<quint64>(bps) * tq)));
            for (int brp = qMax(1, est - 2); brp <= est + 2; ++brp) {
                int tseg2 = qMax(1, tq / 8);
                int tseg1 = tq - 1 - tseg2;
                if (tseg1 < 1 || tseg1 > 255 || tseg2 < 1 || tseg2 > 255)
                    continue;

                dev.resetChannel(static_cast<quint32>(channel));
                ChannelConfig cfg;
                cfg.clock = clk;
                cfg.mode = 0;
                cfg.arbitration.brp = static_cast<quint16>(brp);
                cfg.arbitration.tseg1 = static_cast<quint8>(tseg1);
                cfg.arbitration.tseg2 = static_cast<quint8>(tseg2);
                cfg.arbitration.sjw = static_cast<quint8>(qMin(4, tseg2));
                cfg.arbitration.smp = 0;
                cfg.data = cfg.arbitration;
                if (!dev.initChannel(static_cast<quint32>(channel), cfg))
                    continue;
                if (!dev.startChannel(static_cast<quint32>(channel)))
                    continue;

                int frames = 0;
                ZCAN_20_MSG canBuf[32];
                ZCAN_FD_MSG fdBuf[32];
                QElapsedTimer lt;
                lt.start();
                while (lt.elapsed() < listenMs) {
                    const int rc = dev.receiveCan(static_cast<quint32>(channel), canBuf, 32, 5);
                    for (int k = 0; k < rc; ++k)
                        if (!canBuf[k].hdr.inf.err)
                            ++frames;
                    const int rf = dev.receiveFd(static_cast<quint32>(channel), fdBuf, 32, 5);
                    for (int k = 0; k < rf; ++k)
                        if (!fdBuf[k].hdr.inf.err)
                            ++frames;
                }

                int ack = 0;
                for (int k = 0; k < probes; ++k) {
                    ZCAN_20_MSG tx;
                    buildProbe(&tx, 0x7E0u, static_cast<quint32>(channel));
                    if (dev.transmitCan(static_cast<quint32>(channel), tx))
                        ++ack;
                    QCoreApplication::processEvents();
                }

                Cand c;
                c.clock = clk;
                c.brp = brp;
                c.params = cfg.arbitration;
                c.frames = frames;
                c.ack = ack;
                c.score = ack * 50 + frames;   // ACK 权重更高
                results.append(c);

                if (ack > 0 || frames > 0) {
                    std::printf("  候选 名义%uMHz brp=%-4d tseg1=%-4d tseg2=%-3d "
                                "有效帧=%-5d ACK=%d/%d\n",
                                clk / 1000000u, brp, tseg1, tseg2, frames, ack, probes);
                    std::fflush(stdout);
                }
            }
        }
    }

    // 选最优
    int bestIdx = -1;
    for (int i = 0; i < results.size(); ++i) {
        if (results.at(i).score <= 0)
            continue;
        if (bestIdx < 0 || results.at(i).score > results.at(bestIdx).score)
            bestIdx = i;
    }

    if (bestIdx < 0) {
        std::printf("\n未找到可用参数：对端是否在 %u bps 上发送、接线/终端是否正确？\n", bps);
        dev.resetChannel(static_cast<quint32>(channel));
        dev.close();
        return 1;
    }

    const Cand &b = results.at(bestIdx);
    std::printf("\n最佳：名义 %u MHz，brp=%d tseg1=%d tseg2=%d sjw=%d（有效帧 %d，ACK %d/%d）\n",
                b.clock / 1000000u, b.brp, b.params.tseg1, b.params.tseg2, b.params.sjw,
                b.frames, b.ack, probes);
    BitCalibration::instance().set(static_cast<int>(bps), b.clock, b.params, QStringLiteral("实测"));
    std::printf("已写入标定表（%u bps）。App 中该波特率将优先使用此标定值。\n", bps);

    dev.resetChannel(static_cast<quint32>(channel));
    dev.close();
    return 0;
}
