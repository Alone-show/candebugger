#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVector>

#include "core/FrameData.h"

class CanDevice;
class QTimer;

// 统计引擎：接收管线按批注入，发送路径按次累计；每 intervalMs 聚合一次。
// 不新增逐帧信号（符合全局不变式）。
class Statistics : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double rxFps READ rxFps NOTIFY updated)
    Q_PROPERTY(double txFps READ txFps NOTIFY updated)
    Q_PROPERTY(double busLoad READ busLoad NOTIFY updated)
    Q_PROPERTY(qulonglong totalFrames READ totalFrames NOTIFY updated)
    Q_PROPERTY(qulonglong rxCount READ rxCount NOTIFY updated)
    Q_PROPERTY(qulonglong txCount READ txCount NOTIFY updated)
    Q_PROPERTY(qulonglong errorFrames READ errorFrames NOTIFY updated)
    Q_PROPERTY(QVariantList topN READ topN NOTIFY topNChanged)
    Q_PROPERTY(int topNCount READ topNCount WRITE setTopNCount NOTIFY topNCountChanged)
    Q_PROPERTY(int arbitrationBps READ arbitrationBps WRITE setArbitrationBps NOTIFY bitratesChanged)
    Q_PROPERTY(int dataBps READ dataBps WRITE setDataBps NOTIFY bitratesChanged)

public:
    explicit Statistics(CanDevice *device = nullptr, QObject *parent = nullptr);
    ~Statistics() override;

    // ---- 纯逻辑（静态，可单测） ----
    static double framesPerSecond(quint64 frames, qint64 elapsedUs);
    static void frameBitSplit(const FrameData &f, int *arbBits, int *dataBits);
    static int frameBitEstimate(const FrameData &f);
    static QVariantList topNList(const QHash<quint64, quint64> &counts, int n);

    // 数据注入（GUI 线程）
    void addFrames(const QVector<FrameData> &frames);   // 接收管线
    void countSent(int n = 1);                          // 发送路径

    void setActiveChannels(const QVector<int> &channels);

    void start(int intervalMs = 1000);
    void stop();
    void update(qint64 nowUs);                          // 定时/手动触发聚合

    Q_INVOKABLE void reset();

    double rxFps() const;
    double txFps() const;
    double busLoad() const;
    qulonglong totalFrames() const;
    qulonglong rxCount() const;
    qulonglong txCount() const;
    qulonglong errorFrames() const;
    QVariantList topN() const;
    int topNCount() const;
    void setTopNCount(int n);
    int arbitrationBps() const;
    void setArbitrationBps(int bps);
    int dataBps() const;
    void setDataBps(int bps);

signals:
    void updated();
    void topNChanged();
    void topNCountChanged();
    void bitratesChanged();

private:
    static quint64 topKey(const FrameData &f);

    CanDevice *m_device = nullptr;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    bool m_haveWindow = false;
    qint64 m_windowStartUs = 0;
    QVector<int> m_channels;

    qulonglong m_rx = 0;
    qulonglong m_tx = 0;
    qulonglong m_errors = 0;

    qulonglong m_winRx = 0;
    qulonglong m_winTx = 0;
    double m_winBusTimeUs = 0.0;

    double m_rxFps = 0.0;
    double m_txFps = 0.0;
    double m_busLoad = 0.0;

    QHash<quint64, quint64> m_top;
    QVariantList m_topN;
    int m_topNCount = 10;
    int m_arbBps = 500000;
    int m_dataBps = 2000000;
};
