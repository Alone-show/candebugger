#include <QGuiApplication>
#include <QPalette>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "core/CanDevice.h"
#include "core/CanFilter.h"
#include "core/DeviceController.h"
#include "core/FrameBuilder.h"
#include "core/PeriodicSender.h"
#include "core/Recorder.h"
#include "core/Replayer.h"
#include "core/RxManager.h"
#include "core/Statistics.h"
#include "core/ThemeController.h"
#include "view/RealtimePlot.h"

namespace {

// 探测桌面深浅色（「跟随系统」用）。
// Qt 5.15 没有 Qt.styleHints.colorScheme（6.5+ 才有），只能自己判断：
//   1) QGuiApplication::palette()：装了桌面平台主题插件时（KDE 的
//      KDEPlasmaPlatformTheme5 / GTK 的 libqgtk3）能直接拿到真实调色板；
//   2) 拿到的仍是 Qt 默认浅色调色板时，用 GTK_THEME / gsettings 兜底
//      （GNOME 未装 qgnomeplatform 的常见情形）。
bool detectSystemDark()
{
    if (QGuiApplication::palette().color(QPalette::Window).lightness() < 128)
        return true;

    if (qgetenv("GTK_THEME").toLower().contains("dark"))
        return true;

    QProcess gsettings;
    gsettings.start(QStringLiteral("gsettings"),
                    {QStringLiteral("get"),
                     QStringLiteral("org.gnome.desktop.interface"),
                     QStringLiteral("color-scheme")});
    if (gsettings.waitForFinished(500)) {
        const QString scheme = QString::fromUtf8(gsettings.readAllStandardOutput()).trimmed();
        if (scheme.contains(QLatin1String("prefer-dark")))
            return true;
        if (scheme.contains(QLatin1String("prefer-light")))
            return false;
    }
    return false;
}

// 命令行一次性覆盖：--theme=auto|light|dark（写入配置后长期生效）
QString themeArgFromCommandLine(const QStringList &args)
{
    static const QString prefix = QStringLiteral("--theme=");
    for (const QString &arg : args) {
        if (arg.startsWith(prefix))
            return arg.mid(prefix.size());
    }
    return QString();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("USBCANFD 调试器"));
    app.setOrganizationName(QStringLiteral("CanDebugger"));
#ifdef CANDBG_VERSION
    // 版本号来自 CMake project(VERSION)，QML 侧用 Qt.application.version 读取
    app.setApplicationVersion(QStringLiteral(CANDBG_VERSION));
#endif

    // CanFilter 作为 rx.filter 暴露给 QML，需注册为不可创建类型
    qmlRegisterUncreatableType<CanFilter>("CanDebugger", 1, 0, "CanFilter",
                                          QStringLiteral("内部类型，由 rx.filter 提供"));

    // 零依赖实时绘图：QML 中直接实例化
    qmlRegisterType<RealtimePlot>("CanDebugger", 1, 0, "RealtimePlot");

    // 主题：默认深色；--theme=auto|light|dark 覆盖并记住。
    // Theme.qml 只读 themeController.dark，故必须在 engine.load() 之前定好。
    ThemeController themeController;
    themeController.setSystemDark(detectSystemDark());
    const QString themeArg = themeArgFromCommandLine(app.arguments());
    if (!themeArg.isEmpty())
        themeController.setMode(themeArg);
    // 桌面主题变化（如 KDE 切换配色）时重新探测；仅 auto 模式会影响观感
    QObject::connect(&app, &QGuiApplication::paletteChanged, &themeController,
                     [&themeController]() { themeController.setSystemDark(detectSystemDark()); });

    CanDevice canDevice;
    Statistics statistics(&canDevice);
    Recorder recorder;
    RxManager rxManager(&canDevice);
    rxManager.setStatistics(&statistics);
    rxManager.setRecorder(&recorder);
    FrameBuilder frameBuilder(&canDevice);
    frameBuilder.setStatistics(&statistics);
    frameBuilder.setRecorder(&recorder);
    PeriodicSender periodicSender(&canDevice);
    periodicSender.setStatistics(&statistics);
    periodicSender.setRecorder(&recorder);
    Replayer replayer(&canDevice);
    DeviceController deviceController(&canDevice, &rxManager);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("canDevice"), &canDevice);
    engine.rootContext()->setContextProperty(QStringLiteral("rx"), &rxManager);
    engine.rootContext()->setContextProperty(QStringLiteral("statistics"), &statistics);
    engine.rootContext()->setContextProperty(QStringLiteral("frameBuilder"), &frameBuilder);
    engine.rootContext()->setContextProperty(QStringLiteral("periodicSender"), &periodicSender);
    engine.rootContext()->setContextProperty(QStringLiteral("recorder"), &recorder);
    engine.rootContext()->setContextProperty(QStringLiteral("replayer"), &replayer);
    engine.rootContext()->setContextProperty(QStringLiteral("deviceController"), &deviceController);
    // 深浅色主题状态（Theme.qml 读取其 dark 属性）
    engine.rootContext()->setContextProperty(QStringLiteral("themeController"), &themeController);
    // 无边框标题栏的拖动/缩放策略按后端分流（Wayland 交给合成器，X11 自算）
    engine.rootContext()->setContextProperty(QStringLiteral("platformName"),
                                             QGuiApplication::platformName());

    statistics.start(1000);

    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
