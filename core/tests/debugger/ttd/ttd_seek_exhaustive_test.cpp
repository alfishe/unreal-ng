/// @file ttd_seek_exhaustive_test.cpp
/// @brief Exhaustive seek-matrix coverage for the TTD seek engine.
///
/// Per user request 2026-07-19: "save current session recording to .ttd file.
/// then run extensive tests ensuring that positioning to ANY position
/// (I-frame, P-frame, t-state between frames work correctly and doesn't
/// lead to screen or memory corruption)! ... I saw rare screen corruption
/// on some backward seek operations with recover on next reposition".
///
/// This test exercises:
///
///   1. Recording a session long enough to span multiple I-frames
///      (kKeyFrameInterval = 50 frames).
///   2. Serializing it to a .ttd stream and deserializing back.
///   3. For every checkpoint i:
///        a. Compute reference hashes (full machine state + VRAM-only).
///        b. SeekTo({i, 0}) from multiple starting points (later, earlier).
///        c. Verify the post-seek hashes match the reference exactly.
///   4. Backward sweep pattern (the corruption scenario the user reported):
///        SeekTo(N-1) -> (0) -> (N-2) -> (1) -> ... -> verify every landing.
///   5. Intra-frame t-state matrix: for several frame indices, SeekTo({F,T})
///      at T = 0, 1000, 10000, 35000, frame_end-1.
///   6. Round-trip integrity: serialize -> deserialize -> re-run the sweep.
///
/// The point of this test is to fail loudly on ANY screen-or-memory
/// divergence, providing the diagnostic hashes needed to triage.
///
/// Reference hashing strategy
/// ---------------------------
/// SeekTo({F, 0}) lands at the OnFrameBoundary capture point, which differs
/// from the post-OnFrameStart live state (see ttd_seek_test.cpp note on
/// SeekTo_FrameAligned_RoundTripDeterminism). We therefore use
/// RestoreCheckpointForTesting(i) as the reference - it walks the exact same
/// RestoreCheckpoint code path that SeekTo uses internally.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/machine_state_hash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/ttd_codec_page_store.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{

/// Standard ZX Spectrum VRAM size: 256*192 pixels / 8 bits + 32*24 attrs.
/// Located at offset 0 of RAM page 0 (CPU address 0x4000).
/// Used by HashScreen() to bound the framebuffer digest.
constexpr uint32_t kVramSize = 6912;

} // namespace

// ===========================================================================
// Fixture
// ===========================================================================

class TTD_Seek_Exhaustive_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
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

    /// Run exactly N frames, populating the timeline with N+1 checkpoints
    /// (baseline at StartRecording + N more from OnFrameBoundary).
    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }

    /// @brief Full machine-state hash: CPU + chipset + every model-RAM byte.
    ///
    /// Two SeekTo calls to the same target must produce identical hashes.
    /// Any divergence here means the seek engine restored different machine
    /// state, which is the bug we're hunting.
    uint64_t HashNow()
    {
        Z80* z80 = _context->pCore->GetZ80();
        if (!z80 || !_memory)
            return 0;

        const uint32_t ramBytes =
            static_cast<uint32_t>(_context->config.ramsize) * 1024u;
        const uint64_t ramDigest = ttd::HashBytes(_memory->RAMBase(), ramBytes);

        const auto snap = ttd::CaptureSnapshot(*static_cast<Z80State*>(z80),
                                                _context->emulatorState,
                                                ramDigest);
        return ttd::HashSnapshot(snap);
    }

    /// @brief VRAM-only hash: first 6912 bytes of RAM page 0.
    ///
    /// RAMPageAddress(0) returns the host pointer for the emu-RAM page
    /// currently mapped at CPU address 0x4000 (default screen bank on
    /// 48K/128K/Pentagon). The first 6912 bytes are the standard Spectrum
    /// framebuffer (pixels + attributes).
    ///
    /// A divergence here with HashNow() stable means the seek engine
    /// corrupted the framebuffer specifically - exactly the "rare screen
    /// corruption" the user reported.
    uint64_t HashScreen()
    {
        uint8_t* page0 = _memory->RAMPageAddress(0);
        if (!page0)
            return 0;
        return ttd::HashBytes(page0, kVramSize);
    }

    /// @brief Per-checkpoint reference state, captured via the testing API.
    struct CheckpointRef
    {
        uint64_t fullHash = 0;   ///< HashNow() after RestoreCheckpointForTesting.
        uint64_t vramHash = 0;   ///< HashScreen() after RestoreCheckpointForTesting.
        uint64_t frame     = 0;  ///< cp.time.frame (sanity check on seek).
    };

    /// Capture references for every checkpoint in the current timeline.
    /// Must be called when state is consistent (e.g. just after recording
    /// or just after deserialize). Side effect: leaves emulator at last-cp.
    std::vector<CheckpointRef> CaptureReferences()
    {
        const size_t n = _ttd->GetCheckpointCount();
        std::vector<CheckpointRef> refs;
        refs.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            EXPECT_TRUE(_ttd->RestoreCheckpointForTesting(i))
                << "RestoreCheckpointForTesting(" << i << ") failed";
            CheckpointRef r;
            r.fullHash = HashNow();
            r.vramHash = HashScreen();
            r.frame    = _ttd->GetCheckpoint(i)->time.frame;
            refs.push_back(r);
        }
        return refs;
    }

    /// Verify that SeekTo({refs[i].frame, 0}) reproduces refs[i] exactly.
    /// Pre-seeks to a "decoy" position first to ensure the seek engine does
    /// real work (not a no-op when already at the target).
    testing::AssertionResult VerifySeek(size_t i,
                                         const std::vector<CheckpointRef>& refs,
                                         size_t decoyIdx)
    {
        if (i >= refs.size())
            return testing::AssertionFailure() << "i=" << i << " out of range";
        if (decoyIdx >= refs.size())
            return testing::AssertionFailure() << "decoyIdx=" << decoyIdx << " out of range";
        if (decoyIdx == i)
            return testing::AssertionFailure() << "decoyIdx must differ from i";

        // Decoy: seek to a different frame first so the next seek is real.
        const uint64_t decoyFrame = refs[decoyIdx].frame;
        if (!_ttd->SeekTo({decoyFrame, 0}))
            return testing::AssertionFailure()
                << "decoy SeekTo({" << decoyFrame << ", 0}) failed";

        // Real seek under test.
        const uint64_t targetFrame = refs[i].frame;
        if (!_ttd->SeekTo({targetFrame, 0}))
            return testing::AssertionFailure()
                << "SeekTo({" << targetFrame << ", 0}) failed";

        if (_ttd->CurrentPosition().frame != targetFrame)
            return testing::AssertionFailure()
                << "current frame " << _ttd->CurrentPosition().frame
                << " != target " << targetFrame;

        const uint64_t fullHash = HashNow();
        const uint64_t vramHash = HashScreen();

        if (fullHash != refs[i].fullHash)
            return testing::AssertionFailure()
                << "checkpoint " << i << " (frame " << targetFrame
                << "): fullHash mismatch - seek=" << std::hex << fullHash
                << " ref=" << refs[i].fullHash << std::dec;
        if (vramHash != refs[i].vramHash)
            return testing::AssertionFailure()
                << "checkpoint " << i << " (frame " << targetFrame
                << "): vramHash mismatch - seek=" << std::hex << vramHash
                << " ref=" << refs[i].vramHash << std::dec
                << " (fullHash was OK)";

        return testing::AssertionSuccess();
    }
};

// ===========================================================================
// Fixture-level helpers - record a session that spans multiple I-frames
// ===========================================================================

/// Record 60 frames (so the timeline has 61 checkpoints: baseline + 60
/// OnFrameBoundary). 60 = 1 I-frame at frame 0 + 1 I-frame at frame 50 +
/// 10 P-frames after the second I-frame. Plenty of P-frame delta chains
/// (length up to 50) to exercise the codec.
static constexpr size_t kRecordedFrames = 60;

namespace {
/// Drive recording + StopRecording. ASSERTS inside a helper are tricky with
/// return values; we use EXPECT + early-return and the caller checks _ttd state.
bool RecordSessionForExhaustive(ttd::TimeTravelManager* ttd,
                                 Emulator* emu,
                                 size_t numFrames)
{
    if (!ttd->StartRecording()) return false;
    emu->RunNFrames(static_cast<unsigned>(numFrames), /*skipBreakpoints=*/true);
    ttd->StopRecording();
    return ttd->GetState() == ttd::TTDSessionState::Idle
        && ttd->GetCheckpointCount() == numFrames + 1;
}
} // namespace

// ===========================================================================
// Sanity: the fixture can record a session that spans I-frames
// ===========================================================================

TEST_F(TTD_Seek_Exhaustive_Test, Setup_RecordsSessionSpanningTwoKeyFrames)
{
    ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));
    ASSERT_EQ(_ttd->GetCheckpointCount(), kRecordedFrames + 1);

    // Inspect frame kinds. Baseline (cp 0) is always a KeyFrame.
    // With kKeyFrameInterval = 50 and 60 recorded frames, we expect
    // KeyFrames at indices {0, 50}.
    std::vector<size_t> keyFrameIndices;
    for (size_t i = 0; i < _ttd->GetCheckpointCount(); ++i)
    {
        const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(i);
        ASSERT_NE(cp, nullptr);
        if (cp->frameKind == ttd::TTDFrameKind::KeyFrame)
            keyFrameIndices.push_back(i);
    }
    ASSERT_GE(keyFrameIndices.size(), 2u)
        << "Test requires at least 2 keyframes - increase kRecordedFrames";
    EXPECT_EQ(keyFrameIndices[0], 0u) << "Baseline must be a keyframe";
    // The second keyframe should be at index kKeyFrameInterval (50).
    EXPECT_EQ(keyFrameIndices[1], ttd::TimeTravelManager::kKeyFrameInterval);

    // Confirm presence of P-frames (so the test actually exercises them).
    bool anyPFrame = false;
    for (size_t i = 0; i < _ttd->GetCheckpointCount(); ++i)
    {
        if (_ttd->GetCheckpoint(i)->frameKind == ttd::TTDFrameKind::DeltaFrame)
        {
            anyPFrame = true;
            break;
        }
    }
    EXPECT_TRUE(anyPFrame) << "Test requires at least one P-frame in the timeline";
}

// ===========================================================================
// Exhaustive frame-aligned seek matrix
// ===========================================================================

TEST_F(TTD_Seek_Exhaustive_Test, FrameAligned_SeekEveryCheckpoint_FromMidpoint)
{
    ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));
    const auto refs = CaptureReferences();
    ASSERT_EQ(refs.size(), kRecordedFrames + 1);

    // For every checkpoint i: decoy = midpoint, then seek to i.
    const size_t decoy = refs.size() / 2;
    for (size_t i = 0; i < refs.size(); ++i)
    {
        if (i == decoy) continue;  // would be a no-op decoy
        EXPECT_TRUE(VerifySeek(i, refs, decoy))
            << "FrameAligned_FromMidpoint cp " << i;
    }
}

TEST_F(TTD_Seek_Exhaustive_Test, FrameAligned_SeekEveryCheckpoint_FromEnd)
{
    ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));
    const auto refs = CaptureReferences();
    ASSERT_EQ(refs.size(), kRecordedFrames + 1);

    // For every checkpoint i: decoy = last checkpoint, then seek to i.
    // This stresses backward seeks heavily (decoy > i for all i < N-1),
    // which is the exact scenario the user's corruption report describes.
    const size_t decoy = refs.size() - 1;
    for (size_t i = 0; i < refs.size(); ++i)
    {
        if (i == decoy) continue;
        EXPECT_TRUE(VerifySeek(i, refs, decoy))
            << "FrameAligned_FromEnd cp " << i;
    }
}

TEST_F(TTD_Seek_Exhaustive_Test, FrameAligned_SeekEveryCheckpoint_FromStart)
{
    ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));
    const auto refs = CaptureReferences();
    ASSERT_EQ(refs.size(), kRecordedFrames + 1);

    // Forward seeks only: decoy = baseline (cp 0).
    const size_t decoy = 0;
    for (size_t i = 1; i < refs.size(); ++i)
    {
        EXPECT_TRUE(VerifySeek(i, refs, decoy))
            << "FrameAligned_FromStart cp " << i;
    }
}

// ===========================================================================
// Backward sweep - the user's exact "rare corruption on backward seek"
// scenario. We sweep N-1, 0, N-2, 1, N-3, 2, ...
// ===========================================================================

TEST_F(TTD_Seek_Exhaustive_Test, BackwardSweep_InterleavedMatchesEveryCheckpoint)
{
    ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));
    const auto refs = CaptureReferences();
    ASSERT_EQ(refs.size(), kRecordedFrames + 1);

    const size_t N = refs.size();
    size_t lo = 0, hi = N - 1;
    while (lo < hi)
    {
        // Seek to hi (latest remaining), then back to lo (earliest remaining).
        ASSERT_TRUE(_ttd->SeekTo({refs[hi].frame, 0}));
        {
            const uint64_t h1 = HashNow();
            const uint64_t h2 = HashScreen();
            EXPECT_EQ(h1, refs[hi].fullHash)
                << "BackwardSweep hi=" << hi << " fullHash mismatch";
            EXPECT_EQ(h2, refs[hi].vramHash)
                << "BackwardSweep hi=" << hi << " vramHash mismatch";
        }
        ASSERT_TRUE(_ttd->SeekTo({refs[lo].frame, 0}));
        {
            const uint64_t h1 = HashNow();
            const uint64_t h2 = HashScreen();
            EXPECT_EQ(h1, refs[lo].fullHash)
                << "BackwardSweep lo=" << lo << " fullHash mismatch";
            EXPECT_EQ(h2, refs[lo].vramHash)
                << "BackwardSweep lo=" << lo << " vramHash mismatch";
        }
        ++lo;
        --hi;
    }
    // Handle the middle element when N is odd.
    if (lo == hi)
    {
        ASSERT_TRUE(_ttd->SeekTo({refs[lo].frame, 0}));
        EXPECT_EQ(HashNow(), refs[lo].fullHash);
        EXPECT_EQ(HashScreen(), refs[lo].vramHash);
    }
}

// ===========================================================================
// Random-order seek sweep - stress the seek engine's bookkeeping
// ===========================================================================

TEST_F(TTD_Seek_Exhaustive_Test, RandomOrder_SeekEveryCheckpoint_MatchesReference)
{
    ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));
    const auto refs = CaptureReferences();
    ASSERT_EQ(refs.size(), kRecordedFrames + 1);

    std::mt19937 rng(0xC0DEFEED);  // Fixed seed for reproducibility.
    std::vector<size_t> order(refs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::shuffle(order.begin(), order.end(), rng);

    for (size_t idx : order)
    {
        ASSERT_TRUE(_ttd->SeekTo({refs[idx].frame, 0}));
        const uint64_t h1 = HashNow();
        const uint64_t h2 = HashScreen();
        if (h1 != refs[idx].fullHash || h2 != refs[idx].vramHash)
        {
            FAIL() << "RandomOrder seek to cp " << idx
                   << " (frame " << refs[idx].frame << ") mismatch - "
                   << "fullHash: seek=" << std::hex << h1 << " ref=" << refs[idx].fullHash
                   << "  vramHash: seek=" << h2 << " ref=" << refs[idx].vramHash << std::dec;
        }
    }
}

// ===========================================================================
// Repeated-seek stability: seeking to the same checkpoint many times in a
// row must produce identical state. (Catches refcount leaks where the Nth
// restore corrupts because slot N got freed.)
// ===========================================================================

TEST_F(TTD_Seek_Exhaustive_Test, RepeatSeek_ToSameCheckpoint_IsStableAcrossIterations)
{
    ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));
    const auto refs = CaptureReferences();
    ASSERT_EQ(refs.size(), kRecordedFrames + 1);

    // Pick three representative checkpoints:
    //   - baseline (always an I-frame)
    //   - a P-frame deep in a delta chain (e.g. cp 49 - last P before keyframe 50)
    //   - a P-frame early in a delta chain (e.g. cp 55 - 5 P-frames after keyframe 50)
    const std::vector<size_t> targets = {0, 49, 55};

    for (size_t t : targets)
    {
        ASSERT_LT(t, refs.size());
        const uint64_t refFull = refs[t].fullHash;
        const uint64_t refVram = refs[t].vramHash;

        // Seek to the target 10 times in a row, with a decoy in between to
        // force a real restore each iteration.
        const size_t decoy = (t + 7) % refs.size();
        for (int iter = 0; iter < 10; ++iter)
        {
            ASSERT_TRUE(_ttd->SeekTo({refs[decoy].frame, 0}));
            ASSERT_TRUE(_ttd->SeekTo({refs[t].frame, 0}));
            const uint64_t h1 = HashNow();
            const uint64_t h2 = HashScreen();
            EXPECT_EQ(h1, refFull)
                << "RepeatSeek cp=" << t << " iter=" << iter << " fullHash drift";
            EXPECT_EQ(h2, refVram)
                << "RepeatSeek cp=" << t << " iter=" << iter << " vramHash drift";
        }
    }
}

// ===========================================================================
// Intra-frame t-state seek: SeekTo({F, T}) for T > 0 engages silent replay.
// We verify the position is reached and that re-seeking to the same ({F, T})
// produces a deterministic state (hash equal across two runs).
// ===========================================================================

TEST_F(TTD_Seek_Exhaustive_Test, IntraFrame_TStateSeek_IsDeterministic)
{
    ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));
    const size_t N = _ttd->GetCheckpointCount();
    ASSERT_GT(N, 5u);

    // Read the actual per-frame t-state budget from the emulator config.
    // Different models have different frame sizes (Pentagon: 71680, ZX 128K:
    // 69888, ZX 48K: 69888). Using a hardcoded value risks crossing the frame
    // boundary, which is a different scenario than instruction-boundary
    // overshoot.
    const uint32_t frameT = _context->config.frame;
    ASSERT_GT(frameT, 1000u) << "config.frame is implausibly small";

    // Sample t-state offsets spanning small/mid/large within a frame.
    // We stay safely below frameT to avoid frame-boundary crossing (which
    // would advance frame_counter and is a separate scenario from
    // instruction-boundary overshoot).
    const std::vector<uint32_t> tOffsets = {
        0, 100, 1000, 10000,
        frameT / 4,
        frameT / 2,
        (frameT * 3) / 4,
        frameT - 1000   // Close to end but not crossing
    };

    // Test a handful of frame indices spanning baseline + mid + late.
    const std::vector<size_t> frameIdxs = {0, 1, 25, 49, 50, 55, N - 1};

    for (size_t F : frameIdxs)
    {
        const uint64_t frame = _ttd->GetCheckpoint(F)->time.frame;
        for (uint32_t T : tOffsets)
        {
            // ---------------------------------------------------------------
            // Run 1: seek to {frame, T}, capture actual position + hash.
            // ---------------------------------------------------------------
            ASSERT_TRUE(_ttd->SeekTo({frame, T}))
                << "SeekTo({" << frame << ", " << T << "}) failed";
            const ttd::TTDTimePoint pos1 = _ttd->CurrentPosition();
            const uint64_t h1 = HashNow();

            // CONTRACT (user 2026-07-19): "t-state overshoot is ok, but
            // there must be no drift between runs. always same position
            // right or after specified t-state after current instruction
            // handling ends".
            //
            // So: actual tInFrame may overshoot T (RunTStates stops at the
            // next instruction boundary >= T), but must NEVER undershoot.
            EXPECT_GE(pos1.tInFrame, T)
                << "Run1 frame=" << frame << " T=" << T
                << ": actual tInFrame=" << pos1.tInFrame
                << " undershot target (must be >= T after instr boundary)";
            // Frame must not change (we didn't ask to cross a frame boundary).
            EXPECT_EQ(pos1.frame, frame)
                << "Run1 frame=" << frame << " T=" << T
                << ": actual frame=" << pos1.frame
                << " (intra-frame seek must not advance frame_counter)";

            // ---------------------------------------------------------------
            // Decoy: seek to a different frame to force a fresh restore.
            // ---------------------------------------------------------------
            const size_t decoy = (F + 7) % N;
            const uint64_t decoyFrame = _ttd->GetCheckpoint(decoy)->time.frame;
            ASSERT_TRUE(_ttd->SeekTo({decoyFrame, 0}));

            // ---------------------------------------------------------------
            // Run 2: seek to the same {frame, T}, capture actual position + hash.
            // ---------------------------------------------------------------
            ASSERT_TRUE(_ttd->SeekTo({frame, T}));
            const ttd::TTDTimePoint pos2 = _ttd->CurrentPosition();
            const uint64_t h2 = HashNow();

            // CONTRACT: "no drift between runs". The actual position must be
            // identical across the two runs — same instruction boundary, same
            // hash. Any difference here is the bug we're hunting.
            EXPECT_EQ(pos2.tInFrame, pos1.tInFrame)
                << "DRIFT frame=" << frame << " T=" << T
                << ": run1 landed at tInFrame=" << pos1.tInFrame
                << ", run2 at tInFrame=" << pos2.tInFrame
                << " (must be identical — no drift between runs)";
            EXPECT_EQ(pos2.frame, pos1.frame)
                << "DRIFT frame=" << frame << " T=" << T
                << ": run1 frame=" << pos1.frame
                << ", run2 frame=" << pos2.frame;
            EXPECT_EQ(h2, h1)
                << "DRIFT frame=" << frame << " T=" << T
                << ": hash differs across two runs (std::hex: "
                << std::hex << "run1=" << h1 << " run2=" << h2 << std::dec << ")";
        }
    }
}

// ===========================================================================
// Round-trip via .ttd: serialize, deserialize, re-run the matrix.
// This is the user's explicit "save current session recording to ttd file"
// request.
// ===========================================================================

class TTD_Seek_Exhaustive_RoundTrip_Test : public TTD_Seek_Exhaustive_Test
{
protected:
    /// After recording, round-trip the session through SerializeSession /
    /// DeserializeSession and verify all references still match.
    void RoundTripAndReverify()
    {
        ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));

        // Capture references BEFORE serialize.
        const auto refsBefore = CaptureReferences();
        ASSERT_EQ(refsBefore.size(), kRecordedFrames + 1);

        // Serialize to a string stream (in-memory .ttd).
        std::ostringstream out(std::ios::binary);
        std::string err;
        ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;
        const std::string data = out.str();
        ASSERT_FALSE(data.empty());

        // Deserialize back into the same manager - replaces the live session.
        std::istringstream in(data, std::ios::binary);
        ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;

        // Capture references AFTER deserialize.
        const auto refsAfter = CaptureReferences();
        ASSERT_EQ(refsAfter.size(), refsBefore.size());

        // Round-trip invariant: every reference must match.
        for (size_t i = 0; i < refsBefore.size(); ++i)
        {
            EXPECT_EQ(refsAfter[i].frame,     refsBefore[i].frame)
                << "RoundTrip cp " << i << ": frame mismatch";
            EXPECT_EQ(refsAfter[i].fullHash, refsBefore[i].fullHash)
                << "RoundTrip cp " << i << ": fullHash mismatch";
            EXPECT_EQ(refsAfter[i].vramHash, refsBefore[i].vramHash)
                << "RoundTrip cp " << i << ": vramHash mismatch";
        }

        // Now stress-test the deserialized timeline with backward sweeps.
        const size_t N = refsAfter.size();
        size_t lo = 0, hi = N - 1;
        while (lo < hi)
        {
            ASSERT_TRUE(_ttd->SeekTo({refsAfter[hi].frame, 0}));
            EXPECT_EQ(HashNow(),   refsAfter[hi].fullHash)
                << "PostRoundTrip hi=" << hi << " fullHash drift";
            EXPECT_EQ(HashScreen(), refsAfter[hi].vramHash)
                << "PostRoundTrip hi=" << hi << " vramHash drift";

            ASSERT_TRUE(_ttd->SeekTo({refsAfter[lo].frame, 0}));
            EXPECT_EQ(HashNow(),   refsAfter[lo].fullHash)
                << "PostRoundTrip lo=" << lo << " fullHash drift";
            EXPECT_EQ(HashScreen(), refsAfter[lo].vramHash)
                << "PostRoundTrip lo=" << lo << " vramHash drift";
            ++lo;
            --hi;
        }
    }
};

TEST_F(TTD_Seek_Exhaustive_RoundTrip_Test, SerializeDeserialize_PreservesAllReferences)
{
    RoundTripAndReverify();
}

// ===========================================================================
// Disk-file round-trip: write to a real .ttd file, read back, verify.
// Mirrors how an automation client would consume the recording.
// ===========================================================================

TEST_F(TTD_Seek_Exhaustive_RoundTrip_Test, DiskFile_WriteAndRead_PreservesAllReferences)
{
    ASSERT_TRUE(RecordSessionForExhaustive(_ttd, _emulator, kRecordedFrames));
    const auto refsBefore = CaptureReferences();
    ASSERT_EQ(refsBefore.size(), kRecordedFrames + 1);

    // Write to a temp .ttd file.
    const std::string tmpPath = "/tmp/ttd_seek_exhaustive_session.ttd";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        std::string err;
        ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;
        out.flush();
        ASSERT_TRUE(out.good());
    }

    // Read it back.
    {
        std::ifstream in(tmpPath, std::ios::binary);
        ASSERT_TRUE(in.good());
        std::string err;
        ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;
    }

    // Re-capture references and verify round-trip integrity.
    const auto refsAfter = CaptureReferences();
    ASSERT_EQ(refsAfter.size(), refsBefore.size());

    for (size_t i = 0; i < refsBefore.size(); ++i)
    {
        EXPECT_EQ(refsAfter[i].frame,     refsBefore[i].frame);
        EXPECT_EQ(refsAfter[i].fullHash, refsBefore[i].fullHash)
            << "DiskFile cp " << i << ": fullHash mismatch";
        EXPECT_EQ(refsAfter[i].vramHash, refsBefore[i].vramHash)
            << "DiskFile cp " << i << ": vramHash mismatch";
    }

    // Also stress-test post-deserialize with a backward sweep.
    for (size_t i = refsAfter.size() - 1; i-- > 0; )
    {
        ASSERT_TRUE(_ttd->SeekTo({refsAfter[i].frame, 0}));
        EXPECT_EQ(HashNow(),   refsAfter[i].fullHash)
            << "PostDiskFile cp " << i << ": fullHash drift";
        EXPECT_EQ(HashScreen(), refsAfter[i].vramHash)
            << "PostDiskFile cp " << i << ": vramHash drift";
    }
}

// ===========================================================================
// Long-duration seek stability test — 30 seconds of recording (1500 frames)
// ===========================================================================
//
// Per user request 2026-07-19: "do 30 seconds of demo and test all
// permutations to ensure that in any case we have 100% stable seek/restore,
// no jitter or variations between runs".
//
// 30 seconds @ 50 Hz = 1500 frames. With kKeyFrameInterval = 50, this gives
// us 30 I-frames and ~1470 P-frames with delta chains up to depth 49.
//
// This test class is SLOW (~30-60s wall clock) — it's the canonical
// "nothing flaked over a long demo" coverage. Fast correctness checks live
// in the main TTD_Seek_Exhaustive_Test fixture above.

namespace
{

/// 30 seconds @ 50 Hz = 1500 frames. Plenty of I-frames (30) + deep delta
/// chains (up to 49 levels) + edge cases at session boundaries.
constexpr size_t k30SecondFrames = 1500;

/// Number of complete sweep passes for the "no drift across runs" check.
/// 5 sweeps with different orderings catch any non-determinism that depends
/// on the prior seek target.
constexpr int kNumSweepPasses = 5;

} // namespace

// Inherits SetUp/TearDown/HashNow/HashScreen/RunFrames from
// TTD_Seek_Exhaustive_Test. We only add LongDuration-specific helpers
// (RecordDemo, CaptureAllReferences, Ref struct with frameKind).
class TTD_Seek_LongDuration_Test : public TTD_Seek_Exhaustive_Test
{
protected:
    /// Captured reference state for every checkpoint. Used as the ground
    /// truth that every seek permutation must reproduce. Richer than the
    /// base fixture's CheckpointRef: also captures frameKind so we can
    /// distinguish I-frames from P-frames in the timeline-shape test.
    struct Ref
    {
        uint64_t frame     = 0;
        uint64_t fullHash  = 0;
        uint64_t vramHash  = 0;
        ttd::TTDFrameKind kind = ttd::TTDFrameKind::KeyFrame;
    };

    /// Record the demo session: 1500 frames (~30 sec @ 50 Hz).
    void RecordDemo()
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(static_cast<unsigned>(k30SecondFrames),
                              /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);
        ASSERT_EQ(_ttd->GetCheckpointCount(), k30SecondFrames + 1);
    }

    /// Capture reference state for every checkpoint. Heavy: O(N) restores.
    std::vector<Ref> CaptureAllReferences()
    {
        const size_t N = _ttd->GetCheckpointCount();
        std::vector<Ref> refs;
        refs.reserve(N);
        for (size_t i = 0; i < N; ++i)
        {
            EXPECT_TRUE(_ttd->RestoreCheckpointForTesting(i))
                << "RestoreCheckpointForTesting(" << i << ") failed";
            const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(i);
            Ref r;
            r.frame    = cp->time.frame;
            r.fullHash = HashNow();
            r.vramHash = HashScreen();
            r.kind     = cp->frameKind;
            refs.push_back(r);
        }
        return refs;
    }
};

// ---------------------------------------------------------------------------
// Sanity: 30-second recording completes with expected timeline shape
// ---------------------------------------------------------------------------

TEST_F(TTD_Seek_LongDuration_Test, Record30Seconds_TimelineHasExpectedShape)
{
    ASSERT_NO_FATAL_FAILURE(RecordDemo());
    const size_t N = _ttd->GetCheckpointCount();
    ASSERT_EQ(N, k30SecondFrames + 1);

    // Expect 31 keyframes: baseline + every 50th frame up to and including 1500.
    // Frame 0 (baseline) + frames 50, 100, ..., 1500 = 31 total.
    size_t keyCount = 0, deltaCount = 0;
    for (size_t i = 0; i < N; ++i)
    {
        const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(i);
        ASSERT_NE(cp, nullptr);
        if (cp->frameKind == ttd::TTDFrameKind::KeyFrame) ++keyCount;
        else ++deltaCount;
    }
    EXPECT_EQ(keyCount, (k30SecondFrames / ttd::TimeTravelManager::kKeyFrameInterval) + 1)
        << "Unexpected keyframe count";
    EXPECT_EQ(deltaCount + keyCount, N);

    // Verify keyframe anchor invariant: every P-frame's anchor points to
    // the most recent I-frame at or before its own frame.
    uint64_t lastKeyFrame = 0;
    for (size_t i = 0; i < N; ++i)
    {
        const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(i);
        if (cp->frameKind == ttd::TTDFrameKind::KeyFrame)
        {
            EXPECT_EQ(cp->keyFrameAnchor, cp->time.frame)
                << "I-frame cp " << i << " anchor must equal its own frame";
            lastKeyFrame = cp->time.frame;
        }
        else
        {
            EXPECT_EQ(cp->keyFrameAnchor, lastKeyFrame)
                << "P-frame cp " << i << " (frame " << cp->time.frame
                << ") anchor " << cp->keyFrameAnchor
                << " != most recent I-frame " << lastKeyFrame;
        }
    }
}

// ---------------------------------------------------------------------------
// Core stability: every checkpoint reachable via 5 distinct approaches,
// all producing identical hashes.
// ---------------------------------------------------------------------------

TEST_F(TTD_Seek_LongDuration_Test, EveryCheckpoint_AllApproaches_ProduceSameHash)
{
    ASSERT_NO_FATAL_FAILURE(RecordDemo());
    const std::vector<Ref> refs = CaptureAllReferences();
    ASSERT_EQ(refs.size(), k30SecondFrames + 1);
    const size_t N = refs.size();

    // For each checkpoint, try 5 approaches:
    //   - Self (no-op seek after arriving at same cp)
    //   - From previous neighbor (forward 1 step)
    //   - From next neighbor (backward 1 step)
    //   - From far earlier (long forward delta)
    //   - From far later (long backward delta)
    //
    // All five must reproduce refs[i] exactly. Any variation here is jitter.
    for (size_t i = 0; i < N; ++i)
    {
        const uint64_t targetFrame = refs[i].frame;
        const uint64_t refFull = refs[i].fullHash;
        const uint64_t refVram = refs[i].vramHash;

        // Approach 1: self (no-op path - arrive, then seek again).
        ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}));
        ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}));
        EXPECT_EQ(HashNow(), refFull)
            << "Self-seek cp " << i << " (frame " << targetFrame
            << "): fullHash mismatch";
        EXPECT_EQ(HashScreen(), refVram)
            << "Self-seek cp " << i << " (frame " << targetFrame
            << "): vramHash mismatch";

        // Approach 2: from previous neighbor (skip cp 0).
        if (i >= 1)
        {
            ASSERT_TRUE(_ttd->SeekTo({refs[i - 1].frame, 0}));
            ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}));
            EXPECT_EQ(HashNow(),   refFull)
                << "FromPrev cp " << i << ": fullHash mismatch";
            EXPECT_EQ(HashScreen(), refVram)
                << "FromPrev cp " << i << ": vramHash mismatch";
        }

        // Approach 3: from next neighbor (skip last cp).
        if (i + 1 < N)
        {
            ASSERT_TRUE(_ttd->SeekTo({refs[i + 1].frame, 0}));
            ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}));
            EXPECT_EQ(HashNow(),   refFull)
                << "FromNext cp " << i << ": fullHash mismatch";
            EXPECT_EQ(HashScreen(), refVram)
                << "FromNext cp " << i << ": vramHash mismatch";
        }

        // Approach 4: from far earlier.
        if (i >= 100)
        {
            ASSERT_TRUE(_ttd->SeekTo({refs[i - 100].frame, 0}));
            ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}));
            EXPECT_EQ(HashNow(),   refFull)
                << "FromFarEarlier cp " << i << ": fullHash mismatch";
            EXPECT_EQ(HashScreen(), refVram)
                << "FromFarEarlier cp " << i << ": vramHash mismatch";
        }

        // Approach 5: from far later.
        if (i + 100 < N)
        {
            ASSERT_TRUE(_ttd->SeekTo({refs[i + 100].frame, 0}));
            ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}));
            EXPECT_EQ(HashNow(),   refFull)
                << "FromFarLater cp " << i << ": fullHash mismatch";
            EXPECT_EQ(HashScreen(), refVram)
                << "FromFarLater cp " << i << ": vramHash mismatch";
        }
    }
}

// ---------------------------------------------------------------------------
// No-drift across runs: 5 full sweeps in different orders must produce
// identical hashes for every checkpoint.
// ---------------------------------------------------------------------------

TEST_F(TTD_Seek_LongDuration_Test, FiveFullSweeps_NoDriftAcrossRuns)
{
    ASSERT_NO_FATAL_FAILURE(RecordDemo());
    const std::vector<Ref> refs = CaptureAllReferences();
    ASSERT_EQ(refs.size(), k30SecondFrames + 1);
    const size_t N = refs.size();

    // For each checkpoint, record the (fullHash, vramHash) observed in
    // each of 5 sweeps. All 5 entries must be identical AND equal to refs.
    std::vector<std::vector<uint64_t>> observedFull(N, std::vector<uint64_t>(kNumSweepPasses));
    std::vector<std::vector<uint64_t>> observedVram(N, std::vector<uint64_t>(kNumSweepPasses));

    auto run_sweep = [&](int pass, const std::vector<size_t>& order)
    {
        for (size_t idx : order)
        {
            ASSERT_TRUE(_ttd->SeekTo({refs[idx].frame, 0}));
            observedFull[idx][pass] = HashNow();
            observedVram[idx][pass] = HashScreen();
        }
    };

    // Pass 0: forward sweep.
    {
        std::vector<size_t> order(N);
        for (size_t i = 0; i < N; ++i) order[i] = i;
        run_sweep(0, order);
    }
    // Pass 1: backward sweep.
    {
        std::vector<size_t> order(N);
        for (size_t i = 0; i < N; ++i) order[i] = N - 1 - i;
        run_sweep(1, order);
    }
    // Passes 2-4: random orders with different seeds.
    for (int seed = 0; seed < 3; ++seed)
    {
        std::vector<size_t> order(N);
        for (size_t i = 0; i < N; ++i) order[i] = i;
        std::mt19937 rng(static_cast<uint32_t>(0xDEAD0000 + seed));
        std::shuffle(order.begin(), order.end(), rng);
        run_sweep(2 + seed, order);
    }

    // Verify: every pass produced identical (full, vram) hashes per checkpoint,
    // AND all match the reference captured via RestoreCheckpointForTesting.
    for (size_t i = 0; i < N; ++i)
    {
        for (int pass = 0; pass < kNumSweepPasses; ++pass)
        {
            EXPECT_EQ(observedFull[i][pass], refs[i].fullHash)
                << "cp " << i << " (frame " << refs[i].frame
                << ") pass " << pass << ": fullHash != reference (jitter!)";
            EXPECT_EQ(observedVram[i][pass], refs[i].vramHash)
                << "cp " << i << " (frame " << refs[i].frame
                << ") pass " << pass << ": vramHash != reference (jitter!)";
        }
        // All passes for this cp must agree with each other.
        for (int pass = 1; pass < kNumSweepPasses; ++pass)
        {
            EXPECT_EQ(observedFull[i][pass], observedFull[i][0])
                << "cp " << i << ": fullHash differs between pass 0 and pass "
                << pass << " (drift across runs!)";
            EXPECT_EQ(observedVram[i][pass], observedVram[i][0])
                << "cp " << i << ": vramHash differs between pass 0 and pass "
                << pass << " (drift across runs!)";
        }
    }
}

// ---------------------------------------------------------------------------
// Ping-pong pattern: cp[i] -> cp[i+k] -> cp[i] -> cp[i+1] -> ...
// Stresses refcount bookkeeping under rapid back-and-forth transitions.
// ---------------------------------------------------------------------------

TEST_F(TTD_Seek_LongDuration_Test, PingPong_Pattern_NoCorruptionAfterRapidTransitions)
{
    ASSERT_NO_FATAL_FAILURE(RecordDemo());
    const std::vector<Ref> refs = CaptureAllReferences();
    ASSERT_EQ(refs.size(), k30SecondFrames + 1);
    const size_t N = refs.size();

    // Sample: every 7th checkpoint, ping-pong with offsets 1, 5, 25, 49.
    // Offset 49 is the deepest legal delta chain (just before the next I-frame).
    const std::vector<size_t> offsets = {1, 5, 25, 49};

    for (size_t i = 0; i < N; i += 7)
    {
        for (size_t k : offsets)
        {
            if (i + k >= N) break;

            // Land at i first.
            ASSERT_TRUE(_ttd->SeekTo({refs[i].frame, 0}));
            ASSERT_EQ(HashNow(), refs[i].fullHash)
                << "PingPong baseline cp " << i << ": fullHash drift";

            // Forward to i+k, then back to i. Repeat 3 times.
            for (int iter = 0; iter < 3; ++iter)
            {
                ASSERT_TRUE(_ttd->SeekTo({refs[i + k].frame, 0}));
                EXPECT_EQ(HashNow(),   refs[i + k].fullHash)
                    << "PingPong i=" << i << " k=" << k << " iter=" << iter
                    << ": forward fullHash drift";
                EXPECT_EQ(HashScreen(), refs[i + k].vramHash)
                    << "PingPong i=" << i << " k=" << k << " iter=" << iter
                    << ": forward vramHash drift";

                ASSERT_TRUE(_ttd->SeekTo({refs[i].frame, 0}));
                EXPECT_EQ(HashNow(),   refs[i].fullHash)
                    << "PingPong i=" << i << " k=" << k << " iter=" << iter
                    << ": backward fullHash drift";
                EXPECT_EQ(HashScreen(), refs[i].vramHash)
                    << "PingPong i=" << i << " k=" << k << " iter=" << iter
                    << ": backward vramHash drift";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Deep delta chain walk: at every depth 1, 5, 25, 49 within each I-frame
// interval, verify restore is identical across two runs.
// ---------------------------------------------------------------------------

TEST_F(TTD_Seek_LongDuration_Test, DeltaChain_AllDepths_1through49_RestoreIdentically)
{
    ASSERT_NO_FATAL_FAILURE(RecordDemo());
    const std::vector<Ref> refs = CaptureAllReferences();
    ASSERT_EQ(refs.size(), k30SecondFrames + 1);

    // For each I-frame interval [K*50, (K+1)*50), walk to depths 1, 5, 25, 49.
    const std::vector<size_t> depths = {1, 5, 25, 49};
    const size_t K = ttd::TimeTravelManager::kKeyFrameInterval;

    for (size_t keyIdx = 0; keyIdx + K < refs.size(); keyIdx += K)
    {
        for (size_t d : depths)
        {
            if (keyIdx + d >= refs.size()) break;

            const size_t target = keyIdx + d;
            const uint64_t targetFrame = refs[target].frame;
            const uint64_t refFull = refs[target].fullHash;
            const uint64_t refVram = refs[target].vramHash;

            // Run 1: seek from the I-frame anchor (forces delta chain walk).
            ASSERT_TRUE(_ttd->SeekTo({refs[keyIdx].frame, 0}));
            ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}));
            const uint64_t h1 = HashNow();
            const uint64_t v1 = HashScreen();

            // Run 2: seek from a different starting point.
            const size_t far = (target + 200) % refs.size();
            ASSERT_TRUE(_ttd->SeekTo({refs[far].frame, 0}));
            ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}));
            const uint64_t h2 = HashNow();
            const uint64_t v2 = HashScreen();

            EXPECT_EQ(h1, refFull)
                << "DeltaChain depth " << d << " at keyIdx " << keyIdx
                << ": run1 fullHash != reference";
            EXPECT_EQ(v1, refVram)
                << "DeltaChain depth " << d << " at keyIdx " << keyIdx
                << ": run1 vramHash != reference";
            EXPECT_EQ(h2, refFull)
                << "DeltaChain depth " << d << " at keyIdx " << keyIdx
                << ": run2 fullHash != reference";
            EXPECT_EQ(v2, refVram)
                << "DeltaChain depth " << d << " at keyIdx " << keyIdx
                << ": run2 vramHash != reference";
            EXPECT_EQ(h2, h1)
                << "DeltaChain depth " << d << " at keyIdx " << keyIdx
                << ": drift between run1 and run2";
        }
    }
}

// ---------------------------------------------------------------------------
// Intra-frame determinism across 3 runs at sampled positions.
// Extends the short test's coverage to the 30-second recording.
// ---------------------------------------------------------------------------

TEST_F(TTD_Seek_LongDuration_Test, IntraFrameTState_NoDriftAcrossThreeRuns)
{
    ASSERT_NO_FATAL_FAILURE(RecordDemo());
    const size_t N = _ttd->GetCheckpointCount();
    ASSERT_GT(N, 100u);

    const uint32_t frameT = _context->config.frame;
    ASSERT_GT(frameT, 1000u);

    const std::vector<uint32_t> tOffsets = {
        0, 100, 1000, 10000, frameT / 2, frameT - 1000
    };

    // Sample 12 frame indices across the 1501-checkpoint timeline.
    std::vector<size_t> frameIdxs;
    for (size_t i = 0; i < 12; ++i)
        frameIdxs.push_back((N * i) / 12);
    frameIdxs.push_back(N - 1);  // session-end frame (recently-fixed seek case)

    for (size_t F : frameIdxs)
    {
        const uint64_t frame = _ttd->GetCheckpoint(F)->time.frame;
        for (uint32_t T : tOffsets)
        {
            ttd::TTDTimePoint positions[3];
            uint64_t hashes[3];

            for (int run = 0; run < 3; ++run)
            {
                // Decoy between runs to force a real restore each time.
                const size_t decoy = (F + 7 * (run + 1)) % N;
                ASSERT_TRUE(_ttd->SeekTo({_ttd->GetCheckpoint(decoy)->time.frame, 0}));
                ASSERT_TRUE(_ttd->SeekTo({frame, T}))
                    << "Run " << run << " SeekTo({" << frame << "," << T << "}) failed";
                positions[run] = _ttd->CurrentPosition();
                hashes[run]    = HashNow();
            }

            // Contract: overshoot OK, undershoot NOT OK.
            EXPECT_GE(positions[0].tInFrame, T)
                << "frame=" << frame << " T=" << T
                << ": undershot target (pos=" << positions[0].tInFrame << ")";
            EXPECT_EQ(positions[0].frame, frame)
                << "frame=" << frame << " T=" << T
                << ": wrong frame (got " << positions[0].frame << ")";

            // Contract: zero drift across runs.
            EXPECT_EQ(positions[1].tInFrame, positions[0].tInFrame)
                << "DRIFT frame=" << frame << " T=" << T
                << ": run1 tInFrame=" << positions[1].tInFrame
                << " != run0 tInFrame=" << positions[0].tInFrame;
            EXPECT_EQ(positions[2].tInFrame, positions[0].tInFrame)
                << "DRIFT frame=" << frame << " T=" << T
                << ": run2 tInFrame=" << positions[2].tInFrame
                << " != run0 tInFrame=" << positions[0].tInFrame;
            EXPECT_EQ(hashes[1], hashes[0])
                << "DRIFT frame=" << frame << " T=" << T
                << ": run1 hash differs from run0";
            EXPECT_EQ(hashes[2], hashes[0])
                << "DRIFT frame=" << frame << " T=" << T
                << ": run2 hash differs from run0";
        }
    }
}

// ---------------------------------------------------------------------------
// Round-trip after long recording: serialize -> deserialize -> re-verify
// every checkpoint matches. This is the user's explicit "save current
// session recording to ttd file" scenario on a 30-second demo.
// ---------------------------------------------------------------------------

TEST_F(TTD_Seek_LongDuration_Test, Serialize30SecondSession_AllCheckpointsRoundTripIdentically)
{
    ASSERT_NO_FATAL_FAILURE(RecordDemo());
    const std::vector<Ref> refsBefore = CaptureAllReferences();
    ASSERT_EQ(refsBefore.size(), k30SecondFrames + 1);

    // Serialize to in-memory stream.
    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;
    const std::string data = out.str();
    ASSERT_FALSE(data.empty());

    // Deserialize back.
    std::istringstream in(data, std::ios::binary);
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;

    const std::vector<Ref> refsAfter = CaptureAllReferences();
    ASSERT_EQ(refsAfter.size(), refsBefore.size());

    // Every checkpoint must round-trip byte-identically.
    for (size_t i = 0; i < refsBefore.size(); ++i)
    {
        EXPECT_EQ(refsAfter[i].frame,    refsBefore[i].frame)
            << "RoundTrip cp " << i << ": frame mismatch";
        EXPECT_EQ(refsAfter[i].fullHash, refsBefore[i].fullHash)
            << "RoundTrip cp " << i << " (frame " << refsBefore[i].frame
            << "): fullHash mismatch - THIS IS DATA CORRUPTION";
        EXPECT_EQ(refsAfter[i].vramHash, refsBefore[i].vramHash)
            << "RoundTrip cp " << i << " (frame " << refsBefore[i].frame
            << "): vramHash mismatch - SCREEN CORRUPTION AFTER .ttd ROUND-TRIP";
    }
}

// ---------------------------------------------------------------------------
// Post-roundtrip stability: after deserializing the .ttd, run the same
// 5-sweep no-drift check on the restored timeline.
// ---------------------------------------------------------------------------

TEST_F(TTD_Seek_LongDuration_Test, PostRoundTrip_FiveSweeps_NoDrift)
{
    ASSERT_NO_FATAL_FAILURE(RecordDemo());

    // Round-trip through .ttd first.
    {
        std::ostringstream out(std::ios::binary);
        std::string err;
        ASSERT_TRUE(_ttd->SerializeSession(out, err));
        std::istringstream in(out.str(), std::ios::binary);
        ASSERT_TRUE(_ttd->DeserializeSession(in, err));
    }

    const std::vector<Ref> refs = CaptureAllReferences();
    ASSERT_EQ(refs.size(), k30SecondFrames + 1);
    const size_t N = refs.size();

    std::vector<std::vector<uint64_t>> observedFull(N, std::vector<uint64_t>(kNumSweepPasses));
    std::vector<std::vector<uint64_t>> observedVram(N, std::vector<uint64_t>(kNumSweepPasses));

    auto run_sweep = [&](int pass, const std::vector<size_t>& order)
    {
        for (size_t idx : order)
        {
            ASSERT_TRUE(_ttd->SeekTo({refs[idx].frame, 0}));
            observedFull[idx][pass] = HashNow();
            observedVram[idx][pass] = HashScreen();
        }
    };

    {
        std::vector<size_t> order(N);
        for (size_t i = 0; i < N; ++i) order[i] = i;
        run_sweep(0, order);
    }
    {
        std::vector<size_t> order(N);
        for (size_t i = 0; i < N; ++i) order[i] = N - 1 - i;
        run_sweep(1, order);
    }
    for (int seed = 0; seed < 3; ++seed)
    {
        std::vector<size_t> order(N);
        for (size_t i = 0; i < N; ++i) order[i] = i;
        std::mt19937 rng(static_cast<uint32_t>(0xBEEF0000 + seed));
        std::shuffle(order.begin(), order.end(), rng);
        run_sweep(2 + seed, order);
    }

    for (size_t i = 0; i < N; ++i)
    {
        for (int pass = 1; pass < kNumSweepPasses; ++pass)
        {
            EXPECT_EQ(observedFull[i][pass], observedFull[i][0])
                << "PostRoundTrip cp " << i << " pass " << pass
                << ": fullHash drift across sweeps";
            EXPECT_EQ(observedVram[i][pass], observedVram[i][0])
                << "PostRoundTrip cp " << i << " pass " << pass
                << ": vramHash drift across sweeps";
        }
    }
}
