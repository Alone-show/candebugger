#pragma once

#include <QObject>

#include <functional>

class QThread;

// 轻量后台执行器：把阻塞式设备调用移出 GUI 线程。
//
// - post(fn, done)：fn 在工作线程按提交顺序(FIFO)执行；完成后在调用 post()
//   的线程（通常是 GUI 线程）执行 done，并更新 busy 计数。
// - busy 只在提交线程内变更，避免跨线程读写计数。
// - 所有回调均捕获值语义，调用方对象生命周期需长于执行器（执行器通常作为其成员）。
class AsyncExecutor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AsyncExecutor(QObject *parent = nullptr);
    ~AsyncExecutor() override;

    bool busy() const { return m_busy; }

    void post(std::function<void()> fn, std::function<void()> done = {});

signals:
    void busyChanged();

private:
    void finishOne();

    QThread *m_thread = nullptr;
    QObject *m_worker = nullptr;   // 归属 m_thread
    int m_pending = 0;
    bool m_busy = false;
};
