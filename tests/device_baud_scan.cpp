// 波特率/位定时扫描诊断工具（需实机 + 总线上有对端）
//
// 用途：当「接外部节点发送失败」而本机自收自测正常时，逐个尝试候选位定时，
// 每个候选位定时下发后发送探针帧（正常模式，需要对端 ACK）。哪个候选能拿到
// ACK，哪个就是与对端匹配的位定时。
//
// 用法：
//   ./build/device_baud_scan [目标bps] [采样点%] [通道号]
//   默认： 250000 87.5 0
//
// 说明：对端波特率必须与「目标bps」一致；本工具只改变本机侧时钟/位定时，
// 不会修改对端。若所有候选都失败，问题多半在接线/终端电阻/对端实际波特率。
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QString>
#include <QVariantList>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/BitTiming.h"
#include "core/CanDevice.h"

namespace {

struct Candidate
{
    quint32 clock = 0;
    BitTimingParams params;
    QString note;
};

QString keyOf(const Candidate &c)
{
    return QStringLiteral("%1:%2:%3:%4:%5")
        .arg(c.clock)
        .arg(c.params.brp)
        .arg(c.params.tseg1)
        .arg(c.params.tseg2)
        .arg(c.params.sjw);
}

void addCandidate(QList<Candidate> &out, QSet<QString> &seen, quint32 clock,
                  const BitTimingParams &p, const QString &note)
{
    Candidate c;
    c.clock = clock;
    c.params = p;
    c.note = note;
    const QString k = keyOf(c);
    if (seen.contains(k))
        return;
    seen.insert(k);
    out.append(c);
}

// 构造探针帧
void buildProbe(ZCAN_20_MSG *msg, quint32 id, quint32 chn)
{
    std::memset(msg, 0, sizeof(*msg));
    msg->hdr.inf.txm = 0;   // 正常发送：需要总线 ACK
    msg->hdr.inf.fmt = 0;   // CAN2.0
    msg->hdr.inf.sdf = 0;   // 数据帧
    msg->hdr.inf.sef = 0;   // 标准帧
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

    const quint32 targetBps = argc > 1 ? static_cast<quint32>(QString(argv[1]).toUInt()) : 250000u;
    const double samplePoint = argc > 2 ? QString(argv[2]).toDouble() : 87.5;
    const int channel = argc > 3 ? QString(argv[3]).toInt() : 0;

    CanDevice dev;
    if (!dev.open()) {
        std::fprintf(stderr, "打开设备失败：请确认 USB 权限（需安装 udev 规则）\n");
        return 2;
    }

    const CanDeviceInfo info = dev.deviceInfo();
    std::printf("设备: %s  HW=0x%04x FW=0x%04x  通道=%u\n",
                qPrintable(info.id), info.hwv, info.fwv, info.channels);
    const quint32 recommended = dev.recommendedClock();
    std::printf("推荐时钟: %u MHz（按硬件版本推断）\n", recommended / 1000000u);
    std::printf("扫描目标: %u bps, 采样点 %.1f%%, 通道 %d\n\n", targetBps, samplePoint, channel);

    // ---- 构造候选位定时 ----
    QList<Candidate> candidates;
    QSet<QString> seen;

    // 1) 公式在 60MHz / 80MHz / 推荐名义时钟下的最优解（公式用实际时钟 = 0.8×名义）
    for (quint32 clk : {60000000u, 80000000u, recommended}) {
        if (clk == 0)
            continue;
        const quint32 canClk = BitTiming::canClockForSdkClock(clk);
        const auto r = BitTiming::calculate(canClk, targetBps, samplePoint);
        if (r.ok)
            addCandidate(candidates, seen, clk, r.params,
                         QStringLiteral("公式最优 名义%1MHz").arg(clk / 1000000u));
    }

    // 2) 枚举每个时钟下所有能整除目标波特率的 Tq 数（含 ZLG 惯例 16 Tq）
    for (quint32 clk : {60000000u, 80000000u, recommended}) {
        if (clk == 0)
            continue;
        const quint32 canClk = BitTiming::canClockForSdkClock(clk);
        for (int n = 8; n <= 64; ++n) {
            const quint64 denom = static_cast<quint64>(targetBps) * static_cast<quint64>(n);
            if (denom == 0 || (canClk % denom) != 0)
                continue;
            const quint64 brp = canClk / denom;
            if (brp < 1 || brp > 1024)
                continue;
            int tseg1 = qRound(samplePoint / 100.0 * n) - 1;
            tseg1 = qBound(1, tseg1, n - 2);
            const int tseg2 = n - 1 - tseg1;
            if (tseg2 < 1)
                continue;
            BitTimingParams p;
            p.brp = static_cast<quint16>(brp);
            p.tseg1 = static_cast<quint8>(tseg1);
            p.tseg2 = static_cast<quint8>(tseg2);
            p.sjw = static_cast<quint8>(qBound(1, qMin(4, tseg2), 127));
            p.smp = 0;
            addCandidate(candidates, seen, clk, p,
                         QStringLiteral("枚举 Tq=%1").arg(n));
        }
    }

    std::printf("共 %d 个候选位定时\n\n", int(candidates.size()));
    std::printf("%-4s %-7s %-6s %-5s %-5s %-5s %-8s %-8s %-6s %s\n",
                "#", "时钟MHz", "brp", "tseg1", "tseg2", "sjw", "实际bps", "采样点%", "监听帧", "探针ACK(成功/次)");
    std::printf("-----------------------------------------------------------------------------------\n");

    int winner = -1;
    int heardWinner = -1;
    for (int i = 0; i < candidates.size(); ++i) {
        const Candidate &c = candidates.at(i);

        dev.resetChannel(static_cast<quint32>(channel));

        ChannelConfig cfg;
        cfg.clock = c.clock;
        cfg.mode = 0;
        cfg.arbitration = c.params;
        cfg.data = c.params;   // 经典 CAN 只用到仲裁域；数据域取同值便于初始化
        if (!dev.initChannel(static_cast<quint32>(channel), cfg)) {
            std::printf("%-4d %-10u %-6u %-5u %-5u %-5u %-8s %-8s %s\n",
                        i + 1, c.clock / 1000000u, c.params.brp, c.params.tseg1, c.params.tseg2,
                        c.params.sjw,
                        qPrintable(QString::number(BitTiming::actualBps(BitTiming::canClockForSdkClock(c.clock), c.params), 'f', 0)),
                        qPrintable(QString::number(BitTiming::samplePoint(c.params), 'f', 1)),
                        "InitCAN 失败");
            continue;
        }
        dev.setTerminalResistor(static_cast<quint32>(channel), true);
        if (!dev.startChannel(static_cast<quint32>(channel))) {
            std::printf("%-4d %-10u %-6u %-5u %-5u %-5u %-8s %-8s %s\n",
                        i + 1, c.clock / 1000000u, c.params.brp, c.params.tseg1, c.params.tseg2,
                        c.params.sjw,
                        qPrintable(QString::number(BitTiming::actualBps(BitTiming::canClockForSdkClock(c.clock), c.params), 'f', 0)),
                        qPrintable(QString::number(BitTiming::samplePoint(c.params), 'f', 1)),
                        "StartCAN 失败");
            continue;
        }

        const int probes = 3;
        int ack = 0;
        int heard = 0;

        // 1) 只听模式被动监听：若对端正在以该波特率发送，能收到帧（不依赖 ACK）
        dev.resetChannel(static_cast<quint32>(channel));
        ChannelConfig listenCfg;
        listenCfg.clock = c.clock;
        listenCfg.mode = 1;   // 只听
        listenCfg.arbitration = c.params;
        listenCfg.data = c.params;
        if (dev.initChannel(static_cast<quint32>(channel), listenCfg)
            && dev.startChannel(static_cast<quint32>(channel))) {
            ZCAN_20_MSG buf[64];
            QElapsedTimer listenTimer;
            listenTimer.start();
            while (listenTimer.elapsed() < 200) {
                const int rc = dev.receiveCan(static_cast<quint32>(channel), buf, 64, 10);
                if (rc > 0)
                    heard += rc;
                QCoreApplication::processEvents();
            }
        }

        // 2) 正常模式发送探针：对端在场且波特率匹配才会 ACK
        dev.resetChannel(static_cast<quint32>(channel));
        ChannelConfig txCfg;
        txCfg.clock = c.clock;
        txCfg.mode = 0;   // 正常
        txCfg.arbitration = c.params;
        txCfg.data = c.params;
        if (dev.initChannel(static_cast<quint32>(channel), txCfg)
            && dev.startChannel(static_cast<quint32>(channel))) {
            for (int k = 0; k < probes; ++k) {
                ZCAN_20_MSG tx;
                buildProbe(&tx, 0x7E0u, static_cast<quint32>(channel));
                if (dev.transmitCan(static_cast<quint32>(channel), tx))
                    ++ack;
                QCoreApplication::processEvents();
            }
        } else {
            std::printf("%-4d %-10u %-6u %-5u %-5u %-5u %-8s %-8s %s\n",
                        i + 1, c.clock / 1000000u, c.params.brp, c.params.tseg1, c.params.tseg2,
                        c.params.sjw,
                        qPrintable(QString::number(BitTiming::actualBps(BitTiming::canClockForSdkClock(c.clock), c.params), 'f', 0)),
                        qPrintable(QString::number(BitTiming::samplePoint(c.params), 'f', 1)),
                        "InitCAN/StartCAN 失败");
            continue;
        }

        std::printf("%-4d %-10u %-6u %-5u %-5u %-5u %-8s %-8s %-6d %d/%d%s\n",
                    i + 1, c.clock / 1000000u, c.params.brp, c.params.tseg1, c.params.tseg2,
                    c.params.sjw,
                    qPrintable(QString::number(BitTiming::actualBps(BitTiming::canClockForSdkClock(c.clock), c.params), 'f', 0)),
                    qPrintable(QString::number(BitTiming::samplePoint(c.params), 'f', 1)),
                    heard, ack, probes, (ack > 0 || heard > 0) ? "   <= 命中" : "");
        if ((ack > 0 || heard > 0) && winner < 0)
            winner = i;
        if (heard > 0 && heardWinner < 0)
            heardWinner = i;
    }

    std::printf("\n");
    if (heardWinner >= 0 && winner == heardWinner) {
        const Candidate &c = candidates.at(heardWinner);
        std::printf("结果：命中候选 #%d —— 时钟 %u MHz, brp=%u tseg1=%u tseg2=%u sjw=%u, 实际 %s bps, 采样点 %s%%\n",
                    heardWinner + 1, c.clock / 1000000u, c.params.brp, c.params.tseg1, c.params.tseg2,
                    c.params.sjw,
                    qPrintable(QString::number(BitTiming::actualBps(BitTiming::canClockForSdkClock(c.clock), c.params), 'f', 0)),
                    qPrintable(QString::number(BitTiming::samplePoint(c.params), 'f', 1)));
        std::printf("请在「通道配置」中把控制器时钟设为 %u MHz%s。\n",
                    c.clock / 1000000u,
                    c.clock == 80000000u ? "（或保持“自动”，V1.03+ 硬件会推断为 80MHz）" : "");
    } else if (heardWinner >= 0) {
        const Candidate &c = candidates.at(heardWinner);
        std::printf("结果：枚举候选 #%d 能**被动收到帧**（时钟 %u MHz, brp=%u tseg1=%u tseg2=%u sjw=%u），\n",
                    heardWinner + 1, c.clock / 1000000u, c.params.brp, c.params.tseg1, c.params.tseg2,
                    c.params.sjw);
        std::printf("      但探针未拿到 ACK。说明本机已能正确解析对端报文（位定时基本正确），\n");
        std::printf("      发送侧问题更可能在：对端不 ACK（只听/不响应）、单节点无应答、或本机未真正上总线。\n");
    } else if (winner >= 0) {
        const Candidate &c = candidates.at(winner);
        std::printf("结果：候选 #%d 拿到 ACK（时钟 %u MHz, brp=%u tseg1=%u tseg2=%u sjw=%u）。\n",
                    winner + 1, c.clock / 1000000u, c.params.brp, c.params.tseg1, c.params.tseg2,
                    c.params.sjw);
    } else {
        std::printf("结果：既未被动收到帧，也未拿到 ACK —— 说明**总线上没有可与本机通信的对端**：\n");
        std::printf("  1) 对端是否真的工作在 %u bps、已上电、且正在发送；\n", targetBps);
        std::printf("  2) CAN_H/CAN_L 接线是否正确、是否交叉、是否接在本机对应通道；\n");
        std::printf("  3) 总线终端电阻：两端各 120Ω（本工具已开启本机电阻）；\n");
        std::printf("  4) 对端是否为 CANFD-only 或使用非标准采样点。\n");
        std::printf("（本机自发自收通路可用：可另跑 ./build/device_smoke 佐证设备正常。）\n");
    }

    dev.resetChannel(static_cast<quint32>(channel));
    dev.close();
    return (winner >= 0 || heardWinner >= 0) ? 0 : 1;
}
