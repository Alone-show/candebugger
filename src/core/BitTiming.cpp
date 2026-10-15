#include "core/BitTiming.h"

#include <QtMath>

namespace {

QString makeMessage(quint32 clk, quint32 bps, const BitTimingParams &p, bool exact, double bpsValue, double sp)
{
    const QString detail = QStringLiteral("brp=%1 tseg1=%2 tseg2=%3 sjw=%4 smp=%5")
                               .arg(p.brp)
                               .arg(p.tseg1)
                               .arg(p.tseg2)
                               .arg(p.sjw)
                               .arg(p.smp);
    QString actual = QStringLiteral("实际 %1 bps, 采样点 %2%")
                         .arg(QString::number(bpsValue, 'f', 0), QString::number(sp, 'f', 1));
    if (!exact)
        actual += QStringLiteral("（近似）");
    return QStringLiteral("clk=%1 baud=%2 -> %3 (%4)").arg(clk).arg(bps).arg(detail).arg(actual);
}

} // namespace

quint32 BitTiming::clockForHardwareVersion(quint16 hwv)
{
    // SDK 说明：V1.01 名义时钟 60MHz，V1.03 及以上 80MHz
    return hwv >= HwV1_03 ? Clock80M : DefaultClock;
}

quint32 BitTiming::canClockForSdkClock(quint32 sdkClk)
{
    // 实测：实际 CAN 控制器时钟 = 0.8 × 名义时钟（48MHz / 64MHz）
    return static_cast<quint32>((static_cast<quint64>(sdkClk) * 4ull) / 5ull);
}

BitTimingResult BitTiming::calculate(quint32 clk, quint32 bps, double samplePointPercent,
                                     int maxTq, int preferredTq)
{
    BitTimingResult result;

    if (clk == 0 || bps == 0) {
        result.message = QStringLiteral("时钟或波特率无效");
        return result;
    }
    if (maxTq < 8)
        maxTq = 8;
    if (maxTq > 255)
        maxTq = 255;
    if (preferredTq < 8)
        preferredTq = 8;

    const double target = qBound(0.50, samplePointPercent / 100.0, 0.95);

    bool found = false;
    double bestErr = 1e9;
    bool bestRobust = false;      // sjw >= 2 视为稳健配置
    int bestTqDist = 0;
    BitTimingParams bestParams;

    for (int pass = 0; pass < 2 && !found; ++pass) {
        const bool exactOnly = (pass == 0);
        for (int n = 8; n <= maxTq; ++n) {
            const quint64 denom = static_cast<quint64>(bps) * static_cast<quint64>(n);
            if (exactOnly && (clk % denom) != 0)
                continue;

            quint64 brp = clk / denom;
            if (!exactOnly) {
                const double brpApprox = static_cast<double>(clk) / static_cast<double>(denom);
                brp = static_cast<quint64>(qRound(brpApprox));
            }
            if (brp < 1 || brp > 1024)
                continue;

            int tseg1 = qRound(target * n) - 1;
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

            const double actual = actualBps(clk, p);
            const double baudErr = qAbs(actual - static_cast<double>(bps)) / static_cast<double>(bps);
            const double spErr = qAbs(samplePoint(p) / 100.0 - target);

            // 波特率误差权重更高
            const double score = baudErr * 100.0 + spErr;

            // 仅在误差相同时用稳健性/Tq 数做 tie-break：
            // 1) 优先 sjw >= 2（抗抖动）；2) 其次 Tq 数接近 preferredTq（经典 CAN 约 16）
            const bool robust = (p.sjw >= 2);
            const int tqDist = qAbs(n - preferredTq);

            constexpr double kEps = 1e-9;
            bool better = false;
            if (!found) {
                better = true;
            } else if (score < bestErr - kEps) {
                better = true;
            } else if (score <= bestErr + kEps) {
                if (robust != bestRobust)
                    better = robust;
                else if (tqDist < bestTqDist)
                    better = true;
            }

            if (better) {
                found = true;
                bestErr = score;
                bestRobust = robust;
                bestTqDist = tqDist;
                bestParams = p;
                result.exact = exactOnly;
            }
        }
    }

    if (!found) {
        result.ok = false;
        result.message = QStringLiteral("无法为 %1 bps 找到合适的位定时参数（clk=%2）").arg(bps).arg(clk);
        return result;
    }

    result.ok = true;
    result.params = bestParams;
    result.actualBps = actualBps(clk, bestParams);
    result.samplePoint = samplePoint(bestParams);
    result.message = makeMessage(clk, bps, bestParams, result.exact, result.actualBps, result.samplePoint);
    return result;
}

double BitTiming::actualBps(quint32 clk, const BitTimingParams &p)
{
    if (p.brp == 0)
        return 0.0;
    const int totalTq = 1 + p.tseg1 + p.tseg2;
    if (totalTq <= 0)
        return 0.0;
    return static_cast<double>(clk) / (static_cast<double>(p.brp) * totalTq);
}

double BitTiming::samplePoint(const BitTimingParams &p)
{
    const int totalTq = 1 + p.tseg1 + p.tseg2;
    if (totalTq <= 0)
        return 0.0;
    return 100.0 * (1 + p.tseg1) / totalTq;
}
