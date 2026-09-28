#pragma once

#include <QSortFilterProxyModel>

class CanFilter;

// 模型级软件滤波：过滤后的行才进入视图，避免委托用 visible 隐藏仍占列表项
class FrameFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit FrameFilterProxyModel(CanFilter *filter, QObject *parent = nullptr);

    int count() const;

public slots:
    // invalidateFilter() 在 Qt5 为 protected，这里提供公开封装
    void refresh();

signals:
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    CanFilter *m_filter = nullptr;
};
