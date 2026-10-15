#include "core/BitTiming.h"

#include <QtMath>

namespace {

// 每比特总 Tq 数的下限：1(sync) + 1(tseg1) + 1(tseg2) 的最小合法组合
constexpr int kMinTotalTq = 5;
// brp 字段（寄存器值）范围：有效分频 = brp + 1。下限取 2 而非 1，
// 即不使用 brp=0（部分固件不接受；60/80MHz 下 5Mbps 以内总存在 brp>=1 的解）。
constexpr quint64 kMinBrpDiv = 2;
constexpr quint64 kMaxBrpDiv = 1025;

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
    // SDK 说明：V1.01 时钟 60MHz，V1.03 及以上 80MHz
    return hwv >= HwV1_03 ? Clock80M : DefaultClock;
}

bool BitTiming::paramsForTq(quint32 clk, quint32 bps, double samplePointPercent,
                           int totalTq, BitTimingParams *out)
{
    if (!out || bps == 0 || clk == 0)
        return false;
    if (totalTq < kMinTotalTq)
        return false;

    const quint64 denom = static_cast<quint64>(bps) * static_cast<quint64>(totalTq);
    if (denom == 0 || (clk % denom) != 0)
        return false;

    const quint64 brpDiv = clk / denom;      // = brp + 1
    if (brpDiv < kMinBrpDiv || brpDiv > kMaxBrpDiv)
        return false;

    const double target = qBound(0.50, samplePointPercent / 100.0, 0.95);
    int tseg1 = qRound(target * totalTq) - 2;
    tseg1 = qBound(1, tseg1, totalTq - 4);   // 保证 tseg2 >= 1
    const int tseg2 = totalTq - 3 - tseg1;
    if (tseg2 < 1)
        return false;

    out->brp = static_cast<quint16>(brpDiv - 1);
    out->tseg1 = static_cast<quint8>(tseg1);
    out->tseg2 = static_cast<quint8>(tseg2);
    out->sjw = static_cast<quint8>(qMin(4, tseg2));
    out->smp = 0;
    return true;
}

BitTimingResult BitTiming::calculate(quint32 clk, quint32 bps, double samplePointPercent,
                                     int maxTq, int preferredTq)
{
    BitTimingResult result;

    if (clk == 0 || bps == 0) {
        result.message = QStringLiteral("时钟或波特率无效");
        return result;
    }
    if (maxTq < kMinTotalTq)
        maxTq = kMinTotalTq;
    if (maxTq > 255)
        maxTq = 255;
    if (preferredTq < kMinTotalTq)
        preferredTq = kMinTotalTq;

    const double target = qBound(0.50, samplePointPercent / 100.0, 0.95);

    bool found = false;
    double bestErr = 1e9;
    int bestTqDist = 0;
    int bestSjw = 0;
    BitTimingParams bestParams;

    for (int pass = 0; pass < 2 && !found; ++pass) {
        const bool exactOnly = (pass == 0);
        for (int n = kMinTotalTq; n <= maxTq; ++n) {
            BitTimingParams p;
            if (exactOnly) {
                // 精确匹配：clk 必须能被 bps * n 整除
                if (!paramsForTq(clk, bps, samplePointPercent, n, &p))
                    continue;
            } else {
                // 近似回退：brp 取最近整数，采样点仍按目标求解
                const quint64 denom = static_cast<quint64>(bps) * static_cast<quint64>(n);
                if (denom == 0)
                    continue;
                const quint64 brpDiv =
                    static_cast<quint64>(qRound(static_cast<double>(clk) / static_cast<double>(denom)));
                if (brpDiv < kMinBrpDiv || brpDiv > kMaxBrpDiv)
                    continue;
                int tseg1 = qRound(target * n) - 2;
                tseg1 = qBound(1, tseg1, n - 4);
                const int tseg2 = n - 3 - tseg1;
                if (tseg2 < 1)
                    continue;
                p.brp = static_cast<quint16>(brpDiv - 1);
                p.tseg1 = static_cast<quint8>(tseg1);
                p.tseg2 = static_cast<quint8>(tseg2);
                p.sjw = static_cast<quint8>(qMin(4, tseg2));
                p.smp = 0;
            }

            const double actual = actualBps(clk, p);
            const double baudErr = qAbs(actual - static_cast<double>(bps)) / static_cast<double>(bps);
            const double spErr = qAbs(samplePoint(p) / 100.0 - target);

            // 波特率误差权重更高
            const double score = baudErr * 100.0 + spErr;

            // 仅在误差相同时用稳健性/Tq 数做 tie-break：
            // 1) 优先总 Tq 数接近 preferredTq（经典 CAN/ZLG 惯例 16）；2) 其次 SJW 更大
            const int tqDist = qAbs(n - preferredTq);
            const int sjw = p.sjw;

            constexpr double kEps = 1e-9;
            bool better = false;
            if (!found) {
                better = true;
            } else if (score < bestErr - kEps) {
                better = true;
            } else if (score <= bestErr + kEps) {
                if (tqDist != bestTqDist)
                    better = (tqDist < bestTqDist);
                else if (sjw != bestSjw)
                    better = (sjw > bestSjw);
            }

            if (better) {
                found = true;
                bestErr = score;
                bestTqDist = tqDist;
                bestSjw = sjw;
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
    const int n = totalTq(p);
    if (n <= 0)
        return 0.0;
    const quint64 div = static_cast<quint64>(p.brp + 1) * static_cast<quint64>(n);
    if (div == 0)
        return 0.0;
    return static_cast<double>(clk) / static_cast<double>(div);
}

double BitTiming::samplePoint(const BitTimingParams &p)
{
    const int n = totalTq(p);
    if (n <= 0)
        return 0.0;
    return 100.0 * (p.tseg1 + 2) / n;
}
