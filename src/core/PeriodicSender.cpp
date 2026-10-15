#include "core/PeriodicSender.h"

#include <QDateTime>
#include <QMetaObject>
#include <QVariantMap>

#include <chrono>

#include "core/CanDevice.h"
#include "core/Recorder.h"
#include "core/Statistics.h"

namespace {

constexpr qint64 kMinIntervalUs = 1000;              // 1ms 下限
constexpr qint64 kMaxIntervalUs = 3600LL * 1000000;  // 1h 上限

qint64 nowMonotonicUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// 复位调度进度（启动/停止/设备断开时用），保留帧与周期配置
void resetRows(QVector<PeriodicSender::PeriodicRow> *rows)
{
    for (PeriodicSender::PeriodicRow &r : *rows) {
        r.sent = 0;
        r.done = false;
        r.nextDueUs = 0;
        r.counter = 0;
    }
}

} // namespace

PeriodicSender::PeriodicSender(CanDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
    m_schedThread = std::thread(&PeriodicSender::schedulerLoop, this);
    m_sendThread = std::thread(&PeriodicSender::senderLoop, this);
}

PeriodicSender::~PeriodicSender()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quit = true;
        m_ready = nullptr;      // 先摘掉替身，避免析构期间回调外部对象
        m_transmitBatch = nullptr;
    }
    m_cond.notify_all();
    if (m_schedThread.joinable())
        m_schedThread.join();

    {
        std::lock_guard<std::mutex> lk(m_sendMutex);
        m_sendQuit = true;
        m_sendQueue.clear();   // 停止后不再发送残留帧
    }
    m_sendCond.notify_all();
    if (m_sendThread.joinable())
        m_sendThread.join();
}

bool PeriodicSender::validPort(int port) const
{
    return port >= 0 && port < ChannelCount;
}

void PeriodicSender::setStatistics(Statistics *stats)
{
    m_stats = stats;
}

void PeriodicSender::setRecorder(Recorder *recorder)
{
    m_recorder = recorder;
}

void PeriodicSender::setTransmitBackend(ReadyFn ready, TransmitBatchFn transmitBatch)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_ready = std::move(ready);
    m_transmitBatch = std::move(transmitBatch);
}

bool PeriodicSender::deviceReady() const
{
    if (m_ready)
        return m_ready();
    return m_device && m_device->isOpen();
}

int PeriodicSender::defaultTransmitBatch(int port, const QVector<FrameSpec> &specs, QVector<bool> *ok,
                                        QString *error)
{
    if (ok)
        *ok = QVector<bool>(specs.size(), false);
    if (!m_device)
        return -1;

    QVector<int> canPos;
    QVector<int> fdPos;
    splitSpecIndicesByType(specs, &canPos, &fdPos);

    const quint32 p = static_cast<quint32>(port);
    int submitted = 0;

    // CAN 组：一次 VCI_Transmit(port, msgs, n)；CAN-FD 组：一次 VCI_TransmitFD。
    // 驱动按队列顺序提交，故返回值 ret 视为「组内前 ret 帧已提交」。
    // 两组独立尝试（不因前一组失败而跳过），因此失败总线上最坏是 2 次 txTimeout。
    auto submitGroup = [&](const QVector<int> &pos, bool fd) -> int {
        if (pos.isEmpty())
            return 0;

        QString buildErr;
        int ret = 0;
        if (fd) {
            QVector<ZCAN_FD_MSG> msgs(pos.size());
            for (int i = 0; i < pos.size(); ++i) {
                if (!FrameBuilder::buildFd(specs.at(pos.at(i)), &msgs[i], &buildErr)) {
                    if (error)
                        *error = buildErr;
                    return 0;   // 整组未提交（构建失败时不调用后端）
                }
            }
            ret = m_device->transmitBatchFd(p, msgs.data(), static_cast<quint32>(msgs.size()));
        } else {
            QVector<ZCAN_20_MSG> msgs(pos.size());
            for (int i = 0; i < pos.size(); ++i) {
                if (!FrameBuilder::buildCan(specs.at(pos.at(i)), &msgs[i], &buildErr)) {
                    if (error)
                        *error = buildErr;
                    return 0;
                }
            }
            ret = m_device->transmitBatchCan(p, msgs.data(), static_cast<quint32>(msgs.size()));
        }

        if (ret < 0)
            return -1;
        const int n = qMin(ret, pos.size());
        markGroupSubmitted(pos, n, ok);
        return n;
    };

    const int canSent = submitGroup(canPos, false);
    if (canSent < 0)
        return submitted;
    submitted += canSent;

    const int fdSent = submitGroup(fdPos, true);
    if (fdSent < 0)
        return submitted;
    submitted += fdSent;

    return submitted;
}

// ---------------------------------------------------------------- 纯逻辑

qint64 PeriodicSender::intervalMsToUs(double intervalMs)
{
    const double us = intervalMs * 1000.0;
    if (!(us > 0.0))   // NaN / <=0
        return kMinIntervalUs;
    return static_cast<qint64>(qBound(static_cast<double>(kMinIntervalUs), us,
                                      static_cast<double>(kMaxIntervalUs)));
}

bool PeriodicSender::rowsFromEntries(const QVariantList &entries, QVector<PeriodicRow> *rows,
                                     QString *error)
{
    if (error)
        error->clear();
    if (!rows) {
        if (error)
            *error = QStringLiteral("输出缓冲区为空");
        return false;
    }

    rows->clear();
    rows->reserve(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const QVariantMap map = entries.at(i).toMap();

        FrameSpec spec;
        QString err;
        if (!FrameBuilder::specFromMap(map, &spec, &err)) {
            if (error)
                *error = QStringLiteral("第 %1 条：%2").arg(i + 1).arg(err);
            return false;
        }

        PeriodicRow row;
        row.spec = spec;
        row.intervalUs = intervalMsToUs(map.value(QStringLiteral("intervalMs"), 100.0).toDouble());
        const qint64 rep = map.value(QStringLiteral("repeat"), 0).toLongLong();
        row.repeat = static_cast<quint32>(qBound<qint64>(0, rep, 0xFFFFFFFFLL));
        row.enabled = map.value(QStringLiteral("enabled"), true).toBool();
        row.sent = 0;
        row.nextDueUs = 0;
        row.done = false;
        row.counter = 0;
        rows->append(row);
    }

    return true;
}

QVector<int> PeriodicSender::dueRows(QVector<PeriodicRow> &rows, qint64 nowUs)
{
    QVector<int> due;

    for (int i = 0; i < rows.size(); ++i) {
        PeriodicRow &r = rows[i];
        if (!r.enabled || r.done)
            continue;
        if (r.repeat != 0 && r.sent >= r.repeat) {
            r.done = true;
            continue;
        }
        if (r.nextDueUs > nowUs)
            continue;

        due.append(i);
        ++r.sent;

        if (r.repeat != 0 && r.sent >= r.repeat) {
            r.done = true;
            continue;
        }

        qint64 next = r.nextDueUs + r.intervalUs;
        if (next <= nowUs)
            next = nowUs + r.intervalUs;   // 落后超过一个周期：丢弃落后时隙，重新对齐
        r.nextDueUs = next;
    }

    return due;
}

void PeriodicSender::splitSpecIndicesByType(const QVector<FrameSpec> &specs, QVector<int> *can,
                                           QVector<int> *fd)
{
    if (can) {
        can->clear();
        can->reserve(specs.size());
    }
    if (fd) {
        fd->clear();
        fd->reserve(specs.size());
    }

    for (int i = 0; i < specs.size(); ++i) {
        QVector<int> *dst = specs.at(i).fd ? fd : can;
        if (dst)
            dst->append(i);
    }
}

void PeriodicSender::markGroupSubmitted(const QVector<int> &positions, int submittedCount,
                                       QVector<bool> *submitted)
{
    if (!submitted)
        return;
    const int n = qBound(0, submittedCount, positions.size());
    for (int i = 0; i < n; ++i) {
        const int idx = positions.at(i);
        if (idx >= 0 && idx < submitted->size())
            (*submitted)[idx] = true;
    }
}

bool PeriodicSender::allRowsDone(const QVector<PeriodicRow> &rows)
{
    for (const PeriodicRow &r : rows) {
        if (r.enabled && !r.done)
            return false;
    }
    return true;
}

bool PeriodicSender::anyRowEnabled(const QVector<PeriodicRow> &rows)
{
    for (const PeriodicRow &r : rows) {
        if (r.enabled)
            return true;
    }
    return false;
}

quint32 PeriodicSender::enabledRowCount(const QVector<PeriodicRow> &rows)
{
    quint32 n = 0;
    for (const PeriodicRow &r : rows) {
        if (r.enabled)
            ++n;
    }
    return n;
}

// ---------------------------------------------------------------- 对外操作

bool PeriodicSender::failChannel(int port, const QString &message)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_channels[port].error = message;
    }
    emit channelStatesChanged();
    emit operationFinished(port, false, message);
    return false;
}

bool PeriodicSender::failQuick(int port, const QString &message)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quick[port].error = message;
    }
    emit quickStatesChanged();
    emit quickOperationFinished(port, false, message);
    return false;
}

bool PeriodicSender::apply(int port, const QVariantList &entries)
{
    if (!validPort(port))
        return false;

    QVector<PeriodicRow> rows;
    QString err;
    if (!rowsFromEntries(entries, &rows, &err))
        return failChannel(port, err);

    const int n = rows.size();
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        Channel &ch = m_channels[port];
        ch.active = false;   // 运行中改表：先停该通道，下一轮重新调度
        ch.rows = rows;
        ch.error.clear();
        purgeSendQueue(port, false);
    }
    m_cond.notify_all();
    emit channelStatesChanged();
    emit operationFinished(port, true,
                           QStringLiteral("通道 %1：已下发 %2 条周期配置").arg(port).arg(n));
    return true;
}

bool PeriodicSender::start(int port)
{
    if (!validPort(port))
        return false;

    bool ready = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        ready = deviceReady();
    }
    if (!ready)
        return failChannel(port, QStringLiteral("设备未连接"));

    bool ok = false;
    QString msg;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        Channel &ch = m_channels[port];
        if (ch.rows.isEmpty()) {
            ch.error = QStringLiteral("请先下发周期配置");
            msg = ch.error;
        } else {
            // 以启动时刻为基准：本轮立即发送一次，之后按 now+interval 推进
            const qint64 startUs = nowMonotonicUs();
            for (PeriodicRow &r : ch.rows) {
                r.sent = 0;
                r.done = false;
                r.nextDueUs = startUs;
                r.counter = 0;
            }
            ch.active = true;
            ch.error.clear();
            ok = true;
            msg = QStringLiteral("通道 %1：周期发送已启动").arg(port);
        }
    }
    m_cond.notify_all();
    emit channelStatesChanged();
    emit operationFinished(port, ok, msg);
    return ok;
}

bool PeriodicSender::stop(int port)
{
    if (!validPort(port))
        return false;

    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        Channel &ch = m_channels[port];
        wasActive = ch.active;
        ch.active = false;
        resetRows(&ch.rows);
        purgeSendQueue(port, false);
    }
    if (!wasActive)
        return false;

    m_cond.notify_all();
    emit channelStatesChanged();
    emit operationFinished(port, true, QStringLiteral("通道 %1：周期发送已停止").arg(port));
    return true;
}

// ---------------------------------------------------------------- 快捷槽

bool PeriodicSender::startQuick(int port, const QVariantMap &specMap, double intervalMs, int repeat)
{
    if (!validPort(port))
        return false;

    FrameSpec spec;
    QString err;
    if (!FrameBuilder::specFromMap(specMap, &spec, &err))
        return failQuick(port, err);

    bool ready = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        ready = deviceReady();
    }
    if (!ready)
        return failQuick(port, QStringLiteral("设备未连接"));

    PeriodicRow row;
    row.spec = spec;
    row.intervalUs = intervalMsToUs(intervalMs);
    row.repeat = static_cast<quint32>(qBound<qint64>(0, static_cast<qint64>(repeat), 0xFFFFFFFFLL));
    row.enabled = true;
    row.nextDueUs = nowMonotonicUs();   // 本轮立即发送一次

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        Channel &ch = m_quick[port];
        ch.rows = QVector<PeriodicRow>{row};
        ch.active = true;
        ch.error.clear();
        purgeSendQueue(port, true);   // 只清本槽的残留帧，不碰周期表
    }
    m_cond.notify_all();
    emit quickStatesChanged();
    emit quickOperationFinished(port, true,
                                QStringLiteral("通道 %1：快捷周期发送已启动").arg(port));
    return true;
}

bool PeriodicSender::stopQuick(int port)
{
    if (!validPort(port))
        return false;

    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        Channel &ch = m_quick[port];
        wasActive = ch.active;
        ch.active = false;
        resetRows(&ch.rows);
        purgeSendQueue(port, true);
    }
    if (!wasActive)
        return false;

    m_cond.notify_all();
    emit quickStatesChanged();
    emit quickOperationFinished(port, true,
                                QStringLiteral("通道 %1：快捷周期发送已停止").arg(port));
    return true;
}

bool PeriodicSender::quickActive(int port) const
{
    if (!validPort(port))
        return false;
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_quick[port].active;
}

QVariantList PeriodicSender::quickStates() const
{
    QVariantList out;
    std::lock_guard<std::mutex> lk(m_mutex);
    for (int c = 0; c < ChannelCount; ++c) {
        const Channel &ch = m_quick[c];
        QVariantMap m;
        m.insert(QStringLiteral("port"), c);
        m.insert(QStringLiteral("active"), ch.active);
        m.insert(QStringLiteral("error"), ch.error);
        m.insert(QStringLiteral("intervalMs"),
                 ch.rows.isEmpty() ? 0.0 : static_cast<double>(ch.rows.first().intervalUs) / 1000.0);
        m.insert(QStringLiteral("repeat"),
                 ch.rows.isEmpty() ? 0 : static_cast<int>(ch.rows.first().repeat));
        out.append(m);
    }
    return out;
}

void PeriodicSender::stopAll()
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (int c = 0; c < ChannelCount; ++c) {
            Channel &ch = m_channels[c];
            if (!ch.active)
                continue;
            ch.active = false;
            resetRows(&ch.rows);
            changed = true;
        }
        for (int c = 0; c < ChannelCount; ++c) {
            Channel &ch = m_quick[c];
            if (!ch.active)
                continue;
            ch.active = false;
            resetRows(&ch.rows);
            changed = true;
        }
        if (changed) {
            purgeSendQueue(-1, false);
            purgeSendQueue(-1, true);
        }
    }
    if (changed) {
        m_cond.notify_all();
        emit channelStatesChanged();
        emit quickStatesChanged();
    }
}

bool PeriodicSender::setRowEnabled(int port, const QVariantList &entries, int rowIndex, bool enabled)
{
    if (rowIndex < 0 || rowIndex >= entries.size())
        return failChannel(validPort(port) ? port : 0, QStringLiteral("条目索引越界"));

    QVariantList updated = entries;
    QVariantMap row = updated.at(rowIndex).toMap();
    row.insert(QStringLiteral("enabled"), enabled);
    updated[rowIndex] = row;
    return apply(port, updated);
}

int PeriodicSender::countFor(int port) const
{
    if (!validPort(port))
        return 0;
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_channels[port].rows.size();
}

bool PeriodicSender::activeFor(int port) const
{
    if (!validPort(port))
        return false;
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_channels[port].active;
}

QString PeriodicSender::errorFor(int port) const
{
    if (!validPort(port))
        return QString();
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_channels[port].error;
}

QVariantList PeriodicSender::channelStates() const
{
    QVariantList out;
    std::lock_guard<std::mutex> lk(m_mutex);
    for (int c = 0; c < ChannelCount; ++c) {
        QVariantMap m;
        m.insert(QStringLiteral("port"), c);
        m.insert(QStringLiteral("count"), m_channels[c].rows.size());
        m.insert(QStringLiteral("active"), m_channels[c].active);
        m.insert(QStringLiteral("error"), m_channels[c].error);
        out.append(m);
    }
    return out;
}

int PeriodicSender::totalCount() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    int n = 0;
    for (int c = 0; c < ChannelCount; ++c)
        n += m_channels[c].rows.size();
    return n;
}

bool PeriodicSender::anyActive() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    for (int c = 0; c < ChannelCount; ++c) {
        if (m_channels[c].active)
            return true;
    }
    return false;
}

QString PeriodicSender::lastError() const
{
    for (int c = 0; c < ChannelCount; ++c) {
        const QString e = errorFor(c);
        if (!e.isEmpty())
            return e;
    }
    return QString();
}

// ---------------------------------------------------------------- 调度线程

bool PeriodicSender::anySlotActive(const Channel *slotSet) const
{
    for (int c = 0; c < ChannelCount; ++c) {
        if (slotSet[c].active)
            return true;
    }
    return false;
}

// 排期一组槽位：到期帧推入 due、就地推进进度；次数耗尽的槽位停用并通知。
// 需持 m_mutex（仅调度线程调用）。
void PeriodicSender::processSlotSet(Channel *slotSet, bool quick, qint64 nowUs, QVector<SendItem> *due,
                                    qint64 *nextDeadline, bool *changed)
{
    for (int c = 0; c < ChannelCount; ++c) {
        Channel &ch = slotSet[c];
        if (!ch.active)
            continue;

        const QVector<int> idx = dueRows(ch.rows, nowUs);
        for (int i : idx) {
            PeriodicRow &r = ch.rows[i];
            SendItem item;
            item.port = c;
            item.quick = quick;
            item.spec = r.spec;
            item.spec.counter = r.counter;
            // ID 递增与滚动计数共用该槽位的计数器；ID 在下方按计数器叠加
            item.spec.id = FrameBuilder::idForCounter(item.spec.id, item.spec.extended,
                                                      item.spec.incrementId, r.counter);
            if (r.spec.rolling || r.spec.incrementId)
                ++r.counter;
            due->append(item);
        }

        if (allRowsDone(ch.rows)) {
            ch.active = false;
            *changed = true;
            if (quick) {
                queueNotice(c, true,
                            QStringLiteral("通道 %1：快捷周期发送已完成").arg(c), true);
            } else {
                queueNotice(c, true,
                            anyRowEnabled(ch.rows)
                                ? QStringLiteral("通道 %1：周期发送已完成").arg(c)
                                : QStringLiteral("通道 %1：没有启用任何条目，已停止").arg(c));
            }
            resetRows(&ch.rows);
            continue;
        }

        for (const PeriodicRow &r : ch.rows) {
            if (!r.enabled || r.done)
                continue;
            *nextDeadline = (*nextDeadline < 0) ? r.nextDueUs : qMin(*nextDeadline, r.nextDueUs);
        }
    }
}

// 设备断开等场景：停用一组槽位并逐个通知（需持 m_mutex）
void PeriodicSender::stopSlotSet(Channel *slotSet, bool quick, const QString &messageFmt, bool *changed)
{
    for (int c = 0; c < ChannelCount; ++c) {
        Channel &ch = slotSet[c];
        if (!ch.active)
            continue;
        ch.active = false;
        resetRows(&ch.rows);
        purgeSendQueue(c, quick);
        *changed = true;
        queueNotice(c, false, messageFmt.arg(c), quick);
    }
}

void PeriodicSender::schedulerLoop()
{
    std::unique_lock<std::mutex> lk(m_mutex);

    while (!m_quit) {
        const qint64 nowUs = nowMonotonicUs();
        QVector<SendItem> due;
        qint64 nextDeadline = -1;
        bool changed = false;

        if ((anySlotActive(m_channels) || anySlotActive(m_quick)) && !deviceReady()) {
            // 设备断开：停止全部槽位并提示（仅一次，因为 active 被清掉）
            stopSlotSet(m_channels, false,
                        QStringLiteral("设备未打开，通道 %1 定时发送已停止"), &changed);
            stopSlotSet(m_quick, true,
                        QStringLiteral("设备未打开，通道 %1 快捷周期发送已停止"), &changed);
        } else {
            processSlotSet(m_channels, false, nowUs, &due, &nextDeadline, &changed);
            processSlotSet(m_quick, true, nowUs, &due, &nextDeadline, &changed);
        }

        if (m_quit)
            break;

        if (!due.isEmpty())
            pushSend(due);
        if (changed)
            postPublish();

        if (nextDeadline >= 0) {
            const qint64 delta = nextDeadline - nowMonotonicUs();
            if (delta > 0)
                m_cond.wait_for(lk, std::chrono::microseconds(delta));
        } else {
            // 无待发送计划：等待 start/apply/stop/退出唤醒
            m_cond.wait(lk);
        }
    }
}

void PeriodicSender::pushSend(const QVector<SendItem> &items)
{
    bool overflowed = false;
    {
        std::lock_guard<std::mutex> lk(m_sendMutex);
        for (const SendItem &it : items) {
            if (m_sendQueue.size() >= kSendQueueMax) {
                if (!overflowed) {
                    overflowed = true;
                    queueNotice(it.port, false,
                                QStringLiteral("通道 %1：发送积压，已丢弃部分周期帧").arg(it.port),
                                it.quick);
                }
                continue;
            }
            m_sendQueue.push_back(it);
        }
    }
    m_sendCond.notify_one();
}

void PeriodicSender::purgeSendQueue(int port, bool quick)
{
    std::lock_guard<std::mutex> lk(m_sendMutex);
    for (auto it = m_sendQueue.begin(); it != m_sendQueue.end();) {
        const bool match = (it->quick == quick) && (port < 0 || it->port == port);
        if (match)
            it = m_sendQueue.erase(it);
        else
            ++it;
    }
}

// ---------------------------------------------------------------- 发送线程

void PeriodicSender::senderLoop()
{
    for (;;) {
        QVector<SendItem> batch;
        {
            std::unique_lock<std::mutex> lk(m_sendMutex);
            m_sendCond.wait(lk, [this] { return m_sendQuit || !m_sendQueue.empty(); });
            if (m_sendQueue.empty()) {
                if (m_sendQuit)
                    break;
                continue;
            }
            // 只合并同一通道且同一槽位的连续段：一次 VCI_Transmit 只能指定一个 Port，
            // 且表通道与快捷槽的失败/积压通知要能各自归位。
            // 调度线程按槽位顺序入队，因此同槽位的多条周期项自然连在一起。
            const int port = m_sendQueue.front().port;
            const bool quick = m_sendQueue.front().quick;
            while (!m_sendQueue.empty() && m_sendQueue.front().port == port
                   && m_sendQueue.front().quick == quick && batch.size() < kBatchMax) {
                batch.append(m_sendQueue.front());
                m_sendQueue.pop_front();
            }
        }
        sendBatch(batch);
    }
}

// 一批帧（同一通道）交给后端逐类型提交；按逐帧结果记账，避免"部分提交"被误算。
void PeriodicSender::sendBatch(const QVector<SendItem> &items)
{
    if (items.isEmpty())
        return;

    const int port = items.first().port;
    const bool quick = items.first().quick;

    TransmitBatchFn tx;
    Recorder *recorder = nullptr;
    bool deviceOpen = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        tx = m_transmitBatch;
        recorder = m_recorder;
        deviceOpen = (m_device && m_device->isOpen());
    }

    if (!tx && !deviceOpen) {
        queueNotice(port, false, QStringLiteral("设备未打开，周期帧发送失败"), quick);
        return;
    }

    QVector<FrameSpec> specs;
    specs.reserve(items.size());
    for (const SendItem &item : items)
        specs.append(item.spec);

    QString err;
    QVector<bool> submitted;
    if (tx) {
        submitted = tx(port, specs, &err);
    } else {
        submitted = QVector<bool>(specs.size(), false);
        defaultTransmitBatch(port, specs, &submitted, &err);
    }
    if (submitted.size() != specs.size())
        submitted = QVector<bool>(specs.size(), false);   // 整批失败：长度不符视为全未提交

    int okCount = 0;
    for (bool b : submitted) {
        if (b)
            ++okCount;
    }

    if (okCount == 0) {
        if (err.isEmpty()) {
            err = QStringLiteral(
                "周期帧发送失败：总线无应答（请检查波特率/位定时、接线与终端电阻）");
        }
        queueNotice(port, false, err, quick);
        return;
    }

    if (recorder) {
        // 与 FrameBuilder 发送路径一致：以发送成功为准记录 tx 帧，接收侧回显会被跳过。
        // 同一批共用同一挂钟时间（批内耗时 = 总线发送时间，几 ms 量级）。
        const qint64 wallUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        for (int i = 0; i < specs.size(); ++i) {
            if (!submitted.at(i))
                continue;
            const FrameSpec &spec = specs.at(i);
            FrameData f;
            f.wallUs = wallUs;
            f.hostUs = wallUs;
            f.id = FrameBuilder::clampId(spec.id, spec.extended);
            f.channel = static_cast<quint8>(qBound(0, port, 255));
            f.len = static_cast<quint8>(qBound(0, spec.dlc, spec.fd ? 64 : 8));
            f.extended = spec.extended;
            f.remote = spec.remote;
            f.fd = spec.fd;
            f.brs = spec.fd && spec.brs;
            f.tx = true;
            f.echo = true;
            if (!spec.remote)
                f.data = FrameBuilder::applyModifiers(spec.data, spec, spec.fd ? 64 : 8).left(f.len);
            recorder->enqueueFrame(f);
        }
    }

    // 统计：countSent 非线程安全，累计后回投 GUI 线程
    m_txAccum.fetch_add(static_cast<quint64>(okCount));
    if (!m_txFlushScheduled.exchange(true))
        QMetaObject::invokeMethod(this, [this] { flushTx(); }, Qt::QueuedConnection);

    if (okCount < specs.size()) {
        if (err.isEmpty())
            err = QStringLiteral("总线无应答（请检查波特率/位定时、接线与终端电阻）");
        queueNotice(port, false,
                    QStringLiteral("通道 %1：周期帧部分发送失败（已发出 %2/%3）：%4")
                        .arg(port)
                        .arg(okCount)
                        .arg(specs.size())
                        .arg(err),
                    quick);
    }
}

// ---------------------------------------------------------------- GUI 线程回投

void PeriodicSender::queueNotice(int port, bool ok, const QString &message, bool quick)
{
    {
        std::lock_guard<std::mutex> lk(m_noticeMutex);
        m_notices.push_back(Notice{port, ok, message, quick});
        while (m_notices.size() > 64)
            m_notices.pop_front();
    }
    postPublish();
}

void PeriodicSender::postPublish()
{
    if (m_publishScheduled.exchange(true))
        return;
    QMetaObject::invokeMethod(this, [this] { publish(); }, Qt::QueuedConnection);
}

void PeriodicSender::publish()
{
    m_publishScheduled.store(false);

    std::deque<Notice> local;
    {
        std::lock_guard<std::mutex> lk(m_noticeMutex);
        local.swap(m_notices);
    }

    emit channelStatesChanged();
    emit quickStatesChanged();
    for (const Notice &n : local) {
        if (n.quick)
            emit quickOperationFinished(n.port, n.ok, n.message);
        else
            emit operationFinished(n.port, n.ok, n.message);
    }

    // 交换后又有新通知：补一次
    bool more = false;
    {
        std::lock_guard<std::mutex> lk(m_noticeMutex);
        more = !m_notices.empty();
    }
    if (more && !m_publishScheduled.exchange(true))
        QMetaObject::invokeMethod(this, [this] { publish(); }, Qt::QueuedConnection);
}

void PeriodicSender::flushTx()
{
    m_txFlushScheduled.store(false);
    const quint64 n = m_txAccum.exchange(0);
    if (n > 0 && m_stats)
        m_stats->countSent(static_cast<int>(n));
    if (m_txAccum.load() > 0 && !m_txFlushScheduled.exchange(true))
        QMetaObject::invokeMethod(this, [this] { flushTx(); }, Qt::QueuedConnection);
}
