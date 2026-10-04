/// @file timetravelengine_heap_test.cpp
/// @brief FR-16: the engine's reported memory (HeapBreakdown().Total()) is
/// the real total within 5%: the heap the engine's allocations left behind
/// (counted by HeapCounter: every block at its real size, rounding included)
/// against what the engine reports. Platforms without a block size query skip.
///
/// Found on the way (2026-10-04): the report left out 22% on a small session -
/// the session's own tables (regions, what memory holds, per-region counters
/// and scratch, configuration entries, media slots, segments) and the payload
/// store's entry table. They are counted now (bookkeeping, eventLog).

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/heapcounter.h"
#include "debugger/ttd/timetravelengine.h"

/// A session of a realistic size: 64 pieces (256 KB), 16 of them changing a
/// few bytes each frame, 2,000 frames, bus reads and markers. The engine's
/// steady state matches the allocator frame for frame; a one-off amount at
/// the first capture stays well inside the tolerance. The engine object itself
/// is added to the report before comparing. About 0.1 s at -O2: a check of the
/// whole engine
TEST(TimeTravelEngineHeap_Test, ReportedMemoryIsTheRealTotal)
{
    if (!HeapCounter::Available())
        GTEST_SKIP() << "no allocator block sizes on this platform";
    constexpr uint32_t kPieces = 64;
    for (int pass = 0; pass < 2; ++pass)   // the first pass warms the allocator's own tables
    {
        std::vector<uint8_t> mem(size_t(kPieces) * ttd::kTTDPieceSize);
        for (size_t i = 0; i < mem.size(); ++i)
            mem[i] = static_cast<uint8_t>(i * 7 + i / 977);
        std::vector<ttd::TTDChangedPiece> changed;
        changed.reserve(kPieces);
        struct Counting   // stops counting however the pass ends
        {
            Counting() { HeapCounter::Start(); }
            ~Counting() { HeapCounter::Stop(); }
        } counting;
        auto engine = std::make_unique<ttd::TimeTravelEngine>();
        engine->SetHistoryPolicy({ttd::TTDHistoryMode::Growable, 0, 0});
        ttd::TTDRegionDesc ram;
        ram.name = "ram";
        ram.pieces = kPieces;
        ram.bytes = kPieces * ttd::kTTDPieceSize;
        std::string error;
        ASSERT_TRUE(engine->BeginSession({ram}, {}, error)) << error;
        for (uint64_t f = 0; f < 2000; ++f)
        {
            changed.clear();
            for (uint32_t k = 0; k < (f == 0 ? kPieces : 16); ++k)
            {
                const uint32_t piece = f == 0 ? k : static_cast<uint32_t>((f * 5 + k * 3) % kPieces);
                uint8_t* p = mem.data() + size_t(piece) * ttd::kTTDPieceSize;
                p[(f * 131 + k) % ttd::kTTDPieceSize] ^= static_cast<uint8_t>(f + 1);
                changed.push_back({0, piece, p});
            }
            ttd::TTDFrameInput in;
            in.position.frame = f;
            in.start = f * 69888;
            in.changed = changed;
            ASSERT_TRUE(engine->CaptureFrame(in, error)) << error;
            engine->AppendBusRead({f, 1000, 0xFE, 0x8000, static_cast<uint8_t>(f)});
            if (f % 7 == 3)
            {
                ttd::TTDEvent ev;
                ev.kind = ttd::TTDEventKind::OtherMarker;
                const uint8_t reason[] = {'m', static_cast<uint8_t>(f)};
                ev.payload = engine->Payloads().Store(reason, sizeof(reason));
                engine->AppendEvent(f, 2000, ev);
            }
        }
        HeapCounter::Stop();
        const size_t measured = static_cast<size_t>(HeapCounter::Net());
        const size_t reported = engine->HeapBreakdown().Total() + sizeof(ttd::TimeTravelEngine);
        if (pass == 0)
            continue;
        std::printf("[ heap     ] measured %zu bytes, reported %zu (%+.1f%%)\n", measured, reported,
                    100.0 * (static_cast<double>(reported) - static_cast<double>(measured)) / static_cast<double>(measured));
        EXPECT_NEAR(static_cast<double>(reported), static_cast<double>(measured), measured * 0.05)
            << "the engine's report must be the allocator's count within 5% (FR-16)";
    }
}

/// A small session, where the session's own tables weigh most (they were left
/// out of the report before): four pieces, 400 frames
TEST(TimeTravelEngineHeap_Test, SmallSessionsToo)
{
    if (!HeapCounter::Available())
        GTEST_SKIP() << "no allocator block sizes on this platform";
    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<uint8_t> mem(4 * ttd::kTTDPieceSize, 1);
        struct Counting
        {
            Counting() { HeapCounter::Start(); }
            ~Counting() { HeapCounter::Stop(); }
        } counting;
        auto engine = std::make_unique<ttd::TimeTravelEngine>();
        ttd::TTDRegionDesc ram;
        ram.name = "ram";
        ram.pieces = 4;
        ram.bytes = 4 * ttd::kTTDPieceSize;
        std::string error;
        ASSERT_TRUE(engine->BeginSession({ram}, {}, error)) << error;
        for (uint64_t f = 0; f < 400; ++f)
        {
            mem[(f * 401) % mem.size()]++;
            ttd::TTDFrameInput in;
            in.position.frame = f;
            in.start = f * 69888;
            const uint32_t piece = static_cast<uint32_t>(((f * 401) % mem.size()) / ttd::kTTDPieceSize);
            in.changed.push_back({0, piece, mem.data() + size_t(piece) * ttd::kTTDPieceSize});
            if (f == 0)
                for (uint32_t k = 0; k < 4; ++k)
                    in.changed.push_back({0, k, mem.data() + size_t(k) * ttd::kTTDPieceSize});
            ASSERT_TRUE(engine->CaptureFrame(in, error)) << error;
        }
        HeapCounter::Stop();
        const double measured = static_cast<double>(HeapCounter::Net());
        const double reported = static_cast<double>(engine->HeapBreakdown().Total() + sizeof(ttd::TimeTravelEngine));
        if (pass == 0)
            continue;
        std::printf("[ heap     ] small: measured %.0f, reported %.0f (%+.1f%%)\n", measured, reported,
                    100.0 * (reported - measured) / measured);
        EXPECT_NEAR(reported, measured, measured * 0.05);
    }
}
