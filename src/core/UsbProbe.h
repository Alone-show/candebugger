#pragma once

#include <QString>
#include <QStringList>

// USB 存在性探测：在「打开设备失败」时区分「没插 / 型号不符 / 没权限 / 被占用 / 固件未就绪」，
// 避免把四种原因糊成一句「权限或占用」的猜谜文案。
//
// 实现只读 sysfs(/proc)，**不打开设备**：另一进程已占用设备时照样能探测，也不会产生副作用。
// 因此它只在失败分支调用（成功路径零开销）。
namespace UsbProbe {

enum class Presence
{
    Absent,         // 完全没有 3068:* 设备（没插 / 线缆 / 供电）
    WrongModel,     // 有 ZLG 设备但 PID 不是本程序支持的值
    AccessDenied,   // 目标设备在 sysfs 中存在，但设备节点不可读写
    Busy,           // 目标设备可访问，但已被其它进程持有（libusb 长期持有 devnode fd）
    Present,        // 目标设备可访问，open 仍失败（固件/驱动未就绪）
    Unsupported,    // 非 Linux / sysfs 不可用，无法判定
};

struct Result
{
    Presence presence = Presence::Unsupported;
    int matched = 0;          // 目标 (vid,pid) 数量
    int zlgTotal = 0;         // vid 命中（含目标）的数量
    QStringList otherPids;    // 其它 ZLG 型号，形如 "3068:0001"
    QStringList nodes;        // 目标设备节点 /dev/bus/usb/BBB/DDD
    QStringList holders;      // 占用者 "comm(pid)"，不含本进程
    bool heldBySelf = false;  // 本进程仍持有设备节点（上次打开未释放）
    QString detail;           // 探测过程说明，便于日志/排查
};

// sysRoot/devRoot/procRoot 可注入，便于用假目录树做单元测试。
Result probe(quint16 vid = 0x3068, quint16 pid = 0x0009,
             const QString &sysRoot = QStringLiteral("/sys"),
             const QString &devRoot = QStringLiteral("/dev"),
             const QString &procRoot = QStringLiteral("/proc"));

// 把探测结果转成面向用户的失败原因与修复动作（不含「打开设备失败：」前缀）。
// cardIndex 为请求的卡索引，用于报出多设备时索引越界。
QString describe(const Result &r, int cardIndex = 0);

} // namespace UsbProbe
