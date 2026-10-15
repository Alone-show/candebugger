#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "core/FrameBuilder.h"   // FrameSpec

class CanDevice;
class Statistics;
class Recorder;

// 软件定时发送：由 PC 侧精确定时线程按周期经 canDevice.transmitCan/transmitFd 逐帧发送，
// 不再使用硬件定时表（CMD_CAN_TTX），因此：
//   - 条数不受固件 8 条限制；
//   - 每个通道各自独立的配置表与启停状态。
//
// 线程模型：
//   - 调度线程：以单调时钟（steady_clock）绝对时间计算各通道到期条目，推入有界发送队列；
//     绝不调用 SDK、绝不被阻塞，因此不受 VCI_Transmit 偶发卡顿影响。
//   - 发送线程：唯一执行阻塞式 SDK 发送的地方。
//   - 对 QML 可见的状态变化一律回投 GUI 线程后再发信号（QML 绑定必须在 GUI 线程执行）。
//
// 两通道共用同一组线程：CanDevice 内部已用 IO 锁串行化，且 USB 设备是共享资源，
// 拆分线程仍会在设备锁上排队，恒为 2 条线程即可。
class PeriodicSender : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList channelStates READ channelStates NOTIFY channelStatesChanged)
    Q_PROPERTY(int count READ totalCount NOTIFY channelStatesChanged)
    Q_PROPERTY(bool active READ anyActive NOTIFY channelStatesChanged)

public:
    static constexpr int ChannelCount = 2;

    // 单条周期项：帧 + 周期/次数/启用状态 + 调度进度
    struct PeriodicRow
    {
        FrameSpec spec;
        qint64 intervalUs = 100000;   // 周期（微秒）
        quint32 repeat = 0;           // 0 = 无限循环，N = 发 N 次后停止
        bool enabled = true;
        quint32 sent = 0;             // 已发送次数
        qint64 nextDueUs = 0;         // 下次到期时刻（单调时钟，0 = 立即）
        bool done = false;            // 次数耗尽/已停
        quint32 counter = 0;          // 滚动计数当前值
    };

    explicit PeriodicSender(CanDevice *device, QObject *parent = nullptr);
    ~PeriodicSender() override;

    // ---- 纯逻辑（静态，可单测，无需设备） ----
    // QML 条目列表 -> 周期项（无条数上限）；解析失败返回 false 并写入 error
    static bool rowsFromEntries(const QVariantList &entries, QVector<PeriodicRow> *rows,
                                QString *error = nullptr);
    // 返回 nowUs 时刻到期（且应发送）的条目索引，并就地上报进度；
    // 落后于计划超过一个周期时丢弃落后时隙并重新对齐（nextDue = now + interval）。
    static QVector<int> dueRows(QVector<PeriodicRow> &rows, qint64 nowUs);
    // 所有「启用」条目是否都已发完（无启用条目时也为 true）
    static bool allRowsDone(const QVector<PeriodicRow> &rows);
    static bool anyRowEnabled(const QVector<PeriodicRow> &rows);
    static quint32 enabledRowCount(const QVector<PeriodicRow> &rows);
    // 周期毫秒 -> 微秒，下限 1ms
    static qint64 intervalMsToUs(double intervalMs);

    // ---- QML 接口（GUI 线程） ----
    Q_INVOKABLE bool apply(int port, const QVariantList &entries);
    Q_INVOKABLE bool start(int port);
    Q_INVOKABLE bool stop(int port);   // 返回该通道此前是否在运行
    Q_INVOKABLE void stopAll();
    // 单条启停（重下发整表），保留给设备测试脚本
    Q_INVOKABLE bool setRowEnabled(int port, const QVariantList &entries, int rowIndex, bool enabled);

    // 兼容旧命名：软件实现下均为同步快路径，结果仍以 operationFinished 通知
    Q_INVOKABLE bool applyAsync(int port, const QVariantList &entries) { return apply(port, entries); }
    Q_INVOKABLE bool startAsync(int port) { return start(port); }
    Q_INVOKABLE bool stopAsync(int port) { return stop(port); }

    Q_INVOKABLE int countFor(int port) const;
    Q_INVOKABLE bool activeFor(int port) const;
    Q_INVOKABLE QString errorFor(int port) const;

    QVariantList channelStates() const;
    int totalCount() const;
    bool anyActive() const;
    // 兼容旧接口：任一通道的最近错误
    QString lastError() const;

    // 注入统计/记录（供发送线程使用）。应在启动发送前设置。
    void setStatistics(Statistics *stats);
    void setRecorder(Recorder *recorder);

    // 测试缝：注入发送后端（默认走 CanDevice）。无硬件单测用它验证调度/队列/线程。
    // 必须在 start() 之前设置；传空 std::function 即回落到 CanDevice。
    using ReadyFn = std::function<bool()>;
    using TransmitFn = std::function<bool(int port, const FrameSpec &spec, QString *error)>;
    void setTransmitBackend(ReadyFn ready, TransmitFn transmit);

signals:
    void channelStatesChanged();
    void operationFinished(int port, bool ok, const QString &message);

private:
    struct Channel
    {
        QVector<PeriodicRow> rows;
        bool active = false;
        QString error;
    };
    struct SendItem
    {
        int port = 0;
        FrameSpec spec;
    };
    struct Notice
    {
        int port = 0;
        bool ok = true;
        QString message;
    };

    void schedulerLoop();
    void senderLoop();
    void sendOne(const SendItem &item);            // 仅发送线程
    bool deviceReady() const;                      // 需持 m_mutex
    bool defaultTransmit(int port, const FrameSpec &spec, QString *error);
    void pushSend(const QVector<SendItem> &items); // 需持 m_mutex（内部再取 m_sendMutex）
    void purgeSendQueue(int port);                 // port < 0 表示全部；仅取 m_sendMutex
    void queueNotice(int port, bool ok, const QString &message);   // 线程安全
    void postPublish();                            // 线程安全
    void publish();                                // 仅 GUI 线程
    void flushTx();                                // 仅 GUI 线程
    bool failChannel(int port, const QString &message);
    bool validPort(int port) const;

    mutable std::mutex m_mutex;   // 保护 m_channels / m_quit / m_ready / m_transmit
    std::condition_variable m_cond;
    bool m_quit = false;
    Channel m_channels[ChannelCount];

    std::mutex m_sendMutex;       // 保护 m_sendQueue / m_sendQuit
    std::condition_variable m_sendCond;
    std::deque<SendItem> m_sendQueue;
    bool m_sendQuit = false;
    static constexpr size_t kSendQueueMax = 65536;   // 防止 VCI_Transmit 卡死时队列无界增长

    std::mutex m_noticeMutex;     // 叶子锁：只保护 m_notices
    std::deque<Notice> m_notices;
    std::atomic<bool> m_publishScheduled{false};

    std::atomic<quint64> m_txAccum{0};          // 发送线程累计，回投 GUI 线程再计入统计
    std::atomic<bool> m_txFlushScheduled{false};

    std::thread m_schedThread;
    std::thread m_sendThread;

    CanDevice *m_device = nullptr;
    Statistics *m_stats = nullptr;
    Recorder *m_recorder = nullptr;

    // 测试缝（受 m_mutex 保护；析构时先置空再停线程）
    ReadyFn m_ready;
    TransmitFn m_transmit;
};
