#pragma once

/// @file ttdsyntheticsession.h
/// @brief Test helper: a small synthetic engine session (four pieces, one
/// changing a few bytes per frame, a bus read every frame, a marker with a
/// payload every 7th frame) under a history policy, and a comparison of two
/// checkpoints' memory

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "debugger/ttd/timetravelengine.h"

namespace ttdtest
{
using namespace ttd;
/// Four pieces, one changing a few bytes per frame (differences, not whole
/// pieces), a bus read every frame, a marker with a payload every 7th frame
struct Session
{
    TimeTravelEngine engine;
    std::vector<uint8_t> memory = std::vector<uint8_t>(4 * kTTDPieceSize);
    uint64_t frame = 0;

    explicit Session(const TTDHistoryPolicy& policy)
    {
        engine.SetHistoryPolicy(policy);
        TTDRegionDesc ram;
        ram.name = "ram";
        ram.pieces = 4;
        ram.bytes = 4 * kTTDPieceSize;
        std::string error;
        EXPECT_TRUE(engine.BeginSession({ram}, {}, error)) << error;
        for (size_t i = 0; i < memory.size(); ++i)
            memory[i] = static_cast<uint8_t>(i * 13 + i / 512);
    }

    void Frame()
    {
        const uint32_t piece = static_cast<uint32_t>(frame % 4);
        uint8_t* p = memory.data() + size_t(piece) * kTTDPieceSize;
        for (size_t i = 0; i < kTTDPieceSize; i += 401)
            p[i] = static_cast<uint8_t>(p[i] + frame + 1);
        TTDFrameInput input;
        input.position.frame = frame;
        input.start = frame * 69888;
        if (frame == 0)
            for (uint32_t k = 0; k < 4; ++k)
                input.changed.push_back({0, k, memory.data() + size_t(k) * kTTDPieceSize});
        else
            input.changed.push_back({0, piece, p});
        std::string error;
        ASSERT_TRUE(engine.CaptureFrame(input, error)) << error;
        engine.AppendBusRead({frame, 1000, 0xFE, 0x8000, static_cast<uint8_t>(frame)});
        if (frame % 7 == 3)
        {
            TTDEvent ev;
            ev.kind = TTDEventKind::OtherMarker;
            const uint8_t reason[] = {'x', static_cast<uint8_t>(frame)};
            ev.payload = engine.Payloads().Store(reason, sizeof(reason));
            engine.AppendEvent(frame, 2000, ev);
        }
        ++frame;
    }
};

inline TTDHistoryPolicy Ring(uint32_t window, uint32_t segment)
{
    return {TTDHistoryMode::Ring, window, segment};
}

inline TTDHistoryPolicy Growable(uint32_t segment)
{
    return {TTDHistoryMode::Growable, 0, segment};
}

/// Checkpoint @p i of @p a restores as checkpoint @p j of @p b
inline void ExpectSameCheckpoint(const TimeTravelEngine& a, size_t i, const TimeTravelEngine& b, size_t j)
{
    ASSERT_NE(a.Checkpoint(i), nullptr);
    ASSERT_NE(b.Checkpoint(j), nullptr);
    ASSERT_EQ(a.Checkpoint(i)->position, b.Checkpoint(j)->position);
    std::vector<uint8_t> x(4 * kTTDPieceSize), y(4 * kTTDPieceSize);
    ASSERT_TRUE(a.RestoreRegion(i, 0, x.data()).Ok());
    ASSERT_TRUE(b.RestoreRegion(j, 0, y.data()).Ok());
    ASSERT_TRUE(x == y) << "checkpoint " << i;
}

}  // namespace ttdtest
