// 模型无关的位定时暴力扫描（需实机 + 对端在总线上）
//
// 动机：不知道本机 CAN 控制器的真实时钟，也不知道 SDK 内部的换算公式，
// 因此不假设 60/80MHz，而是直接扫描「分频积 D = brp × TQ」（实际波特率 = 时钟 / D）。
// 对每个 D 同时做两件事：
//   1) 被动监听（正常模式也会接收）——对端在发帧就能收到；
//   2) 发送探针帧并统计 ACK ——只有位速率与对端匹配才会被 ACK（判据更干净）。
//
// 用法：
//   ./build/device_bitrate_scan [时钟MHz] [通道] [最小D/brp] [最大D/brp] [监听ms] [探针数] [固定tq]
//   默认：                   80        0        40          1600      60        2        0
//   - 固定tq=0：扫描分频积 D=brp×tq（自动因式分解，采样点会变，结果可能不单调）
//   - 固定tq>0：固定每比特 Tq 数，只扫 brp（采样点恒定，命中区间连续、中心即真实分频比）
//   时钟MHz 只作为打印/反推参考；是否会真正影响硬件取决于 SDK 是否使用该字段。
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QString>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/CanDevice.h"

namespace {

struct Hit
{
    int d = 0;
    quint16 brp = 0;
    quint8 tseg1 = 0;
    quint8 tseg2 = 0;
    quint8 sjw = 0;
    int frames = 0;
    int ack = 0;
    int probes = 0;
};

bool factorD(int d, quint16 *brp, quint8 *tseg1, quint8 *tseg2, quint8 *sjw)
{
    // 选 tq 最接近 16 的因子分解（仅影响采样点，不影响实际波特率）
    int bestTq = -1;
    for (int tq = 8; tq <= 128; ++tq) {
        if (d % tq != 0)
            continue;
        const int b = d / tq;
        if (b < 1 || b > 1024)
            continue;
        if (bestTq < 0 || qAbs(tq - 16) < qAbs(bestTq - 16))
            bestTq = tq;
    }
    if (bestTq < 0)
        return false;

    const int b = d / bestTq;
    int t2 = qMax(1, bestTq / 8);
    int t1 = bestTq - 1 - t2;
    if (t1 < 1 || t1 > 255 || t2 < 1 || t2 > 255)
        return false;
    *brp = static_cast<quint16>(b);
    *tseg1 = static_cast<quint8>(t1);
    *tseg2 = static_cast<quint8>(t2);
    *sjw = static_cast<quint8>(qMin(4, t2));
    return true;
}

void buildProbe(ZCAN_20_MSG *msg, quint32 id, quint32 chn)
{
    std::memset(msg, 0, sizeof(*msg));
    msg->hdr.inf.txm = 0;   // 正常发送：需要 ACK
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

    const quint32 clk = (argc > 1 ? QString(argv[1]).toUInt() : 80u) * 1000000u;
    const int channel = argc > 2 ? QString(argv[2]).toInt() : 0;
    const int dMin = argc > 3 ? QString(argv[3]).toInt() : 40;
    const int dMax = argc > 4 ? QString(argv[4]).toInt() : 1600;
    const int listenMs = argc > 5 ? QString(argv[5]).toInt() : 60;
    const int probes = argc > 6 ? QString(argv[6]).toInt() : 3;
    const int fixedTq = argc > 7 ? QString(argv[7]).toInt() : 0;

    CanDevice dev;
    if (!dev.open()) {
        std::fprintf(stderr, "打开设备失败：请确认 USB 权限（需安装 udev 规则）\n");
        return 2;
    }
    const CanDeviceInfo info = dev.deviceInfo();
    std::printf("设备: %s  HW=0x%04x FW=0x%04x 通道=%u\n",
                qPrintable(info.id), info.hwv, info.fwv, info.channels);
    if (fixedTq > 0) {
        std::printf("固定 tq=%d，扫描 brp=%d..%d（通道 %d，监听 %dms，探针 %d 次，参考时钟 %u MHz）\n",
                    fixedTq, dMin, dMax, channel, listenMs, probes, clk / 1000000u);
        std::printf("覆盖分频积 D=%d..%d → 实际波特率(按参考时钟) %u .. %u bps\n\n",
                    dMin * fixedTq, dMax * fixedTq, clk / (dMax * fixedTq), clk / (dMin * fixedTq));
    } else {
        std::printf("扫描 D=%d..%d（通道 %d，监听 %dms，探针 %d 次，参考时钟 %u MHz）\n",
                    dMin, dMax, channel, listenMs, probes, clk / 1000000u);
        std::printf("覆盖实际波特率(按参考时钟)：%u .. %u bps\n\n",
                    clk / dMax, clk / dMin);
    }

    dev.setTerminalResistor(static_cast<quint32>(channel), true);
    // 注意：不设 CMD_CAN_TX_TIMEOUT（保持驱动默认，约 1.5s）。因为设小了之后
    // VCI_Transmit 超时也会返回 1（帧已提交），ACK 判据会失真。

    QList<Hit> hits;
    int initFail = 0;
    QElapsedTimer total;
    total.start();

    for (int d = dMin; d <= dMax; ++d) {
        quint16 brp = 0;
        quint8 t1 = 0, t2 = 0, sjw = 0;
        if (fixedTq > 0) {
            const int tq = fixedTq;
            const int b = d;   // d 即 brp
            if (b < 1 || b > 1024)
                continue;
            const int tseg2 = qMax(1, tq / 8);
            const int tseg1 = tq - 1 - tseg2;
            if (tseg1 < 1 || tseg1 > 255 || tseg2 < 1 || tseg2 > 255)
                continue;
            brp = static_cast<quint16>(b);
            t1 = static_cast<quint8>(tseg1);
            t2 = static_cast<quint8>(tseg2);
            sjw = static_cast<quint8>(qMin(4, tseg2));
        } else if (!factorD(d, &brp, &t1, &t2, &sjw)) {
            continue;
        }

        dev.resetChannel(static_cast<quint32>(channel));
        ChannelConfig cfg;
        cfg.clock = clk;
        cfg.mode = 0;   // 正常模式（同样会接收）
        cfg.arbitration.brp = brp;
        cfg.arbitration.tseg1 = t1;
        cfg.arbitration.tseg2 = t2;
        cfg.arbitration.sjw = sjw;
        cfg.arbitration.smp = 0;
        cfg.data = cfg.arbitration;

        if (!dev.initChannel(static_cast<quint32>(channel), cfg)) {
            ++initFail;
            continue;
        }
        if (!dev.startChannel(static_cast<quint32>(channel)))
            continue;

        ZCAN_20_MSG canBuf[32];
        ZCAN_FD_MSG fdBuf[32];
        int valid = 0;    // err==0 的有效帧
        int errFrames = 0;  // 错误帧（位速率不匹配时会大量出现）
        QElapsedTimer lt;
        lt.start();
        while (lt.elapsed() < listenMs) {
            const int rc = dev.receiveCan(static_cast<quint32>(channel), canBuf, 32, 5);
            for (int k = 0; k < rc; ++k) {
                if (canBuf[k].hdr.inf.err)
                    ++errFrames;
                else
                    ++valid;
            }
            const int rf = dev.receiveFd(static_cast<quint32>(channel), fdBuf, 32, 5);
            for (int k = 0; k < rf; ++k) {
                if (fdBuf[k].hdr.inf.err)
                    ++errFrames;
                else
                    ++valid;
            }
        }

        // 只有出现有效帧才探测 ACK（默认长超时下，ACK 才是真实的总线应答）
        int ack = 0;
        if (valid > 0) {
            for (int k = 0; k < probes; ++k) {
                ZCAN_20_MSG tx;
                buildProbe(&tx, 0x7E0u, static_cast<quint32>(channel));
                if (dev.transmitCan(static_cast<quint32>(channel), tx))
                    ++ack;
                QCoreApplication::processEvents();
            }
        }

        if (valid > 0 || ack > 0) {
            Hit h;
            h.d = d;
            h.brp = brp;
            h.tseg1 = t1;
            h.tseg2 = t2;
            h.sjw = sjw;
            h.frames = valid;
            h.ack = ack;
            h.probes = probes;
            hits.append(h);
            const int tq = 1 + t1 + t2;
            std::printf("命中: brp=%-4u tseg1=%-4u tseg2=%-3u (tq=%-4d) 有效帧=%-4d 错帧=%-4d ACK=%d/%d\n",
                        brp, t1, t2, tq, valid, errFrames, ack, probes);
            std::fflush(stdout);
        }
        if (!hits.isEmpty() && (d - dMin) % 20 == 0) {
            std::printf("  ... 扫到 %d (%.0fs)\n", d, total.elapsed() / 1000.0);
            std::fflush(stdout);
        }
    }

    std::printf("\n================ 结果 ================\n");
    if (!hits.isEmpty()) {
        std::printf("命中 %d 档：\n", int(hits.size()));
        int dAckMin = 0, dAckMax = 0, dAckCount = 0;
        for (const Hit &h : hits) {
            const int tq = 1 + h.tseg1 + h.tseg2;
            std::printf("  brp=%-4u tseg1=%-4u tseg2=%-3u (tq=%-4d, D=%u) 有效帧=%-4d ACK=%d/%d\n",
                        h.brp, h.tseg1, h.tseg2, tq, unsigned(h.brp) * unsigned(tq),
                        h.frames, h.ack, h.probes);
            if (h.ack > 0) {
                if (dAckCount == 0)
                    dAckMin = h.d;
                dAckMax = h.d;
                ++dAckCount;
            }
        }
        if (dAckCount > 0) {
            std::printf("拿到 ACK 的扫描值区间 [%d, %d]（共 %d 档）\n", dAckMin, dAckMax, dAckCount);
            if (fixedTq > 0) {
                const double brpCenter = (dAckMin + dAckMax) / 2.0;
                const double dCenter = brpCenter * fixedTq;
                std::printf("brp 中心 ≈ %.1f，对应分频积 D ≈ %.1f（tq=%d）\n",
                            brpCenter, dCenter, fixedTq);
                std::printf("若对端为 250k：本机真实时钟 ≈ %llu MHz\n",
                            static_cast<unsigned long long>(250000ull * static_cast<unsigned long long>(dCenter) / 1000000ull));
                std::printf("若对端为 500k：本机真实时钟 ≈ %llu MHz\n",
                            static_cast<unsigned long long>(500000ull * static_cast<unsigned long long>(dCenter) / 1000000ull));
            }
        }
    } else {
        std::printf("全扫无有效帧且无 ACK（InitCAN 失败 %d 次）。\n", initFail);
        std::printf("可加大范围重试：./build/device_bitrate_scan 80 %d 8 4096 60 2 0\n", channel);
    }

    dev.resetChannel(static_cast<quint32>(channel));
    dev.close();
    return hits.isEmpty() ? 1 : 0;
}
