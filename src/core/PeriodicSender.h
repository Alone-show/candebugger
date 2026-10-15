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
// 两套互不干扰的槽位，共用同一组线程与排期逻辑：
//   - **表通道** `m_channels[2]`：周期页签的配置表（apply/start/stop）；
//   - **快捷槽** `m_quick[2]`：单帧面板的「快捷周期发送」，一个槽位恒为 1 帧，
//     由 startQuick(spec, 周期, 次数) 直接启动，不会读到/写坏周期表，也不需要下发配置。
//   两者可同时在同一物理通道上运行（CanDevice 内部 IO 锁串行化发送）。
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
    Q_PROPERTY(QVariantList quickStates READ quickStates NOTIFY quickStatesChanged)
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
    // 按帧类型拆批的帧下标：写入 `can` / `fd`（CAN 组在前，CAN-FD 组在后）。组内保持
    // 原顺序。因为 `VCI_Transmit`(CAN) 与 `VCI_TransmitFD`(FD) 不能混，一批帧最多需要
    // 2 次调用；跨类型相对顺序因此在同一次调度内可能变化（取舍：优先减少 USB 调用次数）。
    static void splitSpecIndicesByType(const QVector<FrameSpec> &specs, QVector<int> *can,
                                       QVector<int> *fd);
    // 驱动按数组顺序提交，故「组内前 submittedCount 帧成功」要按 positions 映射回原下标。
    // submittedCount 会被夹到 [0, positions.size()]；越界视为全部成功/全部失败。
    static void markGroupSubmitted(const QVector<int> &positions, int submittedCount,
                                   QVector<bool> *submitted);

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

    // ---- 快捷槽（单帧面板的「快捷周期发送」）----
    // 一次性把当前帧与周期/次数交给该物理通道的快捷槽；0 = 无限循环。
    // 与周期表通道互不影响：不改写表配置、不影响 count/active/channelStates。
    // 失败（帧描述非法/设备未连接）返回 false 并经 quickOperationFinished 通知原因。
    Q_INVOKABLE bool startQuick(int port, const QVariantMap &spec, double intervalMs, int repeat);
    Q_INVOKABLE bool stopQuick(int port);      // 返回该快捷槽此前是否在运行
    Q_INVOKABLE bool quickActive(int port) const;
    QVariantList quickStates() const;          // [{port, active, error, intervalMs, repeat}]

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
    // 返回与 specs 等长的逐帧提交结果（true = 该帧已成功提交）；返回空或长度不符
    // 表示整批失败（原因写入 *error）。逐帧结果而不是计数，是因为按类型拆批后
    // 提交顺序与 specs 顺序不再一致，只有逐帧结果才能准确记账。
    using ReadyFn = std::function<bool()>;
    using TransmitBatchFn =
        std::function<QVector<bool>(int port, const QVector<FrameSpec> &specs, QString *error)>;
    void setTransmitBackend(ReadyFn ready, TransmitBatchFn transmitBatch);

signals:
    void channelStatesChanged();
    void operationFinished(int port, bool ok, const QString &message);
    void quickStatesChanged();
    void quickOperationFinished(int port, bool ok, const QString &message);

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
        bool quick = false;
        FrameSpec spec;
    };
    struct Notice
    {
        int port = 0;
        bool ok = true;
        QString message;
        bool quick = false;
    };

    void schedulerLoop();
    void senderLoop();
    void sendBatch(const QVector<SendItem> &items);   // 仅发送线程
    bool deviceReady() const;                         // 需持 m_mutex
    int defaultTransmitBatch(int port, const QVector<FrameSpec> &specs, QVector<bool> *ok,
                             QString *error);         // 仅发送线程（默认后端：CanDevice）
    // 排期一组槽位（表通道 quick=false / 快捷槽 quick=true）。仅调度线程，需持 m_mutex：
    // 把到期帧推入 due、上报进度并更新 nextDeadline；次数耗尽的槽位就地停用。
    void processSlotSet(Channel *slotSet, bool quick, qint64 nowUs, QVector<SendItem> *due,
                        qint64 *nextDeadline, bool *changed);
    bool anySlotActive(const Channel *slotSet) const;   // 需持 m_mutex
    // 设备断开等场景：停用一组槽位并逐个通知。需持 m_mutex
    void stopSlotSet(Channel *slotSet, bool quick, const QString &messageFmt, bool *changed);
    void pushSend(const QVector<SendItem> &items);    // 需持 m_mutex（内部再取 m_sendMutex）
    // 丢弃待发帧：port < 0 表示所有通道；quick 选中表通道或快捷槽
    void purgeSendQueue(int port, bool quick);
    void queueNotice(int port, bool ok, const QString &message, bool quick = false);   // 线程安全
    void postPublish();                            // 线程安全
    void publish();                                // 仅 GUI 线程
    void flushTx();                                // 仅 GUI 线程
    bool failChannel(int port, const QString &message);
    bool failQuick(int port, const QString &message);
    bool validPort(int port) const;

    mutable std::mutex m_mutex;   // 保护 m_channels / m_quick / m_quit / m_ready / m_transmitBatch
    std::condition_variable m_cond;
    bool m_quit = false;
    Channel m_channels[ChannelCount];
    Channel m_quick[ChannelCount];

    std::mutex m_sendMutex;       // 保护 m_sendQueue / m_sendQuit
    std::condition_variable m_sendCond;
    std::deque<SendItem> m_sendQueue;
    bool m_sendQuit = false;
    static constexpr size_t kSendQueueMax = 65536;   // 防止 VCI_Transmit 卡死时队列无界增长
    // 单次后端调用的帧数上限：一批 64 帧在设备侧排队，耗时受总线限制（实机 500k 经典帧
    // 约 0.3ms/帧），同时失败时的最坏阻塞仍只有一次超时（实测与帧数无关）。
    static constexpr int kBatchMax = 64;

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
    TransmitBatchFn m_transmitBatch;
};
