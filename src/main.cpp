#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>
#include <QString>

#include "core/CanDevice.h"
#include "core/CanFilter.h"
#include "core/DeviceController.h"
#include "core/FrameBuilder.h"
#include "core/PeriodicSender.h"
#include "core/Recorder.h"
#include "core/Replayer.h"
#include "core/RxManager.h"
#include "core/Statistics.h"
#include "view/RealtimePlot.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("USBCANFD 调试器"));
    app.setOrganizationName(QStringLiteral("CanDebugger"));

    // CanFilter 作为 rx.filter 暴露给 QML，需注册为不可创建类型
    qmlRegisterUncreatableType<CanFilter>("CanDebugger", 1, 0, "CanFilter",
                                          QStringLiteral("内部类型，由 rx.filter 提供"));

    // 零依赖实时绘图：QML 中直接实例化
    qmlRegisterType<RealtimePlot>("CanDebugger", 1, 0, "RealtimePlot");

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
