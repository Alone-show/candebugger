#include "core/Replayer.h"

#include <QFile>
#include <QStringList>
#include <QTimer>

#include <cmath>
#include <memory>

#include "core/AsyncExecutor.h"
#include "core/CanDevice.h"
#include "core/FrameBuilder.h"
#include "core/Recorder.h"

namespace {
constexpr int kTickMs = 5;
const char kCsvSignature[] = "timestamp,channel,dir,id,type,dlc,data";
} // namespace

Replayer::Replayer(CanDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &Replayer::tick);

    m_executor = new AsyncExecutor(this);
    connect(m_executor, &AsyncExecutor::busyChanged, this, &Replayer::busyChanged);

    startSendThread();
}

Replayer::~Replayer()
{
    stop();
    stopSendThread();
}

// ---------------------------------------------------------------- 纯逻辑

int Replayer::detectFormat(const QString &path, const QString &content)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QLatin1String(".asc")))
        return AscFormat;
    if (lower.endsWith(QLatin1String(".csv")))
        return CsvFormat;

    if (!content.isEmpty()) {
        if (content.contains(QLatin1String("Begin Triggerblock"))
            || content.contains(QLatin1String("base hex")))
            return AscFormat;
        if (content.contains(QLatin1String(kCsvSignature)))
            return CsvFormat;

        const QStringList lines = content.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            FrameData f;
            qint64 rel = 0;
            if (Recorder::parseAscLine(line, &f, &rel))
                return AscFormat;
            if (Recorder::parseCsvLine(line, &f, &rel))
                return CsvFormat;
        }
    }
    return UnknownFormat;
}

QVector<FrameData> Replayer::parseText(const QString &content, int format, QString *error)
{
    if (error)
        error->clear();

    int fmt = format;
    if (fmt == UnknownFormat)
        fmt = detectFormat(QString(), content);
    if (fmt == UnknownFormat) {
        if (error)
            *error = QStringLiteral("无法识别文件格式（非 ASC/CSV）");
        return {};
    }

    QVector<FrameData> out;
    const QStringList lines = content.split(QLatin1Char('\n'));
    out.reserve(lines.size());
    for (const QString &line : lines) {
        FrameData f;
        qint64 rel = 0;
        const bool ok = (fmt == AscFormat) ? Recorder::parseAscLine(line, &f, &rel)
                                           : Recorder::parseCsvLine(line, &f, &rel);
        if (ok)
            out.append(f);
    }
    if (out.isEmpty() && error)
        *error = QStringLiteral("文件中未解析到有效帧");
    return out;
}

QVector<FrameData> Replayer::parseFile(const QString &path, int *format, QString *error)
{
    if (error)
        error->clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("无法打开文件：%1").arg(file.errorString());
        return {};
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    const int fmt = detectFormat(path, content);
    if (format)
        *format = fmt;
    return parseText(content, fmt, error);
}

qint64 Replayer::normalizeTimeline(QVector<FrameData> &frames)
{
    if (frames.isEmpty())
        return 0;
    const qint64 base = frames.first().wallUs;
    for (FrameData &f : frames) {
        f.wallUs -= base;
        f.hostUs -= base;
    }
    return frames.last().wallUs;
}

qint64 Replayer::totalDurationUs(const QVector<FrameData> &frames)
{
    if (frames.isEmpty())
        return 0;
    return frames.last().wallUs - frames.first().wallUs;
}

double Replayer::progressPercent(qint64 positionUs, qint64 durationUs)
{
    if (durationUs <= 0)
        return 0.0;
    return qBound(0.0, static_cast<double>(positionUs) * 100.0 / static_cast<double>(durationUs),
                  100.0);
}

qint64 Replayer::scaledDelayUs(qint64 deltaUs, double speed)
{
    if (speed <= 0.0)
        return deltaUs;
    return static_cast<qint64>(std::llround(static_cast<double>(deltaUs) / speed));
}

int Replayer::dueFrameCount(const QVector<FrameData> &frames, qint64 elapsedUs,
                            double speed, int startIndex)
{
    if (startIndex < 0 || startIndex >= frames.size())
        return 0;
    const double s = (speed > 0.0) ? speed : 1.0;
    const qint64 limit = static_cast<qint64>(static_cast<double>(elapsedUs) * s);
    int i = startIndex;
    while (i < frames.size() && frames.at(i).wallUs <= limit)
        ++i;
    return i - startIndex;
}

// ---------------------------------------------------------------- 加载 / 回放

bool Replayer::load(const QString &path)
{
    stop();

    int fmt = UnknownFormat;
    QString err;
    QVector<FrameData> frames = parseFile(path, &fmt, &err);
    if (frames.isEmpty()) {
        setError(err.isEmpty() ? QStringLiteral("文件为空或无可回放帧") : err);
        return false;
    }

    const qint64 duration = normalizeTimeline(frames);
    m_frames = frames;
    m_filePath = path;
    m_format = fmt;
    m_durationUs = duration;
    m_index = 0;
    m_positionUs = 0;
    m_elapsedBaseUs = 0;
    setError(QString());

    emit filePathChanged();
    emit frameCountChanged();
    emit durationChanged();
    emit positionChanged();
    return true;
}

bool Replayer::loadAsync(const QString &path)
{
    if (busy())
        return false;
    stop();   // GUI 线程：停止当前回放

    auto frames = std::make_shared<QVector<FrameData>>();
    auto fmt = std::make_shared<int>(UnknownFormat);
    auto err = std::make_shared<QString>();
    m_executor->post(
        [path, frames, fmt, err]() {
            *fmt = UnknownFormat;
            *frames = parseFile(path, fmt.get(), err.get());
        },
        [this, path, frames, fmt, err]() {
            applyLoad(path, *frames, *fmt, *err);
        });
    return true;
}

void Replayer::applyLoad(const QString &path, QVector<FrameData> frames, int format,
                         const QString &error)
{
    if (frames.isEmpty()) {
        setError(error.isEmpty() ? QStringLiteral("文件为空或无可回放帧") : error);
        emit loadFinished(false, m_error);
        return;
    }

    const qint64 duration = normalizeTimeline(frames);
    m_frames = frames;
    m_filePath = path;
    m_format = format;
    m_durationUs = duration;
    m_index = 0;
    m_positionUs = 0;
    m_elapsedBaseUs = 0;
    setError(QString());

    emit filePathChanged();
    emit frameCountChanged();
    emit durationChanged();
    emit positionChanged();
    emit loadFinished(true, QString());
}

bool Replayer::play(int port)
{
    if (m_frames.isEmpty()) {
        setError(QStringLiteral("未加载回放文件"));
        return false;
    }
    if (port >= 0)
        setPort(port);

    if (m_playing) {
        if (m_paused)
            resume();
        return true;
    }

    clearSendQueue();
    m_index = 0;
    m_positionUs = 0;
    m_elapsedBaseUs = 0;
    m_clock.start();
    m_playing = true;
    m_paused = false;
    m_sendPaused.store(false, std::memory_order_relaxed);
    m_timer->start();

    emit playingChanged();
    emit pausedChanged();
    emit positionChanged();
    return true;
}

void Replayer::pause()
{
    if (!m_playing || m_paused)
        return;
    m_elapsedBaseUs += m_clock.nsecsElapsed() / 1000;
    m_timer->stop();
    m_paused = true;
    m_sendPaused.store(true, std::memory_order_relaxed);
    {
        QMutexLocker lock(&m_sendMutex);
        m_sendCond.wakeAll();
    }
    emit pausedChanged();
}

void Replayer::resume()
{
    if (!m_playing || !m_paused)
        return;
    m_clock.restart();
    m_paused = false;
    m_sendPaused.store(false, std::memory_order_relaxed);
    {
        QMutexLocker lock(&m_sendMutex);
        m_sendCond.wakeAll();
    }
    m_timer->start();
    emit pausedChanged();
}

void Replayer::stop()
{
    if (m_timer)
        m_timer->stop();
    const bool wasPlaying = m_playing;
    clearSendQueue();
    resetPlayback();
    if (wasPlaying)
        emit playingChanged();
}

void Replayer::resetPlayback()
{
    m_playing = false;
    m_paused = false;
    m_index = 0;
    m_positionUs = 0;
    m_elapsedBaseUs = 0;
    m_sendPaused.store(false, std::memory_order_relaxed);
    emit positionChanged();
}

void Replayer::tick()
{
    if (!m_playing || m_paused)
        return;

    drainPendingError();

    const qint64 elapsed = m_elapsedBaseUs + m_clock.nsecsElapsed() / 1000;
    const int due = dueFrameCount(m_frames, elapsed, m_speed, m_index);
    for (int i = 0; i < due; ++i) {
        enqueueSend(m_frames.at(m_index));   // 非阻塞，发送在后台线程
        ++m_index;
    }

    const qint64 scaledPos = static_cast<qint64>(static_cast<double>(elapsed) * m_speed);
    m_positionUs = qMin(scaledPos, m_durationUs);
    emit positionChanged();

    if (m_index >= m_frames.size()) {
        // 自然结束：不清理发送队列，让后台线程把剩余帧发完
        m_timer->stop();
        m_playing = false;
        m_paused = false;
        m_positionUs = m_durationUs;
        emit positionChanged();
        emit playingChanged();
        emit finished();
    }
}

// ---------------------------------------------------------------- 发送线程

void Replayer::startSendThread()
{
    if (m_sendRun.load(std::memory_order_relaxed))
        return;
    m_sendRun.store(true, std::memory_order_relaxed);
    m_sendThread = std::thread(&Replayer::sendLoop, this);
}

void Replayer::stopSendThread()
{
    {
        QMutexLocker lock(&m_sendMutex);
        m_sendRun.store(false, std::memory_order_relaxed);
        m_sendPaused.store(false, std::memory_order_relaxed);
        m_sendCond.wakeAll();
    }
    if (m_sendThread.joinable())
        m_sendThread.join();
}

void Replayer::sendLoop()
{
    for (;;) {
        FrameData f;
        {
            QMutexLocker lock(&m_sendMutex);
            while (m_sendRun.load(std::memory_order_relaxed)
                   && (m_sendQueue.empty() || m_sendPaused.load(std::memory_order_relaxed)))
                m_sendCond.wait(&m_sendMutex);

            if (!m_sendRun.load(std::memory_order_relaxed) && m_sendQueue.empty())
                break;
            if (m_sendQueue.empty())
                continue;
            f = m_sendQueue.front();
            m_sendQueue.pop_front();
        }
        transmit(f);   // 可能阻塞，但不在 GUI 线程
    }
}

void Replayer::enqueueSend(const FrameData &f)
{
    {
        QMutexLocker lock(&m_sendMutex);
        m_sendQueue.push_back(f);
    }
    m_sendCond.wakeOne();
}

void Replayer::clearSendQueue()
{
    QMutexLocker lock(&m_sendMutex);
    m_sendQueue.clear();
}

bool Replayer::transmit(const FrameData &f)
{
    if (f.error)
        return true;
    if (!m_device || !m_device->isOpen()) {
        QMutexLocker lock(&m_sendMutex);
        m_pendingError = QStringLiteral("设备未打开，无法回放");
        return false;
    }

    FrameSpec spec;
    spec.id = f.id;
    spec.extended = f.extended;
    spec.fd = f.fd;
    spec.remote = f.remote;
    spec.brs = f.brs;
    spec.dlc = f.len;
    spec.data = f.data;
    spec.txm = ZCAN_TX_NORM;
    spec.echo = true;

    QString err;
    bool ok = false;
    if (f.fd) {
        ZCAN_FD_MSG msg;
        if (!FrameBuilder::buildFd(spec, &msg, &err)) {
            QMutexLocker lock(&m_sendMutex);
            m_pendingError = err;
            return false;
        }
        ok = m_device->transmitFd(static_cast<quint32>(m_port), msg);
        if (!ok)
            err = QStringLiteral("FD 帧发送失败");
    } else {
        ZCAN_20_MSG msg;
        if (!FrameBuilder::buildCan(spec, &msg, &err)) {
            QMutexLocker lock(&m_sendMutex);
            m_pendingError = err;
            return false;
        }
        ok = m_device->transmitCan(static_cast<quint32>(m_port), msg);
        if (!ok)
            err = QStringLiteral("CAN 帧发送失败");
    }

    if (!ok) {
        QMutexLocker lock(&m_sendMutex);
        m_pendingError = err;
    }
    return ok;
}

void Replayer::drainPendingError()
{
    QString err;
    {
        QMutexLocker lock(&m_sendMutex);
        err = m_pendingError;
        m_pendingError.clear();
    }
    if (!err.isEmpty())
        setError(err);
}

void Replayer::setError(const QString &msg)
{
    if (m_error == msg)
        return;
    m_error = msg;
    emit errorStringChanged();
}

// ---------------------------------------------------------------- 属性

QString Replayer::filePath() const { return m_filePath; }
int Replayer::format() const { return m_format; }
int Replayer::frameCount() const { return m_frames.size(); }
bool Replayer::isPlaying() const { return m_playing; }
bool Replayer::isPaused() const { return m_paused; }

double Replayer::speed() const { return m_speed; }

void Replayer::setSpeed(double speed)
{
    const double v = qBound(0.1, speed, 100.0);
    if (qFuzzyCompare(v, m_speed))
        return;
    // 切换倍速时以当前已播放时间重新起算，保持位置连续
    if (m_playing && !m_paused) {
        m_elapsedBaseUs += m_clock.nsecsElapsed() / 1000;
        m_clock.restart();
    }
    m_speed = v;
    emit speedChanged();
}

int Replayer::port() const { return m_port; }

void Replayer::setPort(int port)
{
    const int v = qBound(0, port, 255);
    if (v == m_port)
        return;
    m_port = v;
    emit portChanged();
}

qint64 Replayer::durationUs() const { return m_durationUs; }
qint64 Replayer::positionUs() const { return m_positionUs; }

double Replayer::progress() const
{
    return progressPercent(m_positionUs, m_durationUs);
}

QString Replayer::errorString() const { return m_error; }

bool Replayer::busy() const { return m_executor && m_executor->busy(); }
