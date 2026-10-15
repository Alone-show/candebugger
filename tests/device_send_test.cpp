// 步骤 4/5/16 实机验证：单帧发送(自发自收) + 软件定时发送 + 快捷周期发送
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEventLoop>
#include <QSet>
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

int countWithId(const QAbstractItemModel *model, const QString &idText)
{
    int n = 0;
    if (!model)
        return 0;
    for (int r = 0; r < model->rowCount(); ++r) {
        if (model->data(model->index(r, 0), FrameListModel::IdRole).toString() == idText)
            ++n;
    }
    return n;
}

// 落在 [lo, hi] 区间内的 ID（Hex 文本形式），用于验证 ID 递增
QSet<QString> idsWithPrefix(const QAbstractItemModel *model, quint32 lo, quint32 hi)
{
    QSet<QString> out;
    if (!model)
        return out;
    for (int r = 0; r < model->rowCount(); ++r) {
        const QModelIndex idx = model->index(r, 0);
        const QString idText = model->data(idx, FrameListModel::IdRole).toString();
        bool ok = false;
        const quint32 v = idText.startsWith(QStringLiteral("0x"))
                              ? idText.mid(2).toUInt(&ok, 16)
                              : idText.toUInt(&ok, 16);
        if (ok && v >= lo && v <= hi)
            out.insert(idText);
    }
    return out;
}

// 该 ID 的帧数据的「首字节」集合（Hex 文本的首个 token），用于验证滚动计数逐帧递增
QSet<QString> firstBytesWithId(const QAbstractItemModel *model, const QString &idText)
{
    QSet<QString> out;
    if (!model)
        return out;
    for (int r = 0; r < model->rowCount(); ++r) {
        const QModelIndex idx = model->index(r, 0);
        if (model->data(idx, FrameListModel::IdRole).toString() != idText)
            continue;
        const QString data = model->data(idx, FrameListModel::DataRole).toString().trimmed();
        if (!data.isEmpty())
            out.insert(data.section(QLatin1Char(' '), 0, 0));
    }
    return out;
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

    // ---- 快捷周期发送（单帧面板的「快捷周期发送」槽位）----
    // 与周期表同通道同时运行、互不影响；勾「滚动计数 + ID 递增」时首字节与 ID 同步逐帧推进
    QVariantMap quick = e0;
    quick.insert(QStringLiteral("id"), 0x600u);
    quick.insert(QStringLiteral("data"), QStringLiteral("00 00 00 00"));
    quick.insert(QStringLiteral("rolling"), true);
    quick.insert(QStringLiteral("incrementId"), true);

    const bool tableRestarted = ps.apply(0, entries) && ps.start(0);
    const bool quickStarted = ps.startQuick(0, quick, 100, 0);
    wait(600);

    const int quickFrames = countWithId(model, QStringLiteral("0x600"));
    const QSet<QString> firstBytes = firstBytesWithId(model, QStringLiteral("0x600"));
    const QSet<QString> quickIds = idsWithPrefix(model, 0x600, 0x60F);   // 0x600..0x60F
    const bool quickStopped = ps.stopQuick(0);
    const bool tableStillActive = ps.activeFor(0);   // 停快捷槽不应停周期表
    const int tableBeforeQuickStop = countWithId(model, QStringLiteral("0x500"));
    wait(300);
    const int tableAfterQuickStop = countWithId(model, QStringLiteral("0x500"));
    ps.stop(0);

    const bool rollingOk = firstBytes.size() >= 2;   // 滚动计数：0,1,2… 至少出现两个不同值
    const bool idIncrOk = quickIds.size() >= 2;      // ID 递增：至少出现两个不同 ID
    std::printf("快捷周期: 启动=%s 停止=%s 自收 %d 帧(>1 期望)\n",
                quickStarted ? "成功" : "失败", quickStopped ? "成功" : "失败", quickFrames);
    std::printf("快捷周期滚动计数: 首字节去重 %d 个(>=2 期望) 例: %s\n", firstBytes.size(),
                qPrintable(QStringList(firstBytes.begin(), firstBytes.end()).join(QLatin1Char(','))));
    std::printf("快捷周期ID递增: ID 去重 %d 个(>=2 期望) 例: %s\n", quickIds.size(),
                qPrintable(QStringList(quickIds.begin(), quickIds.end()).join(QLatin1Char(','))));
    std::printf("与周期表互不干扰: 表重启=%s 停快捷后表仍在跑=%s 表帧 %d→%d\n",
                tableRestarted ? "成功" : "失败", tableStillActive ? "是" : "否",
                tableBeforeQuickStop, tableAfterQuickStop);

    mgr.stop();
    dev.resetChannel(0);
    dev.close();

    const bool ok = canOk && fdOk && canSeen && fdSeen && applied && started && stopped
                    && (after - before) > 0 && periodicSeen && quickStarted && quickStopped
                    && quickFrames > 1 && rollingOk && idIncrOk && tableStillActive
                    && tableAfterQuickStop > tableBeforeQuickStop;
    std::printf("结论: %s\n", ok ? "通过" : "未通过");
    return ok ? 0 : 6;
}
