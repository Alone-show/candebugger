#include "core/CanFilter.h"

#include <QVariantMap>
#include <cstring>
#include <memory>

#include "core/AsyncExecutor.h"
#include "core/CanDevice.h"
#include "core/FrameData.h"

CanFilter::CanFilter(CanDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
    m_executor = new AsyncExecutor(this);
    connect(m_executor, &AsyncExecutor::busyChanged, this, &CanFilter::busyChanged);
}

bool CanFilter::matchesConfig(const FrameFilterConfig &cfg, const FrameData &f)
{
    if (cfg.direction == 1 && f.tx)
        return false;
    if (cfg.direction == 2 && !f.tx)
        return false;

    if (f.extended && !cfg.showExtended)
        return false;
    if (!f.extended && !cfg.showStandard)
        return false;

    if (f.error && !cfg.showError)
        return false;
    if (f.remote && !cfg.showRemote)
        return false;
    if (f.fd && !cfg.showFd)
        return false;
    if (!f.fd && !cfg.showCan)
        return false;

    if (cfg.idEnabled) {
        // 按帧自身类型取有效位宽后再与范围比较
        const quint32 id = f.extended ? (f.id & 0x1FFFFFFFu) : (f.id & 0x7FFu);
        if (id < cfg.idMin || id > cfg.idMax)
            return false;
    }

    if (!cfg.searchText.isEmpty() && !matchesSearch(cfg.searchText, f))
        return false;

    return true;
}

bool CanFilter::matchesSearch(const QString &query, const FrameData &f)
{
    QString q = query.trimmed().toLower();
    q.remove(QLatin1Char(' '));
    if (q.startsWith(QLatin1String("0x")))
        q = q.mid(2);
    if (q.isEmpty())
        return true;

    const quint32 id = f.extended ? (f.id & 0x1FFFFFFFu) : (f.id & 0x7FFu);
    if (QString::number(id, 16).toLower().contains(q))
        return true;

    QString dataHex;
    dataHex.reserve(f.data.size() * 2);
    for (int i = 0; i < f.data.size(); ++i)
        dataHex += QString::number(static_cast<quint8>(f.data.at(i)), 16)
                       .rightJustified(2, QLatin1Char('0'));
    return !dataHex.isEmpty() && dataHex.contains(q);
}

bool CanFilter::buildHardwareTable(const QVector<HardwareFilterRule> &rules,
                                   ZCAN_FILTER_TABLE *table, QString *error)
{
    if (error)
        error->clear();
    if (!table) {
        if (error)
            *error = QStringLiteral("输出缓冲区为空");
        return false;
    }

    std::memset(table, 0, sizeof(*table));

    int count = 0;
    for (const HardwareFilterRule &r : rules) {
        if (!r.enabled)
            continue;
        if (count >= 64) {
            if (error)
                *error = QStringLiteral("硬件滤波最多 64 组");
            return false;
        }

        const quint32 maxId = r.extended ? 0x1FFFFFFFu : 0x7FFu;
        quint32 start = qMin(r.startId, maxId) & maxId;
        quint32 end = qMin(r.endId, maxId) & maxId;
        if (start > end)
            std::swap(start, end);

        ZCAN_FILTER &entry = table->table[count++];
        entry.type = r.extended ? 1 : 0;
        entry.pad[0] = entry.pad[1] = entry.pad[2] = 0;
        entry.sid = start;
        entry.eid = end;
    }

    table->size = static_cast<U32>(sizeof(ZCAN_FILTER) * count);
    return true;
}

QVector<HardwareFilterRule> CanFilter::rulesFromList(const QVariantList &ranges)
{
    QVector<HardwareFilterRule> rules;
    rules.reserve(ranges.size());
    for (const QVariant &v : ranges) {
        const QVariantMap m = v.toMap();
        HardwareFilterRule r;
        r.enabled = m.value(QStringLiteral("enabled"), true).toBool();
        r.extended = m.value(QStringLiteral("extended"), false).toBool();
        r.startId = m.value(QStringLiteral("start"), 0).toUInt();
        r.endId = m.value(QStringLiteral("end"), r.startId).toUInt();
        rules.append(r);
    }
    return rules;
}

bool CanFilter::matches(const FrameData &f) const
{
    return matchesConfig(m_config, f);
}

FrameFilterConfig CanFilter::config() const
{
    return m_config;
}

void CanFilter::setConfig(const FrameFilterConfig &cfg)
{
    m_config = cfg;
    notifySoftware();
}

bool CanFilter::applyHardware(int port, const QVariantList &ranges)
{
    ZCAN_FILTER_TABLE table;
    QString err;
    if (!buildHardwareTable(rulesFromList(ranges), &table, &err)) {
        setError(err);
        return false;
    }

    const bool ok = m_device && m_device->setHardwareFilter(static_cast<quint32>(port), table);
    setError(ok ? QString() : QStringLiteral("硬件滤波下发失败（设备未打开？）"));
    return ok;
}

bool CanFilter::applySoftware(int port, bool idEnabled, bool idExtended,
                              quint32 idMin, quint32 idMax, int direction,
                              bool showStandard, bool showExtended,
                              bool showCan, bool showFd, bool showRemote, bool showError)
{
    Q_UNUSED(port);
    FrameFilterConfig cfg;
    cfg.idEnabled = idEnabled;
    cfg.idExtended = idExtended;
    cfg.idMin = idMin;
    cfg.idMax = idMax;
    cfg.direction = qBound(0, direction, 2);
    cfg.showStandard = showStandard;
    cfg.showExtended = showExtended;
    cfg.showCan = showCan;
    cfg.showFd = showFd;
    cfg.showRemote = showRemote;
    cfg.showError = showError;
    cfg.searchText = m_config.searchText;
    setConfig(cfg);
    return true;
}

bool CanFilter::clearHardware(int port)
{
    return applyHardware(port, QVariantList());
}

bool CanFilter::applyHardwareAsync(int port, const QVariantList &ranges)
{
    if (!m_device || !m_device->isOpen()) {
        setError(QStringLiteral("设备未连接"));
        emit hardwareApplyFinished(false, m_lastError);
        return false;
    }

    ZCAN_FILTER_TABLE table;
    QString err;
    if (!buildHardwareTable(rulesFromList(ranges), &table, &err)) {
        setError(err);
        emit hardwareApplyFinished(false, err);
        return false;
    }

    auto res = std::make_shared<bool>(false);
    m_executor->post(
        [this, port, table, res]() {
            *res = m_device->setHardwareFilter(static_cast<quint32>(port), table);
        },
        [this, res]() {
            const bool ok = *res;
            setError(ok ? QString() : QStringLiteral("硬件滤波下发失败（设备未打开？）"));
            emit hardwareApplyFinished(ok, m_lastError);
        });
    return true;
}

bool CanFilter::clearHardwareAsync(int port)
{
    return applyHardwareAsync(port, QVariantList());
}

bool CanFilter::busy() const
{
    return m_executor && m_executor->busy();
}

void CanFilter::resetSoftware()
{
    setConfig(FrameFilterConfig());
}

bool CanFilter::idEnabled() const { return m_config.idEnabled; }
bool CanFilter::idExtended() const { return m_config.idExtended; }
quint32 CanFilter::idMin() const { return m_config.idMin; }
quint32 CanFilter::idMax() const { return m_config.idMax; }
int CanFilter::direction() const { return m_config.direction; }
bool CanFilter::showStandard() const { return m_config.showStandard; }
bool CanFilter::showExtended() const { return m_config.showExtended; }
bool CanFilter::showCan() const { return m_config.showCan; }
bool CanFilter::showFd() const { return m_config.showFd; }
bool CanFilter::showRemote() const { return m_config.showRemote; }
bool CanFilter::showError() const { return m_config.showError; }
QString CanFilter::searchText() const { return m_config.searchText; }
QString CanFilter::lastError() const { return m_lastError; }

void CanFilter::setIdEnabled(bool on) { if (on == m_config.idEnabled) return; m_config.idEnabled = on; notifySoftware(); }
void CanFilter::setIdExtended(bool on) { if (on == m_config.idExtended) return; m_config.idExtended = on; notifySoftware(); }
void CanFilter::setIdMin(quint32 v) { if (v == m_config.idMin) return; m_config.idMin = v; notifySoftware(); }
void CanFilter::setIdMax(quint32 v) { if (v == m_config.idMax) return; m_config.idMax = v; notifySoftware(); }
void CanFilter::setDirection(int v)
{
    v = qBound(0, v, 2);
    if (v == m_config.direction)
        return;
    m_config.direction = v;
    notifySoftware();
}
void CanFilter::setShowStandard(bool on) { if (on == m_config.showStandard) return; m_config.showStandard = on; notifySoftware(); }
void CanFilter::setShowExtended(bool on) { if (on == m_config.showExtended) return; m_config.showExtended = on; notifySoftware(); }
void CanFilter::setShowCan(bool on) { if (on == m_config.showCan) return; m_config.showCan = on; notifySoftware(); }
void CanFilter::setShowFd(bool on) { if (on == m_config.showFd) return; m_config.showFd = on; notifySoftware(); }
void CanFilter::setShowRemote(bool on) { if (on == m_config.showRemote) return; m_config.showRemote = on; notifySoftware(); }
void CanFilter::setShowError(bool on) { if (on == m_config.showError) return; m_config.showError = on; notifySoftware(); }
void CanFilter::setSearchText(const QString &v)
{
    if (v == m_config.searchText)
        return;
    m_config.searchText = v;
    notifySoftware();
}

void CanFilter::setError(const QString &msg)
{
    if (msg == m_lastError)
        return;
    m_lastError = msg;
    emit lastErrorChanged();
}

void CanFilter::notifySoftware()
{
    emit softwareFilterChanged();
}
