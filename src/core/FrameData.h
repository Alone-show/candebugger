#pragma once

#include <QtGlobal>
#include <QByteArray>

// 统一后的报文表示，供接收线程、环形缓冲与模型使用
struct FrameData
{
    // 设备时间戳 hdr.ts 的 tick 单位(微秒)。经 tests/device_ts_probe 实测标定：
    // 100ms 间隔对应 Δts≈1057，即 1 tick = 100us = 0.1ms。设备上电后自由运行的
    // 32 位计数器，双通道共用同一时钟，约 4.97 天回绕一次。
    static constexpr qint64 kDevTsTickUs = 100;

    // 设备时间戳是否可用（0 视为无效，此时回退到主机时间）
    bool devTsValid() const { return devTs != 0; }

    qint64 hostUs = 0;      // 自接收工作线程启动以来的单调时间(us)
    qint64 wallUs = 0;      // 挂钟时间(us, epoch)
    quint32 devTs = 0;      // 设备时间戳原始值(tick=100us)
    quint32 id = 0;         // CAN ID（不含标志位）
    quint8 channel = 0;
    quint8 len = 0;
    bool extended = false;  // 扩展帧
    bool remote = false;    // 远程帧
    bool fd = false;        // CANFD
    bool brs = false;       // 位速率切换
    bool error = false;     // 错误帧
    bool tx = false;        // 发送方向（含回显）
    bool echo = false;      // 发送回显
    QByteArray data;        // 0..64 字节
};
