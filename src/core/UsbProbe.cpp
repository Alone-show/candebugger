#include "core/UsbProbe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace UsbProbe {
namespace {

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromLatin1(f.readAll()).trimmed();
}

bool parseHex16(const QString &text, quint16 *out)
{
    if (!out || text.isEmpty())
        return false;
    bool ok = false;
    const uint v = text.toUInt(&ok, 16);
    if (!ok || v > 0xFFFFu)
        return false;
    *out = static_cast<quint16>(v);
    return true;
}

bool parseDec16(const QString &text, quint16 *out)
{
    if (!out || text.isEmpty())
        return false;
    bool ok = false;
    const uint v = text.toUInt(&ok, 10);
    if (!ok || v > 0xFFFFu)
        return false;
    *out = static_cast<quint16>(v);
    return true;
}

QString hexId(quint16 v)
{
    return QStringLiteral("%1").arg(v, 4, 16, QLatin1Char('0'));
}

// devtmpfs 的设备节点路径（libusb 也是按这个路径打开 usbfs 节点）
QString nodePath(const QString &devRoot, quint16 bus, quint16 dev)
{
    return QStringLiteral("%1/bus/usb/%2/%3")
        .arg(devRoot)
        .arg(bus, 3, 10, QLatin1Char('0'))
        .arg(dev, 3, 10, QLatin1Char('0'));
}

// 设备节点对当前进程是否可读写。节点不存在（devtmpfs 未挂载）也视为不可访问：
// 对用户来说修复动作与「无权限」一致（装 udev 规则 / 重新插拔）。
bool nodeAccessible(const QString &node)
{
#if defined(Q_OS_LINUX)
    return ::access(QFile::encodeName(node).constData(), R_OK | W_OK) == 0;
#else
    Q_UNUSED(node);
    return false;
#endif
}

QStringList dedup(const QStringList &in)
{
    QStringList out;
    for (const QString &s : in) {
        if (!out.contains(s))
            out << s;
    }
    return out;
}

// 扫 /proc/<pid>/fd 反查谁持有设备节点。libusb 会话全程持有该 fd，因此能精确定位占用进程。
void collectHolders(const QString &procRoot, const QStringList &nodes,
                    QStringList *others, bool *self)
{
    if (!others || nodes.isEmpty())
        return;

    const qint64 selfPid = QCoreApplication::applicationPid();
    QDir proc(procRoot);
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
    for (const QString &pidName : pids) {
        bool ok = false;
        const qint64 pid = pidName.toLongLong(&ok);
        if (!ok)
            continue;   // 非数字目录（/proc 下的 net、sys 等）

        QDir fdDir(proc.filePath(pidName + QStringLiteral("/fd")));
        const QStringList fds = fdDir.entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
        for (const QString &fd : fds) {
            const QString target = QFileInfo(fdDir.filePath(fd)).symLinkTarget();
            if (target.isEmpty() || !nodes.contains(target))
                continue;
            if (pid == selfPid) {
                if (self)
                    *self = true;
            } else {
                QString comm = readText(proc.filePath(pidName + QStringLiteral("/comm")));
                if (comm.isEmpty())
                    comm = QStringLiteral("pid");
                const QString label = QStringLiteral("%1(%2)").arg(comm).arg(pid);
                if (!others->contains(label))
                    *others << label;
            }
            break;   // 一个进程持有多个 fd 只报一次
        }
    }
}

} // namespace

Result probe(quint16 vid, quint16 pid, const QString &sysRoot,
             const QString &devRoot, const QString &procRoot)
{
    Result r;
#if defined(Q_OS_LINUX)
    QDir dir(sysRoot + QStringLiteral("/bus/usb/devices"));
    if (!dir.exists()) {
        r.presence = Presence::Unsupported;
        r.detail = QStringLiteral("sysfs 不可用：%1 不存在").arg(dir.absolutePath());
        return r;
    }

    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
    QStringList otherPids;
    for (const QString &name : entries) {
        const QString base = dir.filePath(name);
        quint16 v = 0;
        quint16 p = 0;
        // 接口节点(1-1:1.0)没有 idVendor/idProduct，直接跳过
        if (!parseHex16(readText(base + QStringLiteral("/idVendor")), &v))
            continue;
        if (!parseHex16(readText(base + QStringLiteral("/idProduct")), &p))
            continue;
        if (v != vid)
            continue;

        ++r.zlgTotal;
        if (p != pid) {
            otherPids << QStringLiteral("%1:%2").arg(hexId(v), hexId(p));
            continue;
        }

        ++r.matched;
        quint16 bus = 0;
        quint16 dev = 0;
        if (parseDec16(readText(base + QStringLiteral("/busnum")), &bus)
            && parseDec16(readText(base + QStringLiteral("/devnum")), &dev))
            r.nodes << nodePath(devRoot, bus, dev);
        else
            r.detail += QStringLiteral("结点 %1 缺少 busnum/devnum；").arg(name);
    }
    r.otherPids = dedup(otherPids);

    if (r.matched == 0) {
        r.presence = r.zlgTotal > 0 ? Presence::WrongModel : Presence::Absent;
        return r;
    }

    QStringList accessible;
    for (const QString &node : r.nodes) {
        if (nodeAccessible(node))
            accessible << node;
    }
    if (accessible.isEmpty()) {
        r.presence = Presence::AccessDenied;
        return r;
    }

    collectHolders(procRoot, accessible, &r.holders, &r.heldBySelf);
    r.presence = r.holders.isEmpty() ? Presence::Present : Presence::Busy;
    return r;
#else
    Q_UNUSED(vid);
    Q_UNUSED(pid);
    Q_UNUSED(sysRoot);
    Q_UNUSED(devRoot);
    Q_UNUSED(procRoot);
    r.presence = Presence::Unsupported;
    r.detail = QStringLiteral("非 Linux 平台，无 sysfs 探测");
    return r;
#endif
}

QString describe(const Result &r, int cardIndex)
{
    // 多设备 + 索引越界：原因与设备本身无关，单独报出
    if (r.matched > 0 && cardIndex >= r.matched) {
        return QStringLiteral("检测到 %1 个设备，但请求的卡索引 %2 越界（有效 0..%3）")
            .arg(r.matched)
            .arg(cardIndex)
            .arg(r.matched - 1);
    }

    QString text;
    switch (r.presence) {
    case Presence::Absent:
        text = QStringLiteral("未检测到 USBCANFD 设备（3068:0009）。"
                              "请检查 USB 线缆与供电，确认设备已上电后重新插拔");
        break;
    case Presence::WrongModel:
        text = QStringLiteral("检测到 ZLG 设备 %1，但本程序需要 3068:0009，型号不符")
                   .arg(r.otherPids.join(QStringLiteral("、")));
        break;
    case Presence::AccessDenied: {
        const QString where = r.nodes.isEmpty()
            ? QStringLiteral("已检测到设备")
            : QStringLiteral("已检测到设备（节点 %1）").arg(r.nodes.first());
        text = where + QStringLiteral("，但当前用户无法访问该设备节点。"
                                      "请安装 udev 规则：sudo cp udev/50-usbcanfd.rules /etc/udev/rules.d/ "
                                      "&& sudo udevadm control --reload && sudo udevadm trigger，然后重新插拔设备");
        break;
    }
    case Presence::Busy:
        text = QStringLiteral("设备已被其它程序占用：%1。请关闭后重试")
                   .arg(r.holders.join(QStringLiteral("、")));
        break;
    case Presence::Present:
        if (r.heldBySelf) {
            text = QStringLiteral("已检测到设备但打开失败：本程序可能仍持有设备句柄（上次打开未释放），"
                                  "请重启程序或重新插拔设备");
        } else {
            text = QStringLiteral("已检测到设备但打开失败（可能固件/驱动未就绪），"
                                  "请尝试重新插拔设备或重启系统");
        }
        break;
    case Presence::Unsupported:
        // 无法判定时退回原有文案，不误导用户
        text = QStringLiteral("请检查 USB 权限（需 udev 规则）或设备是否被占用");
        break;
    }

    if (r.matched > 1)
        text += QStringLiteral("（共检测到 %1 个设备）").arg(r.matched);
    return text;
}

} // namespace UsbProbe
