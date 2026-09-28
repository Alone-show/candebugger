#include "model/FrameListModel.h"

#include <QDateTime>

FrameListModel::FrameListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int FrameListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_frames.size();
}

QVariant FrameListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_frames.size())
        return QVariant();

    const FrameData &f = m_frames.at(index.row());

    switch (role) {
    case TimeRole:
        return formatTime(f);
    case SequenceRole:
        return QVariant::fromValue(m_sequences.value(index.row(), 0));
    case ChannelRole:
        return f.channel;
    case DirectionRole:
        return f.tx ? QStringLiteral("Tx") : QStringLiteral("Rx");
    case IdRole:
        return formatId(f);
    case TypeRole:
        return f.extended ? QStringLiteral("扩展") : QStringLiteral("标准");
    case FormatRole:
        if (f.error)
            return QStringLiteral("错误");
        if (f.fd)
            return f.brs ? QStringLiteral("FD加速") : QStringLiteral("FD");
        return QStringLiteral("CAN");
    case DlcRole:
        return f.len;
    case DataRole:
        return formatData(f);
    case IsTxRole:
        return f.tx;
    case IsFdRole:
        return f.fd;
    case IsErrorRole:
        return f.error;
    case IsEchoRole:
        return f.echo;
    case ColorRole:
        return colorFor(f);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> FrameListModel::roleNames() const
{
    return {
        {TimeRole, "timeText"},
        {SequenceRole, "sequence"},
        {ChannelRole, "channel"},
        {DirectionRole, "direction"},
        {IdRole, "idText"},
        {TypeRole, "typeText"},
        {FormatRole, "formatText"},
        {DlcRole, "dlc"},
        {DataRole, "dataText"},
        {IsTxRole, "isTx"},
        {IsFdRole, "isFd"},
        {IsErrorRole, "isError"},
        {IsEchoRole, "isEcho"},
        {ColorRole, "rowColor"},
    };
}

int FrameListModel::count() const
{
    return m_frames.size();
}

int FrameListModel::maxRows() const
{
    return m_maxRows;
}

void FrameListModel::setMaxRows(int rows)
{
    rows = qMax(100, rows);
    if (rows == m_maxRows)
        return;
    m_maxRows = rows;
    emit maxRowsChanged();
}

int FrameListModel::timestampMode() const
{
    return m_timestampMode;
}

void FrameListModel::setTimestampMode(int mode)
{
    if (mode == m_timestampMode)
        return;
    m_timestampMode = mode;
    if (!m_frames.isEmpty()) {
        emit dataChanged(index(0), index(m_frames.size() - 1), {TimeRole});
    }
    emit timestampModeChanged();
}

void FrameListModel::append(const QVector<FrameData> &frames)
{
    if (frames.isEmpty())
        return;

    if (m_baseHostUs < 0)
        m_baseHostUs = frames.first().hostUs;
    if (m_baseDevTs < 0) {
        for (const FrameData &f : frames) {
            if (f.devTsValid()) {
                m_baseDevTs = f.devTs;
                break;
            }
        }
    }

    // 1) 先移除既有的最旧行
    const int total = m_frames.size() + frames.size();
    const int removeExisting = qBound(0, total - m_maxRows, m_frames.size());
    if (removeExisting > 0) {
        beginRemoveRows(QModelIndex(), 0, removeExisting - 1);
        m_frames.erase(m_frames.begin(), m_frames.begin() + removeExisting);
        m_sequences.erase(m_sequences.begin(), m_sequences.begin() + removeExisting);
        endRemoveRows();
    }

    // 2) 若单批仍超出上限，丢弃本批最旧部分
    const int overflow = qMax(0, (m_frames.size() + frames.size()) - m_maxRows);
    const int skip = qMin(overflow, frames.size());
    const int toInsert = frames.size() - skip;

    if (toInsert > 0) {
        const int first = m_frames.size();
        beginInsertRows(QModelIndex(), first, first + toInsert - 1);
        for (int i = skip; i < frames.size(); ++i) {
            m_frames.append(frames.at(i));
            m_sequences.append(++m_nextSequence);
        }
        endInsertRows();
    }

    emit countChanged();
}

FrameData FrameListModel::frameAt(int row) const
{
    if (row < 0 || row >= m_frames.size())
        return FrameData();
    return m_frames.at(row);
}

void FrameListModel::clear()
{
    if (m_frames.isEmpty()) {
        m_baseHostUs = -1;
        m_baseDevTs = -1;
        return;
    }
    beginResetModel();
    m_frames.clear();
    m_sequences.clear();
    m_nextSequence = 0;
    m_baseHostUs = -1;
    m_baseDevTs = -1;
    endResetModel();
    emit countChanged();
}

QString FrameListModel::formatId(const FrameData &f)
{
    const quint32 id = f.extended ? (f.id & 0x1FFFFFFFu) : (f.id & 0x7FFu);
    QString hex = QString::number(id, 16).toUpper();
    hex = hex.rightJustified(f.extended ? 8 : 3, QLatin1Char('0'));
    return QStringLiteral("0x") + hex;
}

QString FrameListModel::formatData(const FrameData &f)
{
    if (f.remote)
        return QStringLiteral("远程帧");
    if (f.data.isEmpty())
        return QString();

    QString out;
    out.reserve(f.data.size() * 3);
    for (int i = 0; i < f.data.size(); ++i) {
        if (i)
            out += QLatin1Char(' ');
        out += QStringLiteral("%1").arg(static_cast<quint8>(f.data.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return out;
}

QString FrameListModel::formatTime(const FrameData &f) const
{
    if (m_timestampMode == AbsoluteTime) {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(f.wallUs / 1000);
        return dt.toString(QStringLiteral("HH:mm:ss.zzz"));
    }
    // 相对模式：优先使用设备时间戳（全局 100us tick，含 U32 回绕安全差值）
    if (f.devTsValid() && m_baseDevTs >= 0) {
        const quint32 delta = static_cast<quint32>(f.devTs - static_cast<quint32>(m_baseDevTs));
        const double seconds = static_cast<double>(delta) * FrameData::kDevTsTickUs / 1000000.0;
        return QString::number(seconds, 'f', 6);
    }
    const qint64 base = (m_baseHostUs < 0) ? f.hostUs : m_baseHostUs;
    const double seconds = (f.hostUs - base) / 1000000.0;
    return QString::number(seconds, 'f', 6);
}

QString FrameListModel::colorFor(const FrameData &f)
{
    if (f.error)
        return QStringLiteral("#e06c75");
    if (f.tx)
        return QStringLiteral("#7bd88f");

    // 按 ID 稳定着色
    static const char *palette[] = {
        "#e0b34c", "#4c9aff", "#c678dd", "#56b6c2",
        "#98c379", "#d19a66", "#61afef", "#e06c75"
    };
    const int idx = static_cast<int>(f.id % 8);
    return QString::fromLatin1(palette[idx]);
}
