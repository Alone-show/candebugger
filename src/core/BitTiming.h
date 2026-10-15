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
    // CAN 控制器时钟：
    //   SDK 的 ZCAN_INIT.clk 是「名义时钟」—— V1.01 传 60MHz、V1.03+ 传 80MHz；
    //   实测（device_bitrate_scan，对端 250k）实际控制器时钟 = 0.8 × 名义值，
    //   即 V1.01 = 48MHz、V1.03+ = 64MHz。位定时公式必须用**实际时钟**，
    //   而传给 SDK 的 clk 仍用名义值，否则算出的 brp 会偏小 1.25 倍、无法通信。
    static constexpr quint32 DefaultClock = 60000000;   // 名义（SDK clk，V1.01）
    static constexpr quint32 Clock80M = 80000000;       // 名义（SDK clk，V1.03+）
    static constexpr quint32 CanClock48M = 48000000;    // 实际（V1.01）
    static constexpr quint32 CanClock64M = 64000000;    // 实际（V1.03+）
    static constexpr quint16 HwV1_03 = 0x0103;

    // 依据硬件版本推断名义时钟（即传给 SDK 的 clk）
    static quint32 clockForHardwareVersion(quint16 hwv);

    // 由名义时钟换算出实际 CAN 控制器时钟（0.8×）
    static quint32 canClockForSdkClock(quint32 sdkClk);

    // 依据时钟、目标波特率、目标采样点(%)，计算 brp/tseg1/tseg2/sjw
    // preferredTq: 期望的每比特 Tq 数（经典 CAN 惯例约 16），仅在误差相同的候选间择优，
    //              用于同时获得较大的 SJW（更抗抖动），不会牺牲波特率/采样点精度。
    static BitTimingResult calculate(quint32 clk,
                                     quint32 bps,
                                     double samplePointPercent = 87.5,
                                     int maxTq = 64,
                                     int preferredTq = 16);

    static double actualBps(quint32 clk, const BitTimingParams &p);
    static double samplePoint(const BitTimingParams &p);
};
