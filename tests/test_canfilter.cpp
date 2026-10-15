#include <QtTest>

#include "core/CanFilter.h"
#include "core/FrameData.h"

namespace {

FrameData mk(quint32 id, bool extended = false, bool fd = false,
             bool remote = false, bool error = false, bool tx = false)
{
    FrameData f;
    f.id = id;
    f.extended = extended;
    f.fd = fd;
    f.remote = remote;
    f.error = error;
    f.tx = tx;
    f.len = 8;
    return f;
}

} // namespace

class TestCanFilter : public QObject
{
    Q_OBJECT

private slots:
    void defaultMatchesAll()
    {
        FrameFilterConfig cfg;
        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x123)));
        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x1ABCDEF0, true, true, false, false, true)));
        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x0, false, false, true)));
        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x0, false, false, false, true)));
    }

    void idRangeFilter()
    {
        FrameFilterConfig cfg;
        cfg.idEnabled = true;
        cfg.idMin = 0x100;
        cfg.idMax = 0x200;

        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x100)));
        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x180)));
        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x200)));
        QVERIFY(!CanFilter::matchesConfig(cfg, mk(0x0FF)));
        QVERIFY(!CanFilter::matchesConfig(cfg, mk(0x201)));
    }

    void idSingleValue()
    {
        FrameFilterConfig cfg;
        cfg.idEnabled = true;
        cfg.idMin = cfg.idMax = 0x123;
        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x123)));
        QVERIFY(!CanFilter::matchesConfig(cfg, mk(0x124)));
    }

    void standardVsExtendedMask()
    {
        FrameFilterConfig cfg;
        cfg.idEnabled = true;
        cfg.idMin = cfg.idMax = 0x100;

        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x100, false)));
        // 扩展帧按 29 位取掩码后比较：0x18FF50E5 & 0x7FF = 0xE5 != 0x100
        QVERIFY(!CanFilter::matchesConfig(cfg, mk(0x18FF50E5, true)));

        // 扩展 ID 单值匹配（全 29 位有效）
        FrameFilterConfig ext;
        ext.idEnabled = true;
        ext.idExtended = true;
        ext.idMin = ext.idMax = 0x18FF50E5;
        QVERIFY(CanFilter::matchesConfig(ext, mk(0x18FF50E5, true)));
        QVERIFY(!CanFilter::matchesConfig(ext, mk(0x18FF50E6, true)));
    }

    void directionFilter()
    {
        FrameFilterConfig rxOnly;
        rxOnly.direction = 1;
        QVERIFY(!CanFilter::matchesConfig(rxOnly, mk(0x100, false, false, false, false, true)));
        QVERIFY(CanFilter::matchesConfig(rxOnly, mk(0x100, false, false, false, false, false)));

        FrameFilterConfig txOnly;
        txOnly.direction = 2;
        QVERIFY(CanFilter::matchesConfig(txOnly, mk(0x100, false, false, false, false, true)));
        QVERIFY(!CanFilter::matchesConfig(txOnly, mk(0x100, false, false, false, false, false)));
    }

    void typeFilter()
    {
        FrameFilterConfig cfg;
        cfg.showExtended = false;
        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x100, false)));
        QVERIFY(!CanFilter::matchesConfig(cfg, mk(0x100, true)));

        FrameFilterConfig cfg2;
        cfg2.showStandard = false;
        QVERIFY(!CanFilter::matchesConfig(cfg2, mk(0x100, false)));
        QVERIFY(CanFilter::matchesConfig(cfg2, mk(0x100, true)));
    }

    void formatFilter()
    {
        FrameFilterConfig cfg;
        cfg.showFd = false;
        QVERIFY(!CanFilter::matchesConfig(cfg, mk(0x100, false, true)));
        QVERIFY(CanFilter::matchesConfig(cfg, mk(0x100, false, false)));

        FrameFilterConfig cfg2;
        cfg2.showCan = false;
        QVERIFY(!CanFilter::matchesConfig(cfg2, mk(0x100, false, false)));
        QVERIFY(CanFilter::matchesConfig(cfg2, mk(0x100, false, true)));

        FrameFilterConfig cfg3;
        cfg3.showRemote = false;
        QVERIFY(!CanFilter::matchesConfig(cfg3, mk(0x100, false, false, true)));

        FrameFilterConfig cfg4;
        cfg4.showError = false;
        QVERIFY(!CanFilter::matchesConfig(cfg4, mk(0x100, false, false, false, true)));
    }

    void searchTextFilter()
    {
        FrameData f;
        f.id = 0x123;
        f.len = 3;
        f.data = QByteArray::fromHex("A0B1C2");

        FrameFilterConfig cfg;
        cfg.searchText = "123";
        QVERIFY(CanFilter::matchesConfig(cfg, f));

        cfg.searchText = "0x123";
        QVERIFY(CanFilter::matchesConfig(cfg, f));

        cfg.searchText = "a0b1";
        QVERIFY(CanFilter::matchesConfig(cfg, f));

        cfg.searchText = "A0 B1";
        QVERIFY(CanFilter::matchesConfig(cfg, f));

        cfg.searchText = "999";
        QVERIFY(!CanFilter::matchesConfig(cfg, f));

        cfg.searchText = "";
        QVERIFY(CanFilter::matchesConfig(cfg, f));
    }

    void hardwareTableBytes()
    {
        QVector<HardwareFilterRule> rules;
        rules << HardwareFilterRule{true, false, 0x100, 0x100};
        rules << HardwareFilterRule{true, true, 0x7FF1, 0x7FF2};

        ZCAN_FILTER_TABLE table;
        QString err;
        QVERIFY(CanFilter::buildHardwareTable(rules, &table, &err));
        QVERIFY(err.isEmpty());
        // size 为字节数（参考 SDK demo test.c）
        QCOMPARE(int(table.size), int(sizeof(ZCAN_FILTER) * 2));

        QCOMPARE(int(table.table[0].type), 0);
        QCOMPARE(table.table[0].sid, quint32(0x100));
        QCOMPARE(table.table[0].eid, quint32(0x100));

        QCOMPARE(int(table.table[1].type), 1);
        QCOMPARE(table.table[1].sid, quint32(0x7FF1));
        QCOMPARE(table.table[1].eid, quint32(0x7FF2));
    }

    void hardwareClampAndSwap()
    {
        QVector<HardwareFilterRule> rules;
        rules << HardwareFilterRule{true, true, 0xFFFFFFFFu, 0x10};   // 扩展：截断并交换
        rules << HardwareFilterRule{true, false, 0x1234, 0x10};      // 标准：截断到 0x7FF

        ZCAN_FILTER_TABLE table;
        QVERIFY(CanFilter::buildHardwareTable(rules, &table));

        QCOMPARE(table.table[0].sid, quint32(0x10));
        QCOMPARE(table.table[0].eid, quint32(0x1FFFFFFF));
        QCOMPARE(table.table[1].sid, quint32(0x10));
        QCOMPARE(table.table[1].eid, quint32(0x7FF));
    }

    void hardwareDisabledAndTooMany()
    {
        QVector<HardwareFilterRule> disabled;
        disabled << HardwareFilterRule{false, false, 0x100, 0x200};
        ZCAN_FILTER_TABLE table;
        QVERIFY(CanFilter::buildHardwareTable(disabled, &table));
        QCOMPARE(int(table.size), 0);

        QVector<HardwareFilterRule> many;
        for (int i = 0; i < 65; ++i)
            many << HardwareFilterRule{true, false, static_cast<quint32>(i), static_cast<quint32>(i)};
        QString err;
        QVERIFY(!CanFilter::buildHardwareTable(many, &table, &err));
        QVERIFY(!err.isEmpty());
    }

    void rulesFromListParsesMaps()
    {
        QVariantList list;
        list << QVariantMap{{QStringLiteral("extended"), true},
                            {QStringLiteral("start"), 0x100},
                            {QStringLiteral("end"), 0x1FF}};
        const QVector<HardwareFilterRule> rules = CanFilter::rulesFromList(list);
        QCOMPARE(rules.size(), 1);
        QVERIFY(rules[0].enabled);
        QVERIFY(rules[0].extended);
        QCOMPARE(rules[0].startId, quint32(0x100));
        QCOMPARE(rules[0].endId, quint32(0x1FF));
    }
};

QTEST_APPLESS_MAIN(TestCanFilter)

#include "test_canfilter.moc"
