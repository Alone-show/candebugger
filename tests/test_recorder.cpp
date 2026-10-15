#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVector>

#include "core/FrameData.h"
#include "core/Recorder.h"

namespace {

FrameData mkFrame(quint32 id, bool tx = false, bool fd = false, bool brs = false,
                  bool remote = false, bool extended = false, int channel = 0)
{
    FrameData f;
    f.wallUs = 1000000;
    f.hostUs = 1000000;
    f.id = id;
    f.channel = static_cast<quint8>(channel);
    f.len = 4;
    f.extended = extended;
    f.remote = remote;
    f.fd = fd;
    f.brs = brs;
    f.tx = tx;
    f.data = QByteArray::fromHex("A1B2C3D4");
    if (remote) {
        f.data.clear();
        f.len = 0;
    }
    return f;
}

QVector<FrameData> readFrames(const QString &path, bool asc)
{
    QVector<FrameData> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();
    for (const QString &line : lines) {
        FrameData f;
        qint64 rel = 0;
        const bool ok = asc ? Recorder::parseAscLine(line, &f, &rel)
                            : Recorder::parseCsvLine(line, &f, &rel);
        if (ok)
            out.append(f);
    }
    return out;
}

void compareFrame(const FrameData &a, const FrameData &b)
{
    QCOMPARE(a.id, b.id);
    QCOMPARE(a.extended, b.extended);
    QCOMPARE(a.remote, b.remote);
    QCOMPARE(a.fd, b.fd);
    QCOMPARE(a.brs, b.brs);
    QCOMPARE(a.tx, b.tx);
    QCOMPARE(a.channel, b.channel);
    QCOMPARE(a.len, b.len);
    QCOMPARE(a.data, b.data);
}

} // namespace

class TestRecorder : public QObject
{
    Q_OBJECT

private slots:
    void ascLineRoundTrip()
    {
        FrameData f = mkFrame(0x1ABCDEF0, true, true, true, false, true, 1);
        const qint64 start = 1230000;
        f.wallUs = start + 4567;   // 0.004567 s

        const QString line = Recorder::formatAscLine(f, start);
        QVERIFY(line.startsWith(QStringLiteral("0.004567")));

        FrameData out;
        qint64 rel = 0;
        QVERIFY(Recorder::parseAscLine(line, &out, &rel));
        QCOMPARE(rel, qint64(4567));
        compareFrame(out, f);
    }

    void csvLineRoundTrip()
    {
        FrameData f = mkFrame(0x123, false, false, false, false, false, 2);
        f.wallUs = 1000000 + 250000;   // 0.25 s

        const QString line = Recorder::formatCsvLine(f, 1000000);
        QVERIFY(line.startsWith(QStringLiteral("0.250000")));

        FrameData out;
        qint64 rel = 0;
        QVERIFY(Recorder::parseCsvLine(line, &out, &rel));
        QCOMPARE(rel, qint64(250000));
        compareFrame(out, f);
    }

    void remoteAndErrorRoundTrip()
    {
        FrameData r = mkFrame(0x200, false, false, false, true, false, 0);
        r.wallUs = 1000000;
        const QString asc = Recorder::formatAscLine(r, 1000000);
        FrameData out;
        qint64 rel = 0;
        QVERIFY(Recorder::parseAscLine(asc, &out, &rel));
        QVERIFY(out.remote);
        QCOMPARE(out.len, quint8(0));

        FrameData e;
        e.wallUs = 1000000;
        e.error = true;
        e.id = 0;
        const QString csv = Recorder::formatCsvLine(e, 1000000);
        QVERIFY(csv.contains(QStringLiteral("ERROR")));
        QVERIFY(Recorder::parseCsvLine(csv, &out, &rel));
        QVERIFY(out.error);
    }

    void headerLinesAreSkipped()
    {
        FrameData out;
        qint64 rel = 0;
        QVERIFY(!Recorder::parseAscLine(QStringLiteral("base hex  timestamps relative"), &out, &rel));
        QVERIFY(!Recorder::parseAscLine(QStringLiteral("Begin Triggerblock now"), &out, &rel));
        QVERIFY(!Recorder::parseCsvLine(
            QStringLiteral("timestamp,channel,dir,id,type,dlc,data"), &out, &rel));
        QVERIFY(!Recorder::parseCsvLine(QString(), &out, &rel));
    }

    void defaultFileNames()
    {
        const QDateTime dt = QDateTime::fromString(QStringLiteral("20260924_153012"),
                                                   QStringLiteral("yyyyMMdd_hhmmss"));
        QCOMPARE(Recorder::defaultFileName(dt, Recorder::AscFormat),
                 QStringLiteral("record_20260924_153012.asc"));
        QCOMPARE(Recorder::defaultFileName(dt, Recorder::CsvFormat),
                 QStringLiteral("record_20260924_153012.csv"));
    }

    void writeAscCsvRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ascPath = dir.filePath(QStringLiteral("a.asc"));
        const QString csvPath = dir.filePath(QStringLiteral("b.csv"));

        Recorder r;
        QVERIFY(r.startWithPaths(ascPath, csvPath));
        QVERIFY(r.isRecording());

        const QVector<FrameData> frames{
            mkFrame(0x100, false, false, false, false, false, 0),
            mkFrame(0x101, true, false, false, false, false, 1),
            mkFrame(0x1ABCDEF0, false, true, true, false, true, 1),
            mkFrame(0x200, false, false, false, true, false, 0),
        };
        // 发送路径 tx 帧单独入队，接收批里的 tx 会被跳过
        r.enqueueFrames(QVector<FrameData>{frames[0], frames[2], frames[3]});
        r.enqueueFrame(frames[1]);

        r.stop();
        QVERIFY(!r.isRecording());
        QCOMPARE(r.writtenCount(), qulonglong(4));
        QCOMPARE(r.droppedCount(), qulonglong(0));

        // 写入顺序 = 入队顺序：0、2、3、1
        const QVector<FrameData> expected{frames[0], frames[2], frames[3], frames[1]};
        const QVector<FrameData> ascOut = readFrames(ascPath, true);
        const QVector<FrameData> csvOut = readFrames(csvPath, false);
        QCOMPARE(ascOut.size(), 4);
        QCOMPARE(csvOut.size(), 4);
        for (int i = 0; i < 4; ++i) {
            compareFrame(ascOut.at(i), expected.at(i));
            compareFrame(csvOut.at(i), expected.at(i));
        }
    }

    void receiveTxFramesSkipped()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString csvPath = dir.filePath(QStringLiteral("tx.csv"));

        Recorder r;
        QVERIFY(r.startWithPaths(QString(), csvPath));
        QVector<FrameData> batch;
        batch << mkFrame(0x100)                 // rx
              << mkFrame(0x100, true)           // 回显（跳过）
              << mkFrame(0x101, true)           // 回环（跳过）
              << mkFrame(0x102);                // rx
        r.enqueueFrames(batch);
        r.stop();

        QCOMPARE(r.writtenCount(), qulonglong(2));
        QCOMPARE(readFrames(csvPath, false).size(), 2);
    }

    void overflowDropsWithoutBlocking()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString csvPath = dir.filePath(QStringLiteral("overflow.csv"));

        Recorder r;
        r.setQueueCapacity(16);
        QVERIFY(r.startWithPaths(QString(), csvPath));

        const int kTotal = 50000;
        QVector<FrameData> burst;
        burst.reserve(100);
        for (int i = 0; i < 100; ++i)
            burst.append(mkFrame(0x100 + static_cast<quint32>(i)));
        for (int i = 0; i < kTotal / 100; ++i)
            r.enqueueFrames(burst);

        r.stop();
        // 每帧要么写盘要么丢弃，总数守恒且未阻塞
        QCOMPARE(r.writtenCount() + r.droppedCount(), qulonglong(kTotal));
        QVERIFY(r.droppedCount() > 0);
    }

    void startStopIdempotent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString csvPath = dir.filePath(QStringLiteral("idem.csv"));

        Recorder r;
        QVERIFY(r.startWithPaths(QString(), csvPath));
        QVERIFY(r.startWithPaths(QString(), csvPath));   // 幂等
        QVERIFY(r.isRecording());
        r.stop();
        r.stop();                                        // 幂等
        QVERIFY(!r.isRecording());
    }

    void startFailuresReportError()
    {
        Recorder r;
        QVERIFY(!r.start(QStringLiteral("/no/such/dir/candbg"), true, false));
        QVERIFY(!r.errorString().isEmpty());
        QVERIFY(!r.isRecording());

        QVERIFY(!r.start(QDir::tempPath(), false, false));
        QVERIFY(!r.startWithPaths(QString(), QString()));
    }

    void parseAscMalformed()
    {
        FrameData out;
        qint64 rel = 0;
        QVERIFY(!Recorder::parseAscLine(QString(), &out, &rel));
        QVERIFY(!Recorder::parseAscLine(QStringLiteral("   "), &out, &rel));
        QVERIFY(!Recorder::parseAscLine(QStringLiteral("0.001 1 123"), &out, &rel));        // 字段不足
        QVERIFY(!Recorder::parseAscLine(QStringLiteral("x 1 123 Rx d 2"), &out, &rel));      // 时间非法
        QVERIFY(!Recorder::parseAscLine(QStringLiteral("0.001 1 ZZZ Rx d 2"), &out, &rel));  // ID 非法
        QVERIFY(!Recorder::parseAscLine(QStringLiteral("0.001 1 123 Rx d Z"), &out, &rel));  // DLC 非法
    }

    void parseCsvMalformed()
    {
        FrameData out;
        qint64 rel = 0;
        QVERIFY(!Recorder::parseCsvLine(QString(), &out, &rel));
        QVERIFY(!Recorder::parseCsvLine(QStringLiteral("0.1,1,Rx,123,CAN,2"), &out, &rel));      // 字段不足
        QVERIFY(!Recorder::parseCsvLine(QStringLiteral("x,1,Rx,123,CAN,2,AA"), &out, &rel));      // 时间非法
        QVERIFY(!Recorder::parseCsvLine(QStringLiteral("0.1,1,Rx,ZZ,CAN,2,AA"), &out, &rel));     // ID 非法
        QVERIFY(!Recorder::parseCsvLine(QStringLiteral("0.1,1,Rx,123,CAN,x,AA"), &out, &rel));    // DLC 非法
    }

    void ascExtendedAndError()
    {
        FrameData out;
        qint64 rel = 0;
        QVERIFY(Recorder::parseAscLine(
            QStringLiteral("0.001234 1 1ABCDEF0x Tx fdb 3 00 01 02"), &out, &rel));
        QVERIFY(out.extended);
        QVERIFY(out.fd);
        QVERIFY(out.brs);
        QVERIFY(out.tx);
        QCOMPARE(rel, qint64(1234));

        QVERIFY(Recorder::parseAscLine(QStringLiteral("0.003000 1 0 Rx e 0"), &out, &rel));
        QVERIFY(out.error);
        QCOMPARE(out.len, quint8(0));
    }

    void csvTypeVariants()
    {
        FrameData out;
        qint64 rel = 0;

        QVERIFY(Recorder::parseCsvLine(QStringLiteral("0,1,Rx,123,CANFD,2,11 22"), &out, &rel));
        QVERIFY(out.fd);
        QVERIFY(!out.brs);

        QVERIFY(Recorder::parseCsvLine(QStringLiteral("0,1,Rx,123,CANFD_BRS,2,11 22"), &out, &rel));
        QVERIFY(out.fd);
        QVERIFY(out.brs);

        QVERIFY(Recorder::parseCsvLine(QStringLiteral("0,1,Rx,123,CAN_RTR,2,"), &out, &rel));
        QVERIFY(out.remote);
        QVERIFY(!out.fd);

        QVERIFY(Recorder::parseCsvLine(QStringLiteral("0,1,Rx,123,CANFD_RTR,2,"), &out, &rel));
        QVERIFY(out.remote);
        QVERIFY(out.fd);

        QVERIFY(Recorder::parseCsvLine(QStringLiteral("0,1,Rx,0,ERROR,0,"), &out, &rel));
        QVERIFY(out.error);
        QCOMPARE(out.len, quint8(0));
    }

    void headerAndTimeHelpers()
    {
        QVERIFY(Recorder::ascHeader(QDateTime::currentDateTime()).contains(QStringLiteral("Begin Triggerblock")));
        QVERIFY(Recorder::ascFooter().contains(QStringLiteral("End TriggerBlock")));
        QCOMPARE(Recorder::csvHeader(),
                 QStringLiteral("timestamp,channel,dir,id,type,dlc,data\n"));
        QCOMPARE(Recorder::relativeSeconds(1500000, 1000000), 0.5);
    }
};

QTEST_APPLESS_MAIN(TestRecorder)

#include "test_recorder.moc"
