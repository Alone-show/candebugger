#pragma once

#include <QString>
#include <QtGlobal>

// CAN / CANFD 位定时参数（对应 zcan.h 中 ZCAN_INIT 的 aset / dset）
struct BitTimingParams
{
    quint8 tseg1 = 0;
    quint8 tseg2 = 0;
    quint8 sjw = 0;
    quint8 smp = 0;   // 0-采样一次, 1-采样三次
    quint16 brp = 1;
};

struct BitTimingResult
{
    bool ok = false;
    bool exact = true;      // 是否由整数分频精确得到
    BitTimingParams params;
    double actualBps = 0.0;
    double samplePoint = 0.0;   // 百分比
    QString message;
};

// 位定时计算：baud = clk / (brp * (1 + tseg1 + tseg2))
class BitTiming
{
public:
    static constexpr quint32 DefaultClock = 60000000;

    // 依据时钟、目标波特率、目标采样点(%)，计算 brp/tseg1/tseg2/sjw
    static BitTimingResult calculate(quint32 clk,
                                     quint32 bps,
                                     double samplePointPercent = 87.5,
                                     int maxTq = 64);

    static double actualBps(quint32 clk, const BitTimingParams &p);
    static double samplePoint(const BitTimingParams &p);
};
