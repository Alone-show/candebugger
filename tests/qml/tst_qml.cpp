// L2 QML 测试入口：注册自定义类型并注入上下文属性（与 main.cpp 一致）
#include <QtQuickTest>
#include <QQmlContext>
#include <QQmlEngine>

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

class QmlTestSetup : public QObject
{
    Q_OBJECT
public:
    QmlTestSetup() = default;

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        qmlRegisterUncreatableType<CanFilter>("CanDebugger", 1, 0, "CanFilter",
                                              QStringLiteral("内部类型，由 rx.filter 提供"));
        qmlRegisterType<RealtimePlot>("CanDebugger", 1, 0, "RealtimePlot");

        auto *device = new CanDevice(engine);
        auto *stats = new Statistics(device, engine);
        auto *rx = new RxManager(device, engine);
        rx->setStatistics(stats);
        auto *fb = new FrameBuilder(device, engine);
        fb->setStatistics(stats);
        auto *recorder = new Recorder(engine);
        auto *periodic = new PeriodicSender(device, engine);
        auto *replayer = new Replayer(device, engine);
        auto *controller = new DeviceController(device, rx, engine);

        QQmlContext *ctx = engine->rootContext();
        ctx->setContextProperty(QStringLiteral("canDevice"), device);
        ctx->setContextProperty(QStringLiteral("rx"), rx);
        ctx->setContextProperty(QStringLiteral("statistics"), stats);
        ctx->setContextProperty(QStringLiteral("frameBuilder"), fb);
        ctx->setContextProperty(QStringLiteral("periodicSender"), periodic);
        ctx->setContextProperty(QStringLiteral("recorder"), recorder);
        ctx->setContextProperty(QStringLiteral("replayer"), replayer);
        ctx->setContextProperty(QStringLiteral("deviceController"), controller);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(candbg_qml, QmlTestSetup)

#include "tst_qml.moc"
