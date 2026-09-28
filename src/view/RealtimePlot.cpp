#include "view/RealtimePlot.h"

#include <QMutexLocker>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QtMath>

namespace {

const QVector<QColor> &defaultPalette()
{
    static const QVector<QColor> palette = {
        QColor(QStringLiteral("#4c9aff")), QColor(QStringLiteral("#7bd88f")),
        QColor(QStringLiteral("#e0b34c")), QColor(QStringLiteral("#e06c75")),
        QColor(QStringLiteral("#c678dd")), QColor(QStringLiteral("#56b6c2")),
        QColor(QStringLiteral("#d19a66")), QColor(QStringLiteral("#98c379"))
    };
    return palette;
}

QColor parseColor(const QVariant &value, const QColor &fallback)
{
    if (!value.isValid())
        return fallback;
    if (value.canConvert<QColor>()) {
        const QColor c = value.value<QColor>();
        if (c.isValid())
            return c;
    }
    const QColor c(value.toString());
    return c.isValid() ? c : fallback;
}

const int kMaxSeries = 16;

inline void putPoint(QSGGeometry::Point2D *&v, double x, double y)
{
    v->x = static_cast<float>(x);
    v->y = static_cast<float>(y);
    ++v;
}

} // namespace

RealtimePlot::RealtimePlot(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    m_ring.setSeriesCount(m_series);
    m_ring.setCapacity(m_maxPoints);
}

int RealtimePlot::series() const { return m_series; }

void RealtimePlot::setSeries(int n)
{
    n = qBound(1, n, kMaxSeries);
    if (n == m_series)
        return;
    m_series = n;
    {
        QMutexLocker locker(&m_mutex);
        m_ring.setSeriesCount(n);
    }
    m_structureDirty = true;
    emit seriesChanged();
    update();
}

int RealtimePlot::maxPoints() const { return m_maxPoints; }

void RealtimePlot::setMaxPoints(int n)
{
    n = qBound(2, n, 100000);
    if (n == m_maxPoints)
        return;
    m_maxPoints = n;
    {
        QMutexLocker locker(&m_mutex);
        m_ring.setCapacity(n);
    }
    emit maxPointsChanged();
    update();
}

bool RealtimePlot::autoScale() const { return m_autoScale; }

void RealtimePlot::setAutoScale(bool on)
{
    if (on == m_autoScale)
        return;
    m_autoScale = on;
    emit autoScaleChanged();
    update();
}

bool RealtimePlot::fill() const { return m_fill; }

void RealtimePlot::setFill(bool on)
{
    if (on == m_fill)
        return;
    m_fill = on;
    m_structureDirty = true;
    emit fillChanged();
    update();
}

double RealtimePlot::lineWidth() const { return m_lineWidth; }

void RealtimePlot::setLineWidth(double w)
{
    w = qBound(0.5, w, 10.0);
    if (qFuzzyCompare(w, m_lineWidth))
        return;
    m_lineWidth = w;
    emit lineWidthChanged();
    update();
}

QVariantList RealtimePlot::lineColors() const { return m_lineColors; }

void RealtimePlot::setLineColors(const QVariantList &colors)
{
    if (colors == m_lineColors)
        return;
    m_lineColors = colors;
    emit lineColorsChanged();
    update();
}

double RealtimePlot::minValue() const { return m_minValue; }

void RealtimePlot::setMinValue(double v)
{
    if (qFuzzyCompare(v, m_minValue))
        return;
    m_minValue = v;
    emit minValueChanged();
    update();
}

double RealtimePlot::maxValue() const { return m_maxValue; }

void RealtimePlot::setMaxValue(double v)
{
    if (qFuzzyCompare(v, m_maxValue))
        return;
    m_maxValue = v;
    emit maxValueChanged();
    update();
}

void RealtimePlot::append(int series, double value)
{
    {
        QMutexLocker locker(&m_mutex);
        m_ring.append(series, value);
    }
    update();
}

void RealtimePlot::clear()
{
    {
        QMutexLocker locker(&m_mutex);
        m_ring.clear();
    }
    update();
}

QColor RealtimePlot::seriesColor(int series) const
{
    const QVector<QColor> &palette = defaultPalette();
    QColor c = palette.at(series % palette.size());
    if (series >= 0 && series < m_lineColors.size())
        c = parseColor(m_lineColors.at(series), c);
    return c;
}

QSGNode *RealtimePlot::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (width() <= 0.0 || height() <= 0.0) {
        delete oldNode;
        m_root = nullptr;
        m_nodes.clear();
        m_structureDirty = true;
        return nullptr;
    }

    // 锁内仅做快照拷贝
    {
        QMutexLocker locker(&m_mutex);
        const int sc = m_ring.seriesCount();
        if (m_snapshot.size() != sc)
            m_snapshot.resize(sc);
        for (int s = 0; s < sc; ++s) {
            const int n = m_ring.count(s);
            QVector<double> &dst = m_snapshot[s];
            dst.resize(n);
            for (int i = 0; i < n; ++i)
                dst[i] = m_ring.at(s, i);
        }
    }

    const bool needRebuild = (oldNode == nullptr) || m_structureDirty
            || m_nodes.size() != m_series;
    if (needRebuild && oldNode != nullptr) {
        delete oldNode;
        oldNode = nullptr;
    }

    if (oldNode == nullptr) {
        m_root = new QSGNode;
        m_nodes.clear();
        m_nodes.resize(m_series);
        for (int s = 0; s < m_series; ++s) {
            SeriesNode sn;
            const QColor color = seriesColor(s);

            if (m_fill) {
                QSGGeometryNode *fillNode = new QSGGeometryNode;
                QSGGeometry *fg = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
                fg->setDrawingMode(QSGGeometry::DrawTriangleStrip);
                fillNode->setGeometry(fg);
                fillNode->setFlag(QSGNode::OwnsGeometry);
                QSGFlatColorMaterial *fm = new QSGFlatColorMaterial;
                QColor fc = color;
                fc.setAlpha(60);
                fm->setColor(fc);
                fillNode->setMaterial(fm);
                fillNode->setFlag(QSGNode::OwnsMaterial);
                fillNode->setFlag(QSGNode::OwnedByParent);
                m_root->appendChildNode(fillNode);
                sn.fill = fillNode;
            }

            QSGGeometryNode *lineNode = new QSGGeometryNode;
            QSGGeometry *lg = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
            lg->setDrawingMode(QSGGeometry::DrawTriangles);
            lineNode->setGeometry(lg);
            lineNode->setFlag(QSGNode::OwnsGeometry);
            QSGFlatColorMaterial *lm = new QSGFlatColorMaterial;
            lm->setColor(color);
            lineNode->setMaterial(lm);
            lineNode->setFlag(QSGNode::OwnsMaterial);
            lineNode->setFlag(QSGNode::OwnedByParent);
            m_root->appendChildNode(lineNode);
            sn.line = lineNode;

            m_nodes[s] = sn;
        }
        m_structureDirty = false;
        oldNode = m_root;
    } else {
        m_root = oldNode;
    }

    const double w = width();
    const double h = height();
    const double half = qMax(0.5, m_lineWidth * 0.5);
    const double left = half;
    const double bottom = h - half;
    const double plotW = qMax(1.0, w - 2.0 * half);
    const double plotH = qMax(1.0, h - 2.0 * half);
    const int cap = qMax(2, m_maxPoints);

    // 值域
    double vmin = 0.0;
    double vmax = 1.0;
    if (m_autoScale) {
        bool have = false;
        for (const QVector<double> &pts : m_snapshot) {
            for (double v : pts) {
                if (!have) {
                    vmin = vmax = v;
                    have = true;
                } else {
                    vmin = qMin(vmin, v);
                    vmax = qMax(vmax, v);
                }
            }
        }
        if (!have) {
            vmin = 0.0;
            vmax = 1.0;
        }
    } else {
        vmin = m_minValue;
        vmax = m_maxValue;
    }
    if (vmax - vmin < 1e-9) {
        const double center = vmin;
        vmin = center - 0.5;
        vmax = center + 0.5;
    } else if (m_autoScale) {
        const double pad = (vmax - vmin) * 0.05;
        vmin -= pad;
        vmax += pad;
    }
    const double range = vmax - vmin;
    const double xSpan = static_cast<double>(cap - 1);

    const QVector<double> emptyPts;
    for (int s = 0; s < m_nodes.size(); ++s) {
        SeriesNode &sn = m_nodes[s];
        if (!sn.line)
            continue;
        const QVector<double> &pts = (s < m_snapshot.size()) ? m_snapshot.at(s) : emptyPts;
        const int n = pts.size();

        const double offset = static_cast<double>(qMax(0, cap - n));
        const auto xAt = [&](int i) {
            return left + (offset + i) / xSpan * plotW;
        };
        const auto yAt = [&](double v) {
            return bottom - (v - vmin) / range * plotH;
        };

        // 颜色（随属性变化刷新）
        const QColor color = seriesColor(s);
        static_cast<QSGFlatColorMaterial *>(sn.line->material())->setColor(color);

        // 折线：逐段四边形（三角形）以获得可控线宽
        const int segCount = (n >= 2) ? (n - 1) : 0;
        const int lineVerts = (n >= 2) ? (segCount * 6) : (n == 1 ? 6 : 0);
        QSGGeometry *lg = sn.line->geometry();
        lg->allocate(lineVerts);
        if (lineVerts > 0) {
            QSGGeometry::Point2D *v = lg->vertexDataAsPoint2D();
            if (n == 1) {
                const double x = xAt(0);
                const double y = yAt(pts.at(0));
                putPoint(v, x - half, y - half);
                putPoint(v, x + half, y - half);
                putPoint(v, x + half, y + half);
                putPoint(v, x - half, y - half);
                putPoint(v, x + half, y + half);
                putPoint(v, x - half, y + half);
            } else {
                for (int i = 0; i < segCount; ++i) {
                    const double x0 = xAt(i);
                    const double y0 = yAt(pts.at(i));
                    const double x1 = xAt(i + 1);
                    const double y1 = yAt(pts.at(i + 1));
                    double dx = x1 - x0;
                    double dy = y1 - y0;
                    double len = qSqrt(dx * dx + dy * dy);
                    if (len < 1e-9) {
                        dx = 1.0;
                        dy = 0.0;
                        len = 1.0;
                    }
                    const double nx = -dy / len * half;
                    const double ny = dx / len * half;
                    putPoint(v, x0 + nx, y0 + ny);
                    putPoint(v, x1 + nx, y1 + ny);
                    putPoint(v, x1 - nx, y1 - ny);
                    putPoint(v, x0 + nx, y0 + ny);
                    putPoint(v, x1 - nx, y1 - ny);
                    putPoint(v, x0 - nx, y0 - ny);
                }
            }
        }
        sn.line->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

        // 填充：折线与基线之间的三角带
        if (sn.fill) {
            QColor fc = color;
            fc.setAlpha(60);
            static_cast<QSGFlatColorMaterial *>(sn.fill->material())->setColor(fc);

            QSGGeometry *fg = sn.fill->geometry();
            const int fillVerts = n * 2;
            fg->allocate(fillVerts);
            if (fillVerts > 0) {
                QSGGeometry::Point2D *v = fg->vertexDataAsPoint2D();
                for (int i = 0; i < n; ++i) {
                    const double x = xAt(i);
                    putPoint(v, x, yAt(pts.at(i)));
                    putPoint(v, x, bottom);
                }
            }
            sn.fill->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        }
    }

    return m_root;
}
