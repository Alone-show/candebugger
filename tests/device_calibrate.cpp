// 位定时标定工具：对给定波特率实测出可用参数并写入标定表（与 App 共用 QSettings）
//
// 公式（ZLG 约定）已在 60M/80M 下能精确算出 125k~1M，故本工具主要给
//   非标准速率 / 特殊硬件 / 想复核现有参数 的场合用。
//
// 用法（需实机 + 对端在总线上持续发送）：
//   ./build/device_calibrate <目标bps> [通道] [每个候选监听ms] [探针次数]
//   例： ./build/device_calibrate 250000 0
//        ./build/device_calibrate 125000 0
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
    // 每比特总 Tq 数候选（= tseg1+tseg2+3）：8/16/24/32/40 是 SP=87.5% 的
    // ZLG/CiA 常用形状，其余覆盖其它常用采样点（高波特率数据域等）。
    // 只枚举能整数命中目标波特率的组合（paramsForTq 内部判断）。
    const int totalTqs[] = {8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 38, 40};

    for (quint32 clk : clocks) {
        for (int totalTq : totalTqs) {
            BitTimingParams p;
            if (!BitTiming::paramsForTq(clk, bps, 87.5, totalTq, &p))
                continue;

            dev.resetChannel(static_cast<quint32>(channel));
            ChannelConfig cfg;
            cfg.clock = clk;
            cfg.mode = 0;
            cfg.arbitration = p;
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
            c.brp = p.brp;
            c.params = cfg.arbitration;
            c.frames = frames;
            c.ack = ack;
            // 择优顺序：ACK 优先（逐帧二值、受对端阵发影响小）
            //   → 其次偏好总 Tq=16 的 CiA/ZLG 标准形状（与内置实测值一致，结果确定）
            //   → 最后才看被动收到的有效帧数（受对端阵发影响大，仅作参考）
            c.score = ack * 1000
                      + (BitTiming::totalTq(p) == 16 ? 100 : 0)
                      + qMin(frames, 99);
            results.append(c);

            if (ack > 0 || frames > 0) {
                std::printf("  候选 clk=%uMHz brp=%-4d tseg1=%-4d tseg2=%-3d "
                            "有效帧=%-5d ACK=%d/%d\n",
                            clk / 1000000u, p.brp, p.tseg1, p.tseg2, frames, ack, probes);
                std::fflush(stdout);
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
    std::printf("\n最佳：clk=%u MHz，brp=%d tseg1=%d tseg2=%d sjw=%d（有效帧 %d，ACK %d/%d）\n",
                b.clock / 1000000u, b.brp, b.params.tseg1, b.params.tseg2, b.params.sjw,
                b.frames, b.ack, probes);
    BitCalibration::instance().set(static_cast<int>(bps), b.clock, b.params, QStringLiteral("实测"));
    std::printf("已写入标定表（%u bps）。App 中该波特率将优先使用此标定值。\n", bps);

    dev.resetChannel(static_cast<quint32>(channel));
    dev.close();
    return 0;
}
