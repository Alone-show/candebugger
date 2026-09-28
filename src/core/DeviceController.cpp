#include "core/DeviceController.h"

#include <QMetaObject>
#include <QThread>
#include <QVariantMap>

#include "core/CanDevice.h"
#include "core/RxManager.h"

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

    for (const QVariant &v : channels) {
        const QVariantMap m = v.toMap();
        const int port = m.value(QStringLiteral("port"), 0).toInt();
        const int arb = m.value(QStringLiteral("arb"), 500000).toInt();
        const int dat = m.value(QStringLiteral("dat"), 2000000).toInt();
        const double sample = m.value(QStringLiteral("sample"), 87.5).toDouble();
        const bool listen = m.value(QStringLiteral("listenOnly"), false).toBool();
        const bool term = m.value(QStringLiteral("terminator"), false).toBool();

        const QString msg = m_device->configureChannel(port, arb, dat, sample, listen);
        if (msg.isEmpty())
            return QStringLiteral("通道 %1 配置失败（无匹配位定时）").arg(port + 1);

        m_device->setTerminalResistor(port, term);
        if (!m_device->startChannel(port))
            return QStringLiteral("通道 %1 启动失败").arg(port + 1);
    }

    m_device->setRecvMerge(0, merge);
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
        if (!m_device->isOpen() && !m_device->open(0))
            err = QStringLiteral("打开设备失败：请检查 USB 权限（需 udev 规则）或设备是否被占用");
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
            emit connectedChanged();
            emit succeeded(QStringLiteral("设备已断开"));
        }, Qt::QueuedConnection);
    });
}
