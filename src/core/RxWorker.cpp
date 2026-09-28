#include "core/RxWorker.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>

#include "core/CanDevice.h"
#include "core/RingBuffer.h"

namespace {

constexpr int kBatch = 512;
constexpr quint32 kRxTimeoutMs = 5;

FrameData baseFrame(const ZCAN_MSG_HDR &hdr, qint64 hostUs, qint64 wallUs)
{
    FrameData f;
    f.hostUs = hostUs;
    f.wallUs = wallUs;
    f.devTs = hdr.ts;
    f.id = hdr.id;
    f.channel = hdr.chn;
    f.len = hdr.len;
    f.extended = (hdr.inf.sef != 0);
    f.remote = (hdr.inf.sdf != 0);
    f.fd = (hdr.inf.fmt != 0);
    f.brs = (hdr.inf.brs != 0);
    f.error = (hdr.inf.err != 0);
    f.tx = (hdr.inf.tx != 0);
    f.echo = (hdr.inf.echo != 0);
    return f;
}

void appendCan(const ZCAN_20_MSG &msg, quint32 fallbackChannel, qint64 hostUs, qint64 wallUs, QVector<FrameData> &out)
{
    FrameData f = baseFrame(msg.hdr, hostUs, wallUs);
    if (msg.hdr.chn == 0 && fallbackChannel != 0)
        f.channel = static_cast<quint8>(fallbackChannel);
    const int n = qBound<int>(0, msg.hdr.len, 8);
    f.len = static_cast<quint8>(n);
    f.fd = false;
    f.brs = false;
    f.data = QByteArray(reinterpret_cast<const char *>(msg.dat), n);
    out.append(f);
}

void appendFd(const ZCAN_FD_MSG &msg, quint32 fallbackChannel, qint64 hostUs, qint64 wallUs, QVector<FrameData> &out)
{
    FrameData f = baseFrame(msg.hdr, hostUs, wallUs);
    if (msg.hdr.chn == 0 && fallbackChannel != 0)
        f.channel = static_cast<quint8>(fallbackChannel);
    const int n = qBound<int>(0, msg.hdr.len, 64);
    f.len = static_cast<quint8>(n);
    f.fd = true;
    f.data = QByteArray(reinterpret_cast<const char *>(msg.dat), n);
    out.append(f);
}

} // namespace

RxWorker::RxWorker(CanDevice *device,
                   QSharedPointer<RingBuffer> ring,
                   QVector<int> channels,
                   bool mergeMode,
                   QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_ring(std::move(ring))
    , m_channels(std::move(channels))
    , m_mergeMode(mergeMode)
{
}

void RxWorker::requestStop()
{
    m_stop.store(true, std::memory_order_relaxed);
}

void RxWorker::run()
{
    QElapsedTimer clock;
    clock.start();

    QVector<FrameData> batch;
    batch.reserve(kBatch);

    if (m_mergeMode) {
        QVector<ZCANDataObj> objs(kBatch);
        while (!m_stop.load(std::memory_order_relaxed)) {
            batch.clear();
            const int rc = m_device->receiveData(0, objs.data(), kBatch, kRxTimeoutMs);
            // 采集时刻取在接收返回之后：绝对时间更贴近帧到达时刻
            // （相对时间已改用设备时间戳 hdr.ts，见 FrameData::devTs）
            const qint64 hostUs = clock.nsecsElapsed() / 1000;
            const qint64 wallUs = QDateTime::currentMSecsSinceEpoch() * 1000;
            for (int i = 0; i < rc; ++i) {
                const ZCANDataObj &o = objs[i];
                if (o.dataType == ZCAN_DT_ZCAN_CAN_DATA) {
                    appendCan(o.data.zcanCANData, o.chnl, hostUs, wallUs, batch);
                    batch.last().channel = o.chnl;
                } else if (o.dataType == ZCAN_DT_ZCAN_CANFD_DATA) {
                    appendFd(o.data.zcanCANFDData, o.chnl, hostUs, wallUs, batch);
                    batch.last().channel = o.chnl;
                } else if (o.dataType == ZCAN_DT_ZCAN_ERROR_DATA) {
                    FrameData f = baseFrame(o.data.zcanErrData.hdr, hostUs, wallUs);
                    f.channel = o.chnl;
                    f.error = true;
                    batch.append(f);
                }
            }
            if (!batch.isEmpty())
                m_ring->push(batch);
        }
    } else {
        QVector<ZCAN_20_MSG> canBuf(kBatch);
        QVector<ZCAN_FD_MSG> fdBuf(kBatch);
        while (!m_stop.load(std::memory_order_relaxed)) {
            batch.clear();
            for (int ch : m_channels) {
                const int rc = m_device->receiveCan(ch, canBuf.data(), kBatch, kRxTimeoutMs);
                // 采集时刻取在接收返回之后（见合并分支说明）
                const qint64 hostUs = clock.nsecsElapsed() / 1000;
                const qint64 wallUs = QDateTime::currentMSecsSinceEpoch() * 1000;
                for (int i = 0; i < rc; ++i)
                    appendCan(canBuf[i], ch, hostUs, wallUs, batch);

                const int rf = m_device->receiveFd(ch, fdBuf.data(), kBatch, kRxTimeoutMs);
                const qint64 hostUsFd = clock.nsecsElapsed() / 1000;
                const qint64 wallUsFd = QDateTime::currentMSecsSinceEpoch() * 1000;
                for (int i = 0; i < rf; ++i)
                    appendFd(fdBuf[i], ch, hostUsFd, wallUsFd, batch);
            }
            if (!batch.isEmpty())
                m_ring->push(batch);
        }
    }

    emit finished();
}
