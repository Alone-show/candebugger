#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include "zcan.h"

class CanDevice;
struct FrameData;
class AsyncExecutor;

// 软件滤波配置：用于显示层（模型代理），修改后即时生效
struct FrameFilterConfig
{
    bool idEnabled = false;        // 是否启用 ID 过滤
    bool idExtended = false;       // ID 过滤面向的帧类型（决定取值范围与 UI 上限）
    quint32 idMin = 0;
    quint32 idMax = 0x7FF;

    int direction = 0;             // 0=全部, 1=仅接收(Rx), 2=仅发送(Tx)

    bool showStandard = true;      // 标准帧
    bool showExtended = true;      // 扩展帧
    bool showCan = true;           // CAN2.0
    bool showFd = true;            // CANFD
    bool showRemote = true;        // 远程帧
    bool showError = true;         // 错误帧

    QString searchText;            // 文本搜索：ID 十六进制 / 数据 Hex 子串（忽略空格与大小写）
};

// 硬件验收滤波条目：对应 ZCAN_FILTER 的 [sid, eid] 闭区间范围
struct HardwareFilterRule
{
    bool enabled = true;
    bool extended = false;
    quint32 startId = 0;
    quint32 endId = 0;
};

// CAN 滤波：软件过滤（显示）+ 硬件验收滤波（CMD_CAN_FILTER）。
// 纯逻辑部分为静态方法，便于单测（无需设备）。
class CanFilter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool idEnabled READ idEnabled WRITE setIdEnabled NOTIFY softwareFilterChanged)
    Q_PROPERTY(bool idExtended READ idExtended WRITE setIdExtended NOTIFY softwareFilterChanged)
    Q_PROPERTY(quint32 idMin READ idMin WRITE setIdMin NOTIFY softwareFilterChanged)
    Q_PROPERTY(quint32 idMax READ idMax WRITE setIdMax NOTIFY softwareFilterChanged)
    Q_PROPERTY(int direction READ direction WRITE setDirection NOTIFY softwareFilterChanged)
    Q_PROPERTY(bool showStandard READ showStandard WRITE setShowStandard NOTIFY softwareFilterChanged)
    Q_PROPERTY(bool showExtended READ showExtended WRITE setShowExtended NOTIFY softwareFilterChanged)
    Q_PROPERTY(bool showCan READ showCan WRITE setShowCan NOTIFY softwareFilterChanged)
    Q_PROPERTY(bool showFd READ showFd WRITE setShowFd NOTIFY softwareFilterChanged)
    Q_PROPERTY(bool showRemote READ showRemote WRITE setShowRemote NOTIFY softwareFilterChanged)
    Q_PROPERTY(bool showError READ showError WRITE setShowError NOTIFY softwareFilterChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY softwareFilterChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit CanFilter(CanDevice *device = nullptr, QObject *parent = nullptr);

    // ---- 纯逻辑（静态，可单测） ----
    static bool matchesConfig(const FrameFilterConfig &cfg, const FrameData &f);

    // 文本搜索：ID(十六进制，可含 0x) 或数据 Hex 子串，忽略空格与大小写
    static bool matchesSearch(const QString &query, const FrameData &f);

    // 由规则构建 ZCAN_FILTER_TABLE；table.size 取字节数（参考 SDK demo）
    static bool buildHardwareTable(const QVector<HardwareFilterRule> &rules,
                                   ZCAN_FILTER_TABLE *table, QString *error = nullptr);

    static QVector<HardwareFilterRule> rulesFromList(const QVariantList &ranges);

    // 按当前配置匹配
    bool matches(const FrameData &f) const;
    FrameFilterConfig config() const;
    void setConfig(const FrameFilterConfig &cfg);

    // 硬件滤波：下发（空列表=关闭）/ 清除
    Q_INVOKABLE bool applyHardware(int port, const QVariantList &ranges);
    Q_INVOKABLE bool applySoftware(int port,
                                   bool idEnabled, bool idExtended,
                                   quint32 idMin, quint32 idMax,
                                   int direction,
                                   bool showStandard, bool showExtended,
                                   bool showCan, bool showFd, bool showRemote, bool showError);
    Q_INVOKABLE bool clearHardware(int port);
    // QML：异步版本（后台线程执行，避免 GUI 阻塞）；结果经 hardwareApplyFinished 回传
    Q_INVOKABLE bool applyHardwareAsync(int port, const QVariantList &ranges);
    Q_INVOKABLE bool clearHardwareAsync(int port);
    Q_INVOKABLE void resetSoftware();

    bool idEnabled() const;
    bool idExtended() const;
    quint32 idMin() const;
    quint32 idMax() const;
    int direction() const;
    bool showStandard() const;
    bool showExtended() const;
    bool showCan() const;
    bool showFd() const;
    bool showRemote() const;
    bool showError() const;
    QString searchText() const;
    QString lastError() const;
    bool busy() const;

public slots:
    void setIdEnabled(bool on);
    void setIdExtended(bool on);
    void setIdMin(quint32 v);
    void setIdMax(quint32 v);
    void setDirection(int v);
    void setShowStandard(bool on);
    void setShowExtended(bool on);
    void setShowCan(bool on);
    void setShowFd(bool on);
    void setShowRemote(bool on);
    void setShowError(bool on);
    void setSearchText(const QString &v);

signals:
    void softwareFilterChanged();
    void lastErrorChanged();
    void busyChanged();
    void hardwareApplyFinished(bool ok, const QString &error);

private:
    void setError(const QString &msg);
    void notifySoftware();

    CanDevice *m_device = nullptr;
    AsyncExecutor *m_executor = nullptr;
    FrameFilterConfig m_config;
    QString m_lastError;
};
