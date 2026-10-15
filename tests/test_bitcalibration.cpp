#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include "core/BitCalibration.h"
#include "core/BitTiming.h"

// 标定表：内置实测值 + v1→v2 迁移（旧「0.8×名义时钟」模型算出的条目必须被清掉）
//
// 注意：BitCalibration 是进程内单例，用默认 QSettings；本测试在第一次
// instance() 之前把 QSettings 重定向到临时目录，避免污染用户配置。
class TestBitCalibration : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    void pointSettingsAtTempDir()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("CanDebuggerTest"));
        QCoreApplication::setApplicationName(QStringLiteral("BitCalibrationTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    static QString iniPath(const QString &dir)
    {
        return dir + QStringLiteral("/CanDebuggerTest/BitCalibrationTest.ini");
    }

    void writeV1StaleFile()
    {
        // 模拟上一版留下的配置：seeded=true，250k 是旧模型值（60MHz/brp12/13/2）
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("CanDebuggerTest"), QStringLiteral("BitCalibrationTest"));
        s.clear();
        s.setValue(QStringLiteral("calibration/seeded"), true);
        s.beginGroup(QStringLiteral("calibration/250000"));
        s.setValue(QStringLiteral("valid"), true);
        s.setValue(QStringLiteral("clock"), 60000000u);
        s.setValue(QStringLiteral("brp"), 12);
        s.setValue(QStringLiteral("tseg1"), 13);
        s.setValue(QStringLiteral("tseg2"), 2);
        s.setValue(QStringLiteral("sjw"), 2);
        s.setValue(QStringLiteral("smp"), 0);
        s.setValue(QStringLiteral("source"), QStringLiteral("内置"));
        s.endGroup();
        // 用户手工填的条目应被保留
        s.beginGroup(QStringLiteral("calibration/83333"));
        s.setValue(QStringLiteral("valid"), true);
        s.setValue(QStringLiteral("clock"), 80000000u);
        s.setValue(QStringLiteral("brp"), 59);
        s.setValue(QStringLiteral("tseg1"), 12);
        s.setValue(QStringLiteral("tseg2"), 1);
        s.setValue(QStringLiteral("sjw"), 1);
        s.setValue(QStringLiteral("smp"), 0);
        s.setValue(QStringLiteral("source"), QStringLiteral("手工"));
        s.endGroup();
        s.sync();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        pointSettingsAtTempDir();
        QVERIFY(QDir().mkpath(m_dir.path() + QStringLiteral("/CanDebuggerTest")));
        writeV1StaleFile();
        QVERIFY(QFile::exists(iniPath(m_dir.path())));
    }

    // 第一次取单例即触发 reload()：应完成 v1→v2 迁移并播种实测值
    void migratesStaleEntriesAndSeedsMeasuredValues()
    {
        BitCalibration &cal = BitCalibration::instance();

        // 旧模型条目被替换为实测精确值（80MHz / brp=19 / 12 / 1）
        const CalibrationEntry e250 = cal.lookup(250000);
        QVERIFY(e250.valid);
        QCOMPARE(e250.clock, quint32(80000000));
        QCOMPARE(e250.params.brp, quint16(19));
        QCOMPARE(e250.params.tseg1, quint8(12));
        QCOMPARE(e250.params.tseg2, quint8(1));
        QCOMPARE(e250.source, QStringLiteral("实测"));

        // 四个标准速率的实测值都在
        QCOMPARE(cal.lookup(125000).params.brp, quint16(39));
        QCOMPARE(cal.lookup(500000).params.brp, quint16(9));
        QCOMPARE(cal.lookup(1000000).params.brp, quint16(4));

        // 手工条目保留
        QVERIFY(cal.lookup(83333).valid);
        QCOMPARE(cal.lookup(83333).source, QStringLiteral("手工"));

        // 标定值必须与公式自洽
        for (int bps : {125000, 250000, 500000, 1000000}) {
            const CalibrationEntry e = cal.lookup(bps);
            QVERIFY2(e.valid, qPrintable(QStringLiteral("bps=%1 未标定").arg(bps)));
            QCOMPARE(BitTiming::actualBps(e.clock, e.params), double(bps));
        }
    }

    // 迁移标记已写入：再次 reload 不应重复播种/丢失手工条目
    void reloadKeepsState()
    {
        BitCalibration &cal = BitCalibration::instance();
        cal.reload();
        QVERIFY(cal.lookup(250000).valid);
        QCOMPARE(cal.lookup(250000).params.brp, quint16(19));
        QVERIFY(cal.lookup(83333).valid);

        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("CanDebuggerTest"), QStringLiteral("BitCalibrationTest"));
        QVERIFY(s.value(QStringLiteral("calibration/seeded_v2"), false).toBool());
    }
};

QTEST_APPLESS_MAIN(TestBitCalibration)

#include "test_bitcalibration.moc"
