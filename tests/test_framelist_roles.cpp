// L1 模型契约测试：FrameListModel 全角色、边界、时间戳与着色
#include <QtTest>
#include <QSignalSpy>

#include "core/FrameData.h"
#include "model/FrameListModel.h"

namespace {

FrameData mk(quint32 id, bool extended = false, bool fd = false, bool brs = false,
             bool remote = false, bool error = false, bool tx = false, bool echo = false,
             int channel = 0, qint64 hostUs = 0)
{
    FrameData f;
    f.id = id;
    f.extended = extended;
    f.fd = fd;
    f.brs = brs;
    f.remote = remote;
    f.error = error;
    f.tx = tx;
    f.echo = echo;
    f.channel = static_cast<quint8>(channel);
    f.len = 2;
    f.data = QByteArray::fromHex("A0B1");
    f.hostUs = hostUs;
    f.wallUs = 1700000000000000LL + hostUs;
    return f;
}

} // namespace

class TestFrameListRoles : public QObject
{
    Q_OBJECT

private slots:
    void roleNamesComplete()
    {
        FrameListModel model;
        const QHash<int, QByteArray> names = model.roleNames();
        QCOMPARE(names.size(), 14);
        QCOMPARE(names.value(FrameListModel::TimeRole), QByteArray("timeText"));
        QCOMPARE(names.value(FrameListModel::SequenceRole), QByteArray("sequence"));
        QCOMPARE(names.value(FrameListModel::ChannelRole), QByteArray("channel"));
        QCOMPARE(names.value(FrameListModel::DirectionRole), QByteArray("direction"));
        QCOMPARE(names.value(FrameListModel::IdRole), QByteArray("idText"));
        QCOMPARE(names.value(FrameListModel::TypeRole), QByteArray("typeText"));
        QCOMPARE(names.value(FrameListModel::FormatRole), QByteArray("formatText"));
        QCOMPARE(names.value(FrameListModel::DlcRole), QByteArray("dlc"));
        QCOMPARE(names.value(FrameListModel::DataRole), QByteArray("dataText"));
        QCOMPARE(names.value(FrameListModel::IsTxRole), QByteArray("isTx"));
        QCOMPARE(names.value(FrameListModel::IsFdRole), QByteArray("isFd"));
        QCOMPARE(names.value(FrameListModel::IsErrorRole), QByteArray("isError"));
        QCOMPARE(names.value(FrameListModel::IsEchoRole), QByteArray("isEcho"));
        QCOMPARE(names.value(FrameListModel::ColorRole), QByteArray("rowColor"));
    }

    void dataAllRoles()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{mk(0x123, false, true, true, false, false, true, true, 1, 0)});

        const QModelIndex idx = model.index(0, 0);
        QVERIFY(idx.isValid());
        QCOMPARE(model.data(idx, FrameListModel::ChannelRole).toInt(), 1);
        QCOMPARE(model.data(idx, FrameListModel::DirectionRole).toString(), QStringLiteral("Tx"));
        QCOMPARE(model.data(idx, FrameListModel::IdRole).toString(), QStringLiteral("0x123"));
        QCOMPARE(model.data(idx, FrameListModel::TypeRole).toString(), QStringLiteral("标准"));
        QCOMPARE(model.data(idx, FrameListModel::FormatRole).toString(), QStringLiteral("FD加速"));
        QCOMPARE(model.data(idx, FrameListModel::DlcRole).toInt(), 2);
        QCOMPARE(model.data(idx, FrameListModel::DataRole).toString(), QStringLiteral("A0 B1"));
        QCOMPARE(model.data(idx, FrameListModel::IsTxRole).toBool(), true);
        QCOMPARE(model.data(idx, FrameListModel::IsFdRole).toBool(), true);
        QCOMPARE(model.data(idx, FrameListModel::IsErrorRole).toBool(), false);
        QCOMPARE(model.data(idx, FrameListModel::IsEchoRole).toBool(), true);
    }

    void formatTextVariants()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{
            mk(0x100),                          // CAN
            mk(0x101, false, true, false),      // FD
            mk(0x102, false, true, true),       // FD加速
            mk(0x103, false, false, false, false, true),  // 错误
            mk(0x104, false, false, false, true),         // 远程(CAN)
        });
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::FormatRole).toString(), QStringLiteral("CAN"));
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::FormatRole).toString(), QStringLiteral("FD"));
        QCOMPARE(model.data(model.index(2, 0), FrameListModel::FormatRole).toString(), QStringLiteral("FD加速"));
        QCOMPARE(model.data(model.index(3, 0), FrameListModel::FormatRole).toString(), QStringLiteral("错误"));
        QCOMPARE(model.data(model.index(4, 0), FrameListModel::DataRole).toString(), QStringLiteral("远程帧"));
        QCOMPARE(model.data(model.index(4, 0), FrameListModel::DirectionRole).toString(), QStringLiteral("Rx"));
    }

    void idMasking()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{
            mk(0x1234, false),       // 标准：截断到 11 位 -> 0x234
            mk(0x1FFFFFFF, true),    // 扩展：保留 29 位
            mk(0xFFFFFFFF, true),    // 扩展：掩码 -> 0x1FFFFFFF
        });
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::IdRole).toString(), QStringLiteral("0x234"));
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::IdRole).toString(), QStringLiteral("0x1FFFFFFF"));
        QCOMPARE(model.data(model.index(2, 0), FrameListModel::IdRole).toString(), QStringLiteral("0x1FFFFFFF"));
    }

    void colorRules()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{
            mk(0x000, false, false, false, false, false, false),   // 收 -> palette[0]
            mk(0x123, false, false, false, false, false, true),    // 发 -> 绿
            mk(0x200, false, false, false, false, true, false),    // 错误 -> 红
        });
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::ColorRole).toString(), QStringLiteral("#e0b34c"));
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::ColorRole).toString(), QStringLiteral("#7bd88f"));
        QCOMPARE(model.data(model.index(2, 0), FrameListModel::ColorRole).toString(), QStringLiteral("#e06c75"));
    }

    void emptyDataText()
    {
        FrameListModel model;
        FrameData f = mk(0x100);
        f.data.clear();
        f.len = 0;
        model.append(QVector<FrameData>{f});
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::DataRole).toString(), QString());
    }

    void invalidIndexReturnsInvalid()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{mk(0x100)});
        QVERIFY(!model.data(QModelIndex(), FrameListModel::IdRole).isValid());
        QVERIFY(!model.data(model.index(5, 0), FrameListModel::IdRole).isValid());
        QVERIFY(!model.data(model.index(0, 0), Qt::UserRole + 999).isValid());
    }

    void frameAtBounds()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{mk(0x123)});
        QCOMPARE(model.frameAt(0).id, quint32(0x123));
        QCOMPARE(model.frameAt(-1).id, quint32(0));
        QCOMPARE(model.frameAt(1).id, quint32(0));
        QCOMPARE(model.frameAt(100).id, quint32(0));
    }

    void maxRowsClamp()
    {
        FrameListModel model;
        model.setMaxRows(10);           // 最小值 100
        QCOMPARE(model.maxRows(), 100);
        model.setMaxRows(500);
        QCOMPARE(model.maxRows(), 500);
    }

    void relativeTimestampBase()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{mk(1, false, false, false, false, false, false, false, 0, 1000000)});
        model.append(QVector<FrameData>{mk(2, false, false, false, false, false, false, false, 0, 3000000)});
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::TimeRole).toString(), QStringLiteral("0.000000"));
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::TimeRole).toString(), QStringLiteral("2.000000"));

        model.setTimestampMode(FrameListModel::AbsoluteTime);
        const QString abs = model.data(model.index(0, 0), FrameListModel::TimeRole).toString();
        QVERIFY(abs.contains(QLatin1Char(':')));
    }

    void deviceTimestampPreferred()
    {
        // 相对模式优先设备时间戳（tick=100us）
        FrameData a = mk(1);
        a.devTs = 2000000;
        FrameData b = mk(2);
        b.devTs = 2001000;   // +1000 tick = 0.1s
        FrameListModel model;
        model.append(QVector<FrameData>{a, b});
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::TimeRole).toString(), QStringLiteral("0.000000"));
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::TimeRole).toString(), QStringLiteral("0.100000"));

        // 设备时间戳与主机时间不一致时，应体现设备差值而非主机差值
        FrameData c = mk(3);
        c.devTs = 2003000;   // +2000 tick = 0.2s
        c.hostUs = 999999999;
        model.append(QVector<FrameData>{c});
        QCOMPARE(model.data(model.index(2, 0), FrameListModel::TimeRole).toString(), QStringLiteral("0.300000"));
    }

    void clearResetsTimeBase()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{mk(1, false, false, false, false, false, false, false, 0, 5000000)});
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::TimeRole).toString(), QStringLiteral("0.000000"));
        model.clear();
        model.append(QVector<FrameData>{mk(2, false, false, false, false, false, false, false, 0, 9000000)});
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::TimeRole).toString(), QStringLiteral("0.000000"));
    }

    void countChangedSignals()
    {
        FrameListModel model;
        QSignalSpy spy(&model, &FrameListModel::countChanged);
        model.append(QVector<FrameData>{mk(1)});
        QCOMPARE(spy.count(), 1);
        model.clear();
        QCOMPARE(spy.count(), 2);
    }

    void sequenceMonotonic()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{mk(0x100), mk(0x101), mk(0x102)});
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::SequenceRole).toULongLong(), quint64(1));
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::SequenceRole).toULongLong(), quint64(2));
        QCOMPARE(model.data(model.index(2, 0), FrameListModel::SequenceRole).toULongLong(), quint64(3));

        model.append(QVector<FrameData>{mk(0x103)});
        QCOMPARE(model.data(model.index(3, 0), FrameListModel::SequenceRole).toULongLong(), quint64(4));

        model.clear();
        model.append(QVector<FrameData>{mk(0x200)});
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::SequenceRole).toULongLong(), quint64(1));
    }
};

QTEST_APPLESS_MAIN(TestFrameListRoles)

#include "test_framelist_roles.moc"
