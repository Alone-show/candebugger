#pragma once

#include <QColor>
#include <QMutex>
#include <QQuickItem>
#include <QVariantList>
#include <QVector>

#include "view/PlotRing.h"

class QSGGeometryNode;
class QSGNode;

// 零依赖实时折线图：继承 QQuickItem，自绘场景图（QSGGeometryNode）。
// 数据写入（append）与 updatePaintNode 可能分属 GUI/渲染线程，用互斥锁保护环形缓冲；
// updatePaintNode 仅在锁内做快照拷贝，几何构建在锁外完成，避免阻塞。
class RealtimePlot : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(int series READ series WRITE setSeries NOTIFY seriesChanged)
    Q_PROPERTY(int maxPoints READ maxPoints WRITE setMaxPoints NOTIFY maxPointsChanged)
    Q_PROPERTY(bool autoScale READ autoScale WRITE setAutoScale NOTIFY autoScaleChanged)
    Q_PROPERTY(bool fill READ fill WRITE setFill NOTIFY fillChanged)
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(QVariantList lineColors READ lineColors WRITE setLineColors NOTIFY lineColorsChanged)
    Q_PROPERTY(double minValue READ minValue WRITE setMinValue NOTIFY minValueChanged)
    Q_PROPERTY(double maxValue READ maxValue WRITE setMaxValue NOTIFY maxValueChanged)

public:
    explicit RealtimePlot(QQuickItem *parent = nullptr);

    int series() const;
    void setSeries(int n);

    int maxPoints() const;
    void setMaxPoints(int n);

    bool autoScale() const;
    void setAutoScale(bool on);

    bool fill() const;
    void setFill(bool on);

    double lineWidth() const;
    void setLineWidth(double w);

    QVariantList lineColors() const;
    void setLineColors(const QVariantList &colors);

    double minValue() const;
    void setMinValue(double v);

    double maxValue() const;
    void setMaxValue(double v);

    // 追加一个点（GUI 线程）；满则覆盖最旧点
    Q_INVOKABLE void append(int series, double value);
    Q_INVOKABLE void clear();

signals:
    void seriesChanged();
    void maxPointsChanged();
    void autoScaleChanged();
    void fillChanged();
    void lineWidthChanged();
    void lineColorsChanged();
    void minValueChanged();
    void maxValueChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    struct SeriesNode {
        QSGGeometryNode *line = nullptr;
        QSGGeometryNode *fill = nullptr;
    };

    QColor seriesColor(int series) const;

    mutable QMutex m_mutex;
    PlotRing m_ring;
    int m_series = 1;
    int m_maxPoints = 600;
    bool m_autoScale = true;
    bool m_fill = false;
    double m_lineWidth = 1.5;
    QVariantList m_lineColors;
    double m_minValue = 0.0;
    double m_maxValue = 100.0;

    // 渲染侧状态（仅 updatePaintNode 访问）
    QSGNode *m_root = nullptr;
    QVector<SeriesNode> m_nodes;
    QVector<QVector<double>> m_snapshot;
    bool m_structureDirty = true;
};
