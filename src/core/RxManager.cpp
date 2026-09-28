#include "core/RxManager.h"

#include <QAbstractItemModel>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include "core/CanDevice.h"
#include "core/CanFilter.h"
#include "core/RingBuffer.h"
#include "core/Recorder.h"
#include "core/RxWorker.h"
#include "core/Statistics.h"
#include "model/FrameFilterProxyModel.h"
#include "model/FrameListModel.h"

namespace {
constexpr int kDrainIntervalMs = 33;   // ~30Hz 合并刷新
constexpr int kDrainMaxPerTick = 20000;
} // namespace

RxManager::RxManager(CanDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_sourceModel(new FrameListModel(this))
    , m_filter(new CanFilter(device, this))
    , m_ring(new RingBuffer(1 << 16))
{
    qRegisterMetaType<QAbstractItemModel *>("QAbstractItemModel*");
    m_model = new FrameFilterProxyModel(m_filter, this);
    m_model->setSourceModel(m_sourceModel);

    connect(m_filter, &CanFilter::softwareFilterChanged, m_model, &FrameFilterProxyModel::refresh);
    connect(m_sourceModel, &FrameListModel::timestampModeChanged, this, &RxManager::timestampModeChanged);

    m_timer = new QTimer(this);
    m_timer->setInterval(kDrainIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &RxManager::drain);
}

RxManager::~RxManager()
{
    stop();
}

bool RxManager::start(const QVariantList &channels, bool mergeMode)
{
    if (m_thread)
        return true;
    if (!m_device || !m_device->isOpen())
        return false;

    QVector<int> chs;
    chs.reserve(channels.size());
    for (const QVariant &v : channels) {
        bool ok = false;
        const int ch = v.toInt(&ok);
        if (ok)
            chs.append(ch);
    }
    if (chs.isEmpty() && !mergeMode)
        return false;

    m_thread = new QThread(this);
    m_worker = new RxWorker(m_device, m_ring, chs, mergeMode);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &RxWorker::run);

    if (m_stats)
        m_stats->setActiveChannels(mergeMode ? QVector<int>{0} : chs);

    m_thread->start();
    m_timer->start();

    emit runningChanged();
    return true;
}

void RxManager::stop()
{
    if (!m_thread)
        return;

    m_timer->stop();

    // requestStop 仅访问原子变量，可从 GUI 线程直接调用
    if (m_worker)
        m_worker->requestStop();

    m_thread->quit();
    if (!m_thread->wait(5000))
        m_thread->wait(5000);

    delete m_worker;
    m_worker = nullptr;

    m_thread->deleteLater();
    m_thread = nullptr;

    // 收尾：把残留数据刷入模型
    drain();

    emit runningChanged();
    emit statisticsChanged();
}

void RxManager::clear()
{
    if (m_sourceModel)
        m_sourceModel->clear();
    emit statisticsChanged();
}

void RxManager::drain()
{
    if (!m_ring)
        return;

    if (m_paused)
        return;

    QVector<FrameData> batch;
    const int n = m_ring->pop(batch, kDrainMaxPerTick);
    if (n > 0) {
        // 记录基于真实帧批（显示层软件滤波之前，风险 19）；入队非阻塞
        if (m_recorder)
            m_recorder->enqueueFrames(batch);
        if (m_stats)
            m_stats->addFrames(batch);
        if (m_sourceModel)
            m_sourceModel->append(batch);
    }

    emit statisticsChanged();
}

QAbstractItemModel *RxManager::frameModel() const
{
    return m_model;
}

CanFilter *RxManager::filter() const
{
    return m_filter;
}

void RxManager::setStatistics(Statistics *stats)
{
    m_stats = stats;
}

void RxManager::setRecorder(Recorder *recorder)
{
    m_recorder = recorder;
}

bool RxManager::running() const
{
    return m_thread != nullptr;
}

bool RxManager::paused() const
{
    return m_paused;
}

void RxManager::setPaused(bool paused)
{
    if (paused == m_paused)
        return;
    m_paused = paused;
    emit pausedChanged();
}

int RxManager::timestampMode() const
{
    return m_sourceModel ? m_sourceModel->timestampMode() : FrameListModel::RelativeTime;
}

void RxManager::setTimestampMode(int mode)
{
    if (m_sourceModel)
        m_sourceModel->setTimestampMode(mode);
}

qulonglong RxManager::totalReceived() const
{
    return m_ring ? m_ring->totalPushed() : 0;
}

qulonglong RxManager::totalDropped() const
{
    return m_ring ? m_ring->totalDropped() : 0;
}

int RxManager::bufferedCount() const
{
    return m_ring ? m_ring->size() : 0;
}

int RxManager::displayedCount() const
{
    return m_model ? m_model->rowCount() : 0;
}
