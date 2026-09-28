#include "core/CanDevice.h"

#include <QElapsedTimer>
#include <cstring>
#include <mutex>

CanDevice::CanDevice(QObject *parent)
    : QObject(parent)
{
}

CanDevice::~CanDevice()
{
    if (m_open.load())
        close();
}

bool CanDevice::open(quint32 card)
{
    {
        std::scoped_lock lk(m_rxMutex, m_ioMutex);
        if (m_open.load())
            return true;

        if (VCI_OpenDevice(DeviceType, card, 0) == 0)
            return false;

        m_card = card;
        m_open.store(true);
        readBoardInfo(card);
    }

    emit openChanged();
    emit deviceInfoChanged();
    return true;
}

bool CanDevice::openDevice(int card)
{
    return open(static_cast<quint32>(qMax(0, card)));
}

void CanDevice::close()
{
    {
        std::scoped_lock lk(m_rxMutex, m_ioMutex);
        if (!m_open.load())
            return;

        VCI_CloseDevice(DeviceType, m_card);
        m_open.store(false);
        {
            std::lock_guard<std::mutex> st(m_stateMutex);
            m_info = CanDeviceInfo();
        }
    }
    emit openChanged();
    emit deviceInfoChanged();
}

bool CanDevice::isOpen() const
{
    return m_open.load(std::memory_order_acquire);
}

// 需在持有 m_ioMutex（及生命周期锁）时调用
bool CanDevice::readBoardInfo(quint32 card)
{
    ZCAN_DEV_INF raw;
    std::memset(&raw, 0, sizeof(raw));

    if (VCI_ReadBoardInfo(DeviceType, card, &raw) == 0) {
        std::lock_guard<std::mutex> st(m_stateMutex);
        m_info = CanDeviceInfo();
        return false;
    }

    CanDeviceInfo info;
    info.valid = true;
    info.hwv = raw.hwv;
    info.fwv = raw.fwv;
    info.drv = raw.drv;
    info.api = raw.api;
    info.irq = raw.irq;
    info.channels = raw.chn;
    info.sn = QString::fromLatin1(reinterpret_cast<const char *>(raw.sn), 20).trimmed();
    info.id = QString::fromLatin1(reinterpret_cast<const char *>(raw.id), 40).trimmed();

    std::lock_guard<std::mutex> st(m_stateMutex);
    m_info = info;
    return true;
}

CanDeviceInfo CanDevice::deviceInfo() const
{
    std::lock_guard<std::mutex> st(m_stateMutex);
    return m_info;
}

QString CanDevice::serialNumber() const
{
    std::lock_guard<std::mutex> st(m_stateMutex);
    return m_info.sn;
}

QString CanDevice::cardId() const
{
    std::lock_guard<std::mutex> st(m_stateMutex);
    return m_info.id;
}

QString CanDevice::versionText() const
{
    std::lock_guard<std::mutex> st(m_stateMutex);
    if (!m_info.valid)
        return QString();
    return QStringLiteral("HW %1 / FW %2 / DRV %3 / API %4")
        .arg(m_info.hwv, 4, 16, QLatin1Char('0'))
        .arg(m_info.fwv, 4, 16, QLatin1Char('0'))
        .arg(m_info.drv, 4, 16, QLatin1Char('0'))
        .arg(m_info.api, 4, 16, QLatin1Char('0'));
}

int CanDevice::channels() const
{
    std::lock_guard<std::mutex> st(m_stateMutex);
    return m_info.channels;
}

bool CanDevice::initChannel(quint32 port, const ChannelConfig &cfg)
{
    std::scoped_lock lk(m_rxMutex, m_ioMutex);
    if (!m_open.load())
        return false;

    ZCAN_INIT init;
    std::memset(&init, 0, sizeof(init));
    init.clk = cfg.clock;
    init.mode = cfg.mode;

    init.aset.tseg1 = cfg.arbitration.tseg1;
    init.aset.tseg2 = cfg.arbitration.tseg2;
    init.aset.sjw = cfg.arbitration.sjw;
    init.aset.smp = cfg.arbitration.smp;
    init.aset.brp = cfg.arbitration.brp;

    init.dset.tseg1 = cfg.data.tseg1;
    init.dset.tseg2 = cfg.data.tseg2;
    init.dset.sjw = cfg.data.sjw;
    init.dset.smp = cfg.data.smp;
    init.dset.brp = cfg.data.brp;

    return VCI_InitCAN(DeviceType, m_card, port, &init) != 0;
}

QString CanDevice::configureChannel(int port, int arbitrationBps, int dataBps,
                                    double samplePointPercent, bool listenOnly)
{
    if (!isOpen())
        return QString();

    const quint32 clock = BitTiming::DefaultClock;
    const auto arb = BitTiming::calculate(clock, static_cast<quint32>(qMax(1, arbitrationBps)), samplePointPercent);
    if (!arb.ok)
        return QString();

    BitTimingParams dataParams = arb.params;
    QString dataMsg;
    if (dataBps > 0) {
        const auto dat = BitTiming::calculate(clock, static_cast<quint32>(dataBps), samplePointPercent);
        if (!dat.ok)
            return QString();
        dataParams = dat.params;
        dataMsg = dat.message;
    }

    ChannelConfig cfg;
    cfg.clock = clock;
    cfg.mode = listenOnly ? 1u : 0u;
    cfg.arbitration = arb.params;
    cfg.data = dataParams;

    if (!initChannel(static_cast<quint32>(port), cfg))
        return QString();

    if (dataMsg.isEmpty())
        return arb.message;
    return arb.message + QStringLiteral(" | ") + dataMsg;
}

bool CanDevice::startChannel(int port)
{
    std::scoped_lock lk(m_rxMutex, m_ioMutex);
    if (!m_open.load())
        return false;
    if (VCI_StartCAN(DeviceType, m_card, port) == 0)
        return false;

    // 启动后立即下发发送同步等待上限（超时后帧仍会提交），避免无 ACK 总线上
    // VCI_Transmit 内部 wait(tx) 阻塞默认 ~1.5s 拖死调用线程（GUI）
    quint32 value = static_cast<quint32>(qMax(1, m_txTimeoutMs.load()));
    VCI_SetReference(DeviceType, m_card, port, CMD_CAN_TX_TIMEOUT, &value);
    return true;
}

bool CanDevice::resetChannel(int port)
{
    std::scoped_lock lk(m_rxMutex, m_ioMutex);
    if (!m_open.load())
        return false;
    return VCI_ResetCAN(DeviceType, m_card, port) != 0;
}

bool CanDevice::clearBuffer(int port)
{
    std::scoped_lock lk(m_rxMutex, m_ioMutex);
    if (!m_open.load())
        return false;
    return VCI_ClearBuffer(DeviceType, m_card, port) != 0;
}

int CanDevice::txTimeoutMs() const
{
    return m_txTimeoutMs.load();
}

void CanDevice::setTxTimeoutMs(int ms)
{
    const int v = qMax(1, ms);
    if (v == m_txTimeoutMs.load())
        return;
    m_txTimeoutMs.store(v);
    emit txTimeoutChanged();
}

bool CanDevice::setTxTimeout(int port, int ms)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    quint32 value = static_cast<quint32>(qMax(1, ms));
    return VCI_SetReference(DeviceType, m_card, static_cast<quint32>(port),
                            CMD_CAN_TX_TIMEOUT, &value) != 0;
}

bool CanDevice::setTerminalResistor(int port, bool on)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    quint32 value = on ? 1 : 0;
    return VCI_SetReference(DeviceType, m_card, port, CMD_CAN_TRES, &value) != 0;
}

bool CanDevice::getTerminalResistor(quint32 port, bool *on)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load() || !on)
        return false;
    quint32 value = 0;
    const bool ok = VCI_GetReference(DeviceType, m_card, port, CMD_CAN_TRES, &value) != 0;
    if (ok && on)
        *on = (value != 0);
    return ok;
}

bool CanDevice::setRecvMerge(int port, bool on)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    quint32 value = on ? 1 : 0;
    return VCI_SetReference(DeviceType, m_card, port, ZCAN_CMD_SET_CHNL_RECV_MERGE, &value) != 0;
}

bool CanDevice::getRecvMerge(quint32 port, bool *on)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load() || !on)
        return false;
    quint32 value = 0;
    const bool ok = VCI_GetReference(DeviceType, m_card, port, ZCAN_CMD_GET_CHNL_RECV_MERGE, &value) != 0;
    if (ok && on)
        *on = (value != 0);
    return ok;
}

bool CanDevice::setPeriodicConfig(quint32 port, const ZCAN_TTX_CFG &cfg)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    ZCAN_TTX_CFG local = cfg;
    return VCI_SetReference(DeviceType, m_card, port, CMD_CAN_TTX, &local) != 0;
}

bool CanDevice::setPeriodicEnabled(quint32 port, bool on)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    quint32 value = on ? 1 : 0;
    return VCI_SetReference(DeviceType, m_card, port, CMD_CAN_TTX_CTL, &value) != 0;
}

bool CanDevice::setHardwareFilter(quint32 port, const ZCAN_FILTER_TABLE &table)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    ZCAN_FILTER_TABLE local = table;
    return VCI_SetReference(DeviceType, m_card, port, CMD_CAN_FILTER, &local) != 0;
}

bool CanDevice::getSendQueueSize(quint32 port, quint32 *size)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load() || !size)
        return false;
    quint32 value = 0;
    const bool ok = VCI_GetReference(DeviceType, m_card, port, ZCAN_CMD_GET_SEND_QUEUE_SIZE, &value) != 0;
    if (ok)
        *size = value;
    return ok;
}

bool CanDevice::getSendQueueSpace(quint32 port, quint32 *space)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load() || !space)
        return false;
    quint32 value = 0;
    const bool ok = VCI_GetReference(DeviceType, m_card, port, ZCAN_CMD_GET_SEND_QUEUE_SPACE, &value) != 0;
    if (ok)
        *space = value;
    return ok;
}

bool CanDevice::enableSendQueue(quint32 port, bool on)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    quint32 value = on ? 1 : 0;
    return VCI_SetReference(DeviceType, m_card, port, ZCAN_CMD_SET_SEND_QUEUE_EN, &value) != 0;
}

bool CanDevice::clearSendQueue(quint32 port)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    quint32 value = 1;
    return VCI_SetReference(DeviceType, m_card, port, ZCAN_CMD_SET_SEND_QUEUE_CLR, &value) != 0;
}

bool CanDevice::transmitCan(quint32 port, const ZCAN_20_MSG &msg)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    ZCAN_20_MSG frame = msg;
    return VCI_Transmit(DeviceType, m_card, port, &frame, 1) == 1;
}

bool CanDevice::transmitFd(quint32 port, const ZCAN_FD_MSG &msg)
{
    std::lock_guard<std::mutex> io(m_ioMutex);
    if (!m_open.load())
        return false;
    ZCAN_FD_MSG frame = msg;
    return VCI_TransmitFD(DeviceType, m_card, port, &frame, 1) == 1;
}

int CanDevice::receiveCan(quint32 port, ZCAN_20_MSG *buf, quint32 count, quint32 timeoutMs)
{
    std::lock_guard<std::mutex> rx(m_rxMutex);
    if (!m_open.load() || !buf || count == 0)
        return -1;
    return static_cast<int>(VCI_Receive(DeviceType, m_card, port, buf, count, timeoutMs));
}

int CanDevice::receiveFd(quint32 port, ZCAN_FD_MSG *buf, quint32 count, quint32 timeoutMs)
{
    std::lock_guard<std::mutex> rx(m_rxMutex);
    if (!m_open.load() || !buf || count == 0)
        return -1;
    return static_cast<int>(VCI_ReceiveFD(DeviceType, m_card, port, buf, count, timeoutMs));
}

int CanDevice::receiveData(quint32 port, ZCANDataObj *buf, quint32 count, quint32 timeoutMs)
{
    std::lock_guard<std::mutex> rx(m_rxMutex);
    if (!m_open.load() || !buf || count == 0)
        return -1;
    return static_cast<int>(VCI_ReceiveData(DeviceType, m_card, port, buf, count, timeoutMs));
}

qint64 CanDevice::ioLockWaitUs()
{
    QElapsedTimer t;
    t.start();
    std::lock_guard<std::mutex> io(m_ioMutex);
    return t.nsecsElapsed() / 1000;
}

QString CanDevice::resultString(quint32 code)
{
    switch (code) {
    case 0:
        return QStringLiteral("失败");
    case 1:
        return QStringLiteral("成功");
    case 2:
        return QStringLiteral("无数据");
    case 3:
        return QStringLiteral("缓冲区满");
    default:
        return QStringLiteral("未知状态(%1)").arg(code);
    }
}
