#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "zcan.h"

#include "core/FrameData.h"

class CanDevice;
class Recorder;
class Statistics;
class AsyncExecutor;

// 单帧描述，构建 ZCAN_20_MSG / ZCAN_FD_MSG 的纯数据输入
struct FrameSpec
{
    quint32 id = 0x123;
    bool extended = false;   // 扩展帧
    bool fd = false;         // CANFD
    bool remote = false;     // 远程帧
    bool brs = false;        // 位速率切换（仅 FD）
    int dlc = 8;             // 数据长度 0..8(CAN) / 0..64(FD)，数据不足补零、超出截断
    QByteArray data;         // 原始数据字节（未经数据生成修饰）

    int txm = 2;             // ZCAN_TX_MODE：0正常/1单次/2自发自收/3自发自收单次
    bool echo = true;        // 发送回显

    bool crc = false;        // 末字节写入 CRC-8（对前 n-1 字节）
    bool rolling = false;    // 首字节写入滚动计数
    bool increment = false;  // 逐字节递增填充
    bool incrementId = false; // ID 递增：ID = 基址 + 计数器（按标准/扩展帧位宽回绕）

    quint32 counter = 0;     // 滚动计数/ID 递增当前值（由发送方维护）
};

// CAN 帧构建与批量发送。纯逻辑部分为静态方法，便于单测（无需设备）。
class FrameBuilder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int sentCount READ sentCount NOTIFY sentCountChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    enum DataFormat { HexFormat = 0, AsciiFormat = 1, DecimalFormat = 2 };
    Q_ENUM(DataFormat)

    explicit FrameBuilder(CanDevice *device, QObject *parent = nullptr);
    ~FrameBuilder() override;

    // ---- 纯逻辑（静态，可单测） ----
    static QByteArray parseData(const QString &text, int format, int maxBytes, QString *error = nullptr);
    static QString formatData(const QByteArray &data, int format);
    static quint32 clampId(quint32 id, bool extended);

    // 应用「ID 递增」：incrementId 为假时等价于 clampId(baseId)（不递增）；
    // 为真时返回 (基址 + 计数器) 并按位宽回绕（标准 0x7FF / 扩展 0x1FFFFFFF）。
    static quint32 idForCounter(quint32 baseId, bool extended, bool incrementId, quint32 counter);

    static quint8 crc8(const QByteArray &data);

    // 数据生成修饰：递增填充 -> 滚动计数(首字节) -> CRC(末字节)
    static QByteArray applyModifiers(const QByteArray &data, const FrameSpec &spec, int maxBytes);

    static QVariantMap specToMap(const FrameSpec &spec, int dataFormat = HexFormat);

    // 从 QML 变体映射解析帧描述；失败返回 false 并写入 error
    static bool specFromMap(const QVariantMap &map, FrameSpec *spec, QString *error = nullptr);

    static bool buildCan(const FrameSpec &spec, ZCAN_20_MSG *out, QString *error = nullptr);
    static bool buildFd(const FrameSpec &spec, ZCAN_FD_MSG *out, QString *error = nullptr);

    // 构建定时发送用帧：始终写入 ZCAN_FD_MSG，按 spec.fd 设置 fmt/长度上限
    static bool buildTtx(const FrameSpec &spec, ZCAN_FD_MSG *out, QString *error = nullptr);

    // 生成人类可读摘要；解析失败返回“错误: …”
    static QString describe(const QVariantMap &map);

    // ---- QML 接口 ----
    Q_INVOKABLE QString convertData(const QString &text, int fromFormat, int toFormat, int maxBytes) const;
    Q_INVOKABLE int dataByteCount(const QString &text, int format, int maxBytes) const;

    Q_INVOKABLE bool sendFrame(int port, const QVariantMap &spec);
    Q_INVOKABLE int sendBatch(int port, const QVariantList &specs);

    // QML：异步发送（后台线程执行，避免 GUI 阻塞）；结果经 sendCompleted 回传
    Q_INVOKABLE bool sendFrameAsync(int port, const QVariantMap &spec);
    Q_INVOKABLE int sendBatchAsync(int port, const QVariantList &specs);

    // 成功发送时向统计引擎累计（可为空）
    void setStatistics(Statistics *stats);

    // 成功发送时记录发送路径 tx 帧（可为空），避免与接收侧回显重复
    void setRecorder(Recorder *recorder);

    int sentCount() const;
    QString lastError() const;
    bool busy() const;

signals:
    void sentCountChanged();
    void lastErrorChanged();
    void busyChanged();
    void sendCompleted(bool ok, const QString &description, const QString &error);

private:
    static int maxLenFor(const FrameSpec &spec);
    void setError(const QString &msg);
    // 解析/构建并入队一帧；失败同步返回 false（描述与错误同步给出）
    bool enqueueOne(int port, const QVariantMap &specMap);
    void finishSend(bool ok, const QString &description, const std::shared_ptr<FrameData> &record);

    CanDevice *m_device = nullptr;
    Statistics *m_stats = nullptr;
    Recorder *m_recorder = nullptr;
    AsyncExecutor *m_executor = nullptr;
    quint32 m_counter = 0;
    int m_sentCount = 0;
    QString m_lastError;
};
