#pragma once

#include <QObject>
#include <QSharedPointer>
#include <QVector>
#include <atomic>

class CanDevice;
class RingBuffer;

// 接收工作对象：在独立线程中轮询各通道，写入环形缓冲
class RxWorker : public QObject
{
    Q_OBJECT

public:
    RxWorker(CanDevice *device,
             QSharedPointer<RingBuffer> ring,
             QVector<int> channels,
             bool mergeMode,
             QObject *parent = nullptr);

public slots:
    void run();
    void requestStop();

signals:
    void finished();

private:
    CanDevice *m_device = nullptr;
    QSharedPointer<RingBuffer> m_ring;
    QVector<int> m_channels;
    bool m_mergeMode = false;
    std::atomic<bool> m_stop{false};
};
