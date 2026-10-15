// L3/压力：1000fps 级持续接收（风险 4/21）——吞吐、丢弃率、内存增长
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <cstdio>

#include <unistd.h>
#include <cstring>

#include "core/CanDevice.h"
#include "core/FrameBuilder.h"
#include "core/PeriodicSender.h"
#include "core/RxManager.h"
#include "core/Statistics.h"

namespace {

int envInt(const char *name, int def)
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue(name, &ok);
    return ok ? v : def;
}

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

long rssKb()
{
    QFile f(QStringLiteral("/proc/self/statm"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QList<QByteArray> parts = f.readAll().simplified().split(' ');
    if (parts.size() < 2)
        return -1;
    const long pages = parts.at(1).toLong();
    return pages * (sysconf(_SC_PAGESIZE) / 1024);
}

QVariantMap ttxEntry(quint32 id, int intervalMs)
{
    QVariantMap e;
    e.insert(QStringLiteral("id"), id);
    e.insert(QStringLiteral("dlc"), 8);
    e.insert(QStringLiteral("data"), QStringLiteral("11 22 33 44 55 66 77 88"));
    e.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));
    e.insert(QStringLiteral("txm"), static_cast<int>(ZCAN_SR_NORM));
    e.insert(QStringLiteral("echo"), true);
    e.insert(QStringLiteral("intervalMs"), intervalMs);
    e.insert(QStringLiteral("repeat"), 0);      // 0 = 循环
    e.insert(QStringLiteral("enabled"), true);
    return e;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    CanDevice dev;
    if (!dev.open()) {
        std::fprintf(stderr, "打开设备失败（需 USB 权限）\n");
        return 2;
    }
    dev.resetChannel(0);   // 清除可能残留的上一轮配置
    if (dev.configureChannel(0, 500000, 2000000, 87.5, false).isEmpty()) {
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

    Statistics stats(&dev);
    stats.start(100);
    RxManager mgr(&dev);
    mgr.setStatistics(&stats);
    if (!mgr.start(QVariantList{0}, false)) {
        std::fprintf(stderr, "接收管线启动失败\n");
        dev.close();
        return 5;
    }
    wait(200);
    dev.clearBuffer(0);
    mgr.clear();

    // 基线：单帧自发自收，确认接收管线工作
    {
        ZCAN_20_MSG tx;
        std::memset(&tx, 0, sizeof(tx));
        tx.hdr.inf.txm = ZCAN_SR_NORM;
        tx.hdr.inf.echo = 1;
        tx.hdr.id = 0x5FF;
        tx.hdr.chn = 0;
        tx.hdr.len = 8;
        for (int b = 0; b < 8; ++b)
            tx.dat[b] = static_cast<quint8>(b);
        const bool baseSent = dev.transmitCan(0, tx);
        wait(400);
        std::printf("基线自收: 发送=%s 接收=%llu\n", baseSent ? "成功" : "失败",
                    static_cast<unsigned long long>(mgr.totalReceived()));
        dev.clearBuffer(0);
        mgr.clear();
    }

    // 8 条硬件定时发送，默认各 5ms -> 实测 ~1000 fps（受设备/总线限制）
    // 可通过 STRESS_INTERVAL_MS / STRESS_ENTRIES 调整
    const int intervalMs = envInt("STRESS_INTERVAL_MS", 5);
    const int entryCount = envInt("STRESS_ENTRIES", 8);
    QVariantList entries;
    for (int i = 0; i < entryCount; ++i)
        entries << ttxEntry(0x600 + static_cast<quint32>(i), intervalMs);

    PeriodicSender ps(&dev);
    bool applied = ps.apply(0, entries);
    const long rssBefore = rssKb();
    bool started = ps.start(0);
    wait(500);
    if (started && mgr.totalReceived() == 0) {
        // 设备偶发未调度：清缓冲后重试一次
        std::printf("压力: 首次无流量，重试一次...\n");
        ps.stop(0);
        dev.clearBuffer(0);
        mgr.clear();
        applied = ps.apply(0, entries);
        started = ps.start(0);
        wait(500);
    }
    std::printf("压力: 下发=%s 启动=%s 起始RSS=%ld KB (间隔=%dms 条数=%d 错误=%s)\n",
                applied ? "成功" : "失败", started ? "成功" : "失败", rssBefore,
                intervalMs, entryCount, ps.lastError().toUtf8().constData());
    if (!applied || !started) {
        ps.stop(0);
        mgr.stop();
        dev.close();
        return 6;
    }

    const int kSeconds = 8;
    for (int s = 1; s <= kSeconds; ++s) {
        wait(1000);
        std::printf("  t=%ds 接收=%llu 丢弃=%llu 缓冲=%d 显示=%d rxFps=%.0f 负载=%.1f%%\n",
                    s,
                    static_cast<unsigned long long>(mgr.totalReceived()),
                    static_cast<unsigned long long>(mgr.totalDropped()),
                    mgr.bufferedCount(), mgr.displayedCount(),
                    stats.rxFps(), stats.busLoad());
    }

    ps.stop(0);
    wait(300);
    const qulonglong pushed = mgr.totalReceived();
    const qulonglong dropped = mgr.totalDropped();
    const long rssAfter = rssKb();
    const int rows = mgr.frameModel() ? mgr.frameModel()->rowCount() : -1;
    const qulonglong statsTotal = stats.totalFrames();

    mgr.stop();
    dev.resetChannel(0);
    dev.close();

    const double fps = static_cast<double>(pushed) / kSeconds;
    const double dropRatio = pushed > 0 ? static_cast<double>(dropped) / static_cast<double>(pushed) : 1.0;
    const long rssGrowth = rssAfter - rssBefore;

    std::printf("汇总: 接收=%llu (%.0f fps) 丢弃=%llu (%.1f%%) 模型行=%d 统计总帧=%llu\n",
                static_cast<unsigned long long>(pushed), fps,
                static_cast<unsigned long long>(dropped), dropRatio * 100.0, rows,
                static_cast<unsigned long long>(statsTotal));
    std::printf("内存: 起始=%ld KB 结束=%ld KB 增长=%ld KB\n", rssBefore, rssAfter, rssGrowth);

    // 验收：吞吐 > 500fps、丢弃率 < 50%、内存增长 < 200MB（粗漏检）
    const bool ok = fps > 500.0 && dropRatio < 0.5 && rssGrowth < 200 * 1024;
    std::printf("结论: %s\n", ok ? "通过" : "未通过");
    return ok ? 0 : 7;
}
