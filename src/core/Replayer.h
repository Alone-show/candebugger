#pragma once

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>
#include <QWaitCondition>

#include <atomic>
#include <deque>
#include <thread>

#include "core/FrameData.h"

class CanDevice;
class QTimer;
class AsyncExecutor;

// 回放器：自动识别 ASC/CSV，解析为帧序列后按原始时间间隔或倍速经
// canDevice.transmitCan/transmitFd 发送。
//
// 线程模型：
//   - GUI 线程 QTimer 做时间轴调度（纯 `dueFrameCount`），只把到期帧非阻塞入队；
//   - 独立发送线程消费队列并调用 SDK 发送。实测 `VCI_Transmit` 在接收线程并发时
//     可能阻塞数秒，故发送绝不能在 GUI 线程执行（保证 UI 不阻塞）。
//
// 解析（文件→帧序列）与时间轴换算均为静态纯函数，便于免设备单测。
class Replayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(int format READ format NOTIFY filePathChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameCountChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(qint64 durationUs READ durationUs NOTIFY durationChanged)
    Q_PROPERTY(qint64 positionUs READ positionUs NOTIFY positionChanged)
    Q_PROPERTY(double progress READ progress NOTIFY positionChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    enum Format { UnknownFormat = 0, AscFormat = 1, CsvFormat = 2 };
    Q_ENUM(Format)

    explicit Replayer(CanDevice *device = nullptr, QObject *parent = nullptr);
    ~Replayer() override;

    // ---- 纯逻辑（静态，可单测） ----
    static int detectFormat(const QString &path, const QString &content = QString());
    static QVector<FrameData> parseText(const QString &content, int format, QString *error = nullptr);
    static QVector<FrameData> parseFile(const QString &path, int *format = nullptr, QString *error = nullptr);
    // 归一化时间轴：以首帧为 0，返回时长(us)
    static qint64 normalizeTimeline(QVector<FrameData> &frames);
    static qint64 totalDurationUs(const QVector<FrameData> &frames);
    static double progressPercent(qint64 positionUs, qint64 durationUs);
    static qint64 scaledDelayUs(qint64 deltaUs, double speed);
    // 归一化帧序列上，返回自 startIndex 起已达到播放时间的帧数
    static int dueFrameCount(const QVector<FrameData> &frames, qint64 elapsedUs,
                             double speed, int startIndex);

    Q_INVOKABLE bool load(const QString &path);
    // QML：异步加载（后台线程读文件/解析，避免 GUI 阻塞）；结果经 loadFinished 回传
    Q_INVOKABLE bool loadAsync(const QString &path);
    Q_INVOKABLE bool play(int port = -1);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();

    QString filePath() const;
    int format() const;
    int frameCount() const;
    bool isPlaying() const;
    bool isPaused() const;
    double speed() const;
    void setSpeed(double speed);
    int port() const;
    void setPort(int port);
    qint64 durationUs() const;
    qint64 positionUs() const;
    double progress() const;
    QString errorString() const;
    bool busy() const;

signals:
    void filePathChanged();
    void frameCountChanged();
    void playingChanged();
    void pausedChanged();
    void speedChanged();
    void portChanged();
    void durationChanged();
    void positionChanged();
    void errorStringChanged();
    void finished();
    void busyChanged();
    void loadFinished(bool ok, const QString &error);

private slots:
    void tick();

private:
    // 发送线程：实际调用 SDK（可能阻塞）
    void sendLoop();
    bool transmit(const FrameData &f);
    void enqueueSend(const FrameData &f);
    void clearSendQueue();
    void startSendThread();
    void stopSendThread();
    void drainPendingError();

    void setError(const QString &msg);
    void resetPlayback();
    void applyLoad(const QString &path, QVector<FrameData> frames, int format, const QString &error);

    CanDevice *m_device = nullptr;
    QTimer *m_timer = nullptr;
    AsyncExecutor *m_executor = nullptr;
    QElapsedTimer m_clock;

    QVector<FrameData> m_frames;
    QString m_filePath;
    int m_format = UnknownFormat;
    int m_index = 0;
    qint64 m_elapsedBaseUs = 0;
    bool m_playing = false;
    bool m_paused = false;
    double m_speed = 1.0;
    int m_port = 0;
    qint64 m_durationUs = 0;
    qint64 m_positionUs = 0;
    QString m_error;

    // 发送线程共享状态
    std::thread m_sendThread;
    QMutex m_sendMutex;
    QWaitCondition m_sendCond;
    std::deque<FrameData> m_sendQueue;
    std::atomic<bool> m_sendRun{false};
    std::atomic<bool> m_sendPaused{false};
    QString m_pendingError;   // 由 m_sendMutex 保护，GUI 线程取出后 emit
};
