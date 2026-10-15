#pragma once

#include <QString>
#include <QtGlobal>

// CAN / CANFD 位定时参数（对应 zcan.h 中 ZCAN_INIT 的 aset / dset）。
//
// ⚠ 字段语义 = **寄存器值**（= 有效值 − 1），SDK/控制器会各自 +1：
//   有效 brp   = brp   + 1
//   有效 tseg1 = tseg1 + 1
//   有效 tseg2 = tseg2 + 1
// 见下方 BitTiming 注释里的实测结论。
struct BitTimingParams
{
    quint8 tseg1 = 0;
    quint8 tseg2 = 0;
    quint8 sjw = 0;   // 同步跳转宽度（寄存器值 = 有效值 − 1）
    quint8 smp = 0;   // 采样次数：0-采样一次，1-采样三次
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

// 位定时计算：实测标定出的 **ZLG 约定**（2026-10-15 实机判决 + 4 速率交叉验证）
//
//     bps        = clk / ((brp + 1) * (tseg1 + tseg2 + 3))
//     采样点(%)  = 100 * (tseg1 + 2) / (tseg1 + tseg2 + 3)
//
// 其中 (tseg1 + tseg2 + 3) = 1(sync) + (tseg1+1) + (tseg2+1) 即每比特总 Tq 数；
// clk 就是**真实控制器时钟**（V1.01 = 60MHz、V1.03+ = 80MHz），直接原样使用。
//
// 证据（USBCANFD-200U，HW=0x0300）：
//   - clk=80MHz、tseg1=12/tseg2=1（即 16 Tq、SP 87.5%）下
//     125k→brp=39、250k→brp=19、500k→brp=9、1M→brp=4，四档 ACK 3/3~5/5；
//   - clk=60MHz、tseg1=12/tseg2=1 下 125k→brp=29（ACK 5/5）；
//   - ZLG 官方 demo（test.c，clk=60MHz）仲裁 brp5/t14/s3 → (6)×20 = 500k、
//     数据 brp1/t10/s2 → (2)×15 = 2M，两条注释都精确成立。
// 早期"实际时钟 = 0.8 × 名义"的近似模型已作废（它能凑出 250k/500k，但 125k/1M
// 会偏 6.7%/16.7%，正是接外部节点失败的原因）。
class BitTiming
{
public:
    static constexpr quint32 DefaultClock = 60000000;   // V1.01（SDK clk 即真实时钟）
    static constexpr quint32 Clock80M = 80000000;       // V1.03+
    static constexpr quint16 HwV1_03 = 0x0103;

    // 依据硬件版本推断控制器时钟（即传给 SDK 的 clk）
    static quint32 clockForHardwareVersion(quint16 hwv);

    // 依据时钟、目标波特率、目标采样点(%)，计算 brp/tseg1/tseg2/sjw（均为寄存器值）
    // maxTq:      每比特总 Tq 数上限（tseg1+tseg2+3）
    // preferredTq: 期望的总 Tq 数（经典 CAN/ZLG 惯例 16），仅在误差相同的候选间择优
    static BitTimingResult calculate(quint32 clk,
                                     quint32 bps,
                                     double samplePointPercent = 87.5,
                                     int maxTq = 64,
                                     int preferredTq = 16);

    // 由「每比特总 Tq 数」构造一组精确参数：要求 clk 能被 bps*totalTq 整除，
    // 且 brp 字段 >= 1（不使用 brp=0）、tseg1/tseg2 >= 1。
    // 供扫描/标定工具枚举候选，避免各处重复实现约定。
    static bool paramsForTq(quint32 clk, quint32 bps, double samplePointPercent,
                            int totalTq, BitTimingParams *out);

    // 每比特总 Tq 数 = 1 + (tseg1+1) + (tseg2+1)
    static int totalTq(const BitTimingParams &p) { return p.tseg1 + p.tseg2 + 3; }

    static double actualBps(quint32 clk, const BitTimingParams &p);
    static double samplePoint(const BitTimingParams &p);
};
