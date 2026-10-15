#include <QtTest>

#include "core/FrameData.h"
#include "core/Statistics.h"

namespace {

FrameData mkF(quint32 id, bool tx = false, bool fd = false, bool brs = false,
              bool error = false, int len = 8, bool extended = false)
{
    FrameData f;
    f.id = id;
    f.tx = tx;
    f.fd = fd;
    f.brs = brs;
    f.error = error;
    f.len = static_cast<quint8>(len);
    f.extended = extended;
    return f;
}

} // namespace

class TestStatistics : public QObject
{
    Q_OBJECT

private slots:
    void fpsMath()
    {
        QCOMPARE(Statistics::framesPerSecond(50, 100000), 500.0);
        QCOMPARE(Statistics::framesPerSecond(0, 100000), 0.0);
        QCOMPARE(Statistics::framesPerSecond(100, 0), 0.0);
    }

    void bitEstimateClassic()
    {
        // 标准 CAN 数据帧 8 字节：47 + 64 = 111
        QCOMPARE(Statistics::frameBitEstimate(mkF(0x100)), 111);
        // 扩展帧：67 + 64 = 131
        QCOMPARE(Statistics::frameBitEstimate(mkF(0x100, false, false, false, false, 8, true)), 131);
        // 远程帧无数据：47
        QCOMPARE(Statistics::frameBitEstimate(mkF(0x100, false, false, false, false, 0)), 47);
    }

    void bitSplitFd()
    {
        int arb = 0;
        int data = 0;

        // FD + BRS：数据段按数据域波特率
        Statistics::frameBitSplit(mkF(0x100, false, true, true, false, 8), &arb, &data);
        QCOMPARE(arb, 67);     // 47 + FD 20
        QCOMPARE(data, 64);

        // FD 无 BRS：数据段仍按仲裁域
        Statistics::frameBitSplit(mkF(0x100, false, true, false, false, 8), &arb, &data);
        QCOMPARE(arb, 131);
        QCOMPARE(data, 0);
    }

    void countRxAndErrors()
    {
        Statistics s;
        QVector<FrameData> batch;
        batch << mkF(0x100) << mkF(0x101) << mkF(0x102)
              << mkF(0x103, true)                       // 发送回显，不计入 rx
              << mkF(0x104, false, false, false, true); // 错误帧
        s.addFrames(batch);

        QCOMPARE(s.rxCount(), qulonglong(4));
        QCOMPARE(s.errorFrames(), qulonglong(1));
        QCOMPARE(s.txCount(), qulonglong(0));
    }

    void countSentAndTotal()
    {
        Statistics s;
        s.addFrames(QVector<FrameData>{mkF(0x100), mkF(0x101)});
        s.countSent(3);

        QCOMPARE(s.rxCount(), qulonglong(2));
        QCOMPARE(s.txCount(), qulonglong(3));
        QCOMPARE(s.totalFrames(), qulonglong(5));
    }

    void ratesOverWindow()
    {
        Statistics s;
        s.update(0);   // 建立基线

        QVector<FrameData> batch;
        for (int i = 0; i < 50; ++i)
            batch << mkF(0x100 + static_cast<quint32>(i));
        s.addFrames(batch);
        s.countSent(10);

        s.update(100000);   // 100ms
        QCOMPARE(s.rxFps(), 500.0);
        QCOMPARE(s.txFps(), 100.0);

        // 窗口清零
        s.update(200000);
        QCOMPARE(s.rxFps(), 0.0);
        QCOMPARE(s.txFps(), 0.0);
    }

    void busLoadOverWindow()
    {
        Statistics s;
        s.setArbitrationBps(1000000);
        s.setDataBps(2000000);
        s.update(0);

        QVector<FrameData> batch;
        for (int i = 0; i < 100; ++i)
            batch << mkF(0x100);   // 每帧 111 bit
        s.addFrames(batch);

        s.update(1000000);   // 1s
        // 100*111 bit / 1e6 bps * 100% = 1.11%
        QVERIFY(qAbs(s.busLoad() - 1.11) < 0.01);
    }

    void topNOrdering()
    {
        QHash<quint64, quint64> counts;
        counts.insert(0x10, 5);
        counts.insert(0x20, 5);
        counts.insert(0x30, 9);
        counts.insert((1ull << 32) | 0x1FF, 9);   // 扩展帧 0x1FF

        const QVariantList top = Statistics::topNList(counts, 3);
        QCOMPARE(top.size(), 3);

        // 先按计数降序，计数相同按 key 升序
        QCOMPARE(top[0].toMap().value("idText").toString(), QStringLiteral("0x030"));
        QCOMPARE(top[0].toMap().value("count").toULongLong(), quint64(9));
        QCOMPARE(top[1].toMap().value("idText").toString(), QStringLiteral("0x000001FF"));
        QVERIFY(top[1].toMap().value("extended").toBool());
        QCOMPARE(top[2].toMap().value("idText").toString(), QStringLiteral("0x010"));
    }

    void topNUpdatesOnAggregate()
    {
        Statistics s;
        QVector<FrameData> batch;
        batch << mkF(0x200) << mkF(0x200) << mkF(0x201);
        s.addFrames(batch);
        s.update(0);
        s.update(100000);

        const QVariantList top = s.topN();
        QCOMPARE(top.size(), 2);
        QCOMPARE(top[0].toMap().value("idText").toString(), QStringLiteral("0x200"));
        QCOMPARE(top[0].toMap().value("count").toULongLong(), quint64(2));
    }

    void resetClears()
    {
        Statistics s;
        s.addFrames(QVector<FrameData>{mkF(0x100), mkF(0x101)});
        s.countSent(2);
        s.update(0);
        s.update(100000);
        QVERIFY(s.totalFrames() > 0);

        s.reset();
        QCOMPARE(s.rxCount(), qulonglong(0));
        QCOMPARE(s.txCount(), qulonglong(0));
        QCOMPARE(s.totalFrames(), qulonglong(0));
        QVERIFY(s.topN().isEmpty());
        QCOMPARE(s.rxFps(), 0.0);
    }

    void framesPerSecondNegativeElapsed()
    {
        QCOMPARE(Statistics::framesPerSecond(10, -5), 0.0);
        QCOMPARE(Statistics::framesPerSecond(0, 0), 0.0);
    }

    void bitSplitRemoteAndError()
    {
        int arb = 0;
        int data = 0;
        // 远程帧无数据
        Statistics::frameBitSplit(mkF(0x100, false, false, false, false, 0), &arb, &data);
        QCOMPARE(arb, 47);
        QCOMPARE(data, 0);
        // 错误帧同样无数据
        Statistics::frameBitSplit(mkF(0x100, false, false, false, true, 0), &arb, &data);
        QCOMPARE(data, 0);
        // FD+BRS 扩展帧：仲裁 67+20，数据 8*8
        Statistics::frameBitSplit(mkF(0x1, false, true, true, false, 8, true), &arb, &data);
        QCOMPARE(arb, 87);
        QCOMPARE(data, 64);
    }

    void topNListLimit()
    {
        QHash<quint64, quint64> counts;
        counts.insert(0x10, 3);
        counts.insert(0x20, 2);
        counts.insert(0x30, 1);

        QVERIFY(Statistics::topNList(counts, 0).isEmpty());
        QVERIFY(Statistics::topNList(counts, -1).isEmpty());
        QCOMPARE(Statistics::topNList(counts, 100).size(), 3);
        QVERIFY(Statistics::topNList({}, 5).isEmpty());
    }

    void topNCountClamp()
    {
        Statistics s;
        s.setTopNCount(0);
        QCOMPARE(s.topNCount(), 1);
        s.setTopNCount(1000);
        QCOMPARE(s.topNCount(), 64);
    }

    void bitrateClamp()
    {
        Statistics s;
        s.setArbitrationBps(-5);
        QCOMPARE(s.arbitrationBps(), 0);
        s.setDataBps(-1);
        QCOMPARE(s.dataBps(), 0);
    }

    void countSentIgnoresNonPositive()
    {
        Statistics s;
        s.countSent(0);
        s.countSent(-3);
        QCOMPARE(s.txCount(), qulonglong(0));
        s.countSent(2);
        QCOMPARE(s.txCount(), qulonglong(2));
    }

    void updateWithNoElapsedWindow()
    {
        Statistics s;
        s.update(1000);   // 建立基线
        s.addFrames(QVector<FrameData>{mkF(0x100)});
        s.update(1000);   // elapsed == 0，应提前返回，不清窗口
        s.update(101000); // 100ms 后
        QVERIFY(s.rxFps() > 0.0);
    }
};

QTEST_APPLESS_MAIN(TestStatistics)

#include "test_statistics.moc"
