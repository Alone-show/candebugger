#include <QtTest>

#include "core/CanDevice.h"
#include "core/FrameBuilder.h"

class TestFrameBuilder : public QObject
{
    Q_OBJECT

private slots:
    void hexParseAndFormat()
    {
        const QByteArray b = FrameBuilder::parseData(QStringLiteral("01 02 0A"), FrameBuilder::HexFormat, 8);
        QCOMPARE(b, QByteArray::fromHex("01020a"));
        QCOMPARE(FrameBuilder::formatData(b, FrameBuilder::HexFormat), QStringLiteral("01 02 0A"));

        // 紧凑写法与 0x 前缀
        QCOMPARE(FrameBuilder::parseData(QStringLiteral("0x0102"), FrameBuilder::HexFormat, 8),
                 QByteArray::fromHex("0102"));
    }

    void asciiParseAndFormat()
    {
        const QByteArray b = FrameBuilder::parseData(QStringLiteral("AB"), FrameBuilder::AsciiFormat, 8);
        QCOMPARE(b, QByteArray("AB"));
        QCOMPARE(FrameBuilder::formatData(b, FrameBuilder::AsciiFormat), QStringLiteral("AB"));
    }

    void decimalParseAndFormat()
    {
        const QByteArray b = FrameBuilder::parseData(QStringLiteral("1 2 255"), FrameBuilder::DecimalFormat, 8);
        QCOMPARE(b, QByteArray::fromHex("0102ff"));
        QCOMPARE(FrameBuilder::formatData(b, FrameBuilder::DecimalFormat), QStringLiteral("1 2 255"));
    }

    void parseErrors()
    {
        QString err;
        FrameBuilder::parseData(QStringLiteral("0"), FrameBuilder::HexFormat, 8, &err);
        QVERIFY(!err.isEmpty());
        FrameBuilder::parseData(QStringLiteral("0G"), FrameBuilder::HexFormat, 8, &err);
        QVERIFY(!err.isEmpty());
        FrameBuilder::parseData(QStringLiteral("1 2 3 4 5 6 7 8 9"), FrameBuilder::HexFormat, 8, &err);
        QVERIFY(!err.isEmpty());
        FrameBuilder::parseData(QStringLiteral("256"), FrameBuilder::DecimalFormat, 8, &err);
        QVERIFY(!err.isEmpty());
    }

    void clampIdMasks()
    {
        QCOMPARE(FrameBuilder::clampId(0x1FFFFFFF, true), quint32(0x1FFFFFFF));
        QCOMPARE(FrameBuilder::clampId(0xFFFFFFFF, true), quint32(0x1FFFFFFF));
        QCOMPARE(FrameBuilder::clampId(0xFFFFFFFF, false), quint32(0x7FF));
        QCOMPARE(FrameBuilder::clampId(0x123, false), quint32(0x123));
    }

    void crc8KnownValue()
    {
        QCOMPARE(int(FrameBuilder::crc8(QByteArray("123456789"))), 0xF4);
        QCOMPARE(int(FrameBuilder::crc8(QByteArray())), 0x00);
    }

    void buildCanFields()
    {
        FrameSpec s;
        s.id = 0x123;
        s.dlc = 8;
        s.data = QByteArray::fromHex("0102030405060708");
        s.txm = ZCAN_SR_NORM;
        s.echo = true;

        ZCAN_20_MSG m;
        QString err;
        QVERIFY(FrameBuilder::buildCan(s, &m, &err));
        QVERIFY(err.isEmpty());
        QCOMPARE(m.hdr.id, quint32(0x123));
        QCOMPARE(int(m.hdr.inf.fmt), 0);
        QCOMPARE(int(m.hdr.inf.sdf), 0);
        QCOMPARE(int(m.hdr.inf.sef), 0);
        QCOMPARE(int(m.hdr.inf.brs), 0);
        QCOMPARE(int(m.hdr.inf.echo), 1);
        QCOMPARE(int(m.hdr.inf.txm), int(ZCAN_SR_NORM));
        QCOMPARE(int(m.hdr.len), 8);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(m.dat), 8), s.data);
    }

    void buildFdFields()
    {
        FrameSpec s;
        s.id = 0x1ABCDEF0;
        s.extended = true;
        s.fd = true;
        s.brs = true;
        s.dlc = 12;
        s.data = QByteArray(12, '\x11');

        ZCAN_FD_MSG m;
        QString err;
        QVERIFY(FrameBuilder::buildFd(s, &m, &err));
        QCOMPARE(m.hdr.id, quint32(0x1ABCDEF0));
        QCOMPARE(int(m.hdr.inf.fmt), 1);
        QCOMPARE(int(m.hdr.inf.sef), 1);
        QCOMPARE(int(m.hdr.inf.brs), 1);
        QCOMPARE(int(m.hdr.len), 12);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(m.dat), 12), s.data);
    }

    void remoteFrame()
    {
        FrameSpec s;
        s.id = 0x321;
        s.remote = true;
        s.dlc = 4;
        s.data = QByteArray::fromHex("AABB");

        ZCAN_20_MSG m;
        QVERIFY(FrameBuilder::buildCan(s, &m));
        QCOMPARE(int(m.hdr.inf.sdf), 1);
        QCOMPARE(int(m.hdr.len), 4);
        QCOMPARE(m.dat[0], quint8(0));
        QCOMPARE(m.dat[1], quint8(0));
    }

    void paddingAndTruncation()
    {
        FrameSpec s;
        s.dlc = 4;
        s.data = QByteArray::fromHex("01");
        ZCAN_20_MSG m;
        QVERIFY(FrameBuilder::buildCan(s, &m));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(m.dat), 4), QByteArray::fromHex("01000000"));

        s.data = QByteArray::fromHex("0102030405");
        QVERIFY(FrameBuilder::buildCan(s, &m));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(m.dat), 4), QByteArray::fromHex("01020304"));
    }

    void incrementFill()
    {
        FrameSpec s;
        s.dlc = 4;
        s.increment = true;
        ZCAN_20_MSG m;
        QVERIFY(FrameBuilder::buildCan(s, &m));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(m.dat), 4), QByteArray::fromHex("00010203"));
    }

    void rollingCounter()
    {
        FrameSpec s;
        s.dlc = 4;
        s.data = QByteArray::fromHex("00000000");
        s.rolling = true;
        s.counter = 0x41;
        ZCAN_20_MSG m;
        QVERIFY(FrameBuilder::buildCan(s, &m));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(m.dat), 4), QByteArray::fromHex("41000000"));
    }

    void crcLastByte()
    {
        FrameSpec s;
        s.dlc = 4;
        s.data = QByteArray::fromHex("01020300");
        s.crc = true;
        ZCAN_20_MSG m;
        QVERIFY(FrameBuilder::buildCan(s, &m));
        const quint8 expect = FrameBuilder::crc8(QByteArray::fromHex("010203"));
        QCOMPARE(m.dat[3], expect);
    }

    void specFromMapAndDescribe()
    {
        QVariantMap map;
        map.insert(QStringLiteral("id"), 0x123u);
        map.insert(QStringLiteral("dlc"), 2);
        map.insert(QStringLiteral("data"), QStringLiteral("AA BB"));
        map.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));

        FrameSpec s;
        QString err;
        QVERIFY(FrameBuilder::specFromMap(map, &s, &err));
        QCOMPARE(s.id, quint32(0x123));
        QCOMPARE(s.dlc, 2);
        QCOMPARE(s.data, QByteArray::fromHex("aabb"));
        QCOMPARE(s.txm, int(ZCAN_SR_NORM));   // 默认自发自收

        const QString desc = FrameBuilder::describe(map);
        QVERIFY(desc.contains(QStringLiteral("0x123")));
        QVERIFY(!desc.startsWith(QStringLiteral("错误")));
    }

    void buildCanRejectsFdFlag()
    {
        FrameSpec s;
        s.fd = true;
        ZCAN_20_MSG m;
        QString err;
        QVERIFY(!FrameBuilder::buildCan(s, &m, &err));
        QVERIFY(!err.isEmpty());
    }

    void nullOutBufferRejected()
    {
        FrameSpec s;
        QString err;
        QVERIFY(!FrameBuilder::buildCan(s, nullptr, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!FrameBuilder::buildFd(s, nullptr, &err));
        QVERIFY(!err.isEmpty());
    }

    void dlcClampCan()
    {
        FrameSpec s;
        s.dlc = 20;                      // 超过 CAN 上限
        s.data = QByteArray(20, '\x01');
        ZCAN_20_MSG m;
        QVERIFY(FrameBuilder::buildCan(s, &m));
        QCOMPARE(int(m.hdr.len), 8);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(m.dat), 8), QByteArray(8, '\x01'));
    }

    void specFromMapDlcClamp()
    {
        QVariantMap can;
        can.insert(QStringLiteral("id"), 0x100u);
        can.insert(QStringLiteral("dlc"), 20);
        FrameSpec s;
        QVERIFY(FrameBuilder::specFromMap(can, &s));
        QCOMPARE(s.dlc, 8);              // CAN 上限 8

        QVariantMap fd;
        fd.insert(QStringLiteral("fd"), true);
        fd.insert(QStringLiteral("dlc"), 100);
        QVERIFY(FrameBuilder::specFromMap(fd, &s));
        QCOMPARE(s.dlc, 64);             // FD 上限 64
    }

    void modifiersCombined()
    {
        FrameSpec s;
        s.dlc = 4;
        s.increment = true;   // 00 01 02 03
        s.rolling = true;     // 首字节 = counter
        s.crc = true;         // 末字节 = crc8(前 3 字节)
        s.counter = 0x55;

        ZCAN_20_MSG m;
        QVERIFY(FrameBuilder::buildCan(s, &m));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(m.dat), 3), QByteArray::fromHex("550102"));
        const quint8 expect = FrameBuilder::crc8(QByteArray::fromHex("550102"));
        QCOMPARE(quint8(m.dat[3]), expect);
    }

    void modifiersRemoteEmpty()
    {
        FrameSpec s;
        s.remote = true;
        s.dlc = 4;
        s.increment = true;
        s.crc = true;
        const QByteArray out = FrameBuilder::applyModifiers(QByteArray::fromHex("AABBCCDD"), s, 8);
        QVERIFY(out.isEmpty());
    }

    void remoteSpecFromMapSkipsData()
    {
        QVariantMap map;
        map.insert(QStringLiteral("remote"), true);
        map.insert(QStringLiteral("dlc"), 3);
        map.insert(QStringLiteral("data"), QStringLiteral("ZZZ"));   // 不应被解析
        FrameSpec s;
        QString err;
        QVERIFY(FrameBuilder::specFromMap(map, &s, &err));
        QVERIFY(err.isEmpty());
        QVERIFY(s.remote);
        QVERIFY(s.data.isEmpty());
        QCOMPARE(s.dlc, 3);
    }

    void dataConversionAndCount()
    {
        FrameBuilder fb(nullptr);
        QCOMPARE(fb.convertData(QStringLiteral("41 42"), FrameBuilder::HexFormat,
                                FrameBuilder::AsciiFormat, 8),
                 QStringLiteral("AB"));
        QCOMPARE(fb.dataByteCount(QStringLiteral("41 42"), FrameBuilder::HexFormat, 8), 2);
        QCOMPARE(fb.dataByteCount(QStringLiteral("0"), FrameBuilder::HexFormat, 8), -1);
        // 转换失败时原样返回
        QCOMPARE(fb.convertData(QStringLiteral("ZZ"), FrameBuilder::HexFormat,
                                FrameBuilder::AsciiFormat, 8),
                 QStringLiteral("ZZ"));
    }

    void describeInvalid()
    {
        QVariantMap map;
        map.insert(QStringLiteral("data"), QStringLiteral("0G"));
        map.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));
        QVERIFY(FrameBuilder::describe(map).startsWith(QStringLiteral("错误")));
    }

    void specToMapRoundTrip()
    {
        FrameSpec s;
        s.id = 0x321;
        s.extended = true;
        s.fd = true;
        s.dlc = 4;
        s.data = QByteArray::fromHex("DEADBEEF");
        const QVariantMap map = FrameBuilder::specToMap(s, FrameBuilder::HexFormat);

        FrameSpec back;
        QVERIFY(FrameBuilder::specFromMap(map, &back));
        QCOMPARE(back.id, s.id);
        QCOMPARE(back.extended, s.extended);
        QCOMPARE(back.fd, s.fd);
        QCOMPARE(back.dlc, s.dlc);
        QCOMPARE(back.data, s.data);
    }

    // 异步发送：立即返回（入队），完成后经 sendCompleted 回传；未打开设备 → 失败且不计数
    void asyncSendWithoutDeviceReportsFailure()
    {
        CanDevice device;   // 未打开
        FrameBuilder fb(&device);
        QSignalSpy spy(&fb, &FrameBuilder::sendCompleted);

        QVariantMap spec;
        spec.insert(QStringLiteral("id"), 0x123u);
        spec.insert(QStringLiteral("dlc"), 8);
        spec.insert(QStringLiteral("data"), QStringLiteral("01 02 03 04 05 06 07 08"));
        spec.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));

        const int before = fb.sentCount();
        QVERIFY(fb.sendFrameAsync(0, spec));   // 入队成功（不阻塞）
        QVERIFY(spy.wait(3000));               // 等待完成信号
        QCOMPARE(fb.sentCount(), before);      // 设备未打开 → 未计数
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toBool(), false);
    }

    // 异步发送：非法数据同步拒绝（不入队、无完成信号）
    void asyncSendInvalidSpecRejectedSync()
    {
        CanDevice device;
        FrameBuilder fb(&device);
        QSignalSpy spy(&fb, &FrameBuilder::sendCompleted);

        QVariantMap spec;
        spec.insert(QStringLiteral("id"), 0x123u);
        spec.insert(QStringLiteral("dlc"), 8);
        spec.insert(QStringLiteral("data"), QStringLiteral("ZZ"));
        spec.insert(QStringLiteral("dataFormat"), static_cast<int>(FrameBuilder::HexFormat));

        QVERIFY(!fb.sendFrameAsync(0, spec));
        QVERIFY(!fb.lastError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFrameBuilder)

#include "test_framebuilder.moc"
