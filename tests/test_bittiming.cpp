#include <QtTest>

#include "core/BitTiming.h"

class TestBitTiming : public QObject
{
    Q_OBJECT

private slots:
    void standard500k()
    {
        const auto r = BitTiming::calculate(60000000, 500000, 87.5);
        QVERIFY(r.ok);
        QVERIFY(r.exact);
        // 偏好 Tq≈16 且 sjw>=2：n=24 -> brp=5
        QCOMPARE(r.params.brp, quint16(5));
        QCOMPARE(r.params.tseg1, quint8(20));
        QCOMPARE(r.params.tseg2, quint8(3));
        QVERIFY(r.params.sjw >= 2);
        QVERIFY(qAbs(r.actualBps - 500000.0) < 1.0);
        QVERIFY(qAbs(r.samplePoint - 87.5) < 0.01);
    }

    void standard1M()
    {
        const auto r = BitTiming::calculate(60000000, 1000000, 87.5);
        QVERIFY(r.ok);
        QVERIFY(qAbs(r.actualBps - 1000000.0) < 1.0);
        // 采样点应接近目标
        QVERIFY(qAbs(r.samplePoint - 87.5) <= 2.0);
        QVERIFY(r.params.brp >= 1);
    }

    void standard2M()
    {
        const auto r = BitTiming::calculate(60000000, 2000000, 87.5);
        QVERIFY(r.ok);
        QVERIFY(qAbs(r.actualBps - 2000000.0) < 1.0);
        QCOMPARE(r.params.brp, quint16(2));
        QCOMPARE(r.params.tseg1, quint8(12));
        QCOMPARE(r.params.tseg2, quint8(2));
    }

    void standard250k()
    {
        const auto r = BitTiming::calculate(60000000, 250000, 87.5);
        QVERIFY(r.ok);
        // n=16 -> brp=15（含 sjw=2，抗抖动更好）
        QCOMPARE(r.params.brp, quint16(15));
        QCOMPARE(r.params.tseg1, quint8(13));
        QCOMPARE(r.params.tseg2, quint8(2));
        QVERIFY(r.params.sjw >= 2);
        QVERIFY(qAbs(r.actualBps - 250000.0) < 1.0);
    }

    // 80MHz 硬件（V1.03+）：应得到 ZLG/ZCANPRO 的经典值
    void standard80MHz250k()
    {
        const auto r = BitTiming::calculate(80000000, 250000, 87.5);
        QVERIFY(r.ok);
        QVERIFY(r.exact);
        QCOMPARE(r.params.brp, quint16(20));
        QCOMPARE(r.params.tseg1, quint8(13));
        QCOMPARE(r.params.tseg2, quint8(2));
        QCOMPARE(r.params.sjw, quint8(2));
        QVERIFY(qAbs(r.actualBps - 250000.0) < 1.0);
        QVERIFY(qAbs(r.samplePoint - 87.5) < 0.01);
    }

    void standard80MHz500k()
    {
        const auto r = BitTiming::calculate(80000000, 500000, 87.5);
        QVERIFY(r.ok);
        QCOMPARE(r.params.brp, quint16(10));
        QCOMPARE(r.params.tseg1, quint8(13));
        QCOMPARE(r.params.tseg2, quint8(2));
        QCOMPARE(r.params.sjw, quint8(2));
        QVERIFY(qAbs(r.actualBps - 500000.0) < 1.0);
        QVERIFY(qAbs(r.samplePoint - 87.5) < 0.01);
    }

    void standard80MHz1M()
    {
        const auto r = BitTiming::calculate(80000000, 1000000, 87.5);
        QVERIFY(r.ok);
        QCOMPARE(r.params.brp, quint16(5));
        QCOMPARE(r.params.tseg1, quint8(13));
        QCOMPARE(r.params.tseg2, quint8(2));
        QVERIFY(qAbs(r.actualBps - 1000000.0) < 1.0);
    }

    void clockForHardware()
    {
        QCOMPARE(BitTiming::clockForHardwareVersion(0x0100), quint32(60000000));
        QCOMPARE(BitTiming::clockForHardwareVersion(0x0101), quint32(60000000));
        QCOMPARE(BitTiming::clockForHardwareVersion(0x0103), quint32(80000000));
        QCOMPARE(BitTiming::clockForHardwareVersion(0x0300), quint32(80000000));
    }

    void canClockIsFiveFourths()
    {
        // 实测：实际 CAN 控制器时钟 = 0.8 × 名义（SDK clk）
        QCOMPARE(BitTiming::canClockForSdkClock(60000000), quint32(48000000));
        QCOMPARE(BitTiming::canClockForSdkClock(80000000), quint32(64000000));
        QCOMPARE(BitTiming::canClockForSdkClock(0), quint32(0));
    }

    // 实测标定：V1.01（名义 60MHz，实际 48MHz）250k → brp=12 tseg1=13 tseg2=2
    void measured48MHz250k()
    {
        const auto r = BitTiming::calculate(BitTiming::CanClock48M, 250000, 87.5);
        QVERIFY(r.ok);
        QCOMPARE(r.params.brp, quint16(12));
        QCOMPARE(r.params.tseg1, quint8(13));
        QCOMPARE(r.params.tseg2, quint8(2));
        QCOMPARE(r.params.sjw, quint8(2));
        QVERIFY(qAbs(r.actualBps - 250000.0) < 1.0);
    }

    // 实测标定：V1.03+（名义 80MHz，实际 64MHz）250k → brp=16 tseg1=13 tseg2=2
    void measured64MHz250k()
    {
        const auto r = BitTiming::calculate(BitTiming::CanClock64M, 250000, 87.5);
        QVERIFY(r.ok);
        QCOMPARE(r.params.brp, quint16(16));
        QCOMPARE(r.params.tseg1, quint8(13));
        QCOMPARE(r.params.tseg2, quint8(2));
        QVERIFY(qAbs(r.actualBps - 250000.0) < 1.0);
    }

    void prefersLargeSjwAndSixteenTq()
    {
        // tie-break：误差相同时应选 sjw>=2、Tq 更接近 16 的候选
        const auto r = BitTiming::calculate(80000000, 500000, 87.5);
        QVERIFY(r.ok);
        QVERIFY(r.params.sjw >= 2);
        const int tq = 1 + r.params.tseg1 + r.params.tseg2;
        QVERIFY(tq == 16);
    }

    void exactFieldDivisibility()
    {
        // 夹具参数必须满足 baud = clk / (brp * (1 + tseg1 + tseg2))
        const BitTimingParams p{6, 1, 1, 0, 15};
        QCOMPARE(BitTiming::actualBps(60000000, p), 500000.0);
        QCOMPARE(BitTiming::samplePoint(p), 87.5);
    }

    void samplingPointBounds()
    {
        for (quint32 bps : {125000u, 250000u, 500000u, 1000000u, 2000000u, 4000000u, 5000000u}) {
            const auto r = BitTiming::calculate(60000000, bps, 80.0);
            QVERIFY2(r.ok, qPrintable(QStringLiteral("bps=%1").arg(bps)));
            QVERIFY(r.samplePoint >= 50.0 && r.samplePoint <= 95.0);
            QVERIFY(qAbs(r.actualBps - bps) / bps < 0.02);
        }
    }

    void invalidInput()
    {
        QVERIFY(!BitTiming::calculate(0, 500000).ok);
        QVERIFY(!BitTiming::calculate(60000000, 0).ok);
    }

    void eightyMHzClock()
    {
        // 风险 1：V1.03+ 设备时钟可能为 80MHz，公式需在两种时钟下成立
        const auto r = BitTiming::calculate(80000000, 500000, 87.5);
        QVERIFY(r.ok);
        QVERIFY(qAbs(r.actualBps - 500000.0) / 500000.0 < 0.01);
        QVERIFY(r.params.brp >= 1);
        // brp * (1 + tseg1 + tseg2) * baud == clk（整数自洽）
        const quint64 denom = quint64(r.params.brp) * (1 + r.params.tseg1 + r.params.tseg2);
        QVERIFY(qAbs(double(80000000) / double(denom) - r.actualBps) < 1e-6);
    }

    void arbitraryBaudRate()
    {
        // 非整除速率走近似回退，误差需可控
        const auto r = BitTiming::calculate(60000000, 123456, 80.0);
        QVERIFY(r.ok);
        QVERIFY(qAbs(r.actualBps - 123456.0) / 123456.0 < 0.02);
    }

    void maxTqClamp()
    {
        // maxTq < 8 应被钳制到 8 而非失败
        const auto r = BitTiming::calculate(60000000, 500000, 87.5, 1);
        QVERIFY(r.ok);
        QVERIFY(r.params.brp >= 1);
    }

    void samplePointTargets()
    {
        for (double target : {75.0, 80.0, 87.5}) {
            const auto r = BitTiming::calculate(60000000, 500000, target);
            QVERIFY(r.ok);
            QVERIFY2(qAbs(r.samplePoint - target) <= 3.0,
                     qPrintable(QStringLiteral("target=%1 actual=%2").arg(target).arg(r.samplePoint)));
        }
        // 目标被钳制到 [50, 95]
        QVERIFY(BitTiming::calculate(60000000, 500000, 10.0).samplePoint >= 50.0);
        QVERIFY(BitTiming::calculate(60000000, 500000, 200.0).samplePoint <= 95.0);
    }

    void degenerateParams()
    {
        BitTimingParams zero;
        zero.brp = 0;
        QCOMPARE(BitTiming::actualBps(60000000, zero), 0.0);
    }
};

QTEST_APPLESS_MAIN(TestBitTiming)

#include "test_bittiming.moc"
