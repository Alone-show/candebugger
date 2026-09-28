#pragma once

#include <QVector>
#include <QtGlobal>

// 纯逻辑环形缓冲（仅依赖 QtCore/QVector），可脱离场景图单测。
// 支持多个序列，每个序列固定容量，满时覆盖最旧点；索引 0 为最旧点。
class PlotRing
{
public:
    explicit PlotRing(int capacity = 600, int seriesCount = 1)
    {
        setSeriesCount(seriesCount);
        setCapacity(capacity);
    }

    int capacity() const { return m_capacity; }

    // 调整容量，保留最近 min(size, capacity) 个点
    void setCapacity(int capacity)
    {
        capacity = qMax(1, capacity);
        if (capacity == m_capacity)
            return;
        for (Series &s : m_series) {
            const int keep = qMin(s.size, capacity);
            QVector<double> nd(capacity);
            for (int i = 0; i < keep; ++i)
                nd[i] = rawAt(s, s.size - keep + i);
            s.data = nd;
            s.head = keep % capacity;
            s.size = keep;
        }
        m_capacity = capacity;
    }

    int seriesCount() const { return m_series.size(); }

    void setSeriesCount(int n)
    {
        n = qMax(1, n);
        if (n == m_series.size())
            return;
        m_series.resize(n);
        for (Series &s : m_series) {
            if (s.data.size() != m_capacity) {
                s.data.resize(m_capacity);
                s.head = 0;
                s.size = 0;
            }
        }
    }

    void append(int series, double value)
    {
        if (series < 0 || series >= m_series.size())
            return;
        Series &s = m_series[series];
        if (s.data.size() != m_capacity)
            s.data.resize(m_capacity);
        s.data[s.head] = value;
        s.head = (s.head + 1) % m_capacity;
        if (s.size < m_capacity)
            ++s.size;
    }

    void clear()
    {
        for (Series &s : m_series) {
            s.head = 0;
            s.size = 0;
        }
    }

    int count(int series) const
    {
        if (series < 0 || series >= m_series.size())
            return 0;
        return m_series.at(series).size;
    }

    // index 0 = 最旧点
    double at(int series, int index) const
    {
        if (series < 0 || series >= m_series.size())
            return 0.0;
        const Series &s = m_series.at(series);
        if (index < 0 || index >= s.size)
            return 0.0;
        return rawAt(s, index);
    }

    double newest(int series) const
    {
        const int n = count(series);
        return n > 0 ? at(series, n - 1) : 0.0;
    }

    double minValue(int series) const
    {
        if (series < 0 || series >= m_series.size())
            return 0.0;
        const Series &s = m_series.at(series);
        if (s.size == 0)
            return 0.0;
        double m = rawAt(s, 0);
        for (int i = 1; i < s.size; ++i)
            m = qMin(m, rawAt(s, i));
        return m;
    }

    double maxValue(int series) const
    {
        if (series < 0 || series >= m_series.size())
            return 0.0;
        const Series &s = m_series.at(series);
        if (s.size == 0)
            return 0.0;
        double m = rawAt(s, 0);
        for (int i = 1; i < s.size; ++i)
            m = qMax(m, rawAt(s, i));
        return m;
    }

    // 所有序列的极值
    double minValue() const
    {
        double m = 0.0;
        bool have = false;
        for (int i = 0; i < m_series.size(); ++i) {
            if (count(i) == 0)
                continue;
            const double v = minValue(i);
            m = have ? qMin(m, v) : v;
            have = true;
        }
        return m;
    }

    double maxValue() const
    {
        double m = 0.0;
        bool have = false;
        for (int i = 0; i < m_series.size(); ++i) {
            if (count(i) == 0)
                continue;
            const double v = maxValue(i);
            m = have ? qMax(m, v) : v;
            have = true;
        }
        return m;
    }

private:
    struct Series {
        QVector<double> data;
        int head = 0;   // 下一个写入位置
        int size = 0;   // 有效点数
    };

    static double rawAt(const Series &s, int index)
    {
        const int cap = s.data.size();
        if (cap <= 0 || s.size <= 0 || index < 0 || index >= s.size)
            return 0.0;
        int start = s.head - s.size;
        start %= cap;
        if (start < 0)
            start += cap;
        return s.data.at((start + index) % cap);
    }

    int m_capacity = 600;
    QVector<Series> m_series;
};
