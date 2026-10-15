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
        m_transmit = nullptr;
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

void PeriodicSender::setTransmitBackend(ReadyFn ready, TransmitFn transmit)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_ready = std::move(ready);
    m_transmit = std::move(transmit);
}

bool PeriodicSender::deviceReady() const
{
    if (m_ready)
        return m_ready();
    return m_device && m_device->isOpen();
}

bool PeriodicSender::defaultTransmit(int port, const FrameSpec &spec, QString *error)
{
    const quint32 p = static_cast<quint32>(port);
    if (spec.fd) {
        ZCAN_FD_MSG msg;
        if (!FrameBuilder::buildFd(spec, &msg, error))
            return false;
        return m_device && m_device->transmitFd(p, msg);
    }
    ZCAN_20_MSG msg;
    if (!FrameBuilder::buildCan(spec, &msg, error))
        return false;
    return m_device && m_device->transmitCan(p, msg);
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
        purgeSendQueue(port);
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
        for (PeriodicRow &r : ch.rows) {
            r.sent = 0;
            r.done = false;
            r.nextDueUs = 0;
            r.counter = 0;
        }
        purgeSendQueue(port);
    }
    if (!wasActive)
        return false;

    m_cond.notify_all();
    emit channelStatesChanged();
    emit operationFinished(port, true, QStringLiteral("通道 %1：周期发送已停止").arg(port));
    return true;
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
            for (PeriodicRow &r : ch.rows) {
                r.sent = 0;
                r.done = false;
                r.nextDueUs = 0;
                r.counter = 0;
            }
            changed = true;
        }
        if (changed)
            purgeSendQueue(-1);
    }
    if (changed) {
        m_cond.notify_all();
        emit channelStatesChanged();
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

void PeriodicSender::schedulerLoop()
{
    std::unique_lock<std::mutex> lk(m_mutex);

    while (!m_quit) {
        const qint64 nowUs = nowMonotonicUs();
        QVector<SendItem> due;
        qint64 nextDeadline = -1;
        bool changed = false;

        bool anyActive = false;
        for (int c = 0; c < ChannelCount; ++c) {
            if (m_channels[c].active) {
                anyActive = true;
                break;
            }
        }

        if (anyActive && !deviceReady()) {
            // 设备断开：停止全部通道并提示（仅一次，因为 active 被清掉）
            for (int c = 0; c < ChannelCount; ++c) {
                Channel &ch = m_channels[c];
                if (!ch.active)
                    continue;
                ch.active = false;
                for (PeriodicRow &r : ch.rows) {
                    r.sent = 0;
                    r.done = false;
                    r.nextDueUs = 0;
                    r.counter = 0;
                }
                purgeSendQueue(c);
                changed = true;
                queueNotice(c, false,
                            QStringLiteral("设备未打开，通道 %1 定时发送已停止").arg(c));
            }
        } else {
            for (int c = 0; c < ChannelCount; ++c) {
                Channel &ch = m_channels[c];
                if (!ch.active)
                    continue;

                const QVector<int> idx = dueRows(ch.rows, nowUs);
                for (int i : idx) {
                    PeriodicRow &r = ch.rows[i];
                    SendItem item;
                    item.port = c;
                    item.spec = r.spec;
                    item.spec.counter = r.counter;
                    if (r.spec.rolling)
                        ++r.counter;
                    due.append(item);
                }

                if (allRowsDone(ch.rows)) {
                    ch.active = false;
                    changed = true;
                    queueNotice(c, true,
                                anyRowEnabled(ch.rows)
                                    ? QStringLiteral("通道 %1：周期发送已完成").arg(c)
                                    : QStringLiteral("通道 %1：没有启用任何条目，已停止").arg(c));
                    for (PeriodicRow &r : ch.rows) {
                        r.sent = 0;
                        r.done = false;
                        r.nextDueUs = 0;
                        r.counter = 0;
                    }
                    continue;
                }

                for (const PeriodicRow &r : ch.rows) {
                    if (!r.enabled || r.done)
                        continue;
                    nextDeadline = (nextDeadline < 0) ? r.nextDueUs : qMin(nextDeadline, r.nextDueUs);
                }
            }
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
                                QStringLiteral("通道 %1：发送积压，已丢弃部分周期帧").arg(it.port));
                }
                continue;
            }
            m_sendQueue.push_back(it);
        }
    }
    m_sendCond.notify_one();
}

void PeriodicSender::purgeSendQueue(int port)
{
    std::lock_guard<std::mutex> lk(m_sendMutex);
    if (port < 0) {
        m_sendQueue.clear();
        return;
    }
    for (auto it = m_sendQueue.begin(); it != m_sendQueue.end();) {
        if (it->port == port)
            it = m_sendQueue.erase(it);
        else
            ++it;
    }
}

// ---------------------------------------------------------------- 发送线程

void PeriodicSender::senderLoop()
{
    for (;;) {
        SendItem item;
        {
            std::unique_lock<std::mutex> lk(m_sendMutex);
            m_sendCond.wait(lk, [this] { return m_sendQuit || !m_sendQueue.empty(); });
            if (m_sendQueue.empty()) {
                if (m_sendQuit)
                    break;
                continue;
            }
            item = m_sendQueue.front();
            m_sendQueue.pop_front();
        }
        sendOne(item);
    }
}

void PeriodicSender::sendOne(const SendItem &item)
{
    TransmitFn tx;
    Recorder *recorder = nullptr;
    bool deviceOpen = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        tx = m_transmit;
        recorder = m_recorder;
        deviceOpen = (m_device && m_device->isOpen());
    }

    if (!tx && !deviceOpen) {
        queueNotice(item.port, false, QStringLiteral("设备未打开，周期帧发送失败"));
        return;
    }

    QString err;
    const bool ok = tx ? tx(item.port, item.spec, &err)
                       : defaultTransmit(item.port, item.spec, &err);
    if (!ok) {
        if (err.isEmpty())
            err = QStringLiteral("周期帧发送失败：总线无应答（请检查波特率/位定时、接线与终端电阻）");
        queueNotice(item.port, false, err);
        return;
    }

    const FrameSpec &spec = item.spec;

    if (recorder) {
        // 与 FrameBuilder 发送路径一致：以发送成功为准记录 tx 帧，接收侧回显会被跳过
        const qint64 wallUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        FrameData f;
        f.wallUs = wallUs;
        f.hostUs = wallUs;
        f.id = FrameBuilder::clampId(spec.id, spec.extended);
        f.channel = static_cast<quint8>(qBound(0, item.port, 255));
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

    // 统计：countSent 非线程安全，累计后回投 GUI 线程
    m_txAccum.fetch_add(1);
    if (!m_txFlushScheduled.exchange(true))
        QMetaObject::invokeMethod(this, [this] { flushTx(); }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------- GUI 线程回投

void PeriodicSender::queueNotice(int port, bool ok, const QString &message)
{
    {
        std::lock_guard<std::mutex> lk(m_noticeMutex);
        m_notices.push_back(Notice{port, ok, message});
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
    for (const Notice &n : local)
        emit operationFinished(n.port, n.ok, n.message);

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
