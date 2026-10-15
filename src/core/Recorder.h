#pragma once

#include <QDateTime>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>
#include <QWaitCondition>

#include <atomic>
#include <deque>
#include <thread>

#include "core/FrameData.h"

class QFile;
class QTimer;

// 记录器：生产者（GUI 线程：接收 drain 批 / 发送路径）非阻塞入队，
// 消费者（独立写盘线程）落盘。支持 ASC(CANoe 风格) 与 CSV 双格式，可单选或同时写。
//
// 线程模型：
//   - enqueueFrame/enqueueFrames 在 GUI 线程调用，仅短暂持锁拷贝到队列，不阻塞 UI；
//   - 写盘线程仅访问队列与文件，不与接收路径共享逐帧信号；
//   - 统计属性经 GUI 线程轮询定时器发出 NOTIFY，避免跨线程 emit。
class Recorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(int formatFlags READ formatFlags WRITE setFormatFlags NOTIFY formatFlagsChanged)
    Q_PROPERTY(QString ascPath READ ascPath NOTIFY filePathChanged)
    Q_PROPERTY(QString csvPath READ csvPath NOTIFY filePathChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(int queueCapacity READ queueCapacity WRITE setQueueCapacity NOTIFY queueCapacityChanged)
    Q_PROPERTY(qulonglong writtenCount READ writtenCount NOTIFY statsChanged)
    Q_PROPERTY(qulonglong droppedCount READ droppedCount NOTIFY statsChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    enum Format { AscFormat = 1, CsvFormat = 2 };
    Q_ENUM(Format)

    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    // ---- 纯逻辑（静态，可单测） ----
    static QString ascHeader(const QDateTime &start);
    static QString ascFooter();
    static QString csvHeader();
    static QString formatAscLine(const FrameData &f, qint64 startWallUs);
    static QString formatCsvLine(const FrameData &f, qint64 startWallUs);
    // 解析单行；成功返回 true 并填充 out / relUs（相对文件起点微秒）
    static bool parseAscLine(const QString &line, FrameData *out, qint64 *relUs);
    static bool parseCsvLine(const QString &line, FrameData *out, qint64 *relUs);
    static QString defaultFileName(const QDateTime &dt, int format);
    static double relativeSeconds(qint64 wallUs, qint64 startWallUs);

    // 生产端（GUI 线程）：非阻塞入队
    // 接收路径：跳过 tx 帧（回显/回环），本机发送由发送路径记录，避免重复
    void enqueueFrames(const QVector<FrameData> &frames);
    void enqueueFrame(const FrameData &f);

    // QML：按目录 + 格式开关自动生成带时间戳文件名
    Q_INVOKABLE bool start(const QString &directory = QString(), bool asc = true, bool csv = true);
    // QML/测试：显式指定路径（空串表示该格式不写）
    Q_INVOKABLE bool startWithPaths(const QString &ascPath, const QString &csvPath);
    Q_INVOKABLE void stop();

    bool isRecording() const;
    int formatFlags() const;
    void setFormatFlags(int flags);
    QString ascPath() const;
    QString csvPath() const;
    QString filePath() const;
    int queueCapacity() const;
    void setQueueCapacity(int capacity);
    qulonglong writtenCount() const;
    qulonglong droppedCount() const;
    QString errorString() const;

signals:
    void recordingChanged();
    void formatFlagsChanged();
    void filePathChanged();
    void queueCapacityChanged();
    void statsChanged();
    void errorStringChanged();

private:
    void writerLoop();
    void writeBatch(const QVector<FrameData> &batch);
    void setError(const QString &msg);
    void pollStats();
    void closeFiles();

    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    std::deque<FrameData> m_queue;
    int m_queueMax = 200000;
    std::atomic<bool> m_running{false};
    std::thread m_thread;

    QTimer *m_pollTimer = nullptr;
    QFile *m_ascFile = nullptr;
    QFile *m_csvFile = nullptr;

    // 发送线程（PeriodicSender）也会调用 enqueueFrame，故用原子量避免跨线程读取竞态
    std::atomic<bool> m_recording{false};
    int m_formatFlags = AscFormat | CsvFormat;
    QString m_ascPath;
    QString m_csvPath;
    qint64 m_startWallUs = 0;
    QDateTime m_startTime;
    qulonglong m_written = 0;
    qulonglong m_dropped = 0;
    QString m_error;

    qulonglong m_emittedWritten = 0;
    qulonglong m_emittedDropped = 0;
    QString m_emittedError;
};
