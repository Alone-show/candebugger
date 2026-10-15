#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include "core/BitTiming.h"

// 位定时标定表：对「实机验证过」的速率直接存参数，优先于公式使用，便于
//   1) 固定住已实测的速率档（换机器/清配置也有内置值，见 seedDefaults）；
//   2) 现场用 device_calibrate 标定非标准/特殊速率，或手工覆盖。
// 公式（ZLG 约定）已能在 60M/80M 下精确算出 125k~1M，未标定的速率退回公式即可。
struct CalibrationEntry
{
    bool valid = false;
    quint32 clock = 0;   // 名义时钟（即下发给 SDK 的 ZCAN_INIT.clk）
    BitTimingParams params;
    QString source;      // "内置" / "实测" / "手工"
};

class BitCalibration
{
public:
    static BitCalibration &instance();

    CalibrationEntry lookup(int bps) const;
    void set(int bps, quint32 clock, const BitTimingParams &params, const QString &source);
    void remove(int bps);
    QList<int> rates() const;

    void reload();
    void save() const;

private:
    BitCalibration();
    void seedDefaults();

    QMap<int, CalibrationEntry> m_entries;
};
