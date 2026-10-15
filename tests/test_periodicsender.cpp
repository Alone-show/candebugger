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
    std::atomic<int> calls{0};        // 后端调用次数（合批后应远小于帧数）
    std::atomic<int> lastBatch{0};    // 最近一次调用的帧数
    std::atomic<int> maxBatch{0};     // 历次调用中的最大帧数

    // succeed < 0：整批按 ok 的值成功/失败；succeed >= 0：只提交前 succeed 帧，其余丢弃
    void install(PeriodicSender *s, int succeed = -1)
    {
        auto *self = this;
        s->setTransmitBackend(
            [self] { return self->ready.load(); },
            [self, succeed](int port, const QVector<FrameSpec> &specs, QString *err) {
                const int n = specs.size();
                (port == 0 ? self->count0 : self->count1).fetch_add(n);
                self->total.fetch_add(n);
                self->calls.fetch_add(1);
                self->lastBatch.store(n);
                int prev = self->maxBatch.load();
                while (n > prev && !self->maxBatch.compare_exchange_weak(prev, n)) {
                }

                if (succeed == 0)
                    return QVector<bool>();   // 整批硬失败：长度不符

                QVector<bool> submitted(specs.size(), self->ok.load());
                if (succeed > 0) {
                    for (int i = succeed; i < n; ++i)
                        submitted[i] = false;
                    if (err && succeed < n)
                        *err = QStringLiteral("替身：部分失败");
                }
                return submitted;
            });
    }
};

} // namespace

// 记录后端收到的帧描述（含 counter），用于验证「快捷周期」逐帧递增与字段透传
struct SpecRecorder
{
    std::mutex m;
    QVector<FrameSpec> specs;

    void install(PeriodicSender *s)
    {
        auto *self = this;
        s->setTransmitBackend([] { return true; },
                              [self](int, const QVector<FrameSpec> &in, QString *) {
                                  std::lock_guard<std::mutex> lk(self->m);
                                  self->specs += in;
                                  return QVector<bool>(in.size(), true);
                              });
    }

    int count()
    {
        std::lock_guard<std::mutex> lk(m);
        return specs.size();
    }

    int countWithId(quint32 id)
    {
        std::lock_guard<std::mutex> lk(m);
        int n = 0;
        for (const FrameSpec &s : specs) {
            if (s.id == id)
                ++n;
        }
        return n;
    }

    QVector<FrameSpec> snapshot()
    {
        std::lock_guard<std::mutex> lk(m);
        return specs;
    }
};

// 软件定时发送：纯逻辑 + 调度/发送/线程路径（无硬件）单测。
class TestPeriodicSender : public QObject
{
    Q_OBJECT

private:
    // 通知流里 apply/start 也会发成功通知，故从后往前找最近一条失败通知
    static bool lastFailureNotice(const QSignalSpy *spy, QString *message)
    {
        for (int i = spy->count() - 1; i >= 0; --i) {
            if (spy->at(i).at(1).toBool())
                continue;
            if (message)
                *message = spy->at(i).at(2).toString();
            return true;
        }
        return false;
    }

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

    // 单帧面板「快捷周期发送」用的帧描述（与 QML specMap() 同形）
    static QVariantMap quickSpec(quint32 id = 0x123)
    {
        QVariantMap m;
        m.insert(QStringLiteral("id"), id);
        m.insert(QStringLiteral("extended"), false);
        m.insert(QStringLiteral("fd"), false);
        m.insert(QStringLiteral("remote"), false);
        m.insert(QStringLiteral("dlc"), 2);
        m.insert(QStringLiteral("data"), QStringLiteral("AA BB"));
        m.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));
        return m;
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

    // 拆批：CAN 组在前、FD 组在后，组内保持原顺序
    void splitSpecIndicesByTypeGroups()
    {
        auto spec = [](bool fd, quint32 id) {
            FrameSpec s;
            s.fd = fd;
            s.id = id;
            return s;
        };

        const QVector<FrameSpec> specs{ spec(false, 1), spec(true, 2), spec(false, 3),
                                        spec(true, 4) };
        QVector<int> can;
        QVector<int> fd;
        PeriodicSender::splitSpecIndicesByType(specs, &can, &fd);
        QCOMPARE(can, QVector<int>({ 0, 2 }));
        QCOMPARE(fd, QVector<int>({ 1, 3 }));

        // 全 CAN / 全 FD / 空
        QVector<int> c2;
        QVector<int> f2;
        PeriodicSender::splitSpecIndicesByType({ spec(false, 1), spec(false, 2) }, &c2, &f2);
        QCOMPARE(c2, QVector<int>({ 0, 1 }));
        QCOMPARE(f2.size(), 0);

        PeriodicSender::splitSpecIndicesByType({ spec(true, 1) }, &c2, &f2);
        QCOMPARE(c2.size(), 0);
        QCOMPARE(f2, QVector<int>({ 0 }));

        PeriodicSender::splitSpecIndicesByType({}, &c2, &f2);
        QCOMPARE(c2.size(), 0);
        QCOMPARE(f2.size(), 0);
    }

    // 组内"前 n 帧成功"要按原下标回填；n 越界要夹住，不能越界写
    void markGroupSubmittedMapsBackToOriginalIndices()
    {
        QVector<bool> submitted(5, false);
        const QVector<int> pos{ 4, 0, 2 };   // 组内顺序：specs[4], specs[0], specs[2]

        PeriodicSender::markGroupSubmitted(pos, 2, &submitted);
        QCOMPARE(submitted, QVector<bool>({ true, false, false, false, true }));

        QVector<bool> all(3, false);
        PeriodicSender::markGroupSubmitted(pos, 99, &all);   // 越界 -> 夹到组大小（组内全成功）
        QCOMPARE(all, QVector<bool>({ true, false, true }));   // specs[1] 不在该组内，保持 false

        QVector<bool> none(3, false);
        PeriodicSender::markGroupSubmitted(pos, -5, &none);   // 负数 -> 全不成功
        QCOMPARE(none, QVector<bool>({ false, false, false }));

        PeriodicSender::markGroupSubmitted({}, 3, &none);   // 空组不越界
        QCOMPARE(none, QVector<bool>({ false, false, false }));

        PeriodicSender::markGroupSubmitted(pos, 1, nullptr);   // 允许 ok == nullptr
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

    // 同一 tick 到期的多条条目合并为一次后端调用（而不是每帧一次）
    void dueFramesShareOneCall()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVariantList e;
        for (int i = 0; i < 10; ++i)
            e << entry(0x300 + static_cast<quint32>(i), 1000, 0, true);   // 周期 1s，留足余量
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));

        // 首轮：10 条同时到期 -> 一次调用带 10 帧
        QTRY_COMPARE_WITH_TIMEOUT(bus.total.load(), 10, 3000);
        QCOMPARE(bus.maxBatch.load(), 10);
        QCOMPARE(bus.calls.load(), 1);

        QTest::qWait(60);   // 仍在本周期内，不应有第二轮
        QCOMPARE(bus.calls.load(), 1);

        QVERIFY(s.stop(0));
    }

    // 部分提交：只有成功的帧进入统计，且通知里给出已发/总数
    void partialBatchFailureReported()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s, 3);   // 每批只提交前 3 帧

        QSignalSpy spy(&s, &PeriodicSender::operationFinished);

        QVariantList e;
        for (int i = 0; i < 5; ++i)
            e << entry(0x300 + static_cast<quint32>(i), 20, 0, true);
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));

        QString msg;
        QTRY_VERIFY_WITH_TIMEOUT(lastFailureNotice(&spy, &msg), 3000);
        QVERIFY2(msg.contains(QStringLiteral("3/5")), qPrintable(msg));
        QVERIFY(s.stop(0));
    }

    // 整批硬失败（结果长度不符）：一条都不记账
    void wholeBatchFailureReported()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s, 0);   // 返回空结果 = 整批失败

        QSignalSpy spy(&s, &PeriodicSender::operationFinished);
        QVariantList e;
        e << entry(0x300, 20, 0, true);
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));

        QString msg;
        QTRY_VERIFY_WITH_TIMEOUT(lastFailureNotice(&spy, &msg), 3000);
        QVERIFY(msg.contains(QStringLiteral("无应答")));
        QVERIFY(s.stop(0));
    }

    // 上游队列积压时也要按 kBatchMax 切分，避免单次调用无界膨胀
    void oversizedBatchIsSplit()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVariantList e;
        for (int i = 0; i < 100; ++i)
            e << entry(0x300 + static_cast<quint32>(i), 100, 0, true);
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));

        QTRY_COMPARE_WITH_TIMEOUT(bus.total.load(), 100, 3000);
        QVERIFY2(bus.maxBatch.load() <= 64, qPrintable(QString::number(bus.maxBatch.load())));
        QVERIFY(bus.calls.load() >= 2);
        QVERIFY(s.stop(0));
    }

    // ---------------- 快捷槽（单帧面板的「快捷周期发送」） ----------------

    // 启动立即发一次、按周期推进、次数耗尽自动停；不写入周期表，通知也不串通道
    void quickPeriodicHonoursIntervalAndRepeat()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QSignalSpy quickSpy(&s, &PeriodicSender::quickOperationFinished);
        QSignalSpy tableSpy(&s, &PeriodicSender::operationFinished);

        QElapsedTimer clock;
        clock.start();
        QVERIFY(s.startQuick(0, quickSpec(), 20, 3));
        QTRY_COMPARE_WITH_TIMEOUT(bus.total.load(), 3, 3000);
        const qint64 elapsed = clock.elapsed();

        QTRY_VERIFY_WITH_TIMEOUT(!s.quickActive(0), 3000);   // 次数耗尽自动停
        QVERIFY2(elapsed >= 30, qPrintable(QStringLiteral("elapsed=%1ms").arg(elapsed)));
        QTest::qWait(60);
        QCOMPARE(bus.total.load(), 3);                       // 不再多发
        QCOMPARE(s.countFor(0), 0);                          // 没写进周期表
        QCOMPARE(s.anyActive(), false);
        QVERIFY(quickSpy.count() >= 2);                      // 启动 + 完成
        QCOMPARE(tableSpy.count(), 0);                       // 不污染周期表的通知流
    }

    // 快捷发送原样透传帧描述：滚动计数逐帧递增，递增填充/CRC/发送模式都被保留
    void quickPeriodicCarriesSpecAndIncrementsRollingCounter()
    {
        SpecRecorder rec;
        PeriodicSender s(nullptr);
        rec.install(&s);

        QVariantMap spec = quickSpec();
        spec.insert(QStringLiteral("rolling"), true);
        spec.insert(QStringLiteral("increment"), true);
        spec.insert(QStringLiteral("crc"), true);
        spec.insert(QStringLiteral("txm"), 2);   // 自发自收
        QVERIFY(s.startQuick(0, spec, 15, 3));

        QTRY_COMPARE_WITH_TIMEOUT(rec.count(), 3, 3000);
        const QVector<FrameSpec> got = rec.snapshot();
        QCOMPARE(got.size(), 3);
        QCOMPARE(got.at(0).counter, 0u);
        QCOMPARE(got.at(1).counter, 1u);
        QCOMPARE(got.at(2).counter, 2u);
        QVERIFY(got.at(0).rolling);
        QVERIFY(got.at(0).increment);
        QVERIFY(got.at(0).crc);
        QCOMPARE(got.at(0).txm, 2);
        QCOMPARE(got.at(0).id, 0x123u);
        QCOMPARE(got.at(0).dlc, 2);
        QCOMPARE(got.at(0).data, QByteArray::fromHex("AABB"));
    }

    // ID 递增：勾选后每帧 ID = 基址 + 计数器（标准帧按 0x7FF 回绕）；与滚动计数共用一个计数器
    void quickPeriodicIdIncrement()
    {
        SpecRecorder rec;
        PeriodicSender s(nullptr);
        rec.install(&s);

        QVariantMap spec = quickSpec(0x7FE);
        spec.insert(QStringLiteral("incrementId"), true);
        QVERIFY(s.startQuick(0, spec, 15, 3));

        QTRY_COMPARE_WITH_TIMEOUT(rec.count(), 3, 3000);
        const QVector<FrameSpec> got = rec.snapshot();
        QCOMPARE(got.at(0).id, 0x7FEu);
        QCOMPARE(got.at(1).id, 0x7FFu);
        QCOMPARE(got.at(2).id, 0x000u);   // 回绕
    }

    // ID 递增 + 滚动计数：两者共用同一计数器，首字节与 ID 同步推进
    void quickPeriodicIdIncrementSharesCounterWithRolling()
    {
        SpecRecorder rec;
        PeriodicSender s(nullptr);
        rec.install(&s);

        QVariantMap spec = quickSpec(0x200);
        spec.insert(QStringLiteral("incrementId"), true);
        spec.insert(QStringLiteral("rolling"), true);
        QVERIFY(s.startQuick(0, spec, 15, 3));

        QTRY_COMPARE_WITH_TIMEOUT(rec.count(), 3, 3000);
        const QVector<FrameSpec> got = rec.snapshot();
        QCOMPARE(got.at(0).id, 0x200u);
        QCOMPARE(got.at(1).id, 0x201u);
        QCOMPARE(got.at(2).id, 0x202u);
        QCOMPARE(got.at(0).counter, 0u);
        QCOMPARE(got.at(1).counter, 1u);
        QCOMPARE(got.at(2).counter, 2u);
    }

    // 与周期表通道互不干扰：同通道可同时运行；apply/start/stop 不动快捷槽，stopQuick 不动表
    void quickPeriodicIsIndependentOfTableChannel()
    {
        SpecRecorder rec;
        PeriodicSender s(nullptr);
        rec.install(&s);

        QVariantList e;
        e << entry(0x100, 10, 0, true);   // 表：无限循环
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));
        QTRY_VERIFY_WITH_TIMEOUT(rec.countWithId(0x100) > 0, 3000);

        QVERIFY(s.startQuick(0, quickSpec(0x7AA), 10, 0));   // 同通道同时跑
        QTRY_VERIFY_WITH_TIMEOUT(rec.countWithId(0x7AA) > 0, 3000);

        QCOMPARE(s.countFor(0), 1);            // 快捷发送不写表
        QVERIFY(s.activeFor(0));               // 表仍在运行
        QVERIFY(s.quickActive(0));

        // 重新下发/启停周期表：快捷槽保持运行
        QVariantList e2;
        e2 << entry(0x101, 10, 0, true);
        QVERIFY(s.apply(0, e2));
        QVERIFY(s.start(0));
        QVERIFY(s.quickActive(0));

        QVERIFY(s.stopQuick(0));
        QVERIFY(!s.quickActive(0));
        QVERIFY(s.activeFor(0));               // 表不受影响

        QTest::qWait(30);
        const int quickBefore = rec.countWithId(0x7AA);
        const int tableBefore = rec.countWithId(0x101);
        QTest::qWait(60);
        QCOMPARE(rec.countWithId(0x7AA), quickBefore);       // 快捷槽已停
        QVERIFY(rec.countWithId(0x101) > tableBefore);       // 表仍在发
        QVERIFY(s.stop(0));
    }

    // 设备断开：快捷槽自动停止并给出原因
    void deviceCloseStopsQuickPeriodic()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QSignalSpy spy(&s, &PeriodicSender::quickOperationFinished);
        QVERIFY(s.startQuick(0, quickSpec(), 10, 0));
        QTRY_VERIFY_WITH_TIMEOUT(bus.total.load() > 0, 3000);

        bus.ready.store(false);
        QTRY_VERIFY_WITH_TIMEOUT(!s.quickActive(0), 3000);
        QTest::qWait(50);
        const int n = bus.total.load();
        QTest::qWait(100);
        QCOMPARE(bus.total.load(), n);

        QString msg;
        QVERIFY(lastFailureNotice(&spy, &msg));
        QVERIFY2(msg.contains(QStringLiteral("设备")), qPrintable(msg));
    }

    // 帧描述非法：拒绝启动、不发任何帧
    void quickPeriodicRejectsInvalidSpec()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QSignalSpy spy(&s, &PeriodicSender::quickOperationFinished);
        QVariantMap bad = quickSpec();
        bad.insert(QStringLiteral("data"), QStringLiteral("ZZ"));   // 非法 Hex

        QVERIFY(!s.startQuick(0, bad, 10, 0));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(1).toBool(), false);
        QVERIFY(!s.quickActive(0));
        QTest::qWait(40);
        QCOMPARE(bus.total.load(), 0);
    }

    // quickStates 供 QML 反映运行态/周期/次数
    void quickStatesReflectRunning()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVERIFY(s.quickStates().at(1).toMap().value("active").toBool() == false);
        QVERIFY(s.startQuick(1, quickSpec(), 25, 0));
        const QVariantMap st = s.quickStates().at(1).toMap();
        QCOMPARE(st.value("active").toBool(), true);
        QCOMPARE(st.value("intervalMs").toDouble(), 25.0);
        QCOMPARE(st.value("repeat").toInt(), 0);
        QCOMPARE(s.quickStates().at(0).toMap().value("active").toBool(), false);

        QVERIFY(s.stopQuick(1));
        QCOMPARE(s.quickStates().at(1).toMap().value("active").toBool(), false);
    }

    // stopAll 同时停掉表通道与快捷槽
    void stopAllStopsQuickPeriodicToo()
    {
        FakeBus bus;
        PeriodicSender s(nullptr);
        bus.install(&s);

        QVariantList e;
        e << entry(0x100, 10, 0, true);
        QVERIFY(s.apply(0, e));
        QVERIFY(s.start(0));
        QVERIFY(s.startQuick(0, quickSpec(0x7AA), 10, 0));

        s.stopAll();
        QVERIFY(!s.activeFor(0));
        QVERIFY(!s.quickActive(0));
    }
};

QTEST_GUILESS_MAIN(TestPeriodicSender)

#include "test_periodicsender.moc"
