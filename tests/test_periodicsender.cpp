#include <QtTest>

#include "core/FrameBuilder.h"
#include "core/PeriodicSender.h"

class TestPeriodicSender : public QObject
{
    Q_OBJECT

private:
    static QVariantMap entry(quint32 id, int intervalMs, int repeat, bool enabled)
    {
        QVariantMap m;
        m.insert(QStringLiteral("id"), id);
        m.insert(QStringLiteral("dlc"), 2);
        m.insert(QStringLiteral("data"), QStringLiteral("AA BB"));
        m.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));
        m.insert(QStringLiteral("intervalMs"), intervalMs);
        m.insert(QStringLiteral("repeat"), repeat);
        m.insert(QStringLiteral("enabled"), enabled);
        return m;
    }

private slots:
    void intervalConversion()
    {
        QCOMPARE(PeriodicSender::intervalToUnit(100.0), quint32(1000));  // 100ms
        QCOMPARE(PeriodicSender::intervalToUnit(0.1), quint32(1));
        QCOMPARE(PeriodicSender::intervalToUnit(0.0), quint32(1));
        QCOMPARE(PeriodicSender::intervalToUnit(-5.0), quint32(1));
    }

    void buildConfigFields()
    {
        QVariantList entries;
        entries << entry(0x300, 100, 0, true);
        entries << entry(0x301, 250, 5, false);

        ZCAN_TTX_CFG cfg;
        QString err;
        QVERIFY(PeriodicSender::buildConfig(entries, &cfg, &err));
        QVERIFY(err.isEmpty());
        // size 为字节数（参考 demo test.c）
        QCOMPARE(int(cfg.size), int(sizeof(ZCAN_TTX) * 2));

        QCOMPARE(cfg.table[0].interval, quint32(1000));
        QCOMPARE(int(cfg.table[0].repeat), 0);
        QCOMPARE(int(cfg.table[0].index), 0);
        QCOMPARE(int(cfg.table[0].flags), 1);
        QCOMPARE(cfg.table[0].msg.hdr.id, quint32(0x300));
        QCOMPARE(int(cfg.table[0].msg.hdr.inf.fmt), 0);

        QCOMPARE(cfg.table[1].interval, quint32(2500));
        QCOMPARE(int(cfg.table[1].repeat), 5);
        QCOMPARE(int(cfg.table[1].index), 1);
        QCOMPARE(int(cfg.table[1].flags), 0);
        QCOMPARE(cfg.table[1].msg.hdr.id, quint32(0x301));
    }

    void fdEntrySetsFdFormat()
    {
        QVariantMap m = entry(0x400, 50, 0, true);
        m.insert(QStringLiteral("fd"), true);
        m.insert(QStringLiteral("brs"), true);
        m.insert(QStringLiteral("dlc"), 12);
        m.insert(QStringLiteral("data"), QStringLiteral("01 02 03 04 05 06 07 08 09 0A 0B 0C"));

        QVariantList entries;
        entries << m;
        ZCAN_TTX_CFG cfg;
        QVERIFY(PeriodicSender::buildConfig(entries, &cfg));
        QCOMPARE(int(cfg.table[0].msg.hdr.inf.fmt), 1);
        QCOMPARE(int(cfg.table[0].msg.hdr.inf.brs), 1);
        QCOMPARE(int(cfg.table[0].msg.hdr.len), 12);
    }

    void tooManyEntriesRejected()
    {
        QVariantList entries;
        for (int i = 0; i < 9; ++i)
            entries << entry(0x300 + static_cast<quint32>(i), 100, 0, true);
        ZCAN_TTX_CFG cfg;
        QString err;
        QVERIFY(!PeriodicSender::buildConfig(entries, &cfg, &err));
        QVERIFY(!err.isEmpty());
    }

    void emptyEntriesAllowed()
    {
        QVariantList entries;
        ZCAN_TTX_CFG cfg;
        QVERIFY(PeriodicSender::buildConfig(entries, &cfg));
        QCOMPARE(int(cfg.size), 0);
    }

    void invalidDataRejected()
    {
        QVariantMap m = entry(0x300, 100, 0, true);
        m.insert(QStringLiteral("data"), QStringLiteral("0"));   // 奇数位
        QVariantList entries;
        entries << m;
        ZCAN_TTX_CFG cfg;
        QString err;
        QVERIFY(!PeriodicSender::buildConfig(entries, &cfg, &err));
        QVERIFY(err.contains(QStringLiteral("第 1 条")));
    }
};

QTEST_APPLESS_MAIN(TestPeriodicSender)

#include "test_periodicsender.moc"
