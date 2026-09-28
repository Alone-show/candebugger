#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

class QThread;
class CanDevice;
class RxManager;

// 设备会话控制器：把「连接设备 / 应用配置 / 断开设备」的阻塞式 SDK 调用放到工作线程，
// GUI 线程只负责切换状态与启停接收线程，避免界面冻结。
//
// - connectDevice : 打开设备 → 按配置初始化/启动两通道 → 终端电阻 → 合并开关 → 启动接收
// - applyConfig   : 复位通道 → 重新初始化/启动 → 终端电阻 → 合并开关 → 重启接收
// - disconnectDevice : 停止接收 → 复位通道 → 关闭设备
class DeviceController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyTextChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    DeviceController(CanDevice *device, RxManager *rx, QObject *parent = nullptr);
    ~DeviceController() override;

    bool busy() const { return m_busy; }
    QString busyText() const { return m_busyText; }
    bool connected() const;

    Q_INVOKABLE void connectDevice(const QVariantList &channels, bool merge);
    Q_INVOKABLE void applyConfig(const QVariantList &channels, bool merge);
    Q_INVOKABLE void disconnectDevice();

signals:
    void busyChanged();
    void busyTextChanged();
    void connectedChanged();
    void failed(const QString &message);
    void succeeded(const QString &message);

private:
    void setBusy(bool busy, const QString &text);
    void startWorker(std::function<void()> fn);
    int channelCount() const;
    // 在工作线程执行：可选复位 → 逐通道配置/启动/终端电阻 → 合并开关
    QString configureAll(const QVariantList &channels, bool merge, bool resetFirst);
    // 回到 GUI 线程：启动接收并收尾
    void finishListen(const QVariantList &channels, bool merge, const QString &error);

    CanDevice *m_device = nullptr;
    RxManager *m_rx = nullptr;
    bool m_busy = false;
    QString m_busyText;
    QThread *m_thread = nullptr;
};
