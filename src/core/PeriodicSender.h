#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>

#include "zcan.h"

class CanDevice;
class AsyncExecutor;

// 硬件定时发送封装：CMD_CAN_TTX 下发配置表，CMD_CAN_TTX_CTL 整体启停。
// 每条条目为一个 QVariantMap：enabled/intervalMs/repeat + FrameBuilder 帧字段。
class PeriodicSender : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    static constexpr int MaxEntries = 8;

    explicit PeriodicSender(CanDevice *device, QObject *parent = nullptr);

    // 纯逻辑：由条目构建 ZCAN_TTX_CFG（帧走 FrameBuilder），可单测
    static bool buildConfig(const QVariantList &entries, ZCAN_TTX_CFG *cfg, QString *error = nullptr);

    // 周期毫秒 -> 硬件单位(100us)，最小 1
    static quint32 intervalToUnit(double intervalMs);

    // QML：下发配置表 / 整体启停 / 单条启停（重下发整表）——同步，供测试/CLI
    Q_INVOKABLE bool apply(int port, const QVariantList &entries);
    Q_INVOKABLE bool start(int port);
    Q_INVOKABLE bool stop(int port);
    Q_INVOKABLE bool setRowEnabled(int port, const QVariantList &entries, int rowIndex, bool enabled);

    // QML：异步版本（在后台线程执行，避免 GUI 阻塞）；结果经 operationFinished 回传
    Q_INVOKABLE bool applyAsync(int port, const QVariantList &entries);
    Q_INVOKABLE bool startAsync(int port);
    Q_INVOKABLE bool stopAsync(int port);

    bool active() const;
    int count() const;
    QString lastError() const;
    bool busy() const;

signals:
    void activeChanged();
    void countChanged();
    void lastErrorChanged();
    void busyChanged();
    void operationFinished(bool ok, const QString &message);

private:
    void setError(const QString &msg);
    void setActive(bool on);
    void applyResult(bool ok, const QString &err, int count);

    CanDevice *m_device = nullptr;
    AsyncExecutor *m_executor = nullptr;
    bool m_active = false;
    int m_count = 0;
    QString m_lastError;
};
