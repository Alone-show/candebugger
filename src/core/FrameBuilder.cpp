#include "core/FrameBuilder.h"

#include <QDateTime>
#include <QRegularExpression>
#include <cstring>
#include <memory>

#include "core/AsyncExecutor.h"
#include "core/CanDevice.h"
#include "core/Recorder.h"
#include "core/Statistics.h"

FrameBuilder::FrameBuilder(CanDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
    m_executor = new AsyncExecutor(this);
    connect(m_executor, &AsyncExecutor::busyChanged, this, &FrameBuilder::busyChanged);
}

FrameBuilder::~FrameBuilder() = default;

// ---------------------------------------------------------------- 数据编解码

QByteArray FrameBuilder::parseData(const QString &text, int format, int maxBytes, QString *error)
{
    if (error)
        error->clear();

    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return QByteArray();
    };

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return QByteArray();

    switch (format) {
    case HexFormat: {
        QString s;
        s.reserve(trimmed.size());
        for (const QChar c : trimmed) {
            if (c.isSpace() || c == QLatin1Char(',') || c == QLatin1Char(';'))
                continue;
            s += c;
        }
        s.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
        if (s.isEmpty())
            return QByteArray();
        for (const QChar c : s) {
            if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                  || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
                  || (c >= QLatin1Char('A') && c <= QLatin1Char('F'))))
                return fail(QStringLiteral("包含非十六进制字符"));
        }
        if (s.size() % 2 != 0)
            return fail(QStringLiteral("十六进制位数必须为偶数"));
        if (s.size() / 2 > maxBytes)
            return fail(QStringLiteral("数据超过 %1 字节").arg(maxBytes));
        QByteArray out;
        out.reserve(s.size() / 2);
        for (int i = 0; i < s.size(); i += 2)
            out.append(static_cast<char>(s.mid(i, 2).toUInt(nullptr, 16)));
        return out;
    }
    case AsciiFormat: {
        const QByteArray raw = trimmed.toLatin1();
        if (raw.size() > maxBytes)
            return fail(QStringLiteral("数据超过 %1 字节").arg(maxBytes));
        return raw;
    }
    case DecimalFormat: {
        const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                                Qt::SkipEmptyParts);
        if (parts.size() > maxBytes)
            return fail(QStringLiteral("数据超过 %1 字节").arg(maxBytes));
        QByteArray out;
        out.reserve(parts.size());
        for (const QString &p : parts) {
            bool ok = false;
            const uint v = p.toUInt(&ok, 10);
            if (!ok || v > 255)
                return fail(QStringLiteral("字节越界（应为 0-255）"));
            out.append(static_cast<char>(v));
        }
        return out;
    }
    default:
        return fail(QStringLiteral("未知数据格式"));
    }
}

QString FrameBuilder::formatData(const QByteArray &data, int format)
{
    switch (format) {
    case AsciiFormat: {
        QString out;
        out.reserve(data.size());
        for (const char ch : data) {
            const unsigned char c = static_cast<unsigned char>(ch);
            out += (c >= 32 && c < 127) ? QChar(c) : QLatin1Char('.');
        }
        return out;
    }
    case DecimalFormat: {
        QStringList parts;
        parts.reserve(data.size());
        for (const char ch : data)
            parts << QString::number(static_cast<unsigned char>(ch));
        return parts.join(QLatin1Char(' '));
    }
    case HexFormat:
    default: {
        QStringList parts;
        parts.reserve(data.size());
        for (const char ch : data)
            parts << QStringLiteral("%1").arg(static_cast<unsigned char>(ch), 2, 16, QLatin1Char('0')).toUpper();
        return parts.join(QLatin1Char(' '));
    }
    }
}

quint32 FrameBuilder::clampId(quint32 id, bool extended)
{
    return extended ? (id & 0x1FFFFFFFu) : (id & 0x7FFu);
}

quint32 FrameBuilder::idForCounter(quint32 baseId, bool extended, bool incrementId, quint32 counter)
{
    const quint32 mask = extended ? 0x1FFFFFFFu : 0x7FFu;
    const quint32 base = baseId & mask;
    if (!incrementId)
        return base;
    return (base + counter) & mask;   // 超出位宽即回绕
}

quint8 FrameBuilder::crc8(const QByteArray &data)
{
    quint8 crc = 0;
    for (const char ch : data) {
        crc ^= static_cast<quint8>(ch);
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x80u) ? static_cast<quint8>((crc << 1) ^ 0x07u)
                                : static_cast<quint8>(crc << 1);
    }
    return crc;
}

QByteArray FrameBuilder::applyModifiers(const QByteArray &data, const FrameSpec &spec, int maxBytes)
{
    if (spec.remote)
        return QByteArray();

    const int len = qBound(0, spec.dlc, maxBytes);
    QByteArray out = data;
    if (out.size() < len)
        out.append(QByteArray(len - out.size(), '\0'));
    else if (out.size() > len)
        out.truncate(len);

    if (spec.increment) {
        for (int i = 0; i < out.size(); ++i)
            out[i] = static_cast<char>(i & 0xFF);
    }
    if (spec.rolling && !out.isEmpty())
        out[0] = static_cast<char>(spec.counter & 0xFF);
    if (spec.crc && !out.isEmpty())
        out[out.size() - 1] = static_cast<char>(crc8(out.left(out.size() - 1)));

    return out;
}

int FrameBuilder::maxLenFor(const FrameSpec &spec)
{
    return spec.fd ? 64 : 8;
}

// ---------------------------------------------------------------- 变体解析

bool FrameBuilder::specFromMap(const QVariantMap &map, FrameSpec *spec, QString *error)
{
    if (error)
        error->clear();
    if (!spec)
        return false;

    FrameSpec s;
    s.id = map.value(QStringLiteral("id"), 0x123).toUInt();
    s.extended = map.value(QStringLiteral("extended"), false).toBool();
    s.fd = map.value(QStringLiteral("fd"), false).toBool();
    s.remote = map.value(QStringLiteral("remote"), false).toBool();
    s.brs = map.value(QStringLiteral("brs"), false).toBool();
    s.txm = map.value(QStringLiteral("txm"), static_cast<int>(ZCAN_TX_NORM)).toInt();
    s.echo = map.value(QStringLiteral("echo"), true).toBool();
    s.crc = map.value(QStringLiteral("crc"), false).toBool();
    s.rolling = map.value(QStringLiteral("rolling"), false).toBool();
    s.increment = map.value(QStringLiteral("increment"), false).toBool();
    s.incrementId = map.value(QStringLiteral("incrementId"), false).toBool();
    s.counter = map.value(QStringLiteral("counter"), 0).toUInt();

    const int maxLen = maxLenFor(s);

    QByteArray parsed;
    if (!s.remote) {
        const int fmt = map.value(QStringLiteral("dataFormat"), static_cast<int>(HexFormat)).toInt();
        parsed = parseData(map.value(QStringLiteral("data"), QString()).toString(), fmt, maxLen, error);
        if (error && !error->isEmpty())
            return false;
    }
    s.data = parsed;
    s.dlc = qBound(0, map.value(QStringLiteral("dlc"), parsed.size()).toInt(), maxLen);

    *spec = s;
    return true;
}

QVariantMap FrameBuilder::specToMap(const FrameSpec &spec, int dataFormat)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), spec.id);
    map.insert(QStringLiteral("extended"), spec.extended);
    map.insert(QStringLiteral("fd"), spec.fd);
    map.insert(QStringLiteral("remote"), spec.remote);
    map.insert(QStringLiteral("brs"), spec.brs);
    map.insert(QStringLiteral("dlc"), spec.dlc);
    map.insert(QStringLiteral("txm"), spec.txm);
    map.insert(QStringLiteral("echo"), spec.echo);
    map.insert(QStringLiteral("crc"), spec.crc);
    map.insert(QStringLiteral("rolling"), spec.rolling);
    map.insert(QStringLiteral("increment"), spec.increment);
    map.insert(QStringLiteral("incrementId"), spec.incrementId);
    map.insert(QStringLiteral("dataFormat"), dataFormat);
    map.insert(QStringLiteral("data"), formatData(spec.data, dataFormat));
    return map;
}

// ---------------------------------------------------------------- 构建

bool FrameBuilder::buildCan(const FrameSpec &spec, ZCAN_20_MSG *out, QString *error)
{
    if (error)
        error->clear();
    if (!out) {
        if (error)
            *error = QStringLiteral("输出缓冲区为空");
        return false;
    }
    if (spec.fd) {
        if (error)
            *error = QStringLiteral("CAN2.0 构建收到 FD 标志");
        return false;
    }

    std::memset(out, 0, sizeof(*out));
    const int maxLen = 8;
    const QByteArray data = applyModifiers(spec.data, spec, maxLen);

    out->hdr.id = clampId(spec.id, spec.extended);
    out->hdr.inf.txm = static_cast<U32>(qBound(0, spec.txm, 3));
    out->hdr.inf.fmt = 0;
    out->hdr.inf.sdf = spec.remote ? 1u : 0u;
    out->hdr.inf.sef = spec.extended ? 1u : 0u;
    out->hdr.inf.brs = 0;
    out->hdr.inf.echo = spec.echo ? 1u : 0u;
    out->hdr.len = static_cast<U8>(qBound(0, spec.dlc, maxLen));
    for (int i = 0; i < data.size() && i < 8; ++i)
        out->dat[i] = static_cast<U8>(data.at(i));
    return true;
}

bool FrameBuilder::buildFd(const FrameSpec &spec, ZCAN_FD_MSG *out, QString *error)
{
    if (error)
        error->clear();
    if (!out) {
        if (error)
            *error = QStringLiteral("输出缓冲区为空");
        return false;
    }

    std::memset(out, 0, sizeof(*out));
    const int maxLen = 64;
    const QByteArray data = applyModifiers(spec.data, spec, maxLen);

    out->hdr.id = clampId(spec.id, spec.extended);
    out->hdr.inf.txm = static_cast<U32>(qBound(0, spec.txm, 3));
    out->hdr.inf.fmt = 1;
    out->hdr.inf.sdf = spec.remote ? 1u : 0u;
    out->hdr.inf.sef = spec.extended ? 1u : 0u;
    out->hdr.inf.brs = spec.brs ? 1u : 0u;
    out->hdr.inf.echo = spec.echo ? 1u : 0u;
    out->hdr.len = static_cast<U8>(qBound(0, spec.dlc, maxLen));
    for (int i = 0; i < data.size() && i < 64; ++i)
        out->dat[i] = static_cast<U8>(data.at(i));
    return true;
}

bool FrameBuilder::buildTtx(const FrameSpec &spec, ZCAN_FD_MSG *out, QString *error)
{
    if (error)
        error->clear();
    if (!out) {
        if (error)
            *error = QStringLiteral("输出缓冲区为空");
        return false;
    }

    std::memset(out, 0, sizeof(*out));
    const int maxLen = spec.fd ? 64 : 8;
    const QByteArray data = applyModifiers(spec.data, spec, maxLen);

    out->hdr.id = clampId(spec.id, spec.extended);
    out->hdr.inf.txm = static_cast<U32>(qBound(0, spec.txm, 3));
    out->hdr.inf.fmt = spec.fd ? 1u : 0u;
    out->hdr.inf.sdf = spec.remote ? 1u : 0u;
    out->hdr.inf.sef = spec.extended ? 1u : 0u;
    out->hdr.inf.brs = (spec.fd && spec.brs) ? 1u : 0u;
    out->hdr.inf.echo = spec.echo ? 1u : 0u;
    out->hdr.len = static_cast<U8>(qBound(0, spec.dlc, maxLen));
    for (int i = 0; i < data.size() && i < 64; ++i)
        out->dat[i] = static_cast<U8>(data.at(i));
    return true;
}

QString FrameBuilder::describe(const QVariantMap &map)
{
    FrameSpec s;
    QString err;
    if (!specFromMap(map, &s, &err))
        return QStringLiteral("错误: ") + err;

    const QString idText = QString::number(clampId(s.id, s.extended), 16)
                               .toUpper()
                               .rightJustified(s.extended ? 8 : 3, QLatin1Char('0'));
    QString out = (s.fd ? QStringLiteral("CANFD") : QStringLiteral("CAN"))
                  + QLatin1Char(' ')
                  + (s.extended ? QStringLiteral("扩展") : QStringLiteral("标准"))
                  + QStringLiteral(" 0x") + idText
                  + QStringLiteral(" DLC=") + QString::number(s.dlc);
    if (s.remote)
        out += QStringLiteral(" 远程帧");
    else if (!s.data.isEmpty())
        out += QLatin1Char(' ') + formatData(applyModifiers(s.data, s, maxLenFor(s)), HexFormat);
    return out;
}

// ---------------------------------------------------------------- QML 接口

QString FrameBuilder::convertData(const QString &text, int fromFormat, int toFormat, int maxBytes) const
{
    QString err;
    const QByteArray bytes = parseData(text, fromFormat, maxBytes, &err);
    if (!err.isEmpty())
        return text;
    return formatData(bytes, toFormat);
}

int FrameBuilder::dataByteCount(const QString &text, int format, int maxBytes) const
{
    QString err;
    const QByteArray bytes = parseData(text, format, maxBytes, &err);
    if (!err.isEmpty())
        return -1;
    return bytes.size();
}

bool FrameBuilder::sendFrame(int port, const QVariantMap &specMap)
{
    auto setError = [this](const QString &msg) {
        if (m_lastError == msg)
            return;
        m_lastError = msg;
        emit lastErrorChanged();
    };

    FrameSpec spec;
    QString err;
    if (!specFromMap(specMap, &spec, &err)) {
        setError(err);
        return false;
    }
    spec.counter = m_counter;
    // ID 递增：把计数器叠加到基址上（后续 build/记录统一用 spec.id）
    spec.id = idForCounter(spec.id, spec.extended, spec.incrementId, spec.counter);

    bool ok = false;
    if (spec.fd) {
        ZCAN_FD_MSG msg;
        if (!buildFd(spec, &msg, &err)) {
            setError(err);
            return false;
        }
        ok = m_device && m_device->transmitFd(static_cast<quint32>(port), msg);
    } else {
        ZCAN_20_MSG msg;
        if (!buildCan(spec, &msg, &err)) {
            setError(err);
            return false;
        }
        ok = m_device && m_device->transmitCan(static_cast<quint32>(port), msg);
    }

    // 滚动计数与 ID 递增共用同一个计数器，二者任一勾选都要推进
    if (spec.rolling || spec.incrementId)
        ++m_counter;

    if (ok) {
        ++m_sentCount;
        if (m_stats)
            m_stats->countSent(1);
        if (m_recorder) {
            // 发送路径记录：以发送成功为准，回显帧在接收侧被跳过，避免重复
            const qint64 wallUs = QDateTime::currentMSecsSinceEpoch() * 1000;
            FrameData f;
            f.wallUs = wallUs;
            f.hostUs = wallUs;
            f.id = clampId(spec.id, spec.extended);
            f.channel = static_cast<quint8>(qBound(0, port, 255));
            f.len = static_cast<quint8>(qBound(0, spec.dlc, spec.fd ? 64 : 8));
            f.extended = spec.extended;
            f.remote = spec.remote;
            f.fd = spec.fd;
            f.brs = spec.fd && spec.brs;
            f.tx = true;
            f.echo = true;
            if (!spec.remote)
                f.data = applyModifiers(spec.data, spec, spec.fd ? 64 : 8).left(f.len);
            m_recorder->enqueueFrame(f);
        }
        setError(QString());
        emit sentCountChanged();
    } else {
        setError(m_device
                     ? QStringLiteral("发送失败：总线无应答（请检查波特率/位定时、接线与终端电阻）")
                     : QStringLiteral("发送失败：设备未打开"));
    }
    return ok;
}

int FrameBuilder::sendBatch(int port, const QVariantList &specs)
{
    int sent = 0;
    for (const QVariant &v : specs) {
        if (sendFrame(port, v.toMap()))
            ++sent;
    }
    return sent;
}

// ---------------------------------------------------------------- 异步发送

bool FrameBuilder::enqueueOne(int port, const QVariantMap &specMap)
{
    FrameSpec spec;
    QString err;
    if (!specFromMap(specMap, &spec, &err)) {
        setError(err);
        emit sendCompleted(false, describe(specMap), err);
        return false;
    }
    spec.counter = m_counter;
    // ID 递增：构建前先叠加到基址，保证下发帧与记录帧的 ID 一致
    spec.id = idForCounter(spec.id, spec.extended, spec.incrementId, spec.counter);

    ZCAN_20_MSG can;
    ZCAN_FD_MSG fdf;
    if (spec.fd) {
        if (!buildFd(spec, &fdf, &err)) {
            setError(err);
            emit sendCompleted(false, describe(specMap), err);
            return false;
        }
    } else {
        if (!buildCan(spec, &can, &err)) {
            setError(err);
            emit sendCompleted(false, describe(specMap), err);
            return false;
        }
    }
    // 滚动计数与 ID 递增共用同一个计数器，二者任一勾选都要推进
    if (spec.rolling || spec.incrementId)
        ++m_counter;

    const QString desc = describe(specMap);
    const bool fd = spec.fd;
    const quint32 p = static_cast<quint32>(port);

    auto rec = std::make_shared<FrameData>();
    if (m_recorder) {
        rec->id = clampId(spec.id, spec.extended);
        rec->channel = static_cast<quint8>(qBound(0, port, 255));
        rec->len = static_cast<quint8>(qBound(0, spec.dlc, spec.fd ? 64 : 8));
        rec->extended = spec.extended;
        rec->remote = spec.remote;
        rec->fd = spec.fd;
        rec->brs = spec.fd && spec.brs;
        rec->tx = true;
        rec->echo = true;
        if (!spec.remote)
            rec->data = applyModifiers(spec.data, spec, spec.fd ? 64 : 8).left(rec->len);
    }

    auto okRes = std::make_shared<bool>(false);
    m_executor->post(
        [this, p, fd, can, fdf, okRes]() {
            *okRes = m_device && (fd ? m_device->transmitFd(p, fdf)
                                     : m_device->transmitCan(p, can));
        },
        [this, okRes, desc, rec]() { finishSend(*okRes, desc, rec); });
    return true;
}

bool FrameBuilder::sendFrameAsync(int port, const QVariantMap &spec)
{
    return enqueueOne(port, spec);
}

int FrameBuilder::sendBatchAsync(int port, const QVariantList &specs)
{
    int queued = 0;
    for (const QVariant &v : specs) {
        if (enqueueOne(port, v.toMap()))
            ++queued;
    }
    return queued;
}

void FrameBuilder::finishSend(bool ok, const QString &description,
                              const std::shared_ptr<FrameData> &record)
{
    if (!ok) {
        const QString err = m_device
                                ? QStringLiteral("发送失败：总线无应答（请检查波特率/位定时、接线与终端电阻）")
                                : QStringLiteral("发送失败：设备未打开");
        setError(err);
        emit sendCompleted(false, description, err);
        return;
    }

    ++m_sentCount;
    if (m_stats)
        m_stats->countSent(1);
    if (m_recorder && record) {
        const qint64 wallUs = QDateTime::currentMSecsSinceEpoch() * 1000;
        record->wallUs = wallUs;
        record->hostUs = wallUs;
        m_recorder->enqueueFrame(*record);
    }
    setError(QString());
    emit sentCountChanged();
    emit sendCompleted(true, description, QString());
}

bool FrameBuilder::busy() const
{
    return m_executor && m_executor->busy();
}

void FrameBuilder::setError(const QString &msg)
{
    if (m_lastError == msg)
        return;
    m_lastError = msg;
    emit lastErrorChanged();
}

void FrameBuilder::setStatistics(Statistics *stats)
{
    m_stats = stats;
}

void FrameBuilder::setRecorder(Recorder *recorder)
{
    m_recorder = recorder;
}

int FrameBuilder::sentCount() const
{
    return m_sentCount;
}

QString FrameBuilder::lastError() const
{
    return m_lastError;
}
