#include "core/PeriodicSender.h"

#include <QMetaObject>
#include <QVariantMap>
#include <cstring>
#include <memory>

#include "core/AsyncExecutor.h"
#include "core/CanDevice.h"
#include "core/FrameBuilder.h"

PeriodicSender::PeriodicSender(CanDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
    m_executor = new AsyncExecutor(this);
    connect(m_executor, &AsyncExecutor::busyChanged, this, &PeriodicSender::busyChanged);
}

quint32 PeriodicSender::intervalToUnit(double intervalMs)
{
    if (intervalMs <= 0.0)
        return 1u;
    const qint64 unit = qRound64(intervalMs * 10.0);   // 1 unit = 100us
    return static_cast<quint32>(qBound<qint64>(1, unit, 0xFFFFFFFFLL));
}

bool PeriodicSender::buildConfig(const QVariantList &entries, ZCAN_TTX_CFG *cfg, QString *error)
{
    if (error)
        error->clear();
    if (!cfg) {
        if (error)
            *error = QStringLiteral("配置缓冲区为空");
        return false;
    }
    if (entries.size() > MaxEntries) {
        if (error)
            *error = QStringLiteral("定时发送最多 %1 条").arg(MaxEntries);
        return false;
    }

    std::memset(cfg, 0, sizeof(*cfg));
    // 注意：参考 demo（test.c auto_send）size 取“字节数”，而非条目数
    cfg->size = static_cast<U32>(sizeof(ZCAN_TTX) * entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const QVariantMap map = entries.at(i).toMap();

        FrameSpec spec;
        QString err;
        if (!FrameBuilder::specFromMap(map, &spec, &err)) {
            if (error)
                *error = QStringLiteral("第 %1 条：%2").arg(i + 1).arg(err);
            return false;
        }

        ZCAN_FD_MSG msg;
        if (!FrameBuilder::buildTtx(spec, &msg, &err)) {
            if (error)
                *error = QStringLiteral("第 %1 条：%2").arg(i + 1).arg(err);
            return false;
        }

        ZCAN_TTX &row = cfg->table[i];
        row.msg = msg;
        row.interval = intervalToUnit(map.value(QStringLiteral("intervalMs"), 100.0).toDouble());
        row.repeat = static_cast<U16>(qBound(0, map.value(QStringLiteral("repeat"), 0).toInt(), 0xFFFF));
        row.index = static_cast<U8>(i);
        row.flags = map.value(QStringLiteral("enabled"), true).toBool() ? 1u : 0u;
    }

    return true;
}

bool PeriodicSender::apply(int port, const QVariantList &entries)
{
    ZCAN_TTX_CFG cfg;
    QString err;
    if (!buildConfig(entries, &cfg, &err)) {
        setError(err);
        return false;
    }

    const bool ok = m_device && m_device->setPeriodicConfig(static_cast<quint32>(port), cfg);
    if (ok) {
        if (m_count != entries.size()) {
            m_count = entries.size();
            emit countChanged();
        }
        setError(QString());
    } else {
        setError(QStringLiteral("下发定时发送配置失败（设备未打开或参数无效）"));
    }
    return ok;
}

bool PeriodicSender::start(int port)
{
    const bool ok = m_device && m_device->setPeriodicEnabled(static_cast<quint32>(port), true);
    if (ok) {
        setActive(true);
        setError(QString());
    } else {
        setError(QStringLiteral("启动定时发送失败"));
    }
    return ok;
}

bool PeriodicSender::stop(int port)
{
    const bool ok = m_device && m_device->setPeriodicEnabled(static_cast<quint32>(port), false);
    if (ok) {
        setActive(false);
        setError(QString());
    } else {
        setError(QStringLiteral("停止定时发送失败"));
    }
    return ok;
}

// ---------------------------------------------------------------- 异步版本

void PeriodicSender::setActive(bool on)
{
    if (m_active == on)
        return;
    m_active = on;
    emit activeChanged();
}

void PeriodicSender::applyResult(bool ok, const QString &err, int count)
{
    if (ok) {
        if (m_count != count) {
            m_count = count;
            emit countChanged();
        }
        setError(QString());
        emit operationFinished(true, QStringLiteral("已下发 %1 条定时发送配置").arg(count));
    } else {
        setError(err);
        emit operationFinished(false, err);
    }
}

bool PeriodicSender::applyAsync(int port, const QVariantList &entries)
{
    if (!m_device || !m_device->isOpen()) {
        setError(QStringLiteral("设备未连接"));
        emit operationFinished(false, m_lastError);
        return false;
    }

    QVariantList copy = entries;
    const int n = copy.size();
    auto res = std::make_shared<QPair<bool, QString>>(false, QString());
    m_executor->post(
        [this, port, copy, res]() {
            ZCAN_TTX_CFG cfg;
            QString err;
            bool ok = buildConfig(copy, &cfg, &err);
            if (ok)
                ok = m_device->setPeriodicConfig(static_cast<quint32>(port), cfg);
            if (!ok && err.isEmpty())
                err = QStringLiteral("下发定时发送配置失败（设备未连接或参数无效）");
            *res = qMakePair(ok, err);
        },
        [this, res, n]() { applyResult(res->first, res->second, n); });
    return true;
}

bool PeriodicSender::startAsync(int port)
{
    if (!m_device || !m_device->isOpen()) {
        setError(QStringLiteral("设备未连接"));
        emit operationFinished(false, m_lastError);
        return false;
    }

    auto res = std::make_shared<bool>(false);
    m_executor->post(
        [this, port, res]() { *res = m_device->setPeriodicEnabled(static_cast<quint32>(port), true); },
        [this, res]() {
            if (*res) {
                setActive(true);
                setError(QString());
                emit operationFinished(true, QStringLiteral("定时发送已启动"));
            } else {
                setError(QStringLiteral("启动定时发送失败"));
                emit operationFinished(false, m_lastError);
            }
        });
    return true;
}

bool PeriodicSender::stopAsync(int port)
{
    if (!m_device || !m_device->isOpen()) {
        setError(QStringLiteral("设备未连接"));
        emit operationFinished(false, m_lastError);
        return false;
    }

    auto res = std::make_shared<bool>(false);
    m_executor->post(
        [this, port, res]() { *res = m_device->setPeriodicEnabled(static_cast<quint32>(port), false); },
        [this, res]() {
            if (*res) {
                setActive(false);
                setError(QString());
                emit operationFinished(true, QStringLiteral("定时发送已停止"));
            } else {
                setError(QStringLiteral("停止定时发送失败"));
                emit operationFinished(false, m_lastError);
            }
        });
    return true;
}

bool PeriodicSender::busy() const
{
    return m_executor && m_executor->busy();
}

bool PeriodicSender::setRowEnabled(int port, const QVariantList &entries, int rowIndex, bool enabled)
{
    if (rowIndex < 0 || rowIndex >= entries.size()) {
        setError(QStringLiteral("条目索引越界"));
        return false;
    }
    QVariantList updated = entries;
    QVariantMap row = updated.at(rowIndex).toMap();
    row.insert(QStringLiteral("enabled"), enabled);
    updated[rowIndex] = row;
    return apply(port, updated);
}

bool PeriodicSender::active() const
{
    return m_active;
}

int PeriodicSender::count() const
{
    return m_count;
}

QString PeriodicSender::lastError() const
{
    return m_lastError;
}

void PeriodicSender::setError(const QString &msg)
{
    if (m_lastError == msg)
        return;
    m_lastError = msg;
    emit lastErrorChanged();
}
