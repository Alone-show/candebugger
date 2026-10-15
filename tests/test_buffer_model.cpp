#include <QtTest>

#include "core/FrameData.h"
#include "core/RingBuffer.h"
#include "model/FrameListModel.h"

namespace {

FrameData makeFrame(quint32 id, quint8 ch = 0, bool tx = false)
{
    FrameData f;
    f.id = id;
    f.channel = ch;
    f.tx = tx;
    f.len = 2;
    f.data = QByteArray::fromHex("AABB");
    f.hostUs = id;
    f.wallUs = 1700000000000000LL + id;
    return f;
}

} // namespace

class TestBufferModel : public QObject
{
    Q_OBJECT

private slots:
    void ringBufferOrderAndOverflow()
    {
        RingBuffer ring(4);
        for (quint32 i = 0; i < 6; ++i)
            ring.push(makeFrame(i));

        QCOMPARE(ring.totalPushed(), quint64(6));
        QCOMPARE(ring.totalDropped(), quint64(2));
        QCOMPARE(ring.size(), 4);

        QVector<FrameData> out;
        QCOMPARE(ring.pop(out, 10), 4);
        QCOMPARE(out.size(), 4);
        // 覆盖最旧两条，剩余 2,3,4,5
        QCOMPARE(out.at(0).id, quint32(2));
        QCOMPARE(out.at(3).id, quint32(5));
        QCOMPARE(ring.size(), 0);
    }

    void ringBufferMassPush()
    {
        RingBuffer ring(1000);
        QVector<FrameData> batch;
        for (quint32 i = 0; i < 500; ++i)
            batch.append(makeFrame(i));
        ring.push(batch);
        QCOMPARE(ring.totalPushed(), quint64(500));

        QVector<FrameData> out;
        QCOMPARE(ring.pop(out, 1000), 500);
        QCOMPARE(out.first().id, quint32(0));
        QCOMPARE(out.last().id, quint32(499));
    }

    void modelRoles()
    {
        FrameListModel model;
        QVector<FrameData> batch;
        FrameData f = makeFrame(0x123);
        f.extended = false;
        batch.append(f);

        FrameData ext = makeFrame(0x18FF50E5);
        ext.extended = true;
        ext.fd = true;
        ext.brs = true;
        batch.append(ext);

        FrameData tx = makeFrame(0x200, 1, true);
        batch.append(tx);

        model.append(batch);
        QCOMPARE(model.count(), 3);

        const QModelIndex idx0 = model.index(0, 0);
        QCOMPARE(model.data(idx0, FrameListModel::IdRole).toString(), QStringLiteral("0x123"));
        QCOMPARE(model.data(idx0, FrameListModel::TypeRole).toString(), QStringLiteral("标准"));
        QCOMPARE(model.data(idx0, FrameListModel::DataRole).toString(), QStringLiteral("AA BB"));

        const QModelIndex idx1 = model.index(1, 0);
        QCOMPARE(model.data(idx1, FrameListModel::IdRole).toString(), QStringLiteral("0x18FF50E5"));
        QCOMPARE(model.data(idx1, FrameListModel::TypeRole).toString(), QStringLiteral("扩展"));
        QCOMPARE(model.data(idx1, FrameListModel::FormatRole).toString(), QStringLiteral("FD加速"));

        const QModelIndex idx2 = model.index(2, 0);
        QCOMPARE(model.data(idx2, FrameListModel::DirectionRole).toString(), QStringLiteral("Tx"));
    }

    void modelTrim()
    {
        FrameListModel model;
        model.setMaxRows(100);
        QVector<FrameData> batch;
        for (quint32 i = 0; i < 250; ++i)
            batch.append(makeFrame(i));
        model.append(batch);
        QVERIFY(model.count() <= 100 + 25);
        QVERIFY(model.count() >= 100);
    }

    void modelClear()
    {
        FrameListModel model;
        QVector<FrameData> batch{makeFrame(1), makeFrame(2)};
        model.append(batch);
        QCOMPARE(model.count(), 2);
        model.clear();
        QCOMPARE(model.count(), 0);
    }

    void timestampSwitch()
    {
        FrameListModel model;
        model.append(QVector<FrameData>{makeFrame(1)});
        const QModelIndex idx = model.index(0, 0);
        const QString rel = model.data(idx, FrameListModel::TimeRole).toString();
        QVERIFY(rel.contains(QLatin1Char('.')));

        model.setTimestampMode(FrameListModel::AbsoluteTime);
        const QString abs = model.data(idx, FrameListModel::TimeRole).toString();
        QVERIFY(abs.contains(QLatin1Char(':')));
    }

    void devTsRelative()
    {
        // 设备时间戳 tick=100us：基准 1000000，第二帧 +1000 tick = 0.1s
        FrameData a = makeFrame(1);
        a.devTs = 1000000;
        FrameData b = makeFrame(2);
        b.devTs = 1001000;

        FrameListModel model;
        model.append(QVector<FrameData>{a, b});
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::TimeRole).toString(),
                 QStringLiteral("0.000000"));
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::TimeRole).toString(),
                 QStringLiteral("0.100000"));
    }

    void devTsWraparound()
    {
        // 基准接近 U32 上限，下一帧回绕；差值应为 26 tick = 0.0026s
        FrameData a = makeFrame(1);
        a.devTs = 0xFFFFFFF0u;
        FrameData b = makeFrame(2);
        b.devTs = 10;

        FrameListModel model;
        model.append(QVector<FrameData>{a, b});
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::TimeRole).toString(),
                 QStringLiteral("0.002600"));
    }

    void devTsFallbackWhenZero()
    {
        // devTs 无效(0) 时回退主机时间：hostUs 1000000 -> 1500000 = 0.5s
        FrameData a = makeFrame(1);
        a.hostUs = 1000000;
        FrameData b = makeFrame(2);
        b.hostUs = 1500000;

        FrameListModel model;
        model.append(QVector<FrameData>{a, b});
        QCOMPARE(model.data(model.index(0, 0), FrameListModel::TimeRole).toString(),
                 QStringLiteral("0.000000"));
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::TimeRole).toString(),
                 QStringLiteral("0.500000"));
    }

    void devTsBaseAfterInvalid()
    {
        // 首帧 devTs 无效，基准应取首个有效值，而非 0
        FrameData a = makeFrame(1);
        FrameData b = makeFrame(2);
        b.devTs = 500000;
        FrameData c = makeFrame(3);
        c.devTs = 501000;

        FrameListModel model;
        model.append(QVector<FrameData>{a, b, c});
        QCOMPARE(model.data(model.index(1, 0), FrameListModel::TimeRole).toString(),
                 QStringLiteral("0.000000"));
        QCOMPARE(model.data(model.index(2, 0), FrameListModel::TimeRole).toString(),
                 QStringLiteral("0.100000"));
    }
};

QTEST_APPLESS_MAIN(TestBufferModel)

#include "test_buffer_model.moc"
