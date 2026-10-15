#pragma once

#include <QList>
#include <QMap>
#include <QString>

#include "core/BitTiming.h"

// 位定时标定表：SDK 的实际分频并非简单的 clk/(brp*tq)（实测 250k 与 500k 落在不同
// 名义时钟档，且 brp*tq 相同的组合表现不同），因此对"已验证过"的速率直接存参数，
// 优先于公式使用；未标定的速率退回标准公式。
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
