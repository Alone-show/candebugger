#include <QtTest>

#include <QElapsedTimer>

#include "core/FrameBuilder.h"
#include "core/PeriodicSender.h"

namespace {

// 无硬件替身：捕获共享状态，确保发送线程收尾期间状态仍然有效。
// 声明顺序须先于 PeriodicSender，保证其生命周期更长。
struct FakeBus
{
    std::atomic<bool> ready{true};
    std::atomic<bool> ok{true};
    std::atomic<int> total{0};
    std::atomic<int> count0{0};
    std::atomic<int> count1{0};
    std::atomic<quint32> lastId{0};

    void install(PeriodicSender *s)
    {
        auto *self = this;
        s->setTransmitBackend(
            [self] { return self->ready.load(); },
            [self](int port, const FrameSpec &spec, QString *) {
                self->total.fetch_add(1);
                (port == 0 ? self->count0 : self->count1).fetch_add(1);
                self->lastId.store(spec.id);
                return self->ok.load();
            });
    }
};

} // namespace

// 软件定时发送：纯逻辑 + 调度/发送/线程路径（无硬件）单测。
class TestPeriodicSender : public QObject
{
    Q_OBJECT

private:
    static QVariantMap entry(quint32 id, double intervalMs, int repeat, bool enabled)
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

    static PeriodicSender::PeriodicRow row(qint64 intervalUs, quint32 repeat, bool enabled = true,
                                           qint64 nextDueUs = 0)
    {
        PeriodicSender::PeriodicRow r;
        r.intervalUs = intervalUs;
        r.repeat = repeat;
        r.enabled = enabled;
        r.nextDueUs = nextDueUs;
        return r;
    }

private slots:
    void intervalConversion()
    {
        QCOMPARE(PeriodicSender::intervalMsToUs(100.0), qint64(100000));
        QCOMPARE(PeriodicSender::intervalMsToUs(1.0), qint64(1000));
        QCOMPARE(PeriodicSender::intervalMsToUs(0.5), qint64(1000));   // 下限 1ms
        QCOMPARE(PeriodicSender::intervalMsToUs(0.0), qint64(1000));
        QCOMPARE(PeriodicSender::intervalMsToUs(-5.0), qint64(1000));
    }

    void rowsFromEntriesFields()
    {
        QVariantList entries;
        entries << entry(0x300, 100, 0, true);
        entries << entry(0x301, 250, 5, false);

        QVector<PeriodicSender::PeriodicRow> rows;
        QString err;
        QVERIFY(PeriodicSender::rowsFromEntries(entries, &rows, &err));
        QVERIFY(err.isEmpty());
        QCOMPARE(rows.size(), 2);

        QCOMPARE(rows[0].spec.id, quint32(0x300));
        QCOMPARE(rows[0].intervalUs, qint64(100000));
        QCOMPARE(rows[0].repeat, quint32(0));
        QVERIFY(rows[0].enabled);

        QCOMPARE(rows[1].spec.id, quint32(0x301));
        QCOMPARE(rows[1].intervalUs, qint64(250000));
        QCOMPARE(rows[1].repeat, quint32(5));
        QVERIFY(!rows[1].enabled);
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
        QVector<PeriodicSender::PeriodicRow> rows;
        QVERIFY(PeriodicSender::rowsFromEntries(entries, &rows));
        QVERIFY(rows[0].spec.fd);
        QVERIFY(rows[0].spec.brs);
        QCOMPARE(rows[0].spec.dlc, 12);
    }

    // 软件实现的要点：不再受固件 8 条限制
    void moreThanEightEntriesAccepted()
    {
        QVariantList entries;
        for (int i = 0; i < 64; ++i)
            entries << entry(0x300 + static_cast<quint32>(i), 100, 0, true);

        QVector<PeriodicSender::PeriodicRow> rows;
        QString err;
        QVERIFY(PeriodicSender::rowsFromEntries(entries, &rows, &err));
        QVERIFY(err.isEmpty());
        QCOMPARE(rows.size(), 64);
    }

    void emptyEntriesAllowed()
    {
        QVector<PeriodicSender::PeriodicRow> rows;
        QVERIFY(PeriodicSender::rowsFromEntries(QVariantList(), &rows));
        QCOMPARE(rows.size(), 0);
    }

    void invalidDataRejected()
    {
        QVariantMap m = entry(0x300, 100, 0, true);
        m.insert(QStringLiteral("data"), QStringLiteral("0"));   // 奇数位
        QVariantList entries;
        entries << m;

        QVector<PeriodicSender::PeriodicRow> rows;
        QString err;
        QVERIFY(!PeriodicSender::rowsFromEntries(entries, &rows, &err));
        QVERIFY(err.contains(QStringLiteral("第 1 条")));
    }

    void dueRowsImmediateFirstSend()
    {
        QVector<PeriodicSender::PeriodicRow> rows;
        rows << row(100000, 0, true, 1000000);   // nextDue = 启动时刻

        const QVector<int> due = PeriodicSender::dueRows(rows, 1000000);
        QCOMPARE(due.size(), 1);
        QCOMPARE(due.value(0), 0);
        QCOMPARE(rows[0].sent, quint32(1));
        QCOMPARE(rows[0].nextDueUs, qint64(1100000));   // 启动时刻 + 周期
    }

    // 哨兵 nextDue=0 视为立即到期
    void dueRowsZeroNextDueIsImmediate()
    {
        QVector<PeriodicSender::PeriodicRow> rows;
        rows << row(100000, 0, true, 0);

        const QVector<int> due = PeriodicSender::dueRows(rows, 5);
        QCOMPARE(due.size(), 1);
        QCOMPARE(rows[0].sent, quint32(1));
    }

    // 准时：nextDue 累加，不因 now 抖动而重对齐（避免周期累积漂移）
    void dueRowsKeepsCadenceWhenOnTime()
    {
        QVector<PeriodicSender::PeriodicRow> rows;
        rows << row(100000, 0, true, 100000);

        const QVector<int> due = PeriodicSender::dueRows(rows, 100500);   // 迟到 0.5ms
        QCOMPARE(due.size(), 1);
        QCOMPARE(rows[0].nextDueUs, qint64(200000));   // = 100000 + interval
    }

    // 落后超过一个周期：丢弃落后时隙并重新对齐
    void dueRowsResyncsWhenBehind()
    {
        QVector<PeriodicSender::PeriodicRow> rows;
        rows << row(100000, 0, true, 100000);

        const QVector<int> due = PeriodicSender::dueRows(rows, 500000);   // 落后 4 个周期
        QCOMPARE(due.size(), 1);
        QCOMPARE(rows[0].nextDueUs, qint64(600000));   // now + interval
    }

    void dueRowsSkipsDisabledAndDone()
    {
        QVector<PeriodicSender::PeriodicRow> rows;
        rows << row(1000, 0, false, 0);   // 禁用
        rows << row(1000, 0, true, 0);
        rows[1].done = true;              // 已完成
        rows << row(1000, 0, true, 0);    // 唯一定期

        const QVector<int> due = PeriodicSender::dueRows(rows, 100);
        QCOMPARE(due.size(), 1);
        QCOMPARE(due.value(0), 2);
    }

    void repeatExhaustsRow()
    {
        QVector<PeriodicSender::PeriodicRow> rows;
        rows << row(1000, 2, true, 0);

        QCOMPARE(PeriodicSender::dueRows(rows, 0).size(), 1);
        QVERIFY(!rows[0].done);

        QCOMPARE(PeriodicSender::dueRows(rows, 1000).size(), 1);
        QVERIFY(rows[0].done);

        QCOMPARE(PeriodicSender::dueRows(rows, 2000).size(), 0);
        QVERIFY(PeriodicSender::allRowsDone(rows));
    }

    void doneAndEnabledHelpers()
    {
        QVector<PeriodicSender::PeriodicRow> rows;
        rows << row(1000, 0, false, 0);
        rows << row(1000, 0, true, 0);
        rows[1].done = true;

        QVERIFY(PeriodicSender::allRowsDone(rows));         // 无「启用且未完成」条目
        QVERIFY(PeriodicSender::anyRowEnabled(rows));
        QCOMPARE(PeriodicSender::enabledRowCount(rows), quint32(1));

        rows[1].done = false;
        QVERIFY(!PeriodicSender::allRowsDone(rows));
    }

    // 每通道独立表 + 独立启停（无设备时启动失败，但不影响其它通道）
    void perChannelIndependentState()
    {
        PeriodicSender s(nullptr);

        QVariantList e0;
        e0 << entry(0x100, 100, 0, true);
        QVERIFY(s.apply(0, e0));
        QCOMPARE(s.countFor(0), 1);
        QCOMPARE(s.countFor(1), 0);
        QCOMPARE(s.totalCount(), 1);

        QVariantList e1;
        e1 << entry(0x200, 100, 0, true);
        e1 << entry(0x201, 100, 0, true);
        QVERIFY(s.apply(1, e1));
        QCOMPARE(s.countFor(1), 2);
        QCOMPARE(s.totalCount(), 3);

        QVERIFY(!s.start(0));                              // 无设备：启动失败
        QVERIFY(!s.activeFor(0));
        QCOMPARE(s.errorFor(0), QStringLiteral("设备未连接"));
        QCOMPARE(s.countFor(1), 2);                        // 通道 1 不受影响
        QCOMPARE(s.errorFor(1), QString());

        QVERIFY(!s.apply(5, e0));                          // 非法通道
        QVERIFY(!s.start(-1));
    }

    void channelStatesContent()
    {
        PeriodicSender s(nullptr);
        QVariantList e;
        e << entry(0x100, 100, 0, true);
        QVERIFY(s.apply(1, e));

        const QVariantList st = s.channelStates();
        QCOMPARE(st.size(), 2);

        const QVariantMap c0 = st.at(0).toMap();
        QCOMPARE(c0.value(QStringLiteral("port")).toInt(), 0);
        QCOMPARE(c0.value(QStringLiteral("count")).toInt(), 0);
        QCOMPARE(c0.value(QStringLiteral("active")).toBool(), false);

        const QVariantMap c1 = st.at(1).toMap();
        QCOMPARE(c1.value(QStringLiteral("port")).toInt(), 1);
        QCOMPARE(c1.value(QStringLiteral("count")).toInt(), 1);
    }

    void setRowEnabledRewritesTable()
    {
        PeriodicSender s(nullptr);
        QVariantList entries;
        entries << entry(0x100, 100, 0, true);
        entries << entry(0x101, 100, 0, true);

        QVERIFY(s.setRowEnabled(0, entries, 0, false));
        const QVariantList st = s.channelStates();
        QCOMPARE(st.at(0).toMap().value(QStringLiteral("count")).toInt(), 2);

        QVERIFY(!s.setRowEnabled(0, entries, 9, false));   // 越界
    }

    // ---------------- 调度 / 发送 / 线程路径（无硬件替身） ----------------

    // 启动立即发一次，之后按 repeat 次数发送并在耗尽后自动停止
    void schedulerHonoursRepeatAndAutoStops()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVariantList e;
        e << entry(0x100, 20, 3, true);
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));

        QTRY_COMPARE_WITH_TIMEOUT(bus.total.load(), 3, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!s.activeFor(0), 3000);   // 次数耗尽自动停
        QTest::qWait(60);
        QCOMPARE(bus.total.load(), 3);                     // 不再多发
    }

    // 周期节奏：4 次发送（立即 + 3 个周期）应跨越约 3×25ms，而非一次性突发
    void schedulerKeepsCadence()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVariantList e;
        e << entry(0x100, 25, 0, true);
        QVERIFY(s.apply(0, e));

        QElapsedTimer clock;
        clock.start();
        QVERIFY(s.start(0));
        QTRY_VERIFY_WITH_TIMEOUT(bus.total.load() >= 4, 3000);
        const qint64 elapsed = clock.elapsed();
        QVERIFY(s.stop(0));

        QVERIFY2(elapsed >= 60, qPrintable(QStringLiteral("elapsed=%1ms").arg(elapsed)));
    }

    // 两通道各自独立配置、独立启停、独立计数
    void schedulerChannelsAreIndependent()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVariantList e0;
        e0 << entry(0x100, 20, 2, true);
        QVariantList e1;
        e1 << entry(0x200, 20, 3, true);
        QVERIFY(s.apply(0, e0));
        QVERIFY(s.apply(1, e1));
        QVERIFY(s.start(0));
        QVERIFY(s.start(1));

        QTRY_COMPARE_WITH_TIMEOUT(bus.count0.load(), 2, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(bus.count1.load(), 3, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!s.activeFor(0) && !s.activeFor(1), 3000);
    }

    // 停止后不再发送（含清空待发队列）
    void stopHaltsSending()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVariantList e;
        e << entry(0x100, 10, 0, true);   // 无限循环
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));
        QTRY_VERIFY_WITH_TIMEOUT(bus.total.load() > 0, 3000);

        QVERIFY(s.stop(0));
        QVERIFY(!s.activeFor(0));
        QTest::qWait(50);
        const int n = bus.total.load();
        QTest::qWait(100);
        QCOMPARE(bus.total.load(), n);
    }

    // 设备断开：自动停止该通道
    void deviceCloseStopsSending()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVariantList e;
        e << entry(0x100, 10, 0, true);
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));
        QTRY_VERIFY_WITH_TIMEOUT(bus.total.load() > 0, 3000);

        bus.ready.store(false);
        QTRY_VERIFY_WITH_TIMEOUT(!s.activeFor(0), 3000);
        QTest::qWait(50);
        const int n = bus.total.load();
        QTest::qWait(100);
        QCOMPARE(bus.total.load(), n);
    }

    // 发送失败：经 GUI 线程回投 operationFinished(false, …)
    void sendFailureIsReported()
    {
        FakeBus bus;
        bus.ok.store(false);

        PeriodicSender s(nullptr);
        bus.install(&s);
        QSignalSpy spy(&s, &PeriodicSender::operationFinished);

        QVariantList e;
        e << entry(0x100, 20, 0, true);
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));

        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 3, 3000);
        QCOMPARE(spy.last().at(0).toInt(), 0);
        QCOMPARE(spy.last().at(1).toBool(), false);
        QVERIFY(s.stop(0));
    }

    // 通道状态经 channelStates 暴露给 QML
    void channelStatesReflectRunning()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVariantList e;
        e << entry(0x100, 10, 0, true);
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));
        QTRY_VERIFY_WITH_TIMEOUT(s.channelStates().at(0).toMap().value("active").toBool(), 3000);
        QCOMPARE(s.channelStates().at(0).toMap().value("count").toInt(), 1);
        QCOMPARE(s.anyActive(), true);
        QVERIFY(s.stop(0));
    }
};

QTEST_GUILESS_MAIN(TestPeriodicSender)

#include "test_periodicsender.moc"
