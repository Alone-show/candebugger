#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <mutex>

#include "core/BitTiming.h"
#include "zcan.h"

struct CanDeviceInfo
{
    bool valid = false;
    quint16 hwv = 0;
    quint16 fwv = 0;
    quint16 drv = 0;
    quint16 api = 0;
    quint16 irq = 0;
    quint8 channels = 0;
    QString sn;
    QString id;
};

struct ChannelConfig
{
    quint32 clock = BitTiming::DefaultClock;
    quint32 mode = 0;          // bit0: 0-正常 1-只听; bit1: 0-ISO 1-BOSCH
    BitTimingParams arbitration;   // 仲裁域
    BitTimingParams data;          // 数据域(CANFD)
};

// USBCANFD 设备封装，串行化对 SDK 句柄的访问
class CanDevice : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(QString serialNumber READ serialNumber NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString cardId READ cardId NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString versionText READ versionText NOTIFY deviceInfoChanged)
    Q_PROPERTY(int channels READ channels NOTIFY deviceInfoChanged)
    Q_PROPERTY(int txTimeoutMs READ txTimeoutMs WRITE setTxTimeoutMs NOTIFY txTimeoutChanged)
    // 依据硬件版本推荐的 CAN 控制器时钟（V1.03+ 为 80MHz）
    Q_PROPERTY(quint32 recommendedClock READ recommendedClock NOTIFY deviceInfoChanged)

public:
    static constexpr quint32 DeviceType = 33;   // USBCANFD

    explicit CanDevice(QObject *parent = nullptr);
    ~CanDevice() override;

    bool open(quint32 card = 0);
    Q_INVOKABLE bool openDevice(int card = 0);
    Q_INVOKABLE void close();
    // 只读状态：无锁原子读，QML 绑定不会与阻塞式接收争锁
    bool isOpen() const;

    CanDeviceInfo deviceInfo() const;
    QString serialNumber() const;
    QString cardId() const;
    QString versionText() const;
    int channels() const;

    bool initChannel(quint32 port, const ChannelConfig &cfg);

    // QML 便捷接口：按波特率自动计算位定时；成功返回描述文本，失败返回空串
    // clock: 控制器时钟(clk)，0 表示 BitTiming::DefaultClock(60MHz)；
    //        V1.03+ 硬件应为 80MHz（界面「自动」= recommendedClock()）
    Q_INVOKABLE QString configureChannel(int port, int arbitrationBps, int dataBps,
                                         double samplePointPercent = 87.5,
                                         bool listenOnly = false,
                                         quint32 clock = 0);

    // 高级接口：直接下发原始位定时参数（不做公式计算）；成功返回描述文本，失败返回空串
    Q_INVOKABLE QString configureChannelRaw(int port, quint32 clock, bool listenOnly,
                                            int arbBrp, int arbTseg1, int arbTseg2,
                                            int arbSjw, int arbSmp,
                                            int dataBrp, int dataTseg1, int dataTseg2,
                                            int dataSjw, int dataSmp);

    // 预览公式计算出的位定时（不改动设备状态）
    Q_INVOKABLE QVariantMap bitTimingPreview(int arbitrationBps, int dataBps,
                                             double samplePointPercent, quint32 clock) const;

    // 依据硬件版本给出推荐的 CAN 控制器时钟（设备未打开时返回默认 60MHz）
    Q_INVOKABLE quint32 recommendedClock() const;

    Q_INVOKABLE bool startChannel(int port);
    Q_INVOKABLE bool resetChannel(int port);
    Q_INVOKABLE bool clearBuffer(int port);

    // 发送同步等待上限（CMD_CAN_TX_TIMEOUT，单位 ms）：正常模式发送时驱动会等待
    // 总线 ACK；无应答总线上默认会等到 ~1.5s 才返回。设为较小值可在超时后立即返回
    // （帧仍会提交），避免阻塞 GUI。
    int txTimeoutMs() const;
    void setTxTimeoutMs(int ms);
    Q_INVOKABLE bool setTxTimeout(int port, int ms);

    Q_INVOKABLE bool setTerminalResistor(int port, bool on);
    bool getTerminalResistor(quint32 port, bool *on);
    Q_INVOKABLE bool setRecvMerge(int port, bool on);
    bool getRecvMerge(quint32 port, bool *on);

    // 定时发送：CMD_CAN_TTX(配置表) / CMD_CAN_TTX_CTL(启停)
    bool setPeriodicConfig(quint32 port, const ZCAN_TTX_CFG &cfg);
    bool setPeriodicEnabled(quint32 port, bool on);

    // 硬件验收滤波：CMD_CAN_FILTER（table.size 为字节数；0 表示关闭）
    bool setHardwareFilter(quint32 port, const ZCAN_FILTER_TABLE &table);

    // 发送队列（ZCAN_CMD_*_SEND_QUEUE_*）
    bool getSendQueueSize(quint32 port, quint32 *size);
    bool getSendQueueSpace(quint32 port, quint32 *space);
    bool enableSendQueue(quint32 port, bool on);
    bool clearSendQueue(quint32 port);

    bool transmitCan(quint32 port, const ZCAN_20_MSG &msg);
    bool transmitFd(quint32 port, const ZCAN_FD_MSG &msg);

    // 接收：短超时轮询，供接收线程调用（内部加锁，持锁时间受 timeoutMs 限制）
    int receiveCan(quint32 port, ZCAN_20_MSG *buf, quint32 count, quint32 timeoutMs = 5);
    int receiveFd(quint32 port, ZCAN_FD_MSG *buf, quint32 count, quint32 timeoutMs = 5);
    int receiveData(quint32 port, ZCANDataObj *buf, quint32 count, quint32 timeoutMs = 5);

    static QString resultString(quint32 code);

    // 诊断：测量获取 I/O 锁的等待耗时(us)，不做任何设备操作。
    // 用于区分「SDK 调用本身慢」与「被接收线程在共享锁上饿死」。
    qint64 ioLockWaitUs();

signals:
    void openChanged();
    void deviceInfoChanged();
    void txTimeoutChanged();

private:
    bool readBoardInfo(quint32 card);

    // 锁分层（解决接收线程在共享锁上饿死 GUI 的护航问题）：
    //   m_rxMutex    : 仅接收路径持有（可在阻塞式 VCI_Receive 期间长持锁，无人争用）
    //   m_ioMutex    : 发送 + 配置(get/setReference) + 发送队列，短调用，GUI 走这里
    //   m_stateMutex : 仅保护 m_info
    // 生命周期调用(open/close/init/start/reset/clearBuffer)同时持 rx+io，保证与接收串行。
    mutable std::mutex m_rxMutex;
    mutable std::mutex m_ioMutex;
    mutable std::mutex m_stateMutex;
    std::atomic<bool> m_open{false};
    quint32 m_card = 0;
    CanDeviceInfo m_info;
    std::atomic<int> m_txTimeoutMs{20};
};
