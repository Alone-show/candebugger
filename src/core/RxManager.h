#pragma once

#include <QObject>
#include <QSharedPointer>
#include <QVariantList>
#include <QVector>

class CanDevice;
class CanFilter;
class FrameListModel;
class FrameFilterProxyModel;
class RingBuffer;
class RxWorker;
class Recorder;
class Statistics;
class QAbstractItemModel;
class QThread;
class QTimer;

// 接收管线：线程 + 环形缓冲 + 模型 + 定时合并刷新
class RxManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *frameModel READ frameModel CONSTANT)
    Q_PROPERTY(CanFilter *filter READ filter CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int timestampMode READ timestampMode WRITE setTimestampMode NOTIFY timestampModeChanged)
    Q_PROPERTY(qulonglong totalReceived READ totalReceived NOTIFY statisticsChanged)
    Q_PROPERTY(qulonglong totalDropped READ totalDropped NOTIFY statisticsChanged)
    Q_PROPERTY(int bufferedCount READ bufferedCount NOTIFY statisticsChanged)
    Q_PROPERTY(int displayedCount READ displayedCount NOTIFY statisticsChanged)

public:
    explicit RxManager(CanDevice *device, QObject *parent = nullptr);
    ~RxManager() override;

    Q_INVOKABLE bool start(const QVariantList &channels, bool mergeMode = false);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();

    QAbstractItemModel *frameModel() const;
    CanFilter *filter() const;
    bool running() const;

    // 统计引擎（可为空）；drain() 时按批注入接收帧
    void setStatistics(Statistics *stats);

    // 记录器（可为空）；drain() 时按批非阻塞入队（在显示层过滤前，风险 19）
    void setRecorder(Recorder *recorder);

    bool paused() const;
    void setPaused(bool paused);

    int timestampMode() const;
    void setTimestampMode(int mode);
    qulonglong totalReceived() const;
    qulonglong totalDropped() const;
    int bufferedCount() const;
    int displayedCount() const;

signals:
    void runningChanged();
    void pausedChanged();
    void timestampModeChanged();
    void statisticsChanged();

private slots:
    void drain();

private:
    CanDevice *m_device = nullptr;
    FrameListModel *m_sourceModel = nullptr;
    FrameFilterProxyModel *m_model = nullptr;
    CanFilter *m_filter = nullptr;
    Statistics *m_stats = nullptr;
    Recorder *m_recorder = nullptr;
    QSharedPointer<RingBuffer> m_ring;
    QThread *m_thread = nullptr;
    RxWorker *m_worker = nullptr;
    QTimer *m_timer = nullptr;
    bool m_paused = false;
};
