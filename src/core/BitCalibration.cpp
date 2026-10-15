#include "core/BitCalibration.h"

#include <QCoreApplication>
#include <QSettings>

namespace {

constexpr char kSeededKey[] = "calibration/seeded";

// 保证 App 与各诊断工具使用同一份 QSettings（同一 org/app 名）
void ensureAppIdentity()
{
    if (QCoreApplication::organizationName().isEmpty())
        QCoreApplication::setOrganizationName(QStringLiteral("CanDebugger"));
    if (QCoreApplication::applicationName().isEmpty())
        QCoreApplication::setApplicationName(QStringLiteral("USBCANFD 调试器"));
}

} // namespace

BitCalibration &BitCalibration::instance()
{
    static BitCalibration inst;
    return inst;
}

BitCalibration::BitCalibration()
{
    reload();
}

void BitCalibration::seedDefaults()
{
    // 实测标定（device_bitrate_scan + 外部 ZLG 对端）：
    //   250k：名义 60MHz（实际 48MHz）brp=12 tseg1=13 tseg2=2 sjw=2
    //   500k：名义 80MHz（实际 64MHz）brp=8  tseg1=13 tseg2=2 sjw=2
    BitTimingParams p250;
    p250.brp = 12;
    p250.tseg1 = 13;
    p250.tseg2 = 2;
    p250.sjw = 2;
    p250.smp = 0;
    set(250000, BitTiming::DefaultClock, p250, QStringLiteral("内置"));

    BitTimingParams p500;
    p500.brp = 8;
    p500.tseg1 = 13;
    p500.tseg2 = 2;
    p500.sjw = 2;
    p500.smp = 0;
    set(500000, BitTiming::Clock80M, p500, QStringLiteral("内置"));

    QSettings settings;
    settings.setValue(QLatin1String(kSeededKey), true);
}

void BitCalibration::reload()
{
    ensureAppIdentity();
    m_entries.clear();
    QSettings settings;

    settings.beginGroup(QStringLiteral("calibration"));
    const QStringList keys = settings.childGroups();
    for (const QString &k : keys) {
        bool ok = false;
        const int bps = k.toInt(&ok);
        if (!ok || bps <= 0)
            continue;
        settings.beginGroup(k);
        CalibrationEntry e;
        e.valid = settings.value(QStringLiteral("valid"), false).toBool();
        e.clock = settings.value(QStringLiteral("clock"), 0).toUInt();
        e.params.brp = static_cast<quint16>(settings.value(QStringLiteral("brp"), 0).toUInt());
        e.params.tseg1 = static_cast<quint8>(settings.value(QStringLiteral("tseg1"), 0).toUInt());
        e.params.tseg2 = static_cast<quint8>(settings.value(QStringLiteral("tseg2"), 0).toUInt());
        e.params.sjw = static_cast<quint8>(settings.value(QStringLiteral("sjw"), 0).toUInt());
        e.params.smp = static_cast<quint8>(settings.value(QStringLiteral("smp"), 0).toUInt());
        e.source = settings.value(QStringLiteral("source")).toString();
        settings.endGroup();
        if (e.valid)
            m_entries.insert(bps, e);
    }
    settings.endGroup();

    if (!settings.value(QLatin1String(kSeededKey), false).toBool())
        seedDefaults();
}

void BitCalibration::save() const
{
    ensureAppIdentity();
    QSettings settings;
    settings.remove(QStringLiteral("calibration"));
    settings.beginGroup(QStringLiteral("calibration"));
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        if (!it->valid)
            continue;
        settings.beginGroup(QString::number(it.key()));
        settings.setValue(QStringLiteral("valid"), true);
        settings.setValue(QStringLiteral("clock"), it->clock);
        settings.setValue(QStringLiteral("brp"), it->params.brp);
        settings.setValue(QStringLiteral("tseg1"), it->params.tseg1);
        settings.setValue(QStringLiteral("tseg2"), it->params.tseg2);
        settings.setValue(QStringLiteral("sjw"), it->params.sjw);
        settings.setValue(QStringLiteral("smp"), it->params.smp);
        settings.setValue(QStringLiteral("source"), it->source);
        settings.endGroup();
    }
    settings.endGroup();
    settings.setValue(QLatin1String(kSeededKey), true);
    settings.sync();
}

CalibrationEntry BitCalibration::lookup(int bps) const
{
    return m_entries.value(bps);
}

void BitCalibration::set(int bps, quint32 clock, const BitTimingParams &params, const QString &source)
{
    if (bps <= 0)
        return;
    CalibrationEntry e;
    e.valid = true;
    e.clock = clock;
    e.params = params;
    e.source = source;
    m_entries.insert(bps, e);
    save();
}

void BitCalibration::remove(int bps)
{
    m_entries.remove(bps);
    save();
}

QList<int> BitCalibration::rates() const
{
    return m_entries.keys();
}
