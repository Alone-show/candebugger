// L1 软件滤波代理测试：FrameFilterProxyModel 过滤、映射、动态刷新
#include <QtTest>
#include <QSignalSpy>

#include "core/CanFilter.h"
#include "core/FrameData.h"
#include "model/FrameFilterProxyModel.h"
#include "model/FrameListModel.h"

namespace {

FrameData mk(quint32 id, bool extended = false, bool fd = false, bool tx = false)
{
    FrameData f;
    f.id = id;
    f.extended = extended;
    f.fd = fd;
    f.tx = tx;
    f.len = 2;
    f.data = QByteArray::fromHex("A0B1");
    f.hostUs = id;
    f.wallUs = 1700000000000000LL + id;
    return f;
}

QVariant idOf(const QAbstractItemModel *m, int row)
{
    return m->data(m->index(row, 0), FrameListModel::IdRole);
}

} // namespace

class TestFilterProxy : public QObject
{
    Q_OBJECT

private:
    // 源模型：0=0x100 标准CAN, 1=0x200 标准CAN(tx), 2=0x300 标准FD, 3=扩展CAN
    static void fill(FrameListModel &source)
    {
        source.append(QVector<FrameData>{
            mk(0x100),
            mk(0x200, false, false, true),
            mk(0x300, false, true, false),
            mk(0x1ABCDEF0, true),
        });
    }

private slots:
    void acceptsAllByDefault()
    {
        FrameListModel source;
        fill(source);
        CanFilter filter;
        FrameFilterProxyModel proxy(&filter);
        proxy.setSourceModel(&source);
        QCOMPARE(proxy.rowCount(), 4);
        QCOMPARE(proxy.count(), 4);
    }

    void nullFilterAcceptsAll()
    {
        FrameListModel source;
        fill(source);
        FrameFilterProxyModel proxy(nullptr);
        proxy.setSourceModel(&source);
        QCOMPARE(proxy.rowCount(), 4);
    }

    void idFilterHidesAndMaps()
    {
        FrameListModel source;
        fill(source);
        CanFilter filter;
        FrameFilterProxyModel proxy(&filter);
        proxy.setSourceModel(&source);

        filter.setIdEnabled(true);
        filter.setIdMin(0x200);
        filter.setIdMax(0x200);
        proxy.refresh();

        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(idOf(&proxy, 0).toString(), QStringLiteral("0x200"));
        // 代理行 0 应映射回源行 1
        QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 1);
    }

    void directionFilter()
    {
        FrameListModel source;
        fill(source);
        CanFilter filter;
        FrameFilterProxyModel proxy(&filter);
        proxy.setSourceModel(&source);

        filter.setDirection(2);   // 仅发送
        proxy.refresh();
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(idOf(&proxy, 0).toString(), QStringLiteral("0x200"));

        filter.setDirection(1);   // 仅接收
        proxy.refresh();
        QCOMPARE(proxy.rowCount(), 3);
    }

    void formatFilter()
    {
        FrameListModel source;
        fill(source);
        CanFilter filter;
        FrameFilterProxyModel proxy(&filter);
        proxy.setSourceModel(&source);

        filter.setShowFd(false);   // 隐藏 FD
        proxy.refresh();
        QCOMPARE(proxy.rowCount(), 3);

        filter.setShowExtended(false);   // 再隐藏扩展
        proxy.refresh();
        QCOMPARE(proxy.rowCount(), 2);
    }

    void dynamicRefilterOnInsert()
    {
        FrameListModel source;
        CanFilter filter;
        FrameFilterProxyModel proxy(&filter);
        proxy.setSourceModel(&source);

        filter.setIdEnabled(true);
        filter.setIdMin(0x300);
        filter.setIdMax(0x300);
        proxy.refresh();
        QCOMPARE(proxy.rowCount(), 0);

        // 后续插入应被动态过滤（无需 refresh）
        source.append(QVector<FrameData>{mk(0x100), mk(0x300), mk(0x200), mk(0x300)});
        QCOMPARE(proxy.rowCount(), 2);
        QCOMPARE(idOf(&proxy, 0).toString(), QStringLiteral("0x300"));
        QCOMPARE(idOf(&proxy, 1).toString(), QStringLiteral("0x300"));
    }

    void sourceClearReflects()
    {
        FrameListModel source;
        fill(source);
        CanFilter filter;
        FrameFilterProxyModel proxy(&filter);
        proxy.setSourceModel(&source);
        QCOMPARE(proxy.rowCount(), 4);
        source.clear();
        QCOMPARE(proxy.rowCount(), 0);
    }

    void refreshEmitsCountChanged()
    {
        FrameListModel source;
        fill(source);
        CanFilter filter;
        FrameFilterProxyModel proxy(&filter);
        proxy.setSourceModel(&source);

        QSignalSpy spy(&proxy, &FrameFilterProxyModel::countChanged);
        filter.setIdEnabled(true);
        filter.setIdMin(0x100);
        filter.setIdMax(0x100);
        proxy.refresh();
        QVERIFY(spy.count() >= 1);
        QCOMPARE(proxy.count(), 1);
    }

    void countMatchesRowCount()
    {
        FrameListModel source;
        fill(source);
        CanFilter filter;
        FrameFilterProxyModel proxy(&filter);
        proxy.setSourceModel(&source);
        QCOMPARE(proxy.count(), proxy.rowCount());

        filter.setShowCan(false);
        proxy.refresh();
        QCOMPARE(proxy.count(), proxy.rowCount());
    }
};

QTEST_APPLESS_MAIN(TestFilterProxy)

#include "test_filter_proxy.moc"
