#include <QtTest>

#include <QSettings>
#include <QTemporaryDir>

#include "core/ThemeController.h"

// 主题模式状态机 + 持久化。注入临时 ini 作为 QSettings，避免污染真实配置。
class TestThemeController : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void defaultIsDark();
    void invalidModeFallsBackToDark();
    void modeIsNormalized();
    void persistedModeRoundTrip();
    void toggleFlipsAndPersists();
    void autoFollowsSystemDark();
    void explicitModeIgnoresSystemDark();
    void darkChangedSignals();

private:
    QTemporaryDir m_dir;
    QSettings *m_settings = nullptr;
};

void TestThemeController::init()
{
    QVERIFY(m_dir.isValid());
    m_settings = new QSettings(m_dir.filePath(QStringLiteral("theme.ini")),
                               QSettings::IniFormat);
    m_settings->clear();
}

void TestThemeController::cleanup()
{
    delete m_settings;
    m_settings = nullptr;
}

// 默认观感是深色（老用户基线），配置为空时不得变成浅色
void TestThemeController::defaultIsDark()
{
    ThemeController c(m_settings);
    QCOMPARE(c.mode(), QStringLiteral("dark"));
    QCOMPARE(c.dark(), true);
    QCOMPARE(c.systemDark(), false);
}

void TestThemeController::invalidModeFallsBackToDark()
{
    m_settings->setValue(QStringLiteral("ui/themeMode"), QStringLiteral("banana"));
    ThemeController c(m_settings);
    QCOMPARE(c.mode(), QStringLiteral("dark"));
    QCOMPARE(c.dark(), true);
}

void TestThemeController::modeIsNormalized()
{
    ThemeController c(m_settings);
    c.setMode(QStringLiteral("  LiGhT "));
    QCOMPARE(c.mode(), QStringLiteral("light"));
    QCOMPARE(c.dark(), false);

    c.setMode(QStringLiteral("AUTO"));
    QCOMPARE(c.mode(), QStringLiteral("auto"));

    // 非法值不改变语义，只回退深色
    c.setMode(QStringLiteral("rainbow"));
    QCOMPARE(c.mode(), QStringLiteral("dark"));
    QCOMPARE(c.dark(), true);
}

void TestThemeController::persistedModeRoundTrip()
{
    {
        ThemeController c(m_settings);
        c.setMode(QStringLiteral("light"));
    }
    // 重新构造 = 重启应用
    ThemeController revived(m_settings);
    QCOMPARE(revived.mode(), QStringLiteral("light"));
    QCOMPARE(revived.dark(), false);
}

void TestThemeController::toggleFlipsAndPersists()
{
    ThemeController c(m_settings);
    QCOMPARE(c.dark(), true);

    c.toggle();
    QVERIFY2(c.mode() == QStringLiteral("light"), "toggle 必须是显式选择，不能停在 auto");
    QCOMPARE(c.dark(), false);

    {
        ThemeController revived(m_settings);
        QVERIFY2(revived.dark() == false, "切换后必须落盘");
    }

    c.toggle();
    QCOMPARE(c.mode(), QStringLiteral("dark"));
    QCOMPARE(c.dark(), true);
}

void TestThemeController::autoFollowsSystemDark()
{
    ThemeController c(m_settings);
    c.setMode(QStringLiteral("auto"));

    c.setSystemDark(true);
    QVERIFY2(c.dark() == true, "auto 模式下系统深色 -> 深色");
    QVERIFY2(c.mode() == QStringLiteral("auto"), "跟随系统时 mode 不应被改写");

    c.setSystemDark(false);
    QVERIFY2(c.dark() == false, "auto 模式下系统浅色 -> 浅色");
    QCOMPARE(c.mode(), QStringLiteral("auto"));
}

void TestThemeController::explicitModeIgnoresSystemDark()
{
    ThemeController c(m_settings);
    c.setMode(QStringLiteral("light"));
    c.setSystemDark(true);
    QVERIFY2(c.dark() == false, "显式浅色不应被系统深色覆盖");

    c.setMode(QStringLiteral("dark"));
    c.setSystemDark(false);
    QVERIFY2(c.dark() == true, "显式深色不应被系统浅色覆盖");
}

// darkChanged 只在「最终观感」变化时发；modeChanged 只在模式变化时发
void TestThemeController::darkChangedSignals()
{
    ThemeController c(m_settings);
    QSignalSpy darkSpy(&c, &ThemeController::darkChanged);
    QSignalSpy modeSpy(&c, &ThemeController::modeChanged);
    QVERIFY(darkSpy.isValid() && modeSpy.isValid());

    // 显式模式下系统深浅变化：不应影响观感，也就不能发 darkChanged
    c.setMode(QStringLiteral("dark"));
    darkSpy.clear();
    modeSpy.clear();
    c.setSystemDark(true);
    QCOMPARE(darkSpy.count(), 0);
    QCOMPARE(modeSpy.count(), 0);

    // auto 模式下系统深浅变化：观感变了，发一次 darkChanged
    c.setMode(QStringLiteral("auto"));
    QVERIFY2(c.dark() == true, "auto + 系统深色应为深色");
    darkSpy.clear();
    modeSpy.clear();
    c.setSystemDark(false);
    QCOMPARE(darkSpy.count(), 1);
    QCOMPARE(modeSpy.count(), 0);

    // 重复设置同一值不应重复发信号
    darkSpy.clear();
    c.setSystemDark(false);
    QCOMPARE(darkSpy.count(), 0);

    // 切模式会发 modeChanged；观感同时变化时再发一次 darkChanged
    darkSpy.clear();
    modeSpy.clear();
    c.setMode(QStringLiteral("dark"));
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(darkSpy.count(), 1);
}

QTEST_APPLESS_MAIN(TestThemeController)

#include "test_themecontroller.moc"
