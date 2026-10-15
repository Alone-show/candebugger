#include "core/BitCalibration.h"

#include <QCoreApplication>
#include <QSettings>

namespace {

// 播种标记（v1 → v2：v1 是按已作废的「0.8×名义时钟」模型标出来的值，必须清理）
constexpr char kSeededKeyV2[] = "calibration/seeded_v2";
constexpr char kSeededKeyV1[] = "calibration/seeded";

// 旧模型的标定来源标记；迁移时这些条目会被删除后重新播种
bool isModelDerived(const QString &source)
{
    return source == QStringLiteral("内置") || source == QStringLiteral("实测");
}

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
    // 实机实测（USBCANFD-200U，HW=0x0300；device_bitrate_scan + 外部 ZLG 对端）：
    //   ZLG 约定 bps = clk / ((brp+1) * (tseg1+tseg2+3))，clk 即真实时钟。
    //   80MHz + 形状 tseg1=12/tseg2=1（16 Tq、SP 87.5%，CiA/ZLG 标准形状）下：
    //     125k → brp=39（ACK 5/5）、250k → brp=19（ACK 3/3）、
    //     500k → brp=9（ACK 3/3）、 1M   → brp=4（ACK 3/3）
    //   60MHz 同名形状：125k → brp=29（ACK 5/5）。
    struct Seed
    {
        int bps;
        quint32 clock;
        quint16 brp;
    };
    const Seed seeds[] = {
        {125000, BitTiming::Clock80M, 39},
        {250000, BitTiming::Clock80M, 19},
        {500000, BitTiming::Clock80M, 9},
        {1000000, BitTiming::Clock80M, 4},
    };
    for (const Seed &s : seeds) {
        BitTimingParams p;
        p.brp = s.brp;
        p.tseg1 = 12;
        p.tseg2 = 1;
        p.sjw = 1;
        p.smp = 0;
        set(s.bps, s.clock, p, QStringLiteral("实测"));
    }

    QSettings settings;
    settings.setValue(QLatin1String(kSeededKeyV2), true);
}

void BitCalibration::reload()
{
    ensureAppIdentity();
    m_entries.clear();
    QSettings settings;

    const bool seededV2 = settings.value(QLatin1String(kSeededKeyV2), false).toBool();

    settings.beginGroup(QStringLiteral("calibration"));
    const QStringList keys = settings.childGroups();
    QStringList stale;
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

        // v1 → v2：旧模型（0.8× 名义时钟）标出来的「内置/实测」值不再可信，
        // 删除并交由 seedDefaults() 用实测精确值重新播种；用户「手工」填的保留。
        if (!seededV2 && isModelDerived(e.source)) {
            stale << k;
            continue;
        }
        if (e.valid)
            m_entries.insert(bps, e);
    }
    settings.endGroup();

    for (const QString &k : stale)
        settings.remove(QStringLiteral("calibration/") + k);
    settings.setValue(QLatin1String(kSeededKeyV1), QVariant());

    if (!seededV2)
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
    settings.setValue(QLatin1String(kSeededKeyV2), true);
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
