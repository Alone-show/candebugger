#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "core/UsbProbe.h"

#if defined(Q_OS_LINUX)
#include <unistd.h>
#endif

// USB 探测的判定矩阵（无设备、纯文件系统逻辑）：
//   没插 → Absent / 型号不符 → WrongModel / 节点不可访问 → AccessDenied
//   → 被其它进程持有 → Busy / 可访问但 open 失败 → Present / 无法判定 → Unsupported
// 全部用假 sysfs/dev/proc 树驱动，不接触真实设备。
class TestUsbProbe : public QObject
{
    Q_OBJECT

private:
    // 与 UsbProbe 内部的节点路径规则保持一致
    static QString nodePath(const QString &devRoot, int bus, int dev)
    {
        return QStringLiteral("%1/bus/usb/%2/%3")
            .arg(devRoot)
            .arg(bus, 3, 10, QLatin1Char('0'))
            .arg(dev, 3, 10, QLatin1Char('0'));
    }

    static bool writeFile(const QString &path, const QString &text)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(text.toLatin1());
        return true;
    }

    // 造一个 sysfs 设备条目；createNode 为假时故意不建 /dev 节点（模拟无权限/无 devtmpfs）
    static bool addDevice(const QString &sysRoot, const QString &name,
                          quint16 vid, quint16 pid, int bus, int dev,
                          const QString &devRoot, bool createNode,
                          QFileDevice::Permissions perms
                              = QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                | QFileDevice::ReadGroup | QFileDevice::WriteGroup
                                | QFileDevice::ReadOther | QFileDevice::WriteOther)
    {
        const QString base = sysRoot + QStringLiteral("/bus/usb/devices/") + name;
        if (!writeFile(base + QStringLiteral("/idVendor"),
                       QStringLiteral("%1\n").arg(vid, 4, 16, QLatin1Char('0')))
            || !writeFile(base + QStringLiteral("/idProduct"),
                          QStringLiteral("%1\n").arg(pid, 4, 16, QLatin1Char('0')))
            || !writeFile(base + QStringLiteral("/busnum"), QString::number(bus) + QStringLiteral("\n"))
            || !writeFile(base + QStringLiteral("/devnum"), QString::number(dev) + QStringLiteral("\n")))
            return false;

        if (!createNode)
            return true;

        const QString node = nodePath(devRoot, bus, dev);
        QDir().mkpath(QFileInfo(node).absolutePath());
        QFile f(node);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.close();
        return f.setPermissions(perms);
    }

    // 造一个持有设备节点的进程（/proc/<pid>/fd/N -> 设备节点）
    static bool addHolder(const QString &procRoot, qint64 pid,
                          const QString &comm, const QStringList &nodes)
    {
        const QString dir = procRoot + QStringLiteral("/") + QString::number(pid);
        if (!writeFile(dir + QStringLiteral("/comm"), comm + QStringLiteral("\n")))
            return false;
        if (!QDir().mkpath(dir + QStringLiteral("/fd")))
            return false;
        int fd = 3;
        for (const QString &node : nodes) {
            if (!QFile::link(node, dir + QStringLiteral("/fd/") + QString::number(fd)))
                return false;
            ++fd;
        }
        return true;
    }

private slots:
    void emptyTreeIsAbsent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(QDir().mkpath(tmp.filePath(QStringLiteral("sys/bus/usb/devices"))));

        const auto r = UsbProbe::probe(0x3068, 0x0009, tmp.filePath(QStringLiteral("sys")),
                                      tmp.filePath(QStringLiteral("dev")),
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.presence, UsbProbe::Presence::Absent);
        QCOMPARE(r.matched, 0);
        QVERIFY(UsbProbe::describe(r).contains(QStringLiteral("未检测到")));
    }

    void nonZlgDevicesAreIgnored()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        // 根集线器 + 一个普通外设：都与 ZLG 无关
        QVERIFY(addDevice(sys, QStringLiteral("usb1"), 0x1d6b, 0x0002, 1, 1,
                          tmp.filePath(QStringLiteral("dev")), true));
        QVERIFY(addDevice(sys, QStringLiteral("1-2"), 0x413c, 0x301a, 1, 5,
                          tmp.filePath(QStringLiteral("dev")), true));
        // 接口节点没有 idVendor/idProduct，不应被计数
        QVERIFY(QDir().mkpath(sys + QStringLiteral("/bus/usb/devices/1-2:1.0")));

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, tmp.filePath(QStringLiteral("dev")),
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.presence, UsbProbe::Presence::Absent);
        QCOMPARE(r.zlgTotal, 0);
    }

    void otherZlgPidIsWrongModel()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        QVERIFY(addDevice(sys, QStringLiteral("1-5"), 0x3068, 0x0001, 1, 5,
                          tmp.filePath(QStringLiteral("dev")), true));

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, tmp.filePath(QStringLiteral("dev")),
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.presence, UsbProbe::Presence::WrongModel);
        QCOMPARE(r.matched, 0);
        QCOMPARE(r.zlgTotal, 1);
        QCOMPARE(r.otherPids, QStringList{QStringLiteral("3068:0001")});
        QVERIFY(UsbProbe::describe(r).contains(QStringLiteral("型号不符")));
    }

    void multipleOtherZlgModelsAreAllListed()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        QVERIFY(addDevice(sys, QStringLiteral("1-5"), 0x3068, 0x0001, 1, 5,
                          tmp.filePath(QStringLiteral("dev")), true));
        QVERIFY(addDevice(sys, QStringLiteral("1-6"), 0x3068, 0x0002, 1, 6,
                          tmp.filePath(QStringLiteral("dev")), true));
        QVERIFY(addDevice(sys, QStringLiteral("1-7"), 0x3068, 0x0002, 1, 7,
                          tmp.filePath(QStringLiteral("dev")), true));

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, tmp.filePath(QStringLiteral("dev")),
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.presence, UsbProbe::Presence::WrongModel);
        QCOMPARE(r.zlgTotal, 3);
        // 同型号去重、不同型号全部列出
        QCOMPARE(r.otherPids, QStringList({QStringLiteral("3068:0001"), QStringLiteral("3068:0002")}));
        const QString msg = UsbProbe::describe(r);
        QVERIFY(msg.contains(QStringLiteral("3068:0001")));
        QVERIFY(msg.contains(QStringLiteral("3068:0002")));
    }

    void missingNodeIsAccessDenied()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        // sysfs 有设备，但 /dev 节点不存在（无权限 / devtmpfs 未挂载）
        QVERIFY(addDevice(sys, QStringLiteral("1-5"), 0x3068, 0x0009, 1, 5,
                          tmp.filePath(QStringLiteral("dev")), false));

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, tmp.filePath(QStringLiteral("dev")),
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.presence, UsbProbe::Presence::AccessDenied);
        QCOMPARE(r.matched, 1);
        QCOMPARE(r.nodes, QStringList{nodePath(tmp.filePath(QStringLiteral("dev")), 1, 5)});
        QVERIFY(UsbProbe::describe(r).contains(QStringLiteral("udev 规则")));
        // 文案里要给出具体节点，便于排查
        QVERIFY(UsbProbe::describe(r).contains(QStringLiteral("/dev/bus/usb/001/005")));
    }

    void unreadableNodeIsAccessDenied()
    {
#if defined(Q_OS_LINUX)
        if (::geteuid() == 0)
            QSKIP("以 root 运行时无法构造权限不足场景");
#endif
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        QVERIFY(addDevice(sys, QStringLiteral("1-5"), 0x3068, 0x0009, 1, 5,
                          tmp.filePath(QStringLiteral("dev")), true,
                          QFileDevice::Permissions()));   // chmod 0000

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, tmp.filePath(QStringLiteral("dev")),
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.presence, UsbProbe::Presence::AccessDenied);
    }

    void presentWhenAccessibleAndFree()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        QVERIFY(addDevice(sys, QStringLiteral("1-5"), 0x3068, 0x0009, 1, 5,
                          tmp.filePath(QStringLiteral("dev")), true));

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, tmp.filePath(QStringLiteral("dev")),
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.presence, UsbProbe::Presence::Present);
        QVERIFY(r.holders.isEmpty());
        QVERIFY(!r.heldBySelf);
        QVERIFY(UsbProbe::describe(r).contains(QStringLiteral("固件")));
    }

    void otherProcessHolderIsBusy()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        const QString dev = tmp.filePath(QStringLiteral("dev"));
        QVERIFY(addDevice(sys, QStringLiteral("1-5"), 0x3068, 0x0009, 1, 5, dev, true));
        QVERIFY(addHolder(tmp.filePath(QStringLiteral("proc")), 4242, QStringLiteral("cam_dbg"),
                          QStringList{nodePath(dev, 1, 5)}));

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, dev,
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.presence, UsbProbe::Presence::Busy);
        QCOMPARE(r.holders, QStringList{QStringLiteral("cam_dbg(4242)")});
        QVERIFY(!r.heldBySelf);
        // 文案必须点名占用者，用户才知道关谁
        QVERIFY(UsbProbe::describe(r).contains(QStringLiteral("cam_dbg(4242)")));
    }

    void selfHolderIsNotReportedAsOccupier()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        const QString dev = tmp.filePath(QStringLiteral("dev"));
        const QString proc = tmp.filePath(QStringLiteral("proc"));
        QVERIFY(addDevice(sys, QStringLiteral("1-5"), 0x3068, 0x0009, 1, 5, dev, true));
        // 本进程 + 另一进程同时持有
        QVERIFY(addHolder(proc, 4242, QStringLiteral("cam_dbg"), QStringList{nodePath(dev, 1, 5)}));
        QVERIFY(addHolder(proc, QCoreApplication::applicationPid(), QStringLiteral("CanDebugger"),
                          QStringList{nodePath(dev, 1, 5)}));

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, dev, proc);
        QCOMPARE(r.presence, UsbProbe::Presence::Busy);
        QVERIFY(r.heldBySelf);
        QCOMPARE(r.holders, QStringList{QStringLiteral("cam_dbg(4242)")});
        // 不能把本程序自己列为「占用者」
        QVERIFY(!UsbProbe::describe(r).contains(QStringLiteral("CanDebugger")));
    }

    void selfOnlyHolderHintsLeakedHandle()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        const QString dev = tmp.filePath(QStringLiteral("dev"));
        const QString proc = tmp.filePath(QStringLiteral("proc"));
        QVERIFY(addDevice(sys, QStringLiteral("1-5"), 0x3068, 0x0009, 1, 5, dev, true));
        QVERIFY(addHolder(proc, QCoreApplication::applicationPid(), QStringLiteral("CanDebugger"),
                          QStringList{nodePath(dev, 1, 5)}));

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, dev, proc);
        QCOMPARE(r.presence, UsbProbe::Presence::Present);
        QVERIFY(r.heldBySelf);
        QVERIFY(r.holders.isEmpty());
        QVERIFY(UsbProbe::describe(r).contains(QStringLiteral("本程序可能仍持有")));
    }

    void multipleDevicesReportCountAndIndex()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        const QString dev = tmp.filePath(QStringLiteral("dev"));
        QVERIFY(addDevice(sys, QStringLiteral("1-5"), 0x3068, 0x0009, 1, 5, dev, true));
        QVERIFY(addDevice(sys, QStringLiteral("2-3"), 0x3068, 0x0009, 2, 3, dev, true));

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, dev,
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.matched, 2);
        QCOMPARE(r.nodes.size(), 2);
        QVERIFY(UsbProbe::describe(r, 0).contains(QStringLiteral("共检测到 2 个设备")));
        QVERIFY(UsbProbe::describe(r, 7).contains(QStringLiteral("越界")));
    }

    void unsupportedFallsBackToLegacyText()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // sysfs 不存在（非 Linux 或未挂载）→ 保持原有兜底文案，不误导
        const auto r = UsbProbe::probe(0x3068, 0x0009,
                                      tmp.filePath(QStringLiteral("nonexistent-sys")),
                                      tmp.filePath(QStringLiteral("dev")),
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.presence, UsbProbe::Presence::Unsupported);
        QCOMPARE(UsbProbe::describe(r),
                 QStringLiteral("请检查 USB 权限（需 udev 规则）或设备是否被占用"));
    }

    void missingBusnumDoesNotCountAsNode()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sys = tmp.filePath(QStringLiteral("sys"));
        const QString base = sys + QStringLiteral("/bus/usb/devices/1-5");
        QVERIFY(writeFile(base + QStringLiteral("/idVendor"), QStringLiteral("3068\n")));
        QVERIFY(writeFile(base + QStringLiteral("/idProduct"), QStringLiteral("0009\n")));
        // 故意不写 busnum/devnum：设备算存在，但拿不到节点 → 视为不可访问

        const auto r = UsbProbe::probe(0x3068, 0x0009, sys, tmp.filePath(QStringLiteral("dev")),
                                      tmp.filePath(QStringLiteral("proc")));
        QCOMPARE(r.matched, 1);
        QVERIFY(r.nodes.isEmpty());
        QCOMPARE(r.presence, UsbProbe::Presence::AccessDenied);
        // 节点未知时不能打印占位符
        QVERIFY(!UsbProbe::describe(r).contains(QStringLiteral("?")));
    }
};

QTEST_MAIN(TestUsbProbe)
#include "test_usbprobe.moc"
