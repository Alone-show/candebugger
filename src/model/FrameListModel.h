#pragma once

#include <QAbstractListModel>
#include <QList>

#include "core/FrameData.h"

class FrameListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int maxRows READ maxRows WRITE setMaxRows NOTIFY maxRowsChanged)
    Q_PROPERTY(int timestampMode READ timestampMode WRITE setTimestampMode NOTIFY timestampModeChanged)

public:
    enum Roles {
        TimeRole = Qt::UserRole + 1,
        SequenceRole,
        ChannelRole,
        DirectionRole,
        IdRole,
        TypeRole,
        FormatRole,
        DlcRole,
        DataRole,
        IsTxRole,
        IsFdRole,
        IsErrorRole,
        IsEchoRole,
        ColorRole
    };
    Q_ENUM(Roles)

    enum TimestampMode { RelativeTime = 0, AbsoluteTime = 1 };
    Q_ENUM(TimestampMode)

    explicit FrameListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int maxRows() const;
    void setMaxRows(int rows);

    int timestampMode() const;
    void setTimestampMode(int mode);

    void append(const QVector<FrameData> &frames);

    // 供过滤代理访问原始帧；越界返回默认值
    FrameData frameAt(int row) const;

public slots:
    void clear();

signals:
    void countChanged();
    void maxRowsChanged();
    void timestampModeChanged();

private:
    static QString formatId(const FrameData &f);
    static QString formatData(const FrameData &f);
    QString formatTime(const FrameData &f) const;
    static QString colorFor(const FrameData &f);

    QList<FrameData> m_frames;
    QList<quint64> m_sequences;
    quint64 m_nextSequence = 0;
    int m_maxRows = 100000;
    int m_timestampMode = RelativeTime;
    qint64 m_baseHostUs = -1;   // 主机时间基准(相对模式回退用)
    qint64 m_baseDevTs = -1;    // 设备时间戳基准(相对模式首选，-1 未定)
};
