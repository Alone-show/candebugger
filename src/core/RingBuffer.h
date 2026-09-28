#pragma once

#include <mutex>
#include <QVector>

#include "core/FrameData.h"

// 定长环形缓冲：写满后覆盖最旧数据，并统计丢弃数量
class RingBuffer
{
public:
    explicit RingBuffer(int capacity = 1 << 16);

    void push(const FrameData &frame);
    void push(const QVector<FrameData> &frames);

    // 取出至多 maxCount 条，返回实际取出数量
    int pop(QVector<FrameData> &out, int maxCount);

    int capacity() const;
    int size() const;
    quint64 totalPushed() const;
    quint64 totalDropped() const;

    void clear();

private:
    // std::mutex：与 QMutex 语义等价，且可被 ThreadSanitizer 正确追踪happens-before
    mutable std::mutex m_mutex;
    QVector<FrameData> m_buf;
    int m_capacity;
    int m_head = 0;
    int m_count = 0;
    quint64 m_pushed = 0;
    quint64 m_dropped = 0;
};
