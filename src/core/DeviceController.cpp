#include "core/DeviceController.h"

#include <QMetaObject>
#include <QStringList>
#include <QThread>
#include <QVariantMap>

#include "core/CanDevice.h"
#include "core/RxManager.h"
#include "core/UsbProbe.h"

DeviceController::DeviceController(CanDevice *device, RxManager *rx, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_rx(rx)
{
    if (m_rx)
        connect(m_rx, &RxManager::runningChanged, this, &DeviceController::connectedChanged);
}

DeviceController::~DeviceController()
{
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

bool DeviceController::connected() const
{
    return m_device && m_device->isOpen() && m_rx && m_rx->running();
}

int DeviceController::channelCount() const
{
    if (!m_device)
        return 0;
    const int n = m_device->channels();
    return n > 0 ? qMin(2, n) : 2;
}

void DeviceController::setBusy(bool busy, const QString &text)
{
    if (m_busy == busy && m_busyText == text)
        return;
    m_busy = busy;
    m_busyText = text;
    emit busyChanged();
    emit busyTextChanged();
}

void DeviceController::startWorker(std::function<void()> fn)
{
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    m_thread = QThread::create(std::move(fn));
    m_thread->start();
}

QString DeviceController::configureAll(const QVariantList &channels, bool merge, bool resetFirst)
{
    const int nch = channelCount();
    if (resetFirst) {
        for (int ch = 0; ch < nch; ++ch)
            m_device->resetChannel(ch);
    }

    QStringList report;
    for (const QVariant &v : channels) {
        const QVariantMap m = v.toMap();
        const int port = m.value(QStringLiteral("port"), 0).toInt();
        const int arb = m.value(QStringLiteral("arb"), 500000).toInt();
        const int dat = m.value(QStringLiteral("dat"), 2000000).toInt();
        const double sample = m.value(QStringLiteral("sample"), 87.5).toDouble();
        const bool listen = m.value(QStringLiteral("listenOnly"), false).toBool();
        const bool term = m.value(QStringLiteral("terminator"), false).toBool();
        // clock 缺省按设备硬件版本推断（V1.03+ 为 80MHz）
        quint32 clock = m.value(QStringLiteral("clock"), 0).toUInt();
        if (clock == 0)
            clock = m_device->recommendedClock();
        const bool useRaw = m.value(QStringLiteral("useRaw"), false).toBool();

        QString msg;
        if (useRaw) {
            msg = m_device->configureChannelRaw(port, clock, listen,
                                                m.value(QStringLiteral("arbBrp"), 0).toInt(),
                                                m.value(QStringLiteral("arbTseg1"), 0).toInt(),
                                                m.value(QStringLiteral("arbTseg2"), 0).toInt(),
                                                m.value(QStringLiteral("arbSjw"), 0).toInt(),
                                                m.value(QStringLiteral("arbSmp"), 0).toInt(),
                                                m.value(QStringLiteral("dataBrp"), 0).toInt(),
                                                m.value(QStringLiteral("dataTseg1"), 0).toInt(),
                                                m.value(QStringLiteral("dataTseg2"), 0).toInt(),
                                                m.value(QStringLiteral("dataSjw"), 0).toInt(),
                                                m.value(QStringLiteral("dataSmp"), 0).toInt());
        } else {
            msg = m_device->configureChannel(port, arb, dat, sample, listen, clock);
        }
        if (msg.isEmpty())
            return QStringLiteral("通道 %1 配置失败（无匹配位定时，检查时钟/波特率）").arg(port + 1);

        m_device->setTerminalResistor(port, term);
        if (!m_device->startChannel(port))
            return QStringLiteral("通道 %1 启动失败").arg(port + 1);

        report << QStringLiteral("CH%1: %2").arg(port + 1).arg(msg);
    }

    m_device->setRecvMerge(0, merge);
    m_pendingReport = report.join(QStringLiteral("\n"));
    return QString();
}

void DeviceController::connectDevice(const QVariantList &channels, bool merge)
{
    if (m_busy)
        return;

    setBusy(true, QStringLiteral("正在连接设备…"));
    if (m_rx && m_rx->running())
        m_rx->stop();

    startWorker([this, channels, merge]() {
        QString err;
        // VCI_OpenDevice 只返回 0/1，无法自证失败原因；失败时用 sysfs 探测区分
        // 「没插 / 型号不符 / 没权限 / 被占用 / 固件未就绪」，给出唯一对应的修复动作。
        if (!m_device->isOpen() && !m_device->open(0))
            err = QStringLiteral("打开设备失败：") + UsbProbe::describe(UsbProbe::probe());
        if (err.isEmpty())
            err = configureAll(channels, merge, false);

        if (!err.isEmpty()) {
            const int nch = channelCount();
            for (int ch = 0; ch < nch; ++ch)
                m_device->resetChannel(ch);
            m_device->close();
        }

        QMetaObject::invokeMethod(this, [this, channels, merge, err]() {
            finishListen(channels, merge, err);
        }, Qt::QueuedConnection);
    });
}

void DeviceController::applyConfig(const QVariantList &channels, bool merge)
{
    if (m_busy)
        return;
    if (!m_device->isOpen()) {
        emit failed(QStringLiteral("设备未连接"));
        return;
    }

    setBusy(true, QStringLiteral("正在应用配置…"));
    if (m_rx && m_rx->running())
        m_rx->stop();

    startWorker([this, channels, merge]() {
        const QString err = configureAll(channels, merge, true);
        QMetaObject::invokeMethod(this, [this, channels, merge, err]() {
            finishListen(channels, merge, err);
        }, Qt::QueuedConnection);
    });
}

void DeviceController::finishListen(const QVariantList &channels, bool merge, const QString &error)
{
    QString err = error;
    if (err.isEmpty()) {
        QVariantList ports;
        ports.reserve(channels.size());
        for (const QVariant &v : channels)
            ports << v.toMap().value(QStringLiteral("port"), 0).toInt();

        if (!m_rx || !m_rx->start(ports, merge))
            err = QStringLiteral("启动接收失败");
    }

    setBusy(false, QString());

    if (m_configReport != m_pendingReport) {
        m_configReport = m_pendingReport;
        emit configReportChanged();
    }
    emit connectedChanged();

    if (err.isEmpty())
        emit succeeded(QStringLiteral("设备已连接，正在监听"));
    else
        emit failed(err);
}

void DeviceController::disconnectDevice()
{
    if (m_busy)
        return;

    setBusy(true, QStringLiteral("正在断开设备…"));
    if (m_rx && m_rx->running())
        m_rx->stop();

    startWorker([this]() {
        const int nch = channelCount();
        for (int ch = 0; ch < nch; ++ch)
            m_device->resetChannel(ch);
        m_device->close();

        QMetaObject::invokeMethod(this, [this]() {
            setBusy(false, QString());
            if (!m_configReport.isEmpty()) {
                m_configReport.clear();
                m_pendingReport.clear();
                emit configReportChanged();
            }
            emit connectedChanged();
            emit succeeded(QStringLiteral("设备已断开"));
        }, Qt::QueuedConnection);
    });
}
