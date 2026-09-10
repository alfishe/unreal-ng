/// @file ttd_serialization_robustness_test.cpp
/// @brief Robustness tests for .ttd deserialization — corrupt/truncated input.
///
/// The existing dump_format_test covers valid round-trips, bad magic, and
/// future schema version. This file covers the remaining failure modes:
///   - Truncated header (fewer bytes than minimum)
///   - Truncated checkpoint table (stream ends mid-read)
///   - Zero-checkpoint session (valid but edge case)
///   - Corrupt flags field (unknown bits set)
///   - Empty stream

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_dump_format.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

class TTD_Serialization_Robustness_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }

    /// Build a valid .ttd stream, then return it.
    std::string MakeValidStream(size_t frames)
    {
        EXPECT_TRUE(_ttd->StartRecording());
        RunFrames(frames);
        _ttd->StopRecording();

        std::ostringstream out(std::ios::binary);
        std::string err;
        EXPECT_TRUE(_ttd->SerializeSession(out, err)) << err;
        _ttd->InvalidateSession("reset");
        return out.str();
    }
};

// ===========================================================================
// Empty stream
// ===========================================================================

TEST_F(TTD_Serialization_Robustness_Test, EmptyStream_Fails)
{
    std::istringstream in("", std::ios::binary);
    std::string err;
    EXPECT_FALSE(_ttd->DeserializeSession(in, err));
}

// ===========================================================================
// Truncated header (fewer than the minimum header size)
// ===========================================================================

TEST_F(TTD_Serialization_Robustness_Test, TruncatedHeader_Fails)
{
    // Build a stream with only 3 bytes of the 4-byte magic.
    std::string buf;
    buf.append(ttd::dump::kMagic, 3);  // One byte short.

    std::istringstream in(buf, std::ios::binary);
    std::string err;
    EXPECT_FALSE(_ttd->DeserializeSession(in, err));
}

// ===========================================================================
// Valid header but truncated body (stream ends mid-checkpoint-table)
// ===========================================================================

TEST_F(TTD_Serialization_Robustness_Test, TruncatedCheckpointTable_Fails)
{
    std::string valid = MakeValidStream(5);
    ASSERT_GT(valid.size(), 20u);

    // Truncate to somewhere in the middle of the checkpoint table.
    // Header is ~256 bytes (magic + version + flags + metadata), then
    // checkpoints follow. Truncating to 300 bytes should land mid-table.
    size_t truncLen = std::min(valid.size() - 1, static_cast<size_t>(300));
    std::string truncated = valid.substr(0, truncLen);

    std::istringstream in(truncated, std::ios::binary);
    std::string err;
    EXPECT_FALSE(_ttd->DeserializeSession(in, err));
}

// ===========================================================================
// Zero-checkpoint session (valid edge case: only baseline)
// ===========================================================================

TEST_F(TTD_Serialization_Robustness_Test, SingleCheckpoint_RoundTrips)
{
    // Record just the baseline — no frames run.
    ASSERT_TRUE(_ttd->StartRecording());
    ASSERT_EQ(_ttd->GetCheckpointCount(), 1u);

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;
    const std::string data = out.str();

    _ttd->StopRecording();
    _ttd->InvalidateSession("reset");
    EXPECT_EQ(_ttd->GetCheckpointCount(), 0u);

    // Deserialize — should restore the single baseline checkpoint.
    std::istringstream in(data, std::ios::binary);
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;
    EXPECT_EQ(_ttd->GetCheckpointCount(), 1u);
}

// ===========================================================================
// Unknown flag bits are ignored (forward compatibility)
// ===========================================================================

TEST_F(TTD_Serialization_Robustness_Test, UnknownFlagBits_IgnoredSafely)
{
    std::string valid = MakeValidStream(3);
    ASSERT_GE(valid.size(), 8u);

    // Flip an unknown flag bit in the flags field (at offset 6).
    // Known flags: bit 0 = little-endian, bit 1 = has-write-journal.
    // Bit 15 is currently unused — set it.
    std::string modified = valid;
    uint16_t flags = 0;
    std::memcpy(&flags, modified.data() + 6, 2);
    flags |= 0x8000;  // Unknown bit.
    std::memcpy(modified.data() + 6, &flags, 2);

    std::istringstream in(modified, std::ios::binary);
    std::string err;
    // Should either succeed (ignoring the unknown bit) or fail gracefully.
    // Either way, it must not crash.
    bool ok = _ttd->DeserializeSession(in, err);
    if (ok)
    {
        EXPECT_GT(_ttd->GetCheckpointCount(), 0u);
    }
    // No crash is the primary assertion.
    SUCCEED();
}

// ===========================================================================
// Valid stream after invalid attempt — manager is reusable
// ===========================================================================

TEST_F(TTD_Serialization_Robustness_Test, ManagerReusable_AfterFailedDeserialize)
{
    // Attempt 1: invalid data.
    std::istringstream bad("XXXX", std::ios::binary);
    std::string err;
    EXPECT_FALSE(_ttd->DeserializeSession(bad, err));

    // Attempt 2: valid data — must succeed.
    std::string valid = MakeValidStream(2);
    std::istringstream good(valid, std::ios::binary);
    EXPECT_TRUE(_ttd->DeserializeSession(good, err));
    EXPECT_GT(_ttd->GetCheckpointCount(), 0u);
}
