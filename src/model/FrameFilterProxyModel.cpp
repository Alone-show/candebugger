#include "model/FrameFilterProxyModel.h"

#include "core/CanFilter.h"
#include "model/FrameListModel.h"

FrameFilterProxyModel::FrameFilterProxyModel(CanFilter *filter, QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_filter(filter)
{
    setDynamicSortFilter(true);

    connect(this, &QAbstractItemModel::rowsInserted, this, &FrameFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &FrameFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &FrameFilterProxyModel::countChanged);
}

int FrameFilterProxyModel::count() const
{
    return rowCount();
}

void FrameFilterProxyModel::refresh()
{
    invalidateFilter();
    emit countChanged();
}

bool FrameFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent);
    if (!m_filter)
        return true;
    auto *source = qobject_cast<FrameListModel *>(sourceModel());
    if (!source)
        return true;
    return m_filter->matches(source->frameAt(sourceRow));
}
