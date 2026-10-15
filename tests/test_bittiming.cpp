#include <QtTest>

#include "core/BitTiming.h"

// 位定时约定（2026-10-15 实机判决 + 4 速率交叉验证，USBCANFD-200U HW=0x0300）：
//   bps = clk / ((brp + 1) * (tseg1 + tseg2 + 3))
//   采样点(%) = 100 * (tseg1 + 2) / (tseg1 + tseg2 + 3)
//   字段均为「寄存器值 = 有效值 − 1」，clk 即真实控制器时钟（60M/80M）。
class TestBitTiming : public QObject
{
    Q_OBJECT

private slots:
    // ---- 已实机验证的四档：80MHz + 形状 tseg1=12/tseg2=1（16 Tq、SP 87.5%）----
    void verified80MHzAllRates()
    {
        struct Case
        {
            quint32 bps;
            quint16 brp;
        };
        const Case cases[] = {
            {125000, 39},   // 实测 ACK 5/5
            {250000, 19},   // 实测 ACK 3/3
            {500000, 9},    // 实测 ACK 3/3
            {1000000, 4},   // 实测 ACK 3/3
        };
        for (const Case &c : cases) {
            const auto r = BitTiming::calculate(80000000, c.bps, 87.5);
            QVERIFY2(r.ok, qPrintable(QStringLiteral("bps=%1 无解").arg(c.bps)));
            QVERIFY(r.exact);
            QVERIFY2(r.params.brp == c.brp,
                     qPrintable(QStringLiteral("bps=%1 brp=%2（期望 %3）")
                                    .arg(c.bps).arg(r.params.brp).arg(c.brp)));
            QCOMPARE(r.params.tseg1, quint8(12));
            QCOMPARE(r.params.tseg2, quint8(1));
            QCOMPARE(BitTiming::totalTq(r.params), 16);
            QVERIFY2(qAbs(r.actualBps - double(c.bps)) < 1.0,
                     qPrintable(QStringLiteral("bps=%1 实际 %2").arg(c.bps).arg(r.actualBps)));
            QVERIFY(qAbs(r.samplePoint - 87.5) < 0.01);
        }
    }

    // 实测：60MHz + 同名形状 125k → brp=29（ACK 5/5）
    void verified60MHz125k()
    {
        const auto r = BitTiming::calculate(60000000, 125000, 87.5);
        QVERIFY(r.ok);
        QVERIFY(r.exact);
        QCOMPARE(r.params.brp, quint16(29));
        QCOMPARE(r.params.tseg1, quint8(12));
        QCOMPARE(r.params.tseg2, quint8(1));
        QVERIFY(qAbs(r.actualBps - 125000.0) < 1.0);
        QVERIFY(qAbs(r.samplePoint - 87.5) < 0.01);
    }

    // 其余标准速率在 60MHz 下也应精确（同一约定）
    void standard60MHzAllRates()
    {
        struct Case
        {
            quint32 bps;
            quint16 brp;
            quint8 tseg1;
            quint8 tseg2;
        };
        // 250k  → 15×16 = 240
        // 500k  → 5×24 = 120（24 Tq 形状，SP 87.5%）
        // 1M    → 4×15 = 60（15 Tq 形状，SP 86.7%）
        const Case cases[] = {
            {250000, 14, 12, 1},
            {500000, 4, 19, 2},
            {1000000, 3, 11, 1},
        };
        for (const Case &c : cases) {
            const auto r = BitTiming::calculate(60000000, c.bps, 87.5);
            QVERIFY2(r.ok, qPrintable(QStringLiteral("bps=%1 无解").arg(c.bps)));
            QVERIFY(r.exact);
            QCOMPARE(r.params.brp, c.brp);
            QCOMPARE(r.params.tseg1, c.tseg1);
            QCOMPARE(r.params.tseg2, c.tseg2);
            QVERIFY2(qAbs(r.actualBps - double(c.bps)) < 1.0,
                     qPrintable(QStringLiteral("bps=%1 实际 %2").arg(c.bps).arg(r.actualBps)));
        }
    }

    // ZLG 官方 demo（test.c，clk=60MHz）：仲裁 brp5/t14/t3 → 500k；数据 brp1/t10/t2 → 2M
    void zlgDemoValues()
    {
        BitTimingParams arb;
        arb.brp = 5;
        arb.tseg1 = 14;
        arb.tseg2 = 3;
        QCOMPARE(BitTiming::totalTq(arb), 20);           // 1+(14+1)+(3+1)
        QCOMPARE(BitTiming::actualBps(60000000, arb), 500000.0);

        BitTimingParams dat;
        dat.brp = 1;
        dat.tseg1 = 10;
        dat.tseg2 = 2;
        QCOMPARE(BitTiming::totalTq(dat), 15);
        QCOMPARE(BitTiming::actualBps(60000000, dat), 2000000.0);
    }

    // 参数构造：byTq 必须与 calculate() 选出的同一形状一致
    void paramsForTqMatchesCalculate()
    {
        BitTimingParams p;
        QVERIFY(BitTiming::paramsForTq(80000000, 125000, 87.5, 16, &p));
        QCOMPARE(p.brp, quint16(39));
        QCOMPARE(p.tseg1, quint8(12));
        QCOMPARE(p.tseg2, quint8(1));

        // 不能整数命中的组合应被拒绝
        QVERIFY(!BitTiming::paramsForTq(80000000, 125000, 87.5, 15, &p));
        QVERIFY(!BitTiming::paramsForTq(80000000, 0, 87.5, 16, &p));
        QVERIFY(!BitTiming::paramsForTq(0, 125000, 87.5, 16, &p));
    }

    void clockForHardware()
    {
        QCOMPARE(BitTiming::clockForHardwareVersion(0x0100), quint32(60000000));
        QCOMPARE(BitTiming::clockForHardwareVersion(0x0101), quint32(60000000));
        QCOMPARE(BitTiming::clockForHardwareVersion(0x0103), quint32(80000000));
        QCOMPARE(BitTiming::clockForHardwareVersion(0x0300), quint32(80000000));
    }

    // 采样点公式：(tseg1+2)/(tseg1+tseg2+3)
    void samplePointFormula()
    {
        BitTimingParams p;
        p.brp = 39;
        p.tseg1 = 12;
        p.tseg2 = 1;
        QCOMPARE(BitTiming::samplePoint(p), 87.5);
    }

    void exactFieldDivisibility()
    {
        // 60MHz / 500k：brp=4（有效 5）、tseg1=19/tseg2=2（有效 20/3，总 24 Tq）
        const BitTimingParams p{19, 2, 2, 0, 4};
        QCOMPARE(BitTiming::actualBps(60000000, p), 500000.0);
        QCOMPARE(BitTiming::samplePoint(p), 87.5);
    }

    void samplingPointBounds()
    {
        for (quint32 clk : {60000000u, 80000000u}) {
            for (quint32 bps : {125000u, 250000u, 500000u, 1000000u, 2000000u, 4000000u, 5000000u}) {
                const auto r = BitTiming::calculate(clk, bps, 80.0);
                QVERIFY2(r.ok, qPrintable(QStringLiteral("clk=%1 bps=%2").arg(clk).arg(bps)));
                QVERIFY(r.samplePoint >= 50.0 && r.samplePoint <= 95.0);
                QVERIFY(qAbs(r.actualBps - bps) / bps < 0.02);
                // 波特率必须与公式自洽
                QVERIFY(qAbs(BitTiming::actualBps(clk, r.params) - r.actualBps) < 1e-6);
            }
        }
    }

    void invalidInput()
    {
        QVERIFY(!BitTiming::calculate(0, 500000).ok);
        QVERIFY(!BitTiming::calculate(60000000, 0).ok);
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
        // maxTq < 5 应被钳制到 5 而非失败
        const auto r = BitTiming::calculate(60000000, 500000, 87.5, 1);
        QVERIFY(r.ok);
        QVERIFY(r.params.brp >= 1);
    }

    void samplePointTargets()
    {
        for (double target : {75.0, 80.0, 87.5}) {
            const auto r = BitTiming::calculate(80000000, 500000, target);
            QVERIFY(r.ok);
            QVERIFY2(qAbs(r.samplePoint - target) <= 3.0,
                     qPrintable(QStringLiteral("target=%1 actual=%2").arg(target).arg(r.samplePoint)));
        }
        // 目标被钳制到 [50, 95]
        QVERIFY(BitTiming::calculate(80000000, 500000, 10.0).samplePoint >= 50.0);
        QVERIFY(BitTiming::calculate(80000000, 500000, 200.0).samplePoint <= 95.0);
    }

    // 偏好总 Tq≈16：误差相同时应选 16 Tq（ZLG/CiA 标准形状）
    void prefersSixteenTq()
    {
        const auto r = BitTiming::calculate(80000000, 250000, 87.5);
        QVERIFY(r.ok);
        QCOMPARE(BitTiming::totalTq(r.params), 16);
    }
};

QTEST_APPLESS_MAIN(TestBitTiming)

#include "test_bittiming.moc"
