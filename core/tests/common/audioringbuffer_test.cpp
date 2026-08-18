#include "stdafx.h"
#include "pch.h"

#include <thread>
#include <vector>

#include "common/sound/audioringbuffer.h"

/// AudioRingBuffer tests (audio-sync design, Fix 2 groundwork):
/// occupancy accounting in stereo frames, error counters, SPSC integrity
/// under concurrent producer/consumer.

TEST(AudioRingBuffer_Test, OccupancyInStereoFrames)
{
    AudioRingBuffer<int16_t, 1024> ring;

    EXPECT_EQ(ring.getOccupancyStereoFrames(), 0u);

    int16_t samples[220] = {0};
    EXPECT_EQ(ring.enqueue(samples, 220), 220u);  // 110 stereo frames
    EXPECT_EQ(ring.getOccupancyStereoFrames(), 110u);
    EXPECT_EQ(ring.getAvailableData(), 220u);

    int16_t out[100];
    EXPECT_EQ(ring.dequeue(out, 100), 100u);
    EXPECT_EQ(ring.getOccupancyStereoFrames(), 60u);
}

TEST(AudioRingBuffer_Test, ErrorCountersObservable)
{
    AudioRingBuffer<int16_t, 64> ring;
    int16_t samples[64] = {0};

    // Fill to capacity (Size-1 usable)
    EXPECT_EQ(ring.enqueue(samples, 64), 63u);
    EXPECT_EQ(ring.getEnqueueErrorCount(), 1u) << "Partial write counts as an error";

    EXPECT_EQ(ring.enqueue(samples, 4), 0u);
    EXPECT_EQ(ring.getEnqueueErrorCount(), 2u) << "Full-buffer drop counts";

    int16_t out[64];
    EXPECT_EQ(ring.dequeue(out, 63), 63u);
    EXPECT_EQ(ring.dequeue(out, 4), 0u);
    EXPECT_EQ(ring.getDequeueErrorCount(), 1u) << "Empty-buffer underrun counts";
}

TEST(AudioRingBuffer_Test, DataIntegrityAcrossWrap)
{
    AudioRingBuffer<int16_t, 128> ring;

    int16_t seq = 0;
    int16_t in[48], out[48];

    // Push/pop cycles that force index wraparound; verify FIFO content
    for (int cycle = 0; cycle < 20; cycle++)
    {
        for (int i = 0; i < 48; i++)
            in[i] = seq + i;

        ASSERT_EQ(ring.enqueue(in, 48), 48u) << "cycle " << cycle;
        ASSERT_EQ(ring.dequeue(out, 48), 48u) << "cycle " << cycle;

        for (int i = 0; i < 48; i++)
            ASSERT_EQ(out[i], static_cast<int16_t>(seq + i)) << "cycle " << cycle << " idx " << i;

        seq += 48;
    }

    EXPECT_EQ(ring.getEnqueueErrorCount(), 0u);
    EXPECT_EQ(ring.getDequeueErrorCount(), 0u);
}

TEST(AudioRingBuffer_Test, SPSC_ConcurrentIntegrity)
{
    // One producer, one consumer, monotonically increasing values: the
    // consumer must observe a strictly sequential stream with no tears,
    // duplications, or gaps. Exercises the acquire/release index protocol
    // (previously plain size_t - an actual data race).
    static AudioRingBuffer<int16_t, 4096> ring;
    constexpr int TOTAL = 200000;

    std::thread producer([&]() {
        int16_t value = 0;
        int produced = 0;
        int16_t chunk[128];
        while (produced < TOTAL)
        {
            int n = std::min(128, TOTAL - produced);
            for (int i = 0; i < n; i++)
                chunk[i] = static_cast<int16_t>(value + i);
            size_t written = ring.enqueue(chunk, n);
            value = static_cast<int16_t>(value + written);
            produced += static_cast<int>(written);
            if (written == 0)
                std::this_thread::yield();
        }
    });

    int consumed = 0;
    int16_t expected = 0;
    bool sequenceOk = true;
    int16_t chunk[128];
    while (consumed < TOTAL)
    {
        size_t got = ring.dequeue(chunk, 128);
        for (size_t i = 0; i < got; i++)
        {
            if (chunk[i] != expected)
            {
                sequenceOk = false;
                break;
            }
            expected++;
        }
        if (!sequenceOk)
            break;
        consumed += static_cast<int>(got);
        if (got == 0)
            std::this_thread::yield();
    }

    producer.join();
    EXPECT_TRUE(sequenceOk) << "SPSC stream must be strictly sequential (no tears/gaps)";
    EXPECT_EQ(consumed, TOTAL);
}
