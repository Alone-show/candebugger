#include "core/Recorder.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace {

constexpr int kPollIntervalMs = 200;
constexpr int kWriteBatchMax = 4096;

QString hexId(quint32 id, bool extended)
{
    QString hex = QString::number(id, 16).toUpper();
    hex = hex.rightJustified(extended ? 8 : 3, QLatin1Char('0'));
    return hex;
}

QString hexBytes(const QByteArray &data)
{
    QStringList parts;
    parts.reserve(data.size());
    for (const char ch : data)
        parts << QStringLiteral("%1")
                     .arg(static_cast<unsigned char>(ch), 2, 16, QLatin1Char('0'))
                     .toUpper();
    return parts.join(QLatin1Char(' '));
}

QByteArray parseHexBytes(const QString &text)
{
    QByteArray out;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                         Qt::SkipEmptyParts);
    out.reserve(parts.size());
    for (const QString &p : parts) {
        bool ok = false;
        const uint v = p.toUInt(&ok, 16);
        if (!ok || v > 0xFF)
            return QByteArray();
        out.append(static_cast<char>(v));
    }
    return out;
}

} // namespace

Recorder::Recorder(QObject *parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &Recorder::pollStats);
}

Recorder::~Recorder()
{
    stop();
}

// ---------------------------------------------------------------- 纯逻辑：格式

double Recorder::relativeSeconds(qint64 wallUs, qint64 startWallUs)
{
    return static_cast<double>(wallUs - startWallUs) / 1000000.0;
}

QString Recorder::ascHeader(const QDateTime &start)
{
    const QString date = start.toString(QStringLiteral("ddd MMM d HH:mm:ss yyyy"));
    QString out;
    out += QStringLiteral("date %1\n").arg(date);
    out += QStringLiteral("base hex  timestamps relative\n");
    out += QStringLiteral("internal events logged\n");
    out += QStringLiteral("// CanDebugger ASC 1.0\n");
    out += QStringLiteral("Begin Triggerblock %1\n").arg(date);
    out += QStringLiteral("   0.000000 Start of measurement\n");
    return out;
}

QString Recorder::ascFooter()
{
    return QStringLiteral("End TriggerBlock\n");
}

QString Recorder::csvHeader()
{
    return QStringLiteral("timestamp,channel,dir,id,type,dlc,data\n");
}

QString Recorder::formatAscLine(const FrameData &f, qint64 startWallUs)
{
    const double t = relativeSeconds(f.wallUs, startWallUs);
    QString idText = hexId(f.id, f.extended);
    if (f.extended)
        idText += QLatin1Char('x');

    QString kind;
    if (f.error)
        kind = QStringLiteral("e");
    else if (f.remote)
        kind = f.fd ? QStringLiteral("fdr") : QStringLiteral("r");
    else if (f.fd)
        kind = f.brs ? QStringLiteral("fdb") : QStringLiteral("fd");
    else
        kind = QStringLiteral("d");

    QStringList parts;
    parts << QString::number(t, 'f', 6)
          << QString::number(f.channel)
          << idText
          << (f.tx ? QStringLiteral("Tx") : QStringLiteral("Rx"))
          << kind
          << QString::number(f.len);
    if (!f.error && !f.remote && !f.data.isEmpty())
        parts << hexBytes(f.data);
    return parts.join(QLatin1Char(' ')) + QLatin1Char('\n');
}

QString Recorder::formatCsvLine(const FrameData &f, qint64 startWallUs)
{
    const double t = relativeSeconds(f.wallUs, startWallUs);
    QString idText = hexId(f.id, f.extended);
    if (f.extended)
        idText += QLatin1Char('x');

    QString type;
    if (f.error)
        type = QStringLiteral("ERROR");
    else if (f.remote)
        type = f.fd ? QStringLiteral("CANFD_RTR") : QStringLiteral("CAN_RTR");
    else if (f.fd)
        type = f.brs ? QStringLiteral("CANFD_BRS") : QStringLiteral("CANFD");
    else
        type = QStringLiteral("CAN");

    const QString data = (f.error || f.remote) ? QString() : hexBytes(f.data);

    QStringList parts;
    parts << QString::number(t, 'f', 6)
          << QString::number(f.channel)
          << (f.tx ? QStringLiteral("Tx") : QStringLiteral("Rx"))
          << idText
          << type
          << QString::number(f.len)
          << data;
    return parts.join(QLatin1Char(',')) + QLatin1Char('\n');
}

bool Recorder::parseAscLine(const QString &line, FrameData *out, qint64 *relUs)
{
    const QString s = line.trimmed();
    if (s.isEmpty())
        return false;

    const QStringList tok = s.split(QRegularExpression(QStringLiteral("\\s+")),
                                    Qt::SkipEmptyParts);
    if (tok.size() < 6)
        return false;

    bool ok = false;
    const double t = tok.at(0).toDouble(&ok);
    if (!ok)
        return false;
    const int ch = tok.at(1).toInt(&ok);
    if (!ok)
        return false;

    QString idText = tok.at(2);
    const bool extended = idText.endsWith(QLatin1Char('x'), Qt::CaseInsensitive);
    if (extended)
        idText.chop(1);
    const quint32 id = idText.toUInt(&ok, 16);
    if (!ok)
        return false;

    const QString dir = tok.at(3);
    const QString kind = tok.at(4).toLower();
    const int dlc = tok.at(5).toInt(&ok);
    if (!ok)
        return false;

    FrameData f;
    f.id = id;
    f.extended = extended;
    f.channel = static_cast<quint8>(qBound(0, ch, 255));
    f.tx = dir.startsWith(QLatin1Char('T'), Qt::CaseInsensitive);
    f.len = static_cast<quint8>(qBound(0, dlc, 64));

    if (kind == QLatin1String("e") || kind == QLatin1String("error")) {
        f.error = true;
        f.len = 0;
    } else if (kind == QLatin1String("r") || kind == QLatin1String("fdr")) {
        f.remote = true;
        f.fd = (kind == QLatin1String("fdr"));
        f.len = 0;
    } else {
        f.fd = kind.startsWith(QLatin1String("fd"));
        f.brs = (kind == QLatin1String("fdb"));
    }

    if (!f.error && !f.remote && tok.size() > 6) {
        f.data = parseHexBytes(tok.mid(6).join(QLatin1Char(' ')));
        if (f.data.size() != f.len)
            f.len = static_cast<quint8>(qBound(0, f.data.size(), 64));
    }

    const qint64 us = static_cast<qint64>(t * 1000000.0 + 0.5);
    f.wallUs = us;
    f.hostUs = us;
    if (out)
        *out = f;
    if (relUs)
        *relUs = us;
    return true;
}

bool Recorder::parseCsvLine(const QString &line, FrameData *out, qint64 *relUs)
{
    const QString s = line.trimmed();
    if (s.isEmpty())
        return false;

    const QStringList tok = s.split(QLatin1Char(','));
    if (tok.size() < 7)
        return false;
    if (tok.at(0).trimmed().compare(QStringLiteral("timestamp"), Qt::CaseInsensitive) == 0)
        return false;

    bool ok = false;
    const double t = tok.at(0).trimmed().toDouble(&ok);
    if (!ok)
        return false;
    const int ch = tok.at(1).trimmed().toInt(&ok);
    if (!ok)
        return false;

    const QString dir = tok.at(2).trimmed();
    QString idText = tok.at(3).trimmed();
    const bool extended = idText.endsWith(QLatin1Char('x'), Qt::CaseInsensitive);
    if (extended)
        idText.chop(1);
    const quint32 id = idText.toUInt(&ok, 16);
    if (!ok)
        return false;

    const QString type = tok.at(4).trimmed().toUpper();
    const int dlc = tok.at(5).trimmed().toInt(&ok);
    if (!ok)
        return false;

    FrameData f;
    f.id = id;
    f.extended = extended;
    f.channel = static_cast<quint8>(qBound(0, ch, 255));
    f.tx = dir.startsWith(QLatin1Char('T'), Qt::CaseInsensitive);
    f.len = static_cast<quint8>(qBound(0, dlc, 64));

    if (type == QLatin1String("ERROR")) {
        f.error = true;
        f.len = 0;
    } else if (type.endsWith(QLatin1String("RTR"))) {
        f.remote = true;
        f.fd = type.startsWith(QLatin1String("CANFD"));
        f.len = 0;
    } else {
        f.fd = type.startsWith(QLatin1String("CANFD"));
        f.brs = type.contains(QLatin1String("BRS"));
    }

    if (!f.error && !f.remote) {
        f.data = parseHexBytes(tok.at(6));
        if (f.data.size() != f.len)
            f.len = static_cast<quint8>(qBound(0, f.data.size(), 64));
    }

    const qint64 us = static_cast<qint64>(t * 1000000.0 + 0.5);
    f.wallUs = us;
    f.hostUs = us;
    if (out)
        *out = f;
    if (relUs)
        *relUs = us;
    return true;
}

QString Recorder::defaultFileName(const QDateTime &dt, int format)
{
    const QString stamp = dt.toString(QStringLiteral("yyyyMMdd_hhmmss"));
    const QString ext = (format == CsvFormat) ? QStringLiteral("csv") : QStringLiteral("asc");
    return QStringLiteral("record_%1.%2").arg(stamp, ext);
}

// ---------------------------------------------------------------- 生命周期

bool Recorder::start(const QString &directory, bool asc, bool csv)
{
    if (m_recording)
        return true;
    if (!asc && !csv) {
        setError(QStringLiteral("至少选择一种记录格式"));
        return false;
    }

    const QString dirPath = directory.trimmed().isEmpty() ? QDir::currentPath() : directory.trimmed();
    QDir dir(dirPath);
    if (!dir.exists()) {
        setError(QStringLiteral("目录不存在：%1").arg(dirPath));
        return false;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString ascPath = asc ? dir.filePath(defaultFileName(now, AscFormat)) : QString();
    const QString csvPath = csv ? dir.filePath(defaultFileName(now, CsvFormat)) : QString();
    return startWithPaths(ascPath, csvPath);
}

bool Recorder::startWithPaths(const QString &ascPath, const QString &csvPath)
{
    if (m_recording)
        return true;
    if (ascPath.isEmpty() && csvPath.isEmpty()) {
        setError(QStringLiteral("未指定记录文件"));
        return false;
    }

    std::unique_ptr<QFile> ascFile;
    std::unique_ptr<QFile> csvFile;

    if (!ascPath.isEmpty()) {
        ascFile.reset(new QFile(ascPath));
        if (!ascFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            setError(QStringLiteral("无法写入 ASC 文件：%1").arg(ascFile->errorString()));
            return false;
        }
    }
    if (!csvPath.isEmpty()) {
        csvFile.reset(new QFile(csvPath));
        if (!csvFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            setError(QStringLiteral("无法写入 CSV 文件：%1").arg(csvFile->errorString()));
            return false;
        }
    }

    m_startTime = QDateTime::currentDateTime();
    m_startWallUs = m_startTime.toMSecsSinceEpoch() * 1000;
    {
        QMutexLocker lock(&m_mutex);
        m_queue.clear();
        m_written = 0;
        m_dropped = 0;
        m_error.clear();
    }

    m_ascPath = ascPath;
    m_csvPath = csvPath;
    m_ascFile = ascFile.release();
    m_csvFile = csvFile.release();

    if (m_ascFile)
        m_ascFile->write(ascHeader(m_startTime).toUtf8());
    if (m_csvFile)
        m_csvFile->write(csvHeader().toUtf8());

    m_running.store(true, std::memory_order_relaxed);
    m_thread = std::thread(&Recorder::writerLoop, this);
    m_recording = true;
    m_pollTimer->start();

    emit filePathChanged();
    emit recordingChanged();
    emit statsChanged();
    return true;
}

void Recorder::stop()
{
    if (!m_recording)
        return;

    {
        // 在锁内修改条件并唤醒，避免写盘线程刚好进入 wait 时丢失唤醒
        QMutexLocker lock(&m_mutex);
        m_running.store(false, std::memory_order_relaxed);
        m_cond.wakeAll();
    }
    if (m_thread.joinable())
        m_thread.join();

    closeFiles();
    m_pollTimer->stop();
    m_recording = false;

    pollStats();
    emit recordingChanged();
}

void Recorder::closeFiles()
{
    if (m_ascFile) {
        m_ascFile->write(ascFooter().toUtf8());
        m_ascFile->close();
        delete m_ascFile;
        m_ascFile = nullptr;
    }
    if (m_csvFile) {
        m_csvFile->close();
        delete m_csvFile;
        m_csvFile = nullptr;
    }
}

// ---------------------------------------------------------------- 生产端

void Recorder::enqueueFrames(const QVector<FrameData> &frames)
{
    if (!m_recording)
        return;

    {
        QMutexLocker lock(&m_mutex);
        for (const FrameData &f : frames) {
            // 接收路径中的 tx 帧（回显/回环）代表本机发送，已由发送路径记录，
            // 此处跳过以避免重复写入（风险 17/19）。
            if (f.tx)
                continue;
            if (static_cast<int>(m_queue.size()) >= m_queueMax) {
                ++m_dropped;
                continue;
            }
            m_queue.push_back(f);
        }
    }
    m_cond.wakeOne();
}

void Recorder::enqueueFrame(const FrameData &f)
{
    if (!m_recording)
        return;

    {
        QMutexLocker lock(&m_mutex);
        if (static_cast<int>(m_queue.size()) >= m_queueMax) {
            ++m_dropped;
            return;
        }
        m_queue.push_back(f);
    }
    m_cond.wakeOne();
}

// ---------------------------------------------------------------- 写盘线程

void Recorder::writerLoop()
{
    QVector<FrameData> batch;
    batch.reserve(kWriteBatchMax);

    for (;;) {
        {
            QMutexLocker lock(&m_mutex);
            while (m_queue.empty() && m_running.load(std::memory_order_relaxed))
                m_cond.wait(&m_mutex);
            if (m_queue.empty() && !m_running.load(std::memory_order_relaxed))
                break;

            batch.clear();
            const int n = qMin<int>(kWriteBatchMax, static_cast<int>(m_queue.size()));
            for (int i = 0; i < n; ++i) {
                batch.append(m_queue.front());
                m_queue.pop_front();
            }
        }

        writeBatch(batch);

        QMutexLocker lock(&m_mutex);
        m_written += static_cast<qulonglong>(batch.size());
    }
}

void Recorder::writeBatch(const QVector<FrameData> &batch)
{
    if (!m_ascFile && !m_csvFile)
        return;

    for (const FrameData &f : batch) {
        if (m_ascFile) {
            const QByteArray line = formatAscLine(f, m_startWallUs).toUtf8();
            if (m_ascFile->write(line) != line.size()) {
                setError(QStringLiteral("ASC 写入失败：%1").arg(m_ascFile->errorString()));
                return;
            }
        }
        if (m_csvFile) {
            const QByteArray line = formatCsvLine(f, m_startWallUs).toUtf8();
            if (m_csvFile->write(line) != line.size()) {
                setError(QStringLiteral("CSV 写入失败：%1").arg(m_csvFile->errorString()));
                return;
            }
        }
    }

    if (m_ascFile)
        m_ascFile->flush();
    if (m_csvFile)
        m_csvFile->flush();
}

// ---------------------------------------------------------------- 属性

bool Recorder::isRecording() const
{
    return m_recording;
}

int Recorder::formatFlags() const
{
    return m_formatFlags;
}

void Recorder::setFormatFlags(int flags)
{
    const int v = flags & (AscFormat | CsvFormat);
    if (v == m_formatFlags || v == 0)
        return;
    m_formatFlags = v;
    emit formatFlagsChanged();
}

QString Recorder::ascPath() const { return m_ascPath; }
QString Recorder::csvPath() const { return m_csvPath; }

QString Recorder::filePath() const
{
    if (!m_ascPath.isEmpty())
        return m_ascPath;
    return m_csvPath;
}

int Recorder::queueCapacity() const
{
    return m_queueMax;
}

void Recorder::setQueueCapacity(int capacity)
{
    const int v = qBound(1, capacity, 10000000);
    if (v == m_queueMax)
        return;
    m_queueMax = v;
    emit queueCapacityChanged();
}

qulonglong Recorder::writtenCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_written;
}

qulonglong Recorder::droppedCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_dropped;
}

QString Recorder::errorString() const
{
    QMutexLocker lock(&m_mutex);
    return m_error;
}

void Recorder::setError(const QString &msg)
{
    QMutexLocker lock(&m_mutex);
    m_error = msg;
}

void Recorder::pollStats()
{
    bool statsDirty = false;
    QString err;
    {
        QMutexLocker lock(&m_mutex);
        if (m_written != m_emittedWritten || m_dropped != m_emittedDropped) {
            m_emittedWritten = m_written;
            m_emittedDropped = m_dropped;
            statsDirty = true;
        }
        err = m_error;
    }

    if (statsDirty)
        emit statsChanged();
    if (err != m_emittedError) {
        m_emittedError = err;
        emit errorStringChanged();
    }
}
