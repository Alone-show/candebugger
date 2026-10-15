// 周期发送批量提交 实机验证（步骤 15）
//
// 关注点：一张 N 条的周期表（周期 T）在**同一个 tick** 到期时，相邻两帧在总线上的间隔。
//   改造前：发送线程逐帧调用 VCI_Transmit，每帧一次 USB 往返 -> 实测 ~0.76ms/帧。
//   改造后：同 tick 到期帧按类型合并为一次调用 -> 受总线限制（500k 经典帧 ~0.24ms/帧）。
//
// 方法：自发自收（txm=ZCAN_SR_NORM + echo）让本机发送帧回灌接收缓冲，直接读设备时间戳
//       hdr.ts（100us/tick）计算差值，不经过 UI 管线，避免批量刷新带来的时间量化。
//
// 用法：
//   ./device_periodic_test [条目数=10] [周期ms=100] [运行ms=1200] [--txm N] [--dump]
//   --txm 0 正常模式（上总线等 ACK，需对端/回环提供 ACK）；2 自发自收（默认，单机自测）
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "core/CanDevice.h"
#include "core/FrameBuilder.h"
#include "core/PeriodicSender.h"

namespace {

struct Sample
{
    quint32 id = 0;
    quint32 ts = 0;       // 设备时间戳（100us/tick；0 表示无效）
    qint64 hostUs = 0;    // 接收时刻（回退用）
};

double median(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    int entryCount = 10;
    int intervalMs = 100;
    int runMs = 1200;
    int txm = ZCAN_SR_NORM;
    bool dump = false;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "--dump") {
            dump = true;
            continue;
        }
        if (a == "--txm") {
            ++i;
            if (i < argc)
                txm = QByteArray(argv[i]).toInt();
            continue;
        }
        const int v = a.toInt();
        if (positional == 0)
            entryCount = qMax(1, v);
        else if (positional == 1)
            intervalMs = qMax(1, v);
        else if (positional == 2)
            runMs = qMax(100, v);
        ++positional;
    }

    CanDevice dev;
    if (!dev.open()) {
        std::fprintf(stderr, "打开设备失败（需 USB 权限）\n");
        return 2;
    }
    const QString cfg = dev.configureChannel(0, 500000, 2000000, 87.5, false);
    if (cfg.isEmpty()) {
        std::fprintf(stderr, "通道配置失败\n");
        dev.close();
        return 3;
    }
    dev.setTerminalResistor(0, true);
    if (!dev.startChannel(0)) {
        std::fprintf(stderr, "启动通道失败\n");
        dev.close();
        return 4;
    }
    dev.clearBuffer(0);

    QVariantList entries;
    for (int i = 0; i < entryCount; ++i) {
        QVariantMap e;
        e.insert(QStringLiteral("id"), 0x300u + static_cast<quint32>(i));
        e.insert(QStringLiteral("dlc"), 8);
        e.insert(QStringLiteral("data"), QStringLiteral("01 02 03 04 05 06 07 08"));
        e.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));
        e.insert(QStringLiteral("txm"), txm);
        e.insert(QStringLiteral("echo"), true);
        e.insert(QStringLiteral("intervalMs"), intervalMs);
        e.insert(QStringLiteral("repeat"), 0);
        e.insert(QStringLiteral("enabled"), true);
        entries << e;
    }

    PeriodicSender ps(&dev);
    if (!ps.apply(0, entries)) {
        std::fprintf(stderr, "下发周期配置失败: %s\n", qPrintable(ps.lastError()));
        dev.close();
        return 5;
    }

    // 发送侧自述的成功/失败（经 GUI 线程回投）：与"总线上是否真出现帧"可能不一致 ——
    // 正常模式（txm=0）无对端 ACK 时，帧仍会上总线，但驱动判定为失败。
    int noticeOk = 0;
    int noticeFail = 0;
    QString lastFail;
    QObject::connect(&ps, &PeriodicSender::operationFinished,
                     [&](int, bool ok, const QString &message) {
                         if (ok) {
                             ++noticeOk;
                         } else {
                             ++noticeFail;
                             lastFail = message;
                         }
                     });

    std::vector<Sample> samples;
    const quint32 idBase = 0x300;
    const quint32 idEnd = 0x300 + static_cast<quint32>(entryCount);

    QElapsedTimer clock;
    clock.start();
    if (!ps.start(0)) {
        std::fprintf(stderr, "启动周期发送失败\n");
        dev.close();
        return 6;
    }

    // 采集循环：只关心本机发送（inf.tx=1）且在表内的帧
    ZCAN_20_MSG buf[512];
    while (clock.elapsed() < runMs) {
        const int rc = dev.receiveCan(0, buf, 512, 5);
        const qint64 hostUs = clock.nsecsElapsed() / 1000;
        for (int i = 0; i < rc; ++i) {
            const ZCAN_20_MSG &m = buf[i];
            if (m.hdr.inf.tx == 0)
                continue;
            if (m.hdr.id < idBase || m.hdr.id >= idEnd)
                continue;
            samples.push_back(Sample{ m.hdr.id, m.hdr.ts, hostUs });
        }
    }
    ps.stop(0);

    // 让 GUI 线程回投的通知落地（采样循环未跑事件循环）
    {
        QEventLoop loop;
        QTimer::singleShot(60, &loop, &QEventLoop::quit);
        loop.exec();
    }

    dev.resetChannel(0);
    dev.close();

    std::printf("周期表: %d 条 × %dms（txm=%d），采集 %dms，收到本机发送帧 %zu 条\n", entryCount,
                intervalMs, txm, runMs, samples.size());
    std::printf("发送侧自述: 成功通知=%d 失败通知=%d%s%s\n", noticeOk, noticeFail,
                lastFail.isEmpty() ? "" : " 最后错误: ", qPrintable(lastFail));
    if (samples.empty()) {
        std::printf("结论: 未通过（未收到自发帧：检查 txm/echo/总线 ACK/终端电阻）\n");
        return 7;
    }

    const bool tsValid = samples.front().ts != 0;
    std::printf("时间戳: %s\n", tsValid ? "使用设备 hdr.ts（100us/tick）"
                                        : "设备时间戳为 0 —— 回退接收时刻（量化到接收批次）");
    if (dump) {
        const size_t n = qMin<size_t>(samples.size(), 40);
        for (size_t i = 0; i < n; ++i)
            std::printf("  #%02zu id=0x%X ts=%u hostUs=%lld\n", i, samples[i].id, samples[i].ts,
                        static_cast<long long>(samples[i].hostUs));
    }

    auto stampUs = [&](const Sample &s) -> qint64 {
        return tsValid ? static_cast<qint64>(s.ts) * 100 : s.hostUs;
    };

    // 同一 tick 判定：与前一帧的时间差 < 半个周期
    const qint64 tickGapUs = static_cast<qint64>(intervalMs) * 1000 / 2;

    std::vector<double> intraGapMs;   // 同一 tick 内相邻帧间隔
    std::vector<int> framesPerTick;
    std::vector<double> tickPeriodMs; // 相邻 tick 的首帧间隔

    int curTickFrames = 1;
    qint64 lastTickFirstUs = stampUs(samples.front());
    for (size_t i = 1; i < samples.size(); ++i) {
        const qint64 prev = stampUs(samples[i - 1]);
        const qint64 cur = stampUs(samples[i]);
        const qint64 gap = cur - prev;
        if (gap >= 0 && gap < tickGapUs) {
            intraGapMs.push_back(gap / 1000.0);
            ++curTickFrames;
        } else {
            framesPerTick.push_back(curTickFrames);
            tickPeriodMs.push_back((cur - lastTickFirstUs) / 1000.0);
            lastTickFirstUs = cur;
            curTickFrames = 1;
        }
    }
    framesPerTick.push_back(curTickFrames);

    int minPerTick = framesPerTick.empty() ? 0 : *std::min_element(framesPerTick.begin(), framesPerTick.end());
    int maxPerTick = framesPerTick.empty() ? 0 : *std::max_element(framesPerTick.begin(), framesPerTick.end());

    // 采集窗口末尾多半会切到一个不完整的 tick，判定时只要求"非末尾 tick 都是满表"
    const int fullTicks = static_cast<int>(std::count(framesPerTick.begin(), framesPerTick.end(), entryCount));
    bool completeTicks = true;
    for (size_t i = 0; i + 1 < framesPerTick.size(); ++i) {
        if (framesPerTick[i] != entryCount)
            completeTicks = false;
    }

    std::printf("tick 数: %zu（每 tick 帧数 min=%d max=%d，满表 %d 个）\n", framesPerTick.size(),
                minPerTick, maxPerTick, fullTicks);
    if (!intraGapMs.empty()) {
        const auto mm = std::minmax_element(intraGapMs.begin(), intraGapMs.end());
        std::printf("同一 tick 内相邻帧间隔: 中位=%.3fms 最小=%.3fms 最大=%.3fms（样本 %zu）\n",
                    median(intraGapMs), *mm.first, *mm.second, intraGapMs.size());
    } else {
        std::printf("同一 tick 内相邻帧间隔: 无样本（每 tick 只收到 1 帧）\n");
    }
    if (!tickPeriodMs.empty())
        std::printf("tick 周期: 中位=%.2fms（配置 %dms）\n", median(tickPeriodMs), intervalMs);

    // 判据：非末尾的 tick 都抽满整表，且 tick 内相邻帧间隔远小于"逐帧一次 USB 往返"的 ~0.7ms 量级
    const bool fullTicksOk = completeTicks && fullTicks >= 2;
    const bool busLimited = intraGapMs.empty() || median(intraGapMs) < 0.6;
    const bool cadenceOk = tickPeriodMs.empty() || (median(tickPeriodMs) > intervalMs * 0.7
                                                   && median(tickPeriodMs) < intervalMs * 1.3);
    const bool ok = fullTicksOk && busLimited && cadenceOk;
    std::printf("结论: %s（每 tick 满表=%s 总线级间隔=%s 周期准确=%s）\n", ok ? "通过" : "未通过",
                fullTicksOk ? "是" : "否", busLimited ? "是" : "否", cadenceOk ? "是" : "否");
    return ok ? 0 : 8;
}
