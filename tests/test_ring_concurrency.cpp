// L1/并发：RingBuffer 生产者-消费者不变式（TSan 目标）
#include <QtTest>

#include <atomic>
#include <thread>
#include <vector>

#include "core/FrameData.h"
#include "core/RingBuffer.h"

namespace {

FrameData mkFrame(quint32 id)
{
    FrameData f;
    f.id = id;
    f.len = 2;
    f.data = QByteArray(2, static_cast<char>(id & 0xFF));
    return f;
}

// TSan/慢速环境可通过 CANDBG_TEST_DIV 缩小规模（默认 1）
int testDiv()
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue("CANDBG_TEST_DIV", &ok);
    return (ok && v > 0) ? v : 1;
}

} // namespace

class TestRingConcurrency : public QObject
{
    Q_OBJECT

private slots:
    void sequentialInvariant()
    {
        RingBuffer ring(8);
        for (quint32 i = 0; i < 20; ++i)
            ring.push(mkFrame(i));

        QVector<FrameData> out;
        const int popped = ring.pop(out, 3);

        QCOMPARE(ring.totalPushed(), quint64(20));
        QCOMPARE(ring.totalDropped(), quint64(12));
        QCOMPARE(ring.size(), 5);
        QCOMPARE(quint64(popped), quint64(3));
        // 不变式：pushed == popped + size + dropped
        QCOMPARE(ring.totalPushed(),
                 quint64(popped) + quint64(ring.size()) + ring.totalDropped());
        // 覆盖最旧后，剩余应为 12..19，先取 12,13,14
        QCOMPARE(out.first().id, quint32(12));
        QCOMPARE(out.last().id, quint32(14));
    }

    void singleProducerConsumerOrder()
    {
        RingBuffer ring(1 << 17);   // 容量大于总量，保证不丢
        const int n = 100000 / testDiv();

        std::atomic<bool> done{false};
        quint64 popped = 0;
        bool orderOk = true;
        quint32 expect = 0;

        std::thread consumer([&]() {
            QVector<FrameData> out;
            out.reserve(4096);
            while (!done.load(std::memory_order_acquire) || ring.size() > 0) {
                out.clear();
                const int got = ring.pop(out, 4096);
                if (got == 0) {
                    std::this_thread::yield();
                    continue;
                }
                for (const FrameData &f : out) {
                    if (f.id != expect)
                        orderOk = false;
                    ++expect;
                    ++popped;
                }
            }
        });

        for (int i = 0; i < n; ++i)
            ring.push(mkFrame(static_cast<quint32>(i)));
        done.store(true, std::memory_order_release);
        consumer.join();

        QVERIFY(orderOk);
        QCOMPARE(popped, quint64(n));
        QCOMPARE(ring.totalDropped(), quint64(0));
        QCOMPARE(ring.size(), 0);
    }

    void multiProducerInvariant()
    {
        const int producers = 4;
        const int perProducer = 20000 / testDiv();
        const int total = producers * perProducer;

        RingBuffer ring(4096);
        std::atomic<bool> producersDone{false};
        std::atomic<quint64> poppedTotal{0};
        std::atomic<int> invalid{0};

        std::vector<char> visited(total, 0);   // 仅消费者线程写，join 后主线程读

        std::thread consumer([&]() {
            QVector<FrameData> out;
            out.reserve(1024);
            while (!producersDone.load(std::memory_order_acquire) || ring.size() > 0) {
                out.clear();
                const int got = ring.pop(out, 1024);
                if (got == 0) {
                    std::this_thread::yield();
                    continue;
                }
                for (const FrameData &f : out) {
                    if (f.id < static_cast<quint32>(total)) {
                        if (visited[f.id])
                            invalid.fetch_add(1);
                        visited[f.id] = 1;
                    } else {
                        invalid.fetch_add(1);
                    }
                    poppedTotal.fetch_add(1);
                }
            }
        });

        std::vector<std::thread> pts;
        pts.reserve(producers);
        for (int p = 0; p < producers; ++p) {
            pts.emplace_back([&, p]() {
                for (int i = 0; i < perProducer; ++i)
                    ring.push(mkFrame(static_cast<quint32>(p * perProducer + i)));
            });
        }
        for (auto &t : pts)
            t.join();
        producersDone.store(true, std::memory_order_release);
        consumer.join();

        QCOMPARE(invalid.load(), 0);
        int visitedCount = 0;
        for (char c : visited)
            if (c)
                ++visitedCount;
        // 无重复：取出的帧数应等于被标记的唯一 id 数
        QCOMPARE(quint64(visitedCount), poppedTotal.load());
        QCOMPARE(ring.size(), 0);
        QCOMPARE(ring.totalPushed(), quint64(total));
        QCOMPARE(ring.totalPushed(), poppedTotal.load() + ring.totalDropped());
    }

    void concurrentVectorPushAndPop()
    {
        RingBuffer ring(8192);
        const int threads = 4;
        const int perThread = 5000 / testDiv();

        std::atomic<bool> done{false};
        std::atomic<quint64> popped{0};

        std::thread consumer([&]() {
            QVector<FrameData> out;
            out.reserve(2048);
            while (!done.load(std::memory_order_acquire) || ring.size() > 0) {
                out.clear();
                if (ring.pop(out, 2048) > 0)
                    popped.fetch_add(static_cast<quint64>(out.size()));
                else
                    std::this_thread::yield();
            }
        });

        std::vector<std::thread> pts;
        pts.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            pts.emplace_back([&, t]() {
                QVector<FrameData> batch;
                batch.reserve(perThread);
                for (int i = 0; i < perThread; ++i)
                    batch.append(mkFrame(static_cast<quint32>(t * perThread + i)));
                ring.push(batch);
            });
        }
        for (auto &t : pts)
            t.join();
        done.store(true, std::memory_order_release);
        consumer.join();

        QCOMPARE(ring.totalPushed(), quint64(threads * perThread));
        QCOMPARE(ring.size(), 0);
        QCOMPARE(ring.totalPushed(), popped.load() + ring.totalDropped());
    }
};

QTEST_APPLESS_MAIN(TestRingConcurrency)

#include "test_ring_concurrency.moc"
