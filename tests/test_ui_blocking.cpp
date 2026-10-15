// UI 点击阻塞探针
//
// 目的：逐个“点击”界面上的每个按钮，测量 GUI 线程被阻塞的时长。
//
// 方法：
//   - 用 QQmlApplicationEngine 以 offscreen 平台加载 qrc:/qml/Main.qml（上下文属性与 main.cpp 一致）；
//   - 递归枚举所有带 `clicked()` 信号的对象（AbstractButton 及其派生）；
//   - GUI 线程上跑一个 1ms 精确定时器作为“事件循环延迟探针”，定时器回调的最大间隔即阻塞时长；
//   - 每个按钮在独立子进程中测试（默认 8s 超时），避免真实设备调用（VCI_Transmit 等）
//     挂死拖垮整轮测试；
//   - 支持向模型预填 N 条报文，以还原“10 万行满量”下的点击代价。
//
// 用法：
//   test_ui_blocking                         # 编排：不预填数据，逐按钮子进程测试
//   test_ui_blocking --rows 100000           # 编排：先预填 10 万行再点击
//   test_ui_blocking --open-device           # 点击前先打开设备（设备相关按钮变为可用）
//   test_ui_blocking --list                  # 只列出按钮清单
//   test_ui_blocking --child --index 3 --rows 100000   # 内部：单按钮测试

#include <QAbstractItemModel>
#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include <cstdio>
#include <cstring>

#include <algorithm>
#include <functional>

#include "core/CanDevice.h"
#include "core/CanFilter.h"
#include "core/DeviceController.h"
#include "core/FrameBuilder.h"
#include "core/PeriodicSender.h"
#include "core/Recorder.h"
#include "core/Replayer.h"
#include "core/RxManager.h"
#include "core/Statistics.h"
#include "model/FrameListModel.h"
#include "view/RealtimePlot.h"

namespace {

// ---------------------------------------------------------------- 事件循环工具

void spin(int ms)
{
    QEventLoop loop;
    QTimer t;
    t.setSingleShot(true);
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    t.start(ms);
    loop.exec();
}

// ---------------------------------------------------------------- QML 装载

struct Harness
{
    QQmlApplicationEngine engine;
    CanDevice *device = nullptr;
    Statistics *stats = nullptr;
    RxManager *rx = nullptr;
    FrameBuilder *fb = nullptr;
    Recorder *recorder = nullptr;
    PeriodicSender *periodic = nullptr;
    Replayer *replayer = nullptr;
    DeviceController *controller = nullptr;

    bool load()
    {
        qmlRegisterUncreatableType<CanFilter>("CanDebugger", 1, 0, "CanFilter",
                                              QStringLiteral("内部类型，由 rx.filter 提供"));
        qmlRegisterType<RealtimePlot>("CanDebugger", 1, 0, "RealtimePlot");

        device = new CanDevice(&engine);
        stats = new Statistics(device, &engine);
        rx = new RxManager(device, &engine);
        rx->setStatistics(stats);
        fb = new FrameBuilder(device, &engine);
        fb->setStatistics(stats);
        recorder = new Recorder(&engine);
        periodic = new PeriodicSender(device, &engine);
        replayer = new Replayer(device, &engine);
        controller = new DeviceController(device, rx, &engine);
        rx->setRecorder(recorder);
        fb->setRecorder(recorder);

        QQmlContext *ctx = engine.rootContext();
        ctx->setContextProperty(QStringLiteral("canDevice"), device);
        ctx->setContextProperty(QStringLiteral("rx"), rx);
        ctx->setContextProperty(QStringLiteral("statistics"), stats);
        ctx->setContextProperty(QStringLiteral("frameBuilder"), fb);
        ctx->setContextProperty(QStringLiteral("periodicSender"), periodic);
        ctx->setContextProperty(QStringLiteral("recorder"), recorder);
        ctx->setContextProperty(QStringLiteral("replayer"), replayer);
        ctx->setContextProperty(QStringLiteral("deviceController"), controller);

        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        return !engine.rootObjects().isEmpty();
    }
};

bool isClickable(QObject *o)
{
    return o && o->metaObject()->indexOfSignal("clicked()") >= 0;
}

QList<QObject *> collectButtons(QObject *root)
{
    QList<QObject *> out;
    if (!root)
        return out;
    if (isClickable(root))
        out << root;
    const QList<QObject *> all = root->findChildren<QObject *>();
    for (QObject *o : all) {
        if (isClickable(o))
            out << o;
    }
    return out;
}

QString labelOf(QObject *b)
{
    const QString t = b->property("text").toString();
    if (!t.isEmpty())
        return t;
    return QString::fromLatin1(b->metaObject()->className());
}

QString pathOf(QObject *b)
{
    QStringList parts;
    QObject *o = b;
    while (o) {
        const QString n = o->objectName();
        if (!n.isEmpty())
            parts.prepend(n);
        o = o->parent();
    }
    return parts.join(QLatin1Char('/'));
}

// ---------------------------------------------------------------- 模型预填

void populate(FrameListModel *model, int rows)
{
    if (!model || rows <= 0)
        return;
    QVector<FrameData> batch;
    batch.reserve(rows);
    const qint64 wall0 = QDateTime::currentMSecsSinceEpoch() * 1000;
    for (int i = 0; i < rows; ++i) {
        FrameData f;
        f.hostUs = 1000000 + static_cast<qint64>(i) * 50;
        f.wallUs = wall0 + static_cast<qint64>(i) * 50;
        f.channel = static_cast<quint8>(i % 2);
        f.len = 8;
        f.data = QByteArray(8, static_cast<char>(i & 0xFF));
        if (i % 97 == 0) {
            f.extended = true;
            f.id = 0x18000000u | static_cast<quint32>(i & 0x0FFF);
        } else {
            f.id = 0x100u + static_cast<quint32>(i % 64);
        }
        if (i % 13 == 0)
            f.fd = true;
        batch.append(f);
    }
    model->append(batch);
}

// ---------------------------------------------------------------- 测量核心

struct Measurement
{
    double clickMs = 0.0;   // onClicked 同步处理耗时
    double stallMs = 0.0;   // 点击后事件循环最大停顿
};

Measurement measureClick(QObject *button, int settleMs, int observeMs)
{
    // 探测定时器：1ms 精确计时，回调最大间隔即 GUI 线程阻塞
    QTimer probe;
    probe.setTimerType(Qt::PreciseTimer);
    probe.setInterval(1);
    QElapsedTimer clock;
    qint64 lastUs = 0;
    qint64 maxUs = 0;
    QObject::connect(&probe, &QTimer::timeout, [&]() {
        const qint64 now = clock.nsecsElapsed() / 1000;
        if (lastUs > 0) {
            const qint64 gap = now - lastUs;
            if (gap > maxUs)
                maxUs = gap;
        }
        lastUs = now;
    });

    spin(settleMs);

    clock.start();
    lastUs = clock.nsecsElapsed() / 1000;
    maxUs = 0;
    probe.start();

    QElapsedTimer clickClock;
    clickClock.start();
    QMetaObject::invokeMethod(button, "clicked", Qt::DirectConnection);
    const qint64 clickUs = clickClock.nsecsElapsed() / 1000;

    spin(observeMs);
    probe.stop();

    Measurement m;
    m.clickMs = clickUs / 1000.0;
    m.stallMs = maxUs / 1000.0;
    return m;
}

// ---------------------------------------------------------------- 子进程：单按钮

int runChild(int index, int rows, bool openDevice, int settleMs, int observeMs)
{
    Harness h;
    if (!h.load()) {
        std::fprintf(stderr, "ERROR: 无法加载 Main.qml\n");
        return 2;
    }

    const QList<QObject *> buttons = collectButtons(h.engine.rootObjects().value(0));
    if (index < 0 || index >= buttons.size()) {
        std::fprintf(stderr, "ERROR: index %d 越界（共 %d 个按钮）\n", index, buttons.size());
        return 2;
    }
    QObject *button = buttons.at(index);

    if (openDevice)
        h.device->openDevice(0);

    if (rows > 0) {
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(h.rx->frameModel());
        auto *src = proxy ? qobject_cast<FrameListModel *>(proxy->sourceModel()) : nullptr;
        populate(src, rows);
    }

    spin(100);
    const bool enabled = button->property("enabled").toBool();
    QString label = labelOf(button);
    QString path = pathOf(button);

    const Measurement m = measureClick(button, settleMs, observeMs);

    // 若点击触发了异步会话（连接/应用/断开），等待其收尾，避免子进程在
    // 工作线程运行中退出
    if (h.controller) {
        for (int i = 0; i < 200 && h.controller->property("busy").toBool(); ++i)
            spin(100);
    }

    label.replace(QLatin1Char('|'), QLatin1Char('/'));
    label.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (path.isEmpty())
        path = QStringLiteral("-");
    path.replace(QLatin1Char('|'), QLatin1Char('/'));

    std::printf("RESULT|index=%d|enabled=%d|click_ms=%.2f|stall_ms=%.2f|rows=%d|label=%s|path=%s\n",
                index, enabled ? 1 : 0, m.clickMs, m.stallMs, rows,
                label.toUtf8().constData(), path.toUtf8().constData());
    std::fflush(stdout);
    return 0;
}

// ---------------------------------------------------------------- 编排

QString grade(double stallMs)
{
    if (stallMs >= 100.0)
        return QStringLiteral("BLOCK");
    if (stallMs >= 30.0)
        return QStringLiteral("JANK");
    if (stallMs >= 10.0)
        return QStringLiteral("WARN");
    return QStringLiteral("ok");
}

int runOrchestrator(int rows, bool openDevice, int timeoutMs, bool listOnly)
{
    Harness h;
    if (!h.load()) {
        std::fprintf(stderr, "ERROR: 无法加载 Main.qml\n");
        return 2;
    }
    const QList<QObject *> buttons = collectButtons(h.engine.rootObjects().value(0));

    if (listOnly) {
        std::printf("共 %d 个按钮：\n", buttons.size());
        for (int i = 0; i < buttons.size(); ++i) {
            std::printf("  [%2d] enabled=%d  %-16s  %s\n", i,
                        buttons.at(i)->property("enabled").toBool() ? 1 : 0,
                        labelOf(buttons.at(i)).toUtf8().constData(),
                        pathOf(buttons.at(i)).toUtf8().constData());
        }
        return 0;
    }

    QTemporaryDir tmp;
    const QString exe = QCoreApplication::applicationFilePath();

    std::printf("# UI 点击阻塞报告\n\n");
    std::printf("- 预填报文：%d 行\n", rows);
    std::printf("- 点击前打开设备：%s\n", openDevice ? "是" : "否");
    std::printf("- 子进程超时：%d ms\n", timeoutMs);
    std::printf("- 按钮总数：%d\n\n", buttons.size());
    std::printf("| # | 按钮 | 可用 | 同步处理 ms | 阻塞 ms | 评级 |\n");
    std::printf("|---|------|------|-------------|---------|------|\n");

    struct Row { int index; QString label; QString path; bool enabled; double clickMs; double stallMs; QString status; };
    QVector<Row> results;

    for (int i = 0; i < buttons.size(); ++i) {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        p.setProcessEnvironment(env);
        p.setWorkingDirectory(tmp.path());
        p.setProcessChannelMode(QProcess::SeparateChannels);

        QStringList args{ QStringLiteral("--child"), QStringLiteral("--index"), QString::number(i),
                          QStringLiteral("--rows"), QString::number(rows) };
        if (openDevice)
            args << QStringLiteral("--open-device");
        p.start(exe, args);
        if (!p.waitForStarted(3000)) {
            results.append({ i, labelOf(buttons.at(i)), pathOf(buttons.at(i)), false, 0, -1, QStringLiteral("START-FAIL") });
            continue;
        }

        QString status = QStringLiteral("ok");
        bool timedOut = !p.waitForFinished(timeoutMs);
        if (timedOut) {
            p.kill();
            p.waitForFinished(2000);
            status = QStringLiteral("TIMEOUT");
        }

        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        Row row;
        row.index = i;
        row.label = labelOf(buttons.at(i));
        row.path = pathOf(buttons.at(i));
        row.enabled = buttons.at(i)->property("enabled").toBool();
        row.clickMs = -1;
        row.stallMs = -1;
        row.status = status;

        const QString prefix = QStringLiteral("RESULT|index=");
        const int at = out.indexOf(prefix);
        if (at >= 0) {
            const QString line = out.mid(at).section(QLatin1Char('\n'), 0, 0);
            const QStringList kv = line.split(QLatin1Char('|'));
            for (const QString &tok : kv) {
                const int eq = tok.indexOf(QLatin1Char('='));
                if (eq < 0)
                    continue;
                const QString k = tok.left(eq);
                const QString v = tok.mid(eq + 1);
                if (k == QLatin1String("enabled"))
                    row.enabled = v.toInt() != 0;
                else if (k == QLatin1String("click_ms"))
                    row.clickMs = v.toDouble();
                else if (k == QLatin1String("stall_ms"))
                    row.stallMs = v.toDouble();
            }
        } else if (status != QStringLiteral("TIMEOUT")) {
            row.status = QStringLiteral("NO-RESULT");
        }
        results.append(row);
    }

    for (const Row &r : results) {
        QString label = r.label;
        label.replace(QLatin1Char('|'), QLatin1Char('/'));
        const QString gradeText = (r.stallMs < 0) ? r.status : grade(r.stallMs);
        std::printf("| %d | %s | %s | %s | %s | %s |\n",
                    r.index,
                    label.toUtf8().constData(),
                    r.enabled ? "是" : "否",
                    r.clickMs < 0 ? "-" : QString::number(r.clickMs, 'f', 2).toUtf8().constData(),
                    r.stallMs < 0 ? "-" : QString::number(r.stallMs, 'f', 2).toUtf8().constData(),
                    gradeText.toUtf8().constData());
    }

    std::printf("\n评级：ok < 10ms，WARN 10-30ms，JANK 30-100ms，BLOCK >= 100ms，TIMEOUT 子进程被强杀\n");
    std::fflush(stdout);
    return 0;
}

// ---------------------------------------------------------------- 子进程：控制调用 vs 锁饥饿

namespace {
struct Timing
{
    QVector<qint64> v;
    void add(qint64 us) { v.append(us); }
    qint64 minUs() const { qint64 m = v.isEmpty() ? 0 : v.first(); for (qint64 x : v) m = qMin(m, x); return m; }
    qint64 maxUs() const { qint64 m = 0; for (qint64 x : v) m = qMax(m, x); return m; }
    qint64 medUs() const { QVector<qint64> s = v; std::sort(s.begin(), s.end()); return s.isEmpty() ? 0 : s.at(s.size() / 2); }
};

Timing measureN(int n, const std::function<qint64()> &fn)
{
    Timing t;
    for (int i = 0; i < n; ++i)
        t.add(fn());
    return t;
}

void printTiming(const char *scenario, const char *what, const Timing &t)
{
    std::printf("DIAG|%-8s|%-18s| min=%9.2f  med=%9.2f  max=%9.2f ms\n",
                scenario, what, t.minUs() / 1000.0, t.medUs() / 1000.0, t.maxUs() / 1000.0);
    std::fflush(stdout);
}
} // namespace

int runDiagControl(int iters)
{
    Harness h;
    if (!h.load()) {
        std::fprintf(stderr, "ERROR: 无法加载 Main.qml\n");
        return 2;
    }

    CanDevice *dev = h.device;
    if (!dev->openDevice(0)) {
        std::fprintf(stderr, "ERROR: 打开设备失败\n");
        return 3;
    }
    for (int ch = 0; ch < 2; ++ch) {
        dev->configureChannel(ch, 500000, 2000000, 87.5, false);
        dev->setTerminalResistor(ch, ch == 0);
        dev->startChannel(ch);
    }

    QVariantMap e;
    e.insert(QStringLiteral("id"), 0x300u);
    e.insert(QStringLiteral("dlc"), 8);
    e.insert(QStringLiteral("data"), QStringLiteral("01 02 03 04 05 06 07 08"));
    e.insert(QStringLiteral("dataFormat"), 0);
    e.insert(QStringLiteral("intervalMs"), 10.0);
    e.insert(QStringLiteral("repeat"), 0);
    e.insert(QStringLiteral("enabled"), true);
    e.insert(QStringLiteral("txm"), 2);
    e.insert(QStringLiteral("echo"), true);
    const QVariantList entries{ e };

    ZCAN_20_MSG msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.hdr.id = 0x123;
    msg.hdr.chn = 0;
    msg.hdr.len = 8;
    msg.hdr.inf.txm = 0;   // 正常模式（等 ACK）
    msg.hdr.inf.echo = 1;
    for (int b = 0; b < 8; ++b)
        msg.dat[b] = static_cast<quint8>(b);

    const int N = iters;
    auto timed = [](const std::function<void()> &fn) -> qint64 {
        QElapsedTimer t;
        t.start();
        fn();
        return t.nsecsElapsed() / 1000;
    };

    auto scenario = [&](const char *name, bool startRx, bool traffic, bool ctlEnable) {
        if (startRx) {
            h.rx->start(QVariantList{ 0, 1 }, false);
            spin(300);
        }
        if (traffic) {
            h.periodic->apply(0, entries);
            h.periodic->start(0);
            spin(600);
        }
        printTiming(name, "ioLockWait", measureN(N, [&] { return dev->ioLockWaitUs(); }));
        printTiming(name, "setPeriodicConfig", measureN(N, [&] {
            return timed([&] { h.periodic->apply(0, entries); });
        }));
        printTiming(name, "setPeriodicEnabled", measureN(N, [&] {
            return timed([&] { dev->setPeriodicEnabled(0, ctlEnable); });
        }));
        printTiming(name, "transmitCan", measureN(N, [&] {
            return timed([&] { dev->transmitCan(0, msg); });
        }));
        if (traffic)
            h.periodic->stop(0);
        if (startRx)
            h.rx->stop();
    };

    scenario("RX-off", false, false, false);
    scenario("RX-idle", true, false, false);
    scenario("RX-load", true, true, true);

    dev->close();
    return 0;
}

// 直接点击真实的「添加」+「启动」周期发送按钮，分别测量 RX 关闭 / RX 运行时的阻塞
int runDiagPeriodicStart()
{
    Harness h;
    if (!h.load()) {
        std::fprintf(stderr, "ERROR: 无法加载 Main.qml\n");
        return 2;
    }

    const QList<QObject *> buttons = collectButtons(h.engine.rootObjects().value(0));
    QObject *addBtn = nullptr;
    QObject *startBtn = nullptr;
    QObject *stopBtn = nullptr;
    for (QObject *b : buttons) {
        const QString l = labelOf(b);
        if (l == QStringLiteral("添加") && !addBtn)
            addBtn = b;
        else if (l == QStringLiteral("启动") && !startBtn)
            startBtn = b;
        else if (l == QStringLiteral("停止") && !stopBtn)
            stopBtn = b;
    }
    if (!addBtn || !startBtn) {
        std::fprintf(stderr, "ERROR: 未找到周期发送按钮（添加/启动）\n");
        return 2;
    }

    auto addEntry = [&] {
        QMetaObject::invokeMethod(addBtn, "clicked", Qt::DirectConnection);
        spin(50);
    };

    // 场景一：设备打开、通道启动，但 RX 未运行
    if (!h.device->openDevice(0)) {
        std::fprintf(stderr, "ERROR: 打开设备失败\n");
        return 3;
    }
    h.device->configureChannel(0, 500000, 2000000, 87.5, false);
    h.device->setTerminalResistor(0, true);
    h.device->startChannel(0);
    addEntry();

    Measurement off = measureClick(startBtn, 150, 400);
    if (stopBtn)
        QMetaObject::invokeMethod(stopBtn, "clicked", Qt::DirectConnection);
    std::printf("DIAG|periodic-start RX-off | click=%8.2f ms  stall=%8.2f ms\n", off.clickMs, off.stallMs);
    std::fflush(stdout);

    // 场景二：RX 运行（监听中）——复现用户场景
    h.rx->start(QVariantList{ 0 }, false);
    spin(300);
    addEntry();
    const Measurement on = measureClick(startBtn, 150, 400);
    if (stopBtn)
        QMetaObject::invokeMethod(stopBtn, "clicked", Qt::DirectConnection);
    std::printf("DIAG|periodic-start RX-on  | click=%8.2f ms  stall=%8.2f ms\n", on.clickMs, on.stallMs);
    std::fflush(stdout);

    h.rx->stop();
    h.device->resetChannel(0);
    h.device->close();
    return 0;
}

// 连接后点击真实「发送」按钮，验证异步非阻塞且确实发出
int runDiagSend()
{
    Harness h;
    if (!h.load()) {
        std::fprintf(stderr, "ERROR: 无法加载 Main.qml\n");
        return 2;
    }

    QVariantList specs;
    for (int ch = 0; ch < 2; ++ch) {
        QVariantMap m;
        m.insert(QStringLiteral("port"), ch);
        m.insert(QStringLiteral("arb"), 500000);
        m.insert(QStringLiteral("dat"), 2000000);
        m.insert(QStringLiteral("sample"), 87.5);
        m.insert(QStringLiteral("listenOnly"), false);
        m.insert(QStringLiteral("terminator"), ch == 0);
        specs << m;
    }
    h.controller->connectDevice(specs, false);
    for (int i = 0; i < 200 && h.controller->property("busy").toBool(); ++i)
        spin(100);
    if (!h.controller->connected()) {
        std::fprintf(stderr, "ERROR: 连接失败\n");
        return 3;
    }

    QObject *sendBtn = nullptr;
    const QList<QObject *> buttons = collectButtons(h.engine.rootObjects().value(0));
    for (QObject *b : buttons) {
        if (labelOf(b) == QStringLiteral("发送")) {
            sendBtn = b;
            break;
        }
    }
    if (!sendBtn) {
        std::fprintf(stderr, "ERROR: 未找到「发送」按钮\n");
        return 2;
    }

    bool completed = false;
    bool ok = false;
    QObject::connect(h.fb, &FrameBuilder::sendCompleted,
                     [&](bool o, const QString &, const QString &) { completed = true; ok = o; });

    const int before = h.fb->sentCount();
    const Measurement m = measureClick(sendBtn, 150, 500);
    for (int i = 0; i < 100 && !completed; ++i)
        spin(20);

    std::printf("DIAG|send-click | click=%7.2f ms  stall=%7.2f ms  completed=%d ok=%d sentDelta=%d\n",
                m.clickMs, m.stallMs, completed ? 1 : 0, ok ? 1 : 0, h.fb->sentCount() - before);
    std::fflush(stdout);

    h.controller->disconnectDevice();
    for (int i = 0; i < 100 && h.controller->property("busy").toBool(); ++i)
        spin(50);
    return 0;
}

// ---------------------------------------------------------------- 子进程：连接/配置会话

int runDiagConnect()
{
    Harness h;
    if (!h.load()) {
        std::fprintf(stderr, "ERROR: 无法加载 Main.qml\n");
        return 2;
    }

    QString lastError;
    QString lastOk;
    QObject::connect(h.controller, &DeviceController::failed,
                     [&](const QString &m) { lastError = m; });
    QObject::connect(h.controller, &DeviceController::succeeded,
                     [&](const QString &m) { lastOk = m; });

    QVariantList specs;
    for (int ch = 0; ch < 2; ++ch) {
        QVariantMap m;
        m.insert(QStringLiteral("port"), ch);
        m.insert(QStringLiteral("arb"), 500000);
        m.insert(QStringLiteral("dat"), 2000000);
        m.insert(QStringLiteral("sample"), 87.5);
        m.insert(QStringLiteral("listenOnly"), false);
        m.insert(QStringLiteral("terminator"), ch == 0);
        specs << m;
    }

    QElapsedTimer t;
    t.start();
    h.controller->connectDevice(specs, false);
    while (h.controller->property("busy").toBool() && t.elapsed() < 20000)
        spin(50);
    const double connectMs = t.elapsed();

    std::printf("DIAG|connect async wall   | %9.1f ms\n", connectMs);
    std::printf("DIAG|deviceOpen           | %d\n", h.device->isOpen() ? 1 : 0);
    std::printf("DIAG|rxRunning            | %d\n", h.rx->running() ? 1 : 0);
    std::printf("DIAG|controller.connected  | %d\n", h.controller->connected() ? 1 : 0);
    std::printf("DIAG|message              | ok='%s' err='%s'\n",
                lastOk.toUtf8().constData(), lastError.toUtf8().constData());

    spin(500);
    std::printf("DIAG|framesAfterConnect   | %d\n", h.rx->displayedCount());

    // 断开
    t.restart();
    h.controller->disconnectDevice();
    while (h.controller->property("busy").toBool() && t.elapsed() < 10000)
        spin(50);
    std::printf("DIAG|disconnect wall      | %9.1f ms\n", static_cast<double>(t.elapsed()));
    std::printf("DIAG|afterClose deviceOpen| %d\n", h.device->isOpen() ? 1 : 0);
    return 0;
}

// ---------------------------------------------------------------- 子进程：启动序列分解
int runDiagStart()
{
    Harness h;
    if (!h.load()) {
        std::fprintf(stderr, "ERROR: 无法加载 Main.qml\n");
        return 2;
    }

    auto timed = [](const char *name, auto &&fn) {
        QElapsedTimer t;
        t.start();
        fn();
        const double ms = t.nsecsElapsed() / 1000000.0;
        std::printf("DIAG|%-26s|%9.2f ms\n", name, ms);
        std::fflush(stdout);
        return ms;
    };

    timed("openDevice", [&]() { h.device->openDevice(0); });
    std::printf("DIAG|deviceOpen=%d\n", h.device->isOpen() ? 1 : 0);

    const int nch = h.device->channels() > 0 ? h.device->channels() : 2;
    for (int ch = 0; ch < nch; ++ch) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "configureChannel ch%d", ch);
        timed(buf, [&]() { h.device->configureChannel(ch, 500000, 2000000, 87.5, false); });
        std::snprintf(buf, sizeof(buf), "setTerminalResistor ch%d", ch);
        timed(buf, [&]() { h.device->setTerminalResistor(ch, ch == 0); });
        std::snprintf(buf, sizeof(buf), "startChannel ch%d", ch);
        timed(buf, [&]() { h.device->startChannel(ch); });
    }
    timed("setRecvMerge ch0", [&]() { h.device->setRecvMerge(0, true); });
    timed("rx.start", [&]() {
        QVariantList chans;
        chans << 0 << 1;
        h.rx->start(chans, false);
    });
    spin(500);
    timed("rx.stop", [&]() { h.rx->stop(); });
    timed("resetChannel ch1", [&]() { h.device->resetChannel(1); });
    timed("resetChannel ch0", [&]() { h.device->resetChannel(0); });
    timed("closeDevice", [&]() { h.device->close(); });
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();

    int index = -1;
    int rows = 0;
    int timeoutMs = 8000;
    int settleMs = 150;
    int observeMs = 250;
    int iters = 20;
    bool child = false;
    bool openDevice = false;
    bool listOnly = false;
    bool diagStart = false;
    bool diagConnect = false;
    bool diagControl = false;
    bool diagPeriodic = false;
    bool diagSend = false;

    for (int i = 0; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QLatin1String("--child"))
            child = true;
        else if (a == QLatin1String("--list"))
            listOnly = true;
        else if (a == QLatin1String("--diag-start"))
            diagStart = true;
        else if (a == QLatin1String("--diag-connect"))
            diagConnect = true;
        else if (a == QLatin1String("--diag-control"))
            diagControl = true;
        else if (a == QLatin1String("--diag-periodic-start"))
            diagPeriodic = true;
        else if (a == QLatin1String("--diag-send"))
            diagSend = true;
        else if (a == QLatin1String("--open-device"))
            openDevice = true;
        else if (a == QLatin1String("--index"))
            index = args.value(i + 1).toInt();
        else if (a == QLatin1String("--rows"))
            rows = args.value(i + 1).toInt();
        else if (a == QLatin1String("--timeout"))
            timeoutMs = args.value(i + 1).toInt();
        else if (a == QLatin1String("--settle"))
            settleMs = args.value(i + 1).toInt();
        else if (a == QLatin1String("--observe"))
            observeMs = args.value(i + 1).toInt();
        else if (a == QLatin1String("--iters"))
            iters = qMax(1, args.value(i + 1).toInt());
    }

    if (diagControl)
        return runDiagControl(iters);
    if (diagPeriodic)
        return runDiagPeriodicStart();
    if (diagSend)
        return runDiagSend();
    if (diagConnect)
        return runDiagConnect();
    if (diagStart)
        return runDiagStart();
    if (child)
        return runChild(index, rows, openDevice, settleMs, observeMs);
    return runOrchestrator(rows, openDevice, timeoutMs, listOnly);
}
