/// @file calltrace_test.cpp
/// @brief Unit tests for the CallTraceBuffer hot/cold pipeline (M8 / M9).
///
/// Drives the buffer directly with synthetic control-flow events and tiny
/// thresholds (hot promotion once loop_count exceeds 5, hot timeout of 2
/// frames) to verify: cold loop compression, promotion (which removes the
/// event from cold), in-place hot compression, FlushHotBuffer timeout-based
/// eviction, FlushAllHotToCold session finalization (the calltrace stop
/// endpoint path) with its was_hot re-promotion guard, GetLatestCold tail
/// ordering, distinct-event handling, and Reset clearing compression state.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "emulator/memory/calltrace.h"

namespace
{

constexpr size_t kColdCapacity = 1024;
constexpr uint32_t kHotThreshold = 5;
constexpr uint32_t kHotTimeoutFrames = 2;

Z80ControlFlowEvent MakeEvent(uint16_t pc, uint16_t target, Z80CFType type = Z80CFType::JR)
{
    Z80ControlFlowEvent ev{};
    ev.m1_pc = pc;
    ev.target_addr = target;
    ev.type = type;
    ev.opcode_len = 2;
    ev.opcode_bytes = {0x18, 0x02, 0x00, 0x00};
    return ev;
}

} // namespace

class CallTraceBuffer_Test : public ::testing::Test
{
protected:
    /// Promote (pc → target) into the hot buffer: 6 logs push loop_count past
    /// the threshold of 5. Returns the total loop count accumulated.
    uint32_t Promote(uint16_t pc, uint16_t target, uint64_t frame, int extra = 0)
    {
        Z80ControlFlowEvent ev = MakeEvent(pc, target);
        int logs = kHotThreshold + 1 + extra;
        for (int i = 0; i < logs; i++)
        {
            _buffer.LogEvent(ev, frame);
        }
        return static_cast<uint32_t>(logs);
    }

    CallTraceBuffer _buffer{kColdCapacity, 16, kHotThreshold, kHotTimeoutFrames};
};

TEST_F(CallTraceBuffer_Test, ColdBufferCompressesRepeats)
{
    Z80ControlFlowEvent ev = MakeEvent(0x8000, 0x8005);
    for (int i = 0; i < 3; i++)
    {
        _buffer.LogEvent(ev, 1);
    }

    EXPECT_EQ(_buffer.ColdSize(), 1u);
    EXPECT_EQ(_buffer.HotSize(), 0u);

    std::vector<Z80ControlFlowEvent> all = _buffer.GetAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].loop_count, 3u);
    EXPECT_FALSE(all[0].was_hot);
}

TEST_F(CallTraceBuffer_Test, DistinctEventsAreNotCompressed)
{
    // Same PC but different targets → two separate events
    _buffer.LogEvent(MakeEvent(0x8000, 0x8005), 1);
    _buffer.LogEvent(MakeEvent(0x8000, 0x8007), 1);

    EXPECT_EQ(_buffer.ColdSize(), 2u);
}

TEST_F(CallTraceBuffer_Test, RepeatedLoopPromotesToHotAndLeavesCold)
{
    uint32_t loops = Promote(0x8000, 0x8005, 1);
    ASSERT_GT(loops, kHotThreshold);

    EXPECT_EQ(_buffer.HotSize(), 1u);
    EXPECT_EQ(_buffer.ColdSize(), 0u); // promotion removes the cold entry

    std::vector<HotEvent> hot = _buffer.GetLatestHot(4);
    ASSERT_EQ(hot.size(), 1u);
    EXPECT_EQ(hot[0].loop_count, loops);
    EXPECT_TRUE(hot[0].event.was_hot);

    // The unified profiler API reads cold only: pinned loops are invisible
    EXPECT_TRUE(_buffer.GetRecentEntries(10).empty());
}

TEST_F(CallTraceBuffer_Test, HotEventCompressesInPlace)
{
    Promote(0x8000, 0x8005, 1);
    for (int i = 0; i < 4; i++)
    {
        _buffer.LogEvent(MakeEvent(0x8000, 0x8005), 2);
    }

    EXPECT_EQ(_buffer.HotSize(), 1u);
    EXPECT_EQ(_buffer.ColdSize(), 0u);
    EXPECT_EQ(_buffer.GetLatestHot(1)[0].loop_count, kHotThreshold + 5u);
}

TEST_F(CallTraceBuffer_Test, FlushHotBuffer_EvictsOnlyExpired)
{
    Promote(0x8000, 0x8005, 10); // hot, last seen at frame 10

    _buffer.FlushHotBuffer(12); // 12 - 10 = 2 → not greater than the timeout
    EXPECT_EQ(_buffer.HotSize(), 1u);

    _buffer.FlushHotBuffer(13); // 13 - 10 = 3 > 2 → evicted into cold
    EXPECT_EQ(_buffer.HotSize(), 0u);
    EXPECT_EQ(_buffer.ColdSize(), 1u);

    std::vector<Z80ControlFlowEvent> recent = _buffer.GetRecentEntries(10);
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0].loop_count, kHotThreshold + 1u);
    EXPECT_TRUE(recent[0].was_hot);
}

TEST_F(CallTraceBuffer_Test, FlushAllHotToCold_TransfersEverything)
{
    // Two pinned loops at different addresses
    Promote(0x8000, 0x8005, 1);
    Promote(0x9000, 0x9005, 1);
    ASSERT_EQ(_buffer.HotSize(), 2u);

    _buffer.FlushAllHotToCold();

    EXPECT_EQ(_buffer.HotSize(), 0u);
    EXPECT_EQ(_buffer.ColdSize(), 2u);

    std::vector<Z80ControlFlowEvent> recent = _buffer.GetRecentEntries(10);
    ASSERT_EQ(recent.size(), 2u);
    for (const auto& ev : recent)
    {
        EXPECT_EQ(ev.loop_count, kHotThreshold + 1u); // loop count preserved
        EXPECT_TRUE(ev.was_hot);
    }
}

TEST_F(CallTraceBuffer_Test, FlushAllHotToCold_PreventsRepromotion)
{
    Promote(0x8000, 0x8005, 1);
    _buffer.FlushAllHotToCold();
    ASSERT_EQ(_buffer.ColdSize(), 1u);

    // Loop far past the threshold again: was_hot must keep the event cold
    for (int i = 0; i < 10; i++)
    {
        _buffer.LogEvent(MakeEvent(0x8000, 0x8005), 2);
    }

    EXPECT_EQ(_buffer.HotSize(), 0u);
    EXPECT_EQ(_buffer.ColdSize(), 1u);
    EXPECT_EQ(_buffer.GetRecentEntries(1)[0].loop_count, kHotThreshold + 11u);
}

TEST_F(CallTraceBuffer_Test, GetLatestCold_ReturnsChronologicalTail)
{
    uint16_t targets[] = {0x8100, 0x8200, 0x8300, 0x8400};
    for (int i = 0; i < 4; i++)
    {
        _buffer.LogEvent(MakeEvent(0x8000, targets[i]), 1);
    }

    std::vector<Z80ControlFlowEvent> tail = _buffer.GetLatestCold(2);
    ASSERT_EQ(tail.size(), 2u);
    EXPECT_EQ(tail[0].target_addr, 0x8300u); // older of the two first
    EXPECT_EQ(tail[1].target_addr, 0x8400u);

    EXPECT_EQ(_buffer.GetAll().size(), 4u);
}

TEST_F(CallTraceBuffer_Test, ResetClearsBuffersAndCompressionState)
{
    Z80ControlFlowEvent ev = MakeEvent(0x8000, 0x8005);
    for (int i = 0; i < 3; i++)
    {
        _buffer.LogEvent(ev, 1);
    }
    ASSERT_EQ(_buffer.ColdSize(), 1u);

    _buffer.Reset();
    EXPECT_EQ(_buffer.ColdSize(), 0u);
    EXPECT_EQ(_buffer.HotSize(), 0u);

    // Re-logging the same event must be treated as fresh: loop_count restarts
    // (a stale compression map would silently drop the event instead)
    _buffer.LogEvent(ev, 100);
    ASSERT_EQ(_buffer.ColdSize(), 1u);
    EXPECT_EQ(_buffer.GetRecentEntries(1)[0].loop_count, 1u);
}
