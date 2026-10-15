// 步骤 4/5 实机验证：单帧发送(自发自收) + 硬件定时发送
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>

#include "core/CanDevice.h"
#include "core/FrameBuilder.h"
#include "core/PeriodicSender.h"
#include "core/RxManager.h"
#include "model/FrameListModel.h"

namespace {

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool modelHas(const QAbstractItemModel *model, const QString &idText, const QString &dataText)
{
    for (int r = 0; r < model->rowCount(); ++r) {
        const QModelIndex idx = model->index(r, 0);
        if (model->data(idx, FrameListModel::IdRole).toString() == idText
            && (dataText.isEmpty()
                || model->data(idx, FrameListModel::DataRole).toString() == dataText))
            return true;
    }
    return false;
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

    const QString msg = dev.configureChannel(0, 500000, 2000000, 87.5, false);
    if (msg.isEmpty()) {
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

    RxManager mgr(&dev);
    if (!mgr.start(QVariantList{0}, false)) {
        std::fprintf(stderr, "接收管线启动失败\n");
        dev.close();
        return 5;
    }
    wait(200);
    mgr.clear();

    FrameBuilder fb(&dev);

    // ---- 单帧：标准 CAN 0x123 ----
    QVariantMap canSpec;
    canSpec.insert(QStringLiteral("id"), 0x123u);
    canSpec.insert(QStringLiteral("dlc"), 8);
    canSpec.insert(QStringLiteral("data"), QStringLiteral("01 02 03 04 05 06 07 08"));
    canSpec.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));
    canSpec.insert(QStringLiteral("txm"), static_cast<int>(ZCAN_SR_NORM));
    canSpec.insert(QStringLiteral("echo"), true);
    const bool canOk = fb.sendFrame(0, canSpec);

    // ---- 单帧：扩展 CANFD 0x1ABCDEF0, 12 字节, BRS ----
    QVariantMap fdSpec;
    fdSpec.insert(QStringLiteral("id"), 0x1ABCDEF0u);
    fdSpec.insert(QStringLiteral("extended"), true);
    fdSpec.insert(QStringLiteral("fd"), true);
    fdSpec.insert(QStringLiteral("brs"), true);
    fdSpec.insert(QStringLiteral("dlc"), 12);
    fdSpec.insert(QStringLiteral("data"), QStringLiteral("11 22 33 44 55 66 77 88 99 AA BB CC"));
    fdSpec.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));
    fdSpec.insert(QStringLiteral("txm"), static_cast<int>(ZCAN_SR_NORM));
    fdSpec.insert(QStringLiteral("echo"), true);
    const bool fdOk = fb.sendFrame(0, fdSpec);

    wait(500);

    QAbstractItemModel *model = mgr.frameModel();
    const bool canSeen = model && modelHas(model, QStringLiteral("0x123"),
                                           QStringLiteral("01 02 03 04 05 06 07 08"));
    const bool fdSeen = model && modelHas(model, QStringLiteral("0x1ABCDEF0"),
                                          QStringLiteral("11 22 33 44 55 66 77 88 99 AA BB CC"));
    std::printf("单帧发送: CAN=%s FD=%s\n", canOk ? "成功" : "失败", fdOk ? "成功" : "失败");
    std::printf("单帧自收: CAN 0x123=%s  FD 0x1ABCDEF0=%s\n",
                canSeen ? "收到" : "未收到", fdSeen ? "收到" : "未收到");

    // ---- 定时发送：两条 100ms ----
    QVariantMap e0;
    e0.insert(QStringLiteral("id"), 0x500u);
    e0.insert(QStringLiteral("dlc"), 4);
    e0.insert(QStringLiteral("data"), QStringLiteral("A0 A1 A2 A3"));
    e0.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));
    e0.insert(QStringLiteral("txm"), static_cast<int>(ZCAN_SR_NORM));
    e0.insert(QStringLiteral("echo"), true);
    e0.insert(QStringLiteral("intervalMs"), 100);
    e0.insert(QStringLiteral("repeat"), 0);
    e0.insert(QStringLiteral("enabled"), true);

    QVariantMap e1 = e0;
    e1.insert(QStringLiteral("id"), 0x501u);
    e1.insert(QStringLiteral("data"), QStringLiteral("B0 B1 B2 B3"));

    QVariantList entries{e0, e1};

    PeriodicSender ps(&dev);
    const bool applied = ps.apply(0, entries);
    const int before = model ? model->rowCount() : 0;
    const bool started = ps.start(0);
    wait(700);
    const int after = model ? model->rowCount() : 0;
    const bool stopped = ps.stop(0);
    wait(200);

    const bool periodicSeen = model && modelHas(model, QStringLiteral("0x500"), QString())
                              && modelHas(model, QStringLiteral("0x501"), QString());
    std::printf("定时发送: 下发=%s 启动=%s 停止=%s 增长=%d 帧(>0 期望)\n",
                applied ? "成功" : "失败", started ? "成功" : "失败",
                stopped ? "成功" : "失败", after - before);
    std::printf("定时自收: 0x500=%s 0x501=%s\n",
                (model && modelHas(model, QStringLiteral("0x500"), QString())) ? "收到" : "未收到",
                (model && modelHas(model, QStringLiteral("0x501"), QString())) ? "收到" : "未收到");

    // 单条启停（重下发整表）
    const bool rowToggle = ps.setRowEnabled(0, entries, 0, false);
    std::printf("单条禁用(0x500)重下发: %s\n", rowToggle ? "成功" : "失败");
    ps.stop(0);

    mgr.stop();
    dev.resetChannel(0);
    dev.close();

    const bool ok = canOk && fdOk && canSeen && fdSeen && applied && started && stopped
                    && (after - before) > 0 && periodicSeen;
    std::printf("结论: %s\n", ok ? "通过" : "未通过");
    return ok ? 0 : 6;
}
