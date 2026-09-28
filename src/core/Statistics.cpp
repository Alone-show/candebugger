#include "core/Statistics.h"

#include <QTimer>
#include <QVariantMap>
#include <algorithm>

#include "core/CanDevice.h"

namespace {

// 帧位长近似（含填充位余量），仅用于负载率量级评估：
// - 标准数据帧开销 ~47 bit，扩展帧 ~67 bit（仲裁+控制+CRC+ACK+EOF+IFS+填充余量）
// - CANFD 额外增加 FDF/BRS/ESI/DLC/CRC 等约 20 bit
int overheadBits(const FrameData &f)
{
    int bits = f.extended ? 67 : 47;
    if (f.fd)
        bits += 20;
    return bits;
}

} // namespace

Statistics::Statistics(CanDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
    m_clock.start();
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        update(m_clock.nsecsElapsed() / 1000);
    });
}

Statistics::~Statistics()
{
    stop();
}

double Statistics::framesPerSecond(quint64 frames, qint64 elapsedUs)
{
    if (elapsedUs <= 0)
        return 0.0;
    return static_cast<double>(frames) * 1000000.0 / static_cast<double>(elapsedUs);
}

void Statistics::frameBitSplit(const FrameData &f, int *arbBits, int *dataBits)
{
    const int maxData = f.fd ? 64 : 8;
    const int dataBytes = f.remote ? 0 : qBound(0, static_cast<int>(f.len), maxData);
    const int dataBitLen = 8 * dataBytes;
    const bool dataPhase = f.fd && f.brs;   // 仅 FD+BRS 的数据段走数据域波特率

    const int arb = overheadBits(f) + (dataPhase ? 0 : dataBitLen);
    if (arbBits)
        *arbBits = arb;
    if (dataBits)
        *dataBits = dataPhase ? dataBitLen : 0;
}

int Statistics::frameBitEstimate(const FrameData &f)
{
    int arb = 0;
    int data = 0;
    frameBitSplit(f, &arb, &data);
    return arb + data;
}

quint64 Statistics::topKey(const FrameData &f)
{
    return (static_cast<quint64>(f.extended ? 1u : 0u) << 32) | (f.id & 0xFFFFFFFFu);
}

QVariantList Statistics::topNList(const QHash<quint64, quint64> &counts, int n)
{
    QVector<QPair<quint64, quint64>> items;   // (count, key)
    items.reserve(counts.size());
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        items.append(qMakePair(it.value(), it.key()));

    std::sort(items.begin(), items.end(), [](const QPair<quint64, quint64> &a,
                                             const QPair<quint64, quint64> &b) {
        if (a.first != b.first)
            return a.first > b.first;    // 计数降序
        return a.second < b.second;      // 计数相同时按 key 升序稳定
    });

    QVariantList out;
    const int total = qMin(qMax(0, n), items.size());
    for (int i = 0; i < total; ++i) {
        const quint64 key = items.at(i).second;
        const bool extended = (key >> 32) != 0;
        const quint32 id = static_cast<quint32>(key & 0xFFFFFFFFu);
        QString hex = QString::number(id, 16).toUpper();
        hex = hex.rightJustified(extended ? 8 : 3, QLatin1Char('0'));

        QVariantMap map;
        map.insert(QStringLiteral("id"), id);
        map.insert(QStringLiteral("extended"), extended);
        map.insert(QStringLiteral("idText"), QStringLiteral("0x") + hex);
        map.insert(QStringLiteral("count"), items.at(i).first);
        out.append(map);
    }
    return out;
}

void Statistics::addFrames(const QVector<FrameData> &frames)
{
    for (const FrameData &f : frames) {
        if (f.tx) {
            // 回显帧代表本机发送，txCount 由发送路径累计，这里只计入总线流量
        } else {
            ++m_rx;
            ++m_winRx;
        }

        if (f.error)
            ++m_errors;

        ++m_top[topKey(f)];

        if (m_arbBps > 0 || m_dataBps > 0) {
            int arb = 0;
            int data = 0;
            frameBitSplit(f, &arb, &data);
            if (m_arbBps > 0)
                m_winBusTimeUs += static_cast<double>(arb) * 1000000.0 / m_arbBps;
            if (m_dataBps > 0)
                m_winBusTimeUs += static_cast<double>(data) * 1000000.0 / m_dataBps;
        }
    }
}

void Statistics::countSent(int n)
{
    if (n <= 0)
        return;
    m_tx += static_cast<quint64>(n);
    m_winTx += static_cast<quint64>(n);
}

void Statistics::setActiveChannels(const QVector<int> &channels)
{
    m_channels = channels;
}

void Statistics::start(int intervalMs)
{
    if (intervalMs > 0)
        m_timer->setInterval(intervalMs);
    m_timer->start();
}

void Statistics::stop()
{
    if (m_timer)
        m_timer->stop();
}

void Statistics::update(qint64 nowUs)
{
    if (!m_haveWindow) {
        m_windowStartUs = nowUs;
        m_haveWindow = true;
        return;
    }

    const qint64 elapsed = nowUs - m_windowStartUs;
    if (elapsed <= 0)
        return;

    m_rxFps = framesPerSecond(m_winRx, elapsed);
    m_txFps = framesPerSecond(m_winTx, elapsed);
    const double load = m_winBusTimeUs / static_cast<double>(elapsed) * 100.0;
    m_busLoad = qBound(0.0, load, 100.0);

    m_windowStartUs = nowUs;
    m_winRx = 0;
    m_winTx = 0;
    m_winBusTimeUs = 0.0;

    emit updated();

    const QVariantList list = topNList(m_top, m_topNCount);
    if (list != m_topN) {
        m_topN = list;
        emit topNChanged();
    }
}

void Statistics::reset()
{
    m_rx = 0;
    m_tx = 0;
    m_errors = 0;
    m_winRx = 0;
    m_winTx = 0;
    m_winBusTimeUs = 0.0;
    m_rxFps = 0.0;
    m_txFps = 0.0;
    m_busLoad = 0.0;
    m_haveWindow = false;
    m_windowStartUs = 0;
    m_top.clear();

    const QVariantList empty;
    const bool had = !m_topN.isEmpty();
    m_topN = empty;
    emit updated();
    if (had)
        emit topNChanged();
}

double Statistics::rxFps() const { return m_rxFps; }
double Statistics::txFps() const { return m_txFps; }
double Statistics::busLoad() const { return m_busLoad; }
qulonglong Statistics::totalFrames() const { return m_rx + m_tx; }
qulonglong Statistics::rxCount() const { return m_rx; }
qulonglong Statistics::txCount() const { return m_tx; }
qulonglong Statistics::errorFrames() const { return m_errors; }
QVariantList Statistics::topN() const { return m_topN; }

int Statistics::topNCount() const { return m_topNCount; }
void Statistics::setTopNCount(int n)
{
    n = qBound(1, n, 64);
    if (n == m_topNCount)
        return;
    m_topNCount = n;
    m_topN = topNList(m_top, m_topNCount);
    emit topNCountChanged();
    emit topNChanged();
}

int Statistics::arbitrationBps() const { return m_arbBps; }
void Statistics::setArbitrationBps(int bps)
{
    const int v = qMax(0, bps);
    if (v == m_arbBps)
        return;
    m_arbBps = v;
    emit bitratesChanged();
}

int Statistics::dataBps() const { return m_dataBps; }
void Statistics::setDataBps(int bps)
{
    const int v = qMax(0, bps);
    if (v == m_dataBps)
        return;
    m_dataBps = v;
    emit bitratesChanged();
}
