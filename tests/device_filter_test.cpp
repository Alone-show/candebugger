// 步骤 6 实机验证：硬件验收滤波（CMD_CAN_FILTER）+ 软件模型滤波 + 统计联动
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <cstdio>
#include <cstring>

#include "core/CanDevice.h"
#include "core/CanFilter.h"
#include "core/RxManager.h"
#include "core/Statistics.h"
#include "model/FrameListModel.h"

namespace {

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool sendStd(CanDevice &dev, quint32 id)
{
    ZCAN_20_MSG tx;
    std::memset(&tx, 0, sizeof(tx));
    tx.hdr.inf.txm = ZCAN_SR_NORM;   // 自发自收
    tx.hdr.inf.echo = 1;
    tx.hdr.id = id;
    tx.hdr.chn = 0;
    tx.hdr.len = 2;
    tx.dat[0] = static_cast<quint8>(id & 0xFF);
    tx.dat[1] = 0x5A;
    return dev.transmitCan(0, tx);
}

int countId(QAbstractItemModel *model, const QString &idText)
{
    int n = 0;
    if (!model)
        return -1;
    for (int r = 0; r < model->rowCount(); ++r) {
        if (model->data(model->index(r, 0), FrameListModel::IdRole).toString() == idText)
            ++n;
    }
    return n;
}

// 统计某 ID 中 发送方向(tx) 与 总线接收(tx=0) 的数量：
// 硬件验收滤波对总线接收生效，本机发送回环(tx=1) 不受其影响
int countIdTx(QAbstractItemModel *model, const QString &idText, bool tx)
{
    int n = 0;
    if (!model)
        return -1;
    for (int r = 0; r < model->rowCount(); ++r) {
        const QModelIndex idx = model->index(r, 0);
        if (model->data(idx, FrameListModel::IdRole).toString() != idText)
            continue;
        if (model->data(idx, FrameListModel::IsTxRole).toBool() == tx)
            ++n;
    }
    return n;
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

    RxManager mgr(&dev);
    Statistics stats(&dev);
    mgr.setStatistics(&stats);
    if (!mgr.start(QVariantList{0}, false)) {
        std::fprintf(stderr, "接收管线启动失败\n");
        dev.close();
        return 5;
    }

    QAbstractItemModel *model = mgr.frameModel();
    wait(200);
    dev.clearBuffer(0);
    mgr.clear();

    // ---- A. 基线：三个 ID 自收均可见 ----
    sendStd(dev, 0x100);
    sendStd(dev, 0x200);
    sendStd(dev, 0x300);
    wait(400);

    const int base100 = countId(model, QStringLiteral("0x100"));
    const int base200 = countId(model, QStringLiteral("0x200"));
    const int base300 = countId(model, QStringLiteral("0x300"));
    std::printf("A 基线自收: 0x100=%d 0x200=%d 0x300=%d\n", base100, base200, base300);
    const bool baseline = base100 > 0 && base200 > 0 && base300 > 0;

    // ---- B. 硬件滤波：仅接收 0x200 ----
    QVariantMap rule;
    rule.insert(QStringLiteral("extended"), false);
    rule.insert(QStringLiteral("start"), 0x200);
    rule.insert(QStringLiteral("end"), 0x200);
    const bool hwApplied = mgr.filter()->applyHardware(0, QVariantList{rule});
    wait(150);
    dev.clearBuffer(0);   // 丢弃滤波前已进入设备缓冲的帧
    mgr.clear();

    sendStd(dev, 0x100);
    sendStd(dev, 0x200);
    sendStd(dev, 0x300);
    wait(400);

    const int hw100 = countId(model, QStringLiteral("0x100"));
    const int hw200 = countId(model, QStringLiteral("0x200"));
    const int hw300 = countId(model, QStringLiteral("0x300"));
    const int hw100bus = countIdTx(model, QStringLiteral("0x100"), false);
    const int hw200bus = countIdTx(model, QStringLiteral("0x200"), false);
    const int hw300bus = countIdTx(model, QStringLiteral("0x300"), false);
    std::printf("B 硬件滤波(仅0x200): 下发=%s 0x100=%d(总线%d 回环%d) 0x200=%d(总线%d) 0x300=%d(总线%d 回环%d)\n",
                hwApplied ? "成功" : "失败",
                hw100, hw100bus, hw100 - hw100bus,
                hw200, hw200bus,
                hw300, hw300bus, hw300 - hw300bus);
    // 验收：不匹配 ID 的总线接收帧被硬件滤除（本机发送回环不受影响）
    const bool hwOnly200 = hwApplied && hw200bus > 0 && hw100bus == 0 && hw300bus == 0;

    // ---- C. 清除硬件滤波后恢复接收 ----
    const bool hwCleared = mgr.filter()->clearHardware(0);
    wait(150);
    dev.clearBuffer(0);
    mgr.clear();
    sendStd(dev, 0x100);
    wait(300);
    const int restored100 = countId(model, QStringLiteral("0x100"));
    std::printf("C 清除硬件滤波: 清除=%s 0x100=%d\n",
                hwCleared ? "成功" : "失败", restored100);
    const bool hwRestored = hwCleared && restored100 > 0;

    // ---- D. 软件模型滤波：仅显示 0x300 ----
    mgr.filter()->setIdEnabled(true);
    mgr.filter()->setIdExtended(false);
    mgr.filter()->setIdMin(0x300);
    mgr.filter()->setIdMax(0x300);
    wait(100);
    dev.clearBuffer(0);
    mgr.clear();

    sendStd(dev, 0x100);
    sendStd(dev, 0x200);
    sendStd(dev, 0x300);
    wait(400);

    const int sw100 = countId(model, QStringLiteral("0x100"));
    const int sw200 = countId(model, QStringLiteral("0x200"));
    const int sw300 = countId(model, QStringLiteral("0x300"));
    const int shown = model ? model->rowCount() : -1;
    std::printf("D 软件滤波(仅0x300): 0x100=%d 0x200=%d 0x300=%d 显示行=%d\n",
                sw100, sw200, sw300, shown);
    const bool swOnly300 = sw300 > 0 && sw100 == 0 && sw200 == 0;

    // ---- E. 统计联动 ----
    std::printf("E 统计: 总帧=%llu 收=%llu 发=%llu 错误=%llu\n",
                static_cast<unsigned long long>(stats.totalFrames()),
                static_cast<unsigned long long>(stats.rxCount()),
                static_cast<unsigned long long>(stats.txCount()),
                static_cast<unsigned long long>(stats.errorFrames()));
    const bool statsSeen = stats.totalFrames() > 0;

    mgr.stop();
    dev.resetChannel(0);
    dev.close();

    const bool ok = baseline && hwOnly200 && hwRestored && swOnly300 && statsSeen;
    std::printf("结论: %s\n", ok ? "通过" : "未通过");
    return ok ? 0 : 6;
}
