#include "core/AsyncExecutor.h"

#include <QMetaObject>
#include <QThread>

AsyncExecutor::AsyncExecutor(QObject *parent)
    : QObject(parent)
{
    m_thread = new QThread;
    m_worker = new QObject;
    m_worker->moveToThread(m_thread);
    m_thread->start();
}

AsyncExecutor::~AsyncExecutor()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        delete m_worker;
        m_worker = nullptr;
        delete m_thread;
        m_thread = nullptr;
    }
}

void AsyncExecutor::post(std::function<void()> fn, std::function<void()> done)
{
    if (m_pending == 0) {
        m_busy = true;
        emit busyChanged();
    }
    ++m_pending;

    QMetaObject::invokeMethod(m_worker, [this, fn = std::move(fn), done = std::move(done)]() mutable {
        if (fn)
            fn();
        QMetaObject::invokeMethod(this, [this, done = std::move(done)]() mutable {
            if (done)
                done();
            finishOne();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void AsyncExecutor::finishOne()
{
    if (m_pending <= 0)
        return;
    --m_pending;
    if (m_pending == 0 && m_busy) {
        m_busy = false;
        emit busyChanged();
    }
}
