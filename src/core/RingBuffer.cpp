#include "core/RingBuffer.h"

#include <mutex>

RingBuffer::RingBuffer(int capacity)
    : m_capacity(qMax(1, capacity))
{
    m_buf.resize(m_capacity);
}

void RingBuffer::push(const FrameData &frame)
{
    std::lock_guard<std::mutex> locker(m_mutex);
    if (m_count == m_capacity) {
        // 覆盖最旧
        m_buf[m_head] = frame;
        m_head = (m_head + 1) % m_capacity;
        ++m_dropped;
    } else {
        const int idx = (m_head + m_count) % m_capacity;
        m_buf[idx] = frame;
        ++m_count;
    }
    ++m_pushed;
}

void RingBuffer::push(const QVector<FrameData> &frames)
{
    std::lock_guard<std::mutex> locker(m_mutex);
    for (const FrameData &frame : frames) {
        if (m_count == m_capacity) {
            m_buf[m_head] = frame;
            m_head = (m_head + 1) % m_capacity;
            ++m_dropped;
        } else {
            const int idx = (m_head + m_count) % m_capacity;
            m_buf[idx] = frame;
            ++m_count;
        }
        ++m_pushed;
    }
}

int RingBuffer::pop(QVector<FrameData> &out, int maxCount)
{
    std::lock_guard<std::mutex> locker(m_mutex);
    const int n = qMin(maxCount, m_count);
    out.reserve(out.size() + n);
    for (int i = 0; i < n; ++i) {
        out.append(m_buf[m_head]);
        m_head = (m_head + 1) % m_capacity;
    }
    m_count -= n;
    return n;
}

int RingBuffer::capacity() const
{
    std::lock_guard<std::mutex> locker(m_mutex);
    return m_capacity;
}

int RingBuffer::size() const
{
    std::lock_guard<std::mutex> locker(m_mutex);
    return m_count;
}

quint64 RingBuffer::totalPushed() const
{
    std::lock_guard<std::mutex> locker(m_mutex);
    return m_pushed;
}

quint64 RingBuffer::totalDropped() const
{
    std::lock_guard<std::mutex> locker(m_mutex);
    return m_dropped;
}

void RingBuffer::clear()
{
    std::lock_guard<std::mutex> locker(m_mutex);
    m_head = 0;
    m_count = 0;
}
