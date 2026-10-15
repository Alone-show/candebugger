#include <QtTest>

#include "view/PlotRing.h"

class TestPlotRing : public QObject
{
    Q_OBJECT

private slots:
    void basics()
    {
        PlotRing ring(4, 1);
        QCOMPARE(ring.capacity(), 4);
        QCOMPARE(ring.seriesCount(), 1);
        QCOMPARE(ring.count(0), 0);

        ring.append(0, 1.0);
        ring.append(0, 2.0);
        ring.append(0, 3.0);

        QCOMPARE(ring.count(0), 3);
        QCOMPARE(ring.at(0, 0), 1.0);
        QCOMPARE(ring.at(0, 1), 2.0);
        QCOMPARE(ring.at(0, 2), 3.0);
        QCOMPARE(ring.newest(0), 3.0);
        QCOMPARE(ring.minValue(0), 1.0);
        QCOMPARE(ring.maxValue(0), 3.0);
    }

    void overflowKeepsNewest()
    {
        PlotRing ring(3, 1);
        for (int i = 1; i <= 5; ++i)
            ring.append(0, i);

        QCOMPARE(ring.count(0), 3);
        QCOMPARE(ring.at(0, 0), 3.0);
        QCOMPARE(ring.at(0, 1), 4.0);
        QCOMPARE(ring.at(0, 2), 5.0);
        QCOMPARE(ring.newest(0), 5.0);
    }

    void multipleSeries()
    {
        PlotRing ring(4, 2);
        ring.append(0, 10);
        ring.append(1, 100);
        ring.append(0, 20);
        ring.append(1, 200);

        QCOMPARE(ring.count(0), 2);
        QCOMPARE(ring.count(1), 2);
        QCOMPARE(ring.at(0, 1), 20.0);
        QCOMPARE(ring.at(1, 1), 200.0);
        QCOMPARE(ring.minValue(), 10.0);
        QCOMPARE(ring.maxValue(), 200.0);
    }

    void invalidSeriesIgnored()
    {
        PlotRing ring(4, 1);
        ring.append(-1, 5.0);
        ring.append(3, 5.0);
        QCOMPARE(ring.count(0), 0);
        QCOMPARE(ring.at(0, 0), 0.0);
    }

    void resizeSeries()
    {
        PlotRing ring(4, 1);
        ring.append(0, 1.0);
        ring.append(0, 2.0);

        ring.setSeriesCount(3);
        QCOMPARE(ring.seriesCount(), 3);
        QCOMPARE(ring.count(0), 2);   // 已有序列数据保留
        QCOMPARE(ring.count(2), 0);

        ring.append(2, 9.0);
        QCOMPARE(ring.at(2, 0), 9.0);

        ring.setSeriesCount(1);
        QCOMPARE(ring.seriesCount(), 1);
        QCOMPARE(ring.count(0), 2);
    }

    void resizeCapacityKeepsNewest()
    {
        PlotRing ring(5, 1);
        for (int i = 1; i <= 5; ++i)
            ring.append(0, i);

        ring.setCapacity(3);
        QCOMPARE(ring.capacity(), 3);
        QCOMPARE(ring.count(0), 3);
        QCOMPARE(ring.at(0, 0), 3.0);
        QCOMPARE(ring.at(0, 2), 5.0);

        ring.setCapacity(6);
        QCOMPARE(ring.capacity(), 6);
        QCOMPARE(ring.count(0), 3);
        QCOMPARE(ring.at(0, 2), 5.0);

        ring.append(0, 6.0);
        QCOMPARE(ring.count(0), 4);
        QCOMPARE(ring.newest(0), 6.0);
    }

    void clearResets()
    {
        PlotRing ring(4, 2);
        ring.append(0, 1.0);
        ring.append(1, 2.0);
        ring.clear();

        QCOMPARE(ring.count(0), 0);
        QCOMPARE(ring.count(1), 0);
        QCOMPARE(ring.minValue(), 0.0);
        QCOMPARE(ring.maxValue(), 0.0);

        ring.append(0, 7.0);
        QCOMPARE(ring.newest(0), 7.0);
    }
};

QTEST_APPLESS_MAIN(TestPlotRing)

#include "test_plotring.moc"
