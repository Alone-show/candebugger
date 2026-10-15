#include <QtTest>

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QVector>

#include "core/FrameData.h"
#include "core/Recorder.h"
#include "core/Replayer.h"

namespace {

FrameData timed(quint32 id, qint64 wallUs)
{
    FrameData f;
    f.id = id;
    f.wallUs = wallUs;
    f.hostUs = wallUs;
    f.len = 2;
    f.data = QByteArray::fromHex("1122");
    return f;
}

const char kCsvText[] =
    "timestamp,channel,dir,id,type,dlc,data\n"
    "0.000000,1,Rx,123,CAN,2,11 22\n"
    "0.001500,1,Tx,1ABCDEF0x,CANFD_BRS,3,00 01 02\n"
    "0.002000,2,Rx,200,CAN_RTR,0,\n";

const char kAscText[] =
    "date Thu Sep 24 15:30:12 2026\n"
    "base hex  timestamps relative\n"
    "Begin Triggerblock Thu Sep 24 15:30:12 2026\n"
    "   0.000000 1  123             Rx   d 2 11 22\n"
    "   0.001234 1  1ABCDEF0x       Tx   fdb 3 00 01 02\n"
    "   0.002000 2  200             Rx   r 0\n"
    "   0.003000 1  0               Rx   e 0\n"
    "End TriggerBlock\n";

} // namespace

class TestReplayer : public QObject
{
    Q_OBJECT

private slots:
    void detectByExtension()
    {
        QCOMPARE(Replayer::detectFormat(QStringLiteral("x.asc")), int(Replayer::AscFormat));
        QCOMPARE(Replayer::detectFormat(QStringLiteral("x.CSV")), int(Replayer::CsvFormat));
        QCOMPARE(Replayer::detectFormat(QStringLiteral("x.bin")), int(Replayer::UnknownFormat));
    }

    void detectByContent()
    {
        QCOMPARE(Replayer::detectFormat(QString(), QString::fromLatin1(kCsvText)),
                 int(Replayer::CsvFormat));
        QCOMPARE(Replayer::detectFormat(QString(), QString::fromLatin1(kAscText)),
                 int(Replayer::AscFormat));
    }

    void parseCsvText()
    {
        QString err;
        const QVector<FrameData> frames =
            Replayer::parseText(QString::fromLatin1(kCsvText), Replayer::CsvFormat, &err);
        QVERIFY(err.isEmpty());
        QCOMPARE(frames.size(), 3);

        QCOMPARE(frames[0].id, quint32(0x123));
        QVERIFY(!frames[0].extended);
        QVERIFY(!frames[0].tx);
        QCOMPARE(frames[0].channel, quint8(1));
        QCOMPARE(frames[0].len, quint8(2));
        QCOMPARE(frames[0].data, QByteArray::fromHex("1122"));

        QCOMPARE(frames[1].id, quint32(0x1ABCDEF0));
        QVERIFY(frames[1].extended);
        QVERIFY(frames[1].tx);
        QVERIFY(frames[1].fd);
        QVERIFY(frames[1].brs);
        QCOMPARE(frames[1].data, QByteArray::fromHex("000102"));

        QVERIFY(frames[2].remote);
        QCOMPARE(frames[2].len, quint8(0));
    }

    void parseAscText()
    {
        QString err;
        const QVector<FrameData> frames =
            Replayer::parseText(QString::fromLatin1(kAscText), Replayer::AscFormat, &err);
        QVERIFY(err.isEmpty());
        QCOMPARE(frames.size(), 4);

        QCOMPARE(frames[0].id, quint32(0x123));
        QCOMPARE(frames[0].data, QByteArray::fromHex("1122"));
        QCOMPARE(frames[1].id, quint32(0x1ABCDEF0));
        QVERIFY(frames[1].extended);
        QVERIFY(frames[1].fd);
        QVERIFY(frames[1].brs);
        QVERIFY(frames[2].remote);
        QVERIFY(frames[3].error);
    }

    void normalizeAndDuration()
    {
        QVector<FrameData> frames{timed(0x1, 100), timed(0x2, 350), timed(0x3, 600)};
        const qint64 dur = Replayer::normalizeTimeline(frames);
        QCOMPARE(dur, qint64(500));
        QCOMPARE(frames.first().wallUs, qint64(0));
        QCOMPARE(frames.last().wallUs, qint64(500));
        QCOMPARE(Replayer::totalDurationUs(frames), qint64(500));
        QCOMPARE(Replayer::totalDurationUs({}), qint64(0));
    }

    void progressAndScale()
    {
        QCOMPARE(Replayer::progressPercent(500, 1000), 50.0);
        QCOMPARE(Replayer::progressPercent(2000, 1000), 100.0);
        QCOMPARE(Replayer::progressPercent(500, 0), 0.0);
        QCOMPARE(Replayer::scaledDelayUs(1000, 2.0), qint64(500));
        QCOMPARE(Replayer::scaledDelayUs(1000, 0.5), qint64(2000));
        QCOMPARE(Replayer::scaledDelayUs(1000, 0.0), qint64(1000));
    }

    void dueScheduling()
    {
        const QVector<FrameData> frames{
            timed(0x1, 0), timed(0x2, 1000), timed(0x3, 2000), timed(0x4, 3000)};

        QCOMPARE(Replayer::dueFrameCount(frames, 1500, 1.0, 0), 2);
        QCOMPARE(Replayer::dueFrameCount(frames, 1000, 2.0, 0), 3);
        QCOMPARE(Replayer::dueFrameCount(frames, 3000, 1.0, 2), 2);
        QCOMPARE(Replayer::dueFrameCount(frames, 3000, 1.0, 4), 0);
        QCOMPARE(Replayer::dueFrameCount(frames, 0, 1.0, 0), 1);
    }

    void loadCsvFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("play.csv"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(kCsvText);
        file.close();

        Replayer r(nullptr);
        QVERIFY(r.load(path));
        QCOMPARE(r.frameCount(), 3);
        QCOMPARE(r.format(), int(Replayer::CsvFormat));
        QCOMPARE(r.durationUs(), qint64(2000));
        QCOMPARE(r.positionUs(), qint64(0));
        QVERIFY(!r.isPlaying());

        r.stop();   // 幂等
        QVERIFY(!r.load(QStringLiteral("/no/such/file.csv")));
        QVERIFY(!r.errorString().isEmpty());
    }

    void loadAscFileRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("play.asc"));

        // 用 Recorder 生成 ASC，再由 Replayer 解析，验证闭环
        Recorder rec;
        QVERIFY(rec.startWithPaths(path, QString()));
        const qint64 base = QDateTime::currentMSecsSinceEpoch() * 1000;
        rec.enqueueFrame(timed(0x111, base + 1000));
        rec.enqueueFrame(timed(0x222, base + 3000));
        rec.stop();

        Replayer r(nullptr);
        QVERIFY(r.load(path));
        QCOMPARE(r.frameCount(), 2);
        QCOMPARE(r.format(), int(Replayer::AscFormat));
        QCOMPARE(r.durationUs(), qint64(2000));
    }

    void detectUnknownContent()
    {
        QCOMPARE(Replayer::detectFormat(QString(), QStringLiteral("hello world\n")),
                 int(Replayer::UnknownFormat));
        QCOMPARE(Replayer::detectFormat(QStringLiteral("x.bin"), QStringLiteral("garbage")),
                 int(Replayer::UnknownFormat));
    }

    void parseTextUnknownFormatFails()
    {
        QString err;
        const QVector<FrameData> frames =
            Replayer::parseText(QStringLiteral("hello world\n"), Replayer::UnknownFormat, &err);
        QVERIFY(frames.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    void parseTextNoValidFrames()
    {
        QString err;
        const QVector<FrameData> frames =
            Replayer::parseText(QStringLiteral("garbage,line,here\n"), Replayer::CsvFormat, &err);
        QVERIFY(frames.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    void normalizeEmptyAndSingle()
    {
        QVector<FrameData> empty;
        QCOMPARE(Replayer::normalizeTimeline(empty), qint64(0));
        QCOMPARE(Replayer::totalDurationUs({}), qint64(0));

        QVector<FrameData> one{timed(0x1, 500)};
        QCOMPARE(Replayer::normalizeTimeline(one), qint64(0));
        QCOMPARE(one.first().wallUs, qint64(0));
    }

    void dueSchedulingEdge()
    {
        const QVector<FrameData> frames{timed(0x1, 0), timed(0x2, 1000), timed(0x3, 2000)};
        QCOMPARE(Replayer::dueFrameCount(frames, 1500, 0.0, 0), 2);   // speed 0 视作 1
        QCOMPARE(Replayer::dueFrameCount(frames, 1500, 1.0, -1), 0);  // 非法起点
        QCOMPARE(Replayer::dueFrameCount(frames, 1500, 1.0, 3), 0);   // 起点越界
        QCOMPARE(Replayer::dueFrameCount({}, 1500, 1.0, 0), 0);
    }

    void progressAndDelayEdges()
    {
        QCOMPARE(Replayer::progressPercent(-5, 1000), 0.0);
        QCOMPARE(Replayer::progressPercent(0, 0), 0.0);
        QCOMPARE(Replayer::scaledDelayUs(1000, -1.0), qint64(1000));
    }
};

QTEST_APPLESS_MAIN(TestReplayer)

#include "test_replayer.moc"
