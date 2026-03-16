#include "pch.h"
#include "spsc_ringbuffer_test.h"
#include "common/spsc_ringbuffer.h"

#include <cstring>
#include <vector>

/// region <SetUp / TearDown>

void SPSCRingBuffer_Test::SetUp()
{
    size_t totalSize = emu::SPSCRingBuffer::totalMemoryRequired(TEST_CAPACITY);
    _memory = std::calloc(1, totalSize);
    ASSERT_NE(_memory, nullptr);
}

void SPSCRingBuffer_Test::TearDown()
{
    std::free(_memory);
    _memory = nullptr;
}

/// endregion </SetUp / TearDown>

// =============================================================================
// Header layout tests
// =============================================================================

TEST_F(SPSCRingBuffer_Test, HeaderSize)
{
    EXPECT_EQ(sizeof(emu::SPSCRingBuffer::Header), 192u);
}

TEST_F(SPSCRingBuffer_Test, MessageHeaderSize)
{
    EXPECT_EQ(sizeof(emu::SPSCRingBuffer::MessageHeader), 24u);
}

TEST_F(SPSCRingBuffer_Test, TotalMemoryRequired)
{
    EXPECT_EQ(emu::SPSCRingBuffer::totalMemoryRequired(4096), 192u + 4096u);
    EXPECT_EQ(emu::SPSCRingBuffer::totalMemoryRequired(1024), 192u + 1024u);
}

// =============================================================================
// Create / Initialize
// =============================================================================

TEST_F(SPSCRingBuffer_Test, CreateInitializesCorrectly)
{
    auto* rb = emu::SPSCRingBuffer::create(_memory, TEST_CAPACITY);
    ASSERT_NE(rb, nullptr);

    EXPECT_EQ(rb->capacity(), TEST_CAPACITY);
    EXPECT_EQ(rb->writePos(), 0u);
    EXPECT_EQ(rb->readPos(), 0u);
    EXPECT_EQ(rb->droppedCount(), 0u);
    EXPECT_EQ(rb->available(), 0u);
}

// =============================================================================
// Write / Read single message
// =============================================================================

TEST_F(SPSCRingBuffer_Test, WriteAndReadSingleMessage)
{
    auto* rb = emu::SPSCRingBuffer::create(_memory, TEST_CAPACITY);

    const char* payload = "Hello, monitor!";
    uint32_t len = static_cast<uint32_t>(std::strlen(payload));

    // Write
    bool written = rb->try_write(0x12345678, 2, payload, len);
    EXPECT_TRUE(written);
    EXPECT_GT(rb->available(), 0u);

    // Read
    emu::SPSCRingBuffer::MessageHeader hdr{};
    char buf[256] = {};
    bool read = rb->try_read(hdr, buf, sizeof(buf));
    EXPECT_TRUE(read);

    EXPECT_EQ(hdr.category_id, 0x12345678u);
    EXPECT_EQ(hdr.length, len);
    EXPECT_EQ(hdr.level, 2u);
    EXPECT_GT(hdr.timestamp, 0u);
    EXPECT_STREQ(buf, payload);

    // Buffer should be empty now
    EXPECT_EQ(rb->available(), 0u);
}

// =============================================================================
// Empty buffer
// =============================================================================

TEST_F(SPSCRingBuffer_Test, ReadFromEmptyReturnsFalse)
{
    auto* rb = emu::SPSCRingBuffer::create(_memory, TEST_CAPACITY);

    emu::SPSCRingBuffer::MessageHeader hdr{};
    char buf[64] = {};
    EXPECT_FALSE(rb->try_read(hdr, buf, sizeof(buf)));
}

// =============================================================================
// Multiple messages
// =============================================================================

TEST_F(SPSCRingBuffer_Test, WriteAndReadMultipleMessages)
{
    auto* rb = emu::SPSCRingBuffer::create(_memory, TEST_CAPACITY);

    // Write 10 messages
    for (uint32_t i = 0; i < 10; i++)
    {
        char msg[32];
        int n = std::snprintf(msg, sizeof(msg), "msg_%u", i);
        bool ok = rb->try_write(i, 1, msg, static_cast<uint32_t>(n));
        EXPECT_TRUE(ok) << "Failed to write message " << i;
    }

    // Read them back in order
    for (uint32_t i = 0; i < 10; i++)
    {
        emu::SPSCRingBuffer::MessageHeader hdr{};
        char buf[64] = {};
        bool ok = rb->try_read(hdr, buf, sizeof(buf));
        EXPECT_TRUE(ok) << "Failed to read message " << i;
        EXPECT_EQ(hdr.category_id, i);

        char expected[32];
        std::snprintf(expected, sizeof(expected), "msg_%u", i);
        EXPECT_STREQ(buf, expected);
    }

    // Should be empty
    emu::SPSCRingBuffer::MessageHeader hdr{};
    char buf[64] = {};
    EXPECT_FALSE(rb->try_read(hdr, buf, sizeof(buf)));
}

// =============================================================================
// Buffer full — message dropped
// =============================================================================

TEST_F(SPSCRingBuffer_Test, DropMessageWhenFull)
{
    // Use a tiny capacity to fill quickly
    size_t tinyCapacity = 256;
    size_t totalSize = emu::SPSCRingBuffer::totalMemoryRequired(tinyCapacity);
    std::vector<char> mem(totalSize, 0);

    auto* rb = emu::SPSCRingBuffer::create(mem.data(), tinyCapacity);

    // Each message: 24B header + 8B payload (aligned) = 32B
    // 256 / 32 = 8 messages max
    int written = 0;
    for (int i = 0; i < 20; i++)
    {
        if (rb->try_write(static_cast<uint32_t>(i), 1, "12345678", 8))
            written++;
    }

    EXPECT_GT(written, 0);
    EXPECT_LT(written, 20);  // Some must have been dropped
    EXPECT_GT(rb->droppedCount(), 0u);
}

// =============================================================================
// Wrap-around correctness
// =============================================================================

TEST_F(SPSCRingBuffer_Test, WrapAroundPreservesData)
{
    // 256 byte data area — will wrap quickly
    size_t smallCapacity = 256;
    size_t totalSize = emu::SPSCRingBuffer::totalMemoryRequired(smallCapacity);
    std::vector<char> mem(totalSize, 0);

    auto* rb = emu::SPSCRingBuffer::create(mem.data(), smallCapacity);

    // Write and read in rounds to force wrap-around
    for (int round = 0; round < 10; round++)
    {
        const char* payload = "wrap_test";
        uint32_t len = static_cast<uint32_t>(std::strlen(payload));

        bool ok = rb->try_write(static_cast<uint32_t>(round), 1, payload, len);
        EXPECT_TRUE(ok) << "Round " << round;

        emu::SPSCRingBuffer::MessageHeader hdr{};
        char buf[64] = {};
        ok = rb->try_read(hdr, buf, sizeof(buf));
        EXPECT_TRUE(ok) << "Round " << round;
        EXPECT_EQ(hdr.category_id, static_cast<uint32_t>(round));
        EXPECT_STREQ(buf, payload);
    }
}

// =============================================================================
// Drain batch read
// =============================================================================

TEST_F(SPSCRingBuffer_Test, DrainReadsMultipleMessages)
{
    auto* rb = emu::SPSCRingBuffer::create(_memory, TEST_CAPACITY);

    // Write 5 messages
    for (uint32_t i = 0; i < 5; i++)
    {
        rb->try_write(i, 1, "data", 4);
    }

    // Drain all
    emu::SPSCRingBuffer::MessageHeader hdrs[8] = {};
    char payloadBufs[8][64] = {};
    char* payloads[8];
    for (int i = 0; i < 8; i++)
        payloads[i] = payloadBufs[i];

    uint32_t drained = rb->drain(hdrs, payloads, 64, 8);
    EXPECT_EQ(drained, 5u);
    EXPECT_EQ(rb->available(), 0u);
}

// =============================================================================
// Zero-length payload
// =============================================================================

TEST_F(SPSCRingBuffer_Test, ZeroLengthPayload)
{
    auto* rb = emu::SPSCRingBuffer::create(_memory, TEST_CAPACITY);

    bool ok = rb->try_write(42, 3, nullptr, 0);
    EXPECT_TRUE(ok);

    emu::SPSCRingBuffer::MessageHeader hdr{};
    char buf[64] = {};
    ok = rb->try_read(hdr, buf, sizeof(buf));
    EXPECT_TRUE(ok);
    EXPECT_EQ(hdr.category_id, 42u);
    EXPECT_EQ(hdr.length, 0u);
}
