#include "core/CanDevice.h"

#include <QElapsedTimer>
#include <cstring>
#include <mutex>

#include "core/BitCalibration.h"

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
                                    double samplePointPercent, bool listenOnly, quint32 clock)
{
    if (!isOpen())
        return QString();

    if (clock == 0)
        clock = BitTiming::DefaultClock;

    BitTimingParams arbParams;
    QString arbMsg;
    bool calibrated = false;

    // 优先使用实测标定表（SDK 的实际分频不能可靠地用公式外推）
    const CalibrationEntry cal = BitCalibration::instance().lookup(arbitrationBps);
    if (cal.valid) {
        arbParams = cal.params;
        clock = cal.clock;   // 用标定时验证过的名义时钟
        calibrated = true;
        arbMsg = QStringLiteral("已标定 clk=%1 brp=%2 tseg1=%3 tseg2=%4 sjw=%5")
                     .arg(clock)
                     .arg(cal.params.brp)
                     .arg(cal.params.tseg1)
                     .arg(cal.params.tseg2)
                     .arg(cal.params.sjw);
    } else {
        // 实测标定出的 ZLG 约定：clk 即真实控制器时钟（60M/80M），字段是寄存器值
        const auto r = BitTiming::calculate(clock, static_cast<quint32>(qMax(1, arbitrationBps)),
                                            samplePointPercent);
        if (!r.ok)
            return QString();
        arbParams = r.params;
        arbMsg = r.message;
    }

    // 数据域：同样用 ZLG 约定（CANFD 加速段）
    BitTimingParams dataParams = arbParams;
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
    cfg.arbitration = arbParams;
    cfg.data = dataParams;

    if (!initChannel(static_cast<quint32>(port), cfg))
        return QString();

    Q_UNUSED(calibrated);
    if (dataMsg.isEmpty())
        return arbMsg;
    return arbMsg + QStringLiteral(" | ") + dataMsg;
}

QString CanDevice::configureChannelRaw(int port, quint32 clock, bool listenOnly,
                                      int arbBrp, int arbTseg1, int arbTseg2, int arbSjw, int arbSmp,
                                      int dataBrp, int dataTseg1, int dataTseg2, int dataSjw, int dataSmp)
{
    if (!isOpen())
        return QString();
    if (clock == 0)
        clock = BitTiming::DefaultClock;

    BitTimingParams arb;
    arb.brp = static_cast<quint16>(qBound(1, arbBrp, 1024));
    arb.tseg1 = static_cast<quint8>(qBound(1, arbTseg1, 255));
    arb.tseg2 = static_cast<quint8>(qBound(1, arbTseg2, 255));
    arb.sjw = static_cast<quint8>(qBound(1, arbSjw, 255));
    arb.smp = static_cast<quint8>(qBound(0, arbSmp, 1));

    BitTimingParams dat = arb;
    bool hasData = false;
    if (dataBrp > 0 && dataTseg1 > 0 && dataTseg2 > 0) {
        dat.brp = static_cast<quint16>(qBound(1, dataBrp, 1024));
        dat.tseg1 = static_cast<quint8>(qBound(1, dataTseg1, 255));
        dat.tseg2 = static_cast<quint8>(qBound(1, dataTseg2, 255));
        dat.sjw = static_cast<quint8>(qBound(1, dataSjw, 255));
        dat.smp = static_cast<quint8>(qBound(0, dataSmp, 1));
        hasData = true;
    }

    ChannelConfig cfg;
    cfg.clock = clock;
    cfg.mode = listenOnly ? 1u : 0u;
    cfg.arbitration = arb;
    cfg.data = dat;

    if (!initChannel(static_cast<quint32>(port), cfg))
        return QString();

    auto describe = [clock](const BitTimingParams &p) {
        return QStringLiteral("brp=%1 tseg1=%2 tseg2=%3 sjw=%4 -> %5 bps 采样点 %6%")
            .arg(p.brp)
            .arg(p.tseg1)
            .arg(p.tseg2)
            .arg(p.sjw)
            .arg(QString::number(BitTiming::actualBps(clock, p), 'f', 0))
            .arg(QString::number(BitTiming::samplePoint(p), 'f', 1));
    };

    QString msg = QStringLiteral("原始位定时 clk=%1 仲裁 %2").arg(clock).arg(describe(arb));
    if (hasData)
        msg += QStringLiteral(" | 数据 %1").arg(describe(dat));
    return msg;
}

QVariantMap CanDevice::bitTimingPreview(int arbitrationBps, int dataBps,
                                        double samplePointPercent, quint32 clock) const
{
    QVariantMap out;
    if (clock == 0)
        clock = BitTiming::DefaultClock;

    bool calibrated = false;
    BitTimingParams arbParams;
    double arbActual = 0.0;
    double arbSp = 0.0;
    QString arbMessage;

    const CalibrationEntry cal = BitCalibration::instance().lookup(arbitrationBps);
    if (cal.valid) {
        calibrated = true;
        clock = cal.clock;
        arbParams = cal.params;
        arbActual = BitTiming::actualBps(clock, cal.params);
        arbSp = BitTiming::samplePoint(cal.params);
        arbMessage = QStringLiteral("已标定（%1）brp=%2 tseg1=%3 tseg2=%4 sjw=%5")
                         .arg(cal.source)
                         .arg(cal.params.brp)
                         .arg(cal.params.tseg1)
                         .arg(cal.params.tseg2)
                         .arg(cal.params.sjw);
    } else {
        const auto arb = BitTiming::calculate(clock, static_cast<quint32>(qMax(1, arbitrationBps)),
                                              samplePointPercent);
        arbParams = arb.params;
        arbActual = arb.actualBps;
        arbSp = arb.samplePoint;
        arbMessage = arb.message;
        out.insert(QStringLiteral("arbOk"), arb.ok);
        if (!arb.ok) {
            out.insert(QStringLiteral("ok"), false);
            out.insert(QStringLiteral("message"), arb.message);
            out.insert(QStringLiteral("clock"), clock);
            out.insert(QStringLiteral("calibrated"), false);
            return out;
        }
    }

    out.insert(QStringLiteral("clock"), clock);
    out.insert(QStringLiteral("calibrated"), calibrated);
    out.insert(QStringLiteral("arbOk"), true);
    out.insert(QStringLiteral("arbMessage"), arbMessage);
    out.insert(QStringLiteral("arbActualBps"), arbActual);
    out.insert(QStringLiteral("arbSamplePoint"), arbSp);
    out.insert(QStringLiteral("arbBrp"), arbParams.brp);
    out.insert(QStringLiteral("arbTseg1"), arbParams.tseg1);
    out.insert(QStringLiteral("arbTseg2"), arbParams.tseg2);
    out.insert(QStringLiteral("arbSjw"), arbParams.sjw);

    const bool ok = true;
    QString message = arbMessage;
    if (dataBps > 0) {
        const auto dat = BitTiming::calculate(clock, static_cast<quint32>(dataBps), samplePointPercent);
        out.insert(QStringLiteral("dataOk"), dat.ok);
        out.insert(QStringLiteral("dataMessage"), dat.message);
        out.insert(QStringLiteral("dataActualBps"), dat.actualBps);
        out.insert(QStringLiteral("dataSamplePoint"), dat.samplePoint);
        out.insert(QStringLiteral("dataBrp"), dat.params.brp);
        out.insert(QStringLiteral("dataTseg1"), dat.params.tseg1);
        out.insert(QStringLiteral("dataTseg2"), dat.params.tseg2);
        out.insert(QStringLiteral("dataSjw"), dat.params.sjw);
        if (dat.ok)
            message += QStringLiteral(" | ") + dat.message;
    } else {
        out.insert(QStringLiteral("dataOk"), true);
    }

    out.insert(QStringLiteral("ok"), ok);
    out.insert(QStringLiteral("message"), message);
    return out;
}

quint32 CanDevice::recommendedClock() const
{
    // V1.01 硬件时钟 60MHz、V1.03+ 为 80MHz（ZLG 文档）。实测本机（HW=0x0300）
    // 在 80MHz 下 125k/250k/500k/1M 都能用同一形状（12/1、SP 87.5%）精确命中。
    std::scoped_lock lk(m_stateMutex);
    if (!m_info.valid)
        return BitTiming::DefaultClock;
    return BitTiming::clockForHardwareVersion(m_info.hwv);
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
