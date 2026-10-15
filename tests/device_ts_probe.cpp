// 设备时间戳标定探针：确定 ZCAN_MSG_HDR.ts 的单位与行为
//
// 方法：以已知间隔(默认 100ms)自发自收(echo)一帧，比较设备时间戳增量与主机
// 单调时钟增量，推断 tick 单位：
//   ΔdevTs ≈ 100000 (对 100ms) -> 1us
//   ΔdevTs ≈ 1000             -> 100us
//   ΔdevTs ≈ 100              -> 1ms
// 同时观察首帧原始值、是否单调、是否回绕、错误帧是否带 ts。
//
// 手动运行（需真实设备与 USB 权限）：
//   scripts/run_hw_tests.sh 不含本用例；直接执行 ./build/device_ts_probe [帧数] [间隔ms]
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/BitTiming.h"
#include "core/CanDevice.h"

namespace {

constexpr quint32 kProbeId = 0x7AB;

struct Sample
{
    quint32 rawTs = 0;
    qint64 hostNs = 0;
};

// 等待并取出一个匹配 id 的自收帧；返回是否取到
bool waitEcho(CanDevice &dev, quint32 port, int maxIters, Sample *out, bool *sawError)
{
    ZCAN_FD_MSG buf[64];
    ZCAN_20_MSG canBuf[64];
    for (int it = 0; it < maxIters; ++it) {
        const int rf = dev.receiveFd(port, buf, 64, 5);
        for (int i = 0; i < rf; ++i) {
            if (buf[i].hdr.inf.err)
                *sawError = true;
            if ((buf[i].hdr.id & 0x1FFFFFFFu) == kProbeId) {
                out->rawTs = buf[i].hdr.ts;
                return true;
            }
        }
        const int rc = dev.receiveCan(port, canBuf, 64, 5);
        for (int i = 0; i < rc; ++i) {
            if (canBuf[i].hdr.inf.err)
                *sawError = true;
            if ((canBuf[i].hdr.id & 0x7FFu) == kProbeId) {
                out->rawTs = canBuf[i].hdr.ts;
                return true;
            }
        }
        QCoreApplication::processEvents();
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    int frames = (argc > 1) ? std::atoi(argv[1]) : 20;
    int gapMs = (argc > 2) ? std::atoi(argv[2]) : 100;
    if (frames < 2)
        frames = 2;
    if (gapMs < 1)
        gapMs = 1;

    CanDevice dev;
    if (!dev.open()) {
        std::fprintf(stderr, "打开设备失败：请确认 USB 权限（需安装 udev 规则）\n");
        return 2;
    }
    const CanDeviceInfo info = dev.deviceInfo();
    std::printf("设备 SN=%s 通道数=%u\n", qPrintable(info.sn), info.channels);

    ChannelConfig cfg;
    cfg.clock = BitTiming::DefaultClock;   // clk 即真实时钟（60MHz，V1.01）
    const auto arb = BitTiming::calculate(cfg.clock, 500000, 87.5);
    const auto dat = BitTiming::calculate(cfg.clock, 2000000, 87.5);
    if (!arb.ok || !dat.ok) {
        std::fprintf(stderr, "位定时计算失败\n");
        dev.close();
        return 3;
    }
    cfg.arbitration = arb.params;
    cfg.data = dat.params;

    if (!dev.initChannel(0, cfg)) {
        std::fprintf(stderr, "初始化通道 0 失败\n");
        dev.close();
        return 4;
    }
    dev.setTerminalResistor(0, true);
    if (!dev.startChannel(0)) {
        std::fprintf(stderr, "启动通道 0 失败\n");
        dev.close();
        return 5;
    }
    std::printf("通道 0 已启动，探测 %d 帧，间隔 %d ms\n\n", frames, gapMs);

    // 若设备有第二通道，一并启动，用于判断两通道是否共用同一时钟
    bool ch1Ok = false;
    if (info.channels > 1) {
        if (dev.initChannel(1, cfg)) {
            dev.setTerminalResistor(1, true);
            ch1Ok = dev.startChannel(1);
        }
        std::printf("通道 1 : %s\n", ch1Ok ? "已启动(用于跨通道时钟对比)" : "启动失败/跳过");
    }

    // CAN 帧（fmt=0）自带 echo，经 SDK 回送
    Sample cur;
    Sample prev;
    bool havePrev = false;
    bool sawError = false;
    bool monotonic = true;
    int got = 0;

    QElapsedTimer clock;
    clock.start();

    for (int i = 0; i < frames; ++i) {
        ZCAN_20_MSG tx;
        std::memset(&tx, 0, sizeof(tx));
        tx.hdr.inf.txm = ZCAN_SR_NORM;
        tx.hdr.inf.fmt = 0;
        tx.hdr.inf.sef = 0;
        tx.hdr.inf.echo = 1;
        tx.hdr.id = kProbeId;
        tx.hdr.chn = 0;
        tx.hdr.len = 4;
        tx.dat[0] = static_cast<quint8>(i);

        const qint64 sendNs = clock.nsecsElapsed();
        if (!dev.transmitCan(0, tx)) {
            std::fprintf(stderr, "第 %d 帧发送失败\n", i);
            break;
        }

        Sample s;
        if (!waitEcho(dev, 0, 200, &s, &sawError)) {
            std::fprintf(stderr, "第 %d 帧未收到回显\n", i);
            continue;
        }
        const qint64 recvNs = clock.nsecsElapsed();
        const qint64 roundTripUs = (recvNs - sendNs) / 1000;

        cur = s;
        cur.hostNs = recvNs;

        if (!havePrev) {
            std::printf("首帧 rawTs = %u (0x%08x)\n", cur.rawTs, cur.rawTs);
            std::printf("%4s  %12s  %12s  %12s  %12s  %12s\n",
                        "idx", "rawTs", "dTs", "dHost(us)", "dTs/dHost", "rtt(us)");
            std::printf("%4d  %12u  %12s  %12s  %12s  %12lld\n",
                        i, cur.rawTs, "-", "-", "-", static_cast<long long>(roundTripUs));
        } else {
            const quint32 dTs = static_cast<quint32>(cur.rawTs - prev.rawTs);
            const qint64 dHostUs = (cur.hostNs - prev.hostNs) / 1000;
            const double ratio = dHostUs > 0 ? static_cast<double>(dTs) / dHostUs : 0.0;
            if (cur.rawTs < prev.rawTs)
                monotonic = false;
            std::printf("%4d  %12u  %12u  %12lld  %12.4f  %12lld\n",
                        i, cur.rawTs, dTs, static_cast<long long>(dHostUs), ratio,
                        static_cast<long long>(roundTripUs));
            ++got;
        }
        prev = cur;
        havePrev = true;

        // 发送到下一次发送之间等待 gapMs（回显接收已占用一部分时间）
        const qint64 elapsedMs = (clock.nsecsElapsed() - recvNs) / 1000000;
        const qint64 remain = gapMs - elapsedMs;
        if (remain > 0)
            QThread::msleep(static_cast<unsigned long>(remain));
    }

    std::printf("\n---- 结论 ----\n");
    std::printf("有效样本数      : %d\n", got);
    std::printf("原始时间戳单调  : %s\n", monotonic ? "是" : "否(发生回绕或乱序)");
    std::printf("观察到错误帧    : %s\n", sawError ? "是" : "否");

    // 跨通道时钟对比：清空两通道后，ch1 单独发一帧取 ts，再 ch0 单独发一帧取 ts。
    // 两帧相隔仅一次往返(~ms)，共用时钟则 ts 差值很小；独立时钟则差值任意。
    if (ch1Ok) {
        const quint32 id0 = 0x7A0;
        const quint32 id1 = 0x7B0;
        Sample s0;
        Sample s1;
        bool ok0 = false;
        bool ok1 = false;

        ZCAN_20_MSG m;
        std::memset(&m, 0, sizeof(m));
        m.hdr.inf.txm = ZCAN_SR_NORM;
        m.hdr.inf.echo = 1;
        m.hdr.len = 1;

        dev.clearBuffer(0);
        dev.clearBuffer(1);

        m.hdr.chn = 1;
        m.hdr.id = id1;
        if (dev.transmitCan(1, m)) {
            ZCAN_20_MSG rbuf[64];
            for (int tries = 0; tries < 40 && !ok1; ++tries) {
                const int rc = dev.receiveCan(1, rbuf, 64, 5);
                for (int k = 0; k < rc; ++k)
                    if ((rbuf[k].hdr.id & 0x7FFu) == id1) { s1.rawTs = rbuf[k].hdr.ts; ok1 = true; }
                QCoreApplication::processEvents();
            }
        }

        m.hdr.chn = 0;
        m.hdr.id = id0;
        if (dev.transmitCan(0, m)) {
            ZCAN_20_MSG rbuf[64];
            for (int tries = 0; tries < 40 && !ok0; ++tries) {
                const int rc = dev.receiveCan(0, rbuf, 64, 5);
                for (int k = 0; k < rc; ++k)
                    if ((rbuf[k].hdr.id & 0x7FFu) == id0) { s0.rawTs = rbuf[k].hdr.ts; ok0 = true; }
                QCoreApplication::processEvents();
            }
        }

        if (ok0 && ok1) {
            const quint32 diff = static_cast<quint32>(s0.rawTs - s1.rawTs);
            std::printf("\n跨通道时钟: ch1=%u (先发)  ch0=%u (后发)  后-前=%u ticks (~%lld us)\n",
                        s1.rawTs, s0.rawTs, diff, static_cast<long long>(diff) * 100);
            std::printf("  差值小(<几十ms) -> 两通道共用同一时钟；差值巨大 -> 各通道独立时钟\n");
        } else {
            std::printf("\n跨通道时钟: 采样失败(ch0=%d ch1=%d)\n", ok0 ? 1 : 0, ok1 ? 1 : 0);
        }
    }

    std::printf("提示：结合 dTs/dHost 比值判断单位。\n");
    std::printf("  ratio≈1 -> tick=1us ; ratio≈0.1 -> tick=100us ; ratio≈0.001 -> tick=1ms\n");

    dev.resetChannel(0);
    if (ch1Ok)
        dev.resetChannel(1);
    dev.close();
    return 0;
}
