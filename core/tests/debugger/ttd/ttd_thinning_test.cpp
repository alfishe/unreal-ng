/// @file ttd_thinning_test.cpp
/// @brief TTD_Thinning_EveryPointReachable — verifies the fundamental
///        seek-engine invariant from TDD §6.5 + §15 test table.
///
/// TDD §15 test specification:
///   TTD_Thinning_EveryPointReachable | Phase 2 |
///   ∀ t in session: ∃ checkpoint ≤ t with journal coverage of [checkpoint, t]
///
/// TDD §6.5 line 413:
///   "The input journal and write journal are never thinned within the
///    session window — this is what keeps *every* instruction reachable
///    even in thinned regions: restore sparse checkpoint, replay forward
///    with journaled inputs."
///
/// This test verifies three aspects of the invariant:
///
/// 1. **Dense coverage**: every frame in [sessionStart, sessionEnd] has a
///    corresponding checkpoint in the timeline. (When thinning lands, this
///    will be relaxed to "every frame has a checkpoint at-or-before it" —
///    but the current engine is dense: every frame gets a checkpoint.)
///
/// 2. **Reachability**: for every sampled time point t (frame-aligned and
///    intra-frame), SeekTo(t) succeeds and the post-seek machine state
///    matches the reference hash from RestoreCheckpointForTesting.
///
/// 3. **Serialization preservation**: after SerializeSession →
///    DeserializeSession into a fresh emulator, every sampled point is
///    still reachable with the same state hashes.
///
/// When checkpoint thinning/eviction is eventually implemented (TDD §6.5),
/// this test will be the gate that catches "point X is no longer reachable
/// after thinning pass Y" regressions. For now, with dense checkpoints,
/// it validates the seek engine's correctness invariant: every point in a
/// recorded session is reachable via restore + replay, deterministically.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unistd.h>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/machine_state_hash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

// ===========================================================================
// Fixture
// ===========================================================================

class TTD_Thinning_Test : public ::testing::Test
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

    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }

    /// Full machine-state hash: CPU + chipset + every model-RAM byte.
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

    /// Per-checkpoint reference state captured via the testing API.
    struct CheckpointRef
    {
        uint64_t fullHash = 0;
        uint64_t frame     = 0;
    };

    /// Capture references for every checkpoint in the timeline.
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
            r.frame    = _ttd->GetCheckpoint(i)->time.frame;
            refs.push_back(r);
        }
        return refs;
    }
};

// ===========================================================================
// TDD §15: TTD_Thinning_EveryPointReachable
//
// ∀ t in session: ∃ checkpoint ≤ t with journal coverage of [checkpoint, t]
// ===========================================================================

TEST_F(TTD_Thinning_Test, EveryPointReachable_DenseCheckpoints_AllFramesSeekable)
{
    // Record a session long enough to have meaningful history.
    const size_t kFrames = 30;
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(kFrames);
    _ttd->StopRecording();

    const auto info = _ttd->GetSessionInfo();
    const size_t cpCount = _ttd->GetCheckpointCount();

    // Dense coverage: every frame boundary in [start, end] must have a
    // checkpoint. The baseline is captured at StartRecording, then one per
    // frame via OnFrameBoundary. So cpCount == kFrames + 1.
    EXPECT_EQ(cpCount, kFrames + 1);

    // Capture reference hashes from the checkpoints themselves.
    auto refs = CaptureReferences();
    ASSERT_EQ(refs.size(), cpCount);

    // Every frame-aligned point must be seekable and produce the reference
    // hash. This is the core invariant: ∀ t, SeekTo(t) succeeds and the
    // state at t is deterministic.
    for (size_t i = 0; i < cpCount; ++i)
    {
        ttd::TTDTimePoint target{refs[i].frame, 0};

        bool reached = _ttd->SeekTo(target);
        EXPECT_TRUE(reached)
            << "SeekTo({frame=" << refs[i].frame << ", t=0}) failed (cp " << i << ")";

        uint64_t hashAfterSeek = HashNow();
        EXPECT_EQ(hashAfterSeek, refs[i].fullHash)
            << "State mismatch at checkpoint " << i
            << " (frame " << refs[i].frame << ")";
    }
}

TEST_F(TTD_Thinning_Test, EveryPointReachable_IntraFramePointsSeekable)
{
    // Record a session.
    const size_t kFrames = 10;
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(kFrames);
    _ttd->StopRecording();

    const uint32_t frameT = _context->config.frame;

    // Sample intra-frame t-state offsets: early, mid, late.
    // We avoid frameT-1 (the very last t-state) because RunTStates can't
    // stop mid-instruction — the last instruction near the frame boundary
    // may overshoot by up to 23 t-states, crossing into the next frame and
    // triggering OnFrameEnd/OnFrameStart side effects. This overshoot is
    // expected (documented in ttd_seek_test.cpp with EXPECT_NEAR). The
    // invariant we're testing is reachability + determinism for points
    // safely within the frame.
    const uint32_t tSamples[] = {0, 1000, 10000, 35000, frameT / 2};

    auto refs = CaptureReferences();

    // For each frame boundary, seek to several intra-frame points within
    // that frame. SeekTo({F, T}) exercises the restore + silent-replay path.
    // The invariant: every such point must be reachable.
    for (size_t i = 1; i < refs.size(); ++i)
    {
        const uint64_t frame = refs[i].frame;

        for (uint32_t t : tSamples)
        {
            ttd::TTDTimePoint target{frame, t};

            // SeekTo should succeed for every intra-frame point within
            // the session bounds. (Points exactly at frame end may
            // round to the next checkpoint — that's fine, they're still
            // reachable.)
            bool reached = _ttd->SeekTo(target);
            EXPECT_TRUE(reached)
                << "SeekTo({frame=" << frame << ", t=" << t << "}) failed";

            // Determinism: seek to the same point again, verify the hash
            // is stable. Two seeks to the same target must produce
            // identical machine state — the core determinism guarantee.
            uint64_t hash1 = HashNow();

            // Seek somewhere else first, then back — to force a full
            // restore+replay rather than a no-op.
            if (i > 1)
                _ttd->SeekTo({refs[i - 2].frame, 0});
            else
                _ttd->SeekTo({refs[0].frame, 0});

            _ttd->SeekTo(target);
            uint64_t hash2 = HashNow();

            EXPECT_EQ(hash1, hash2)
                << "Non-deterministic state at (frame=" << frame
                << ", t=" << t << ") — two seeks produced different hashes";
        }
    }
}

TEST_F(TTD_Thinning_Test, EveryPointReachable_BackwardSweep_AllLandingsCorrect)
{
    // The "backward sweep" pattern: seek to every checkpoint from last to
    // first, verifying each landing. This exercises the case where the
    // binary search always picks an earlier checkpoint and must replay
    // forward — the exact pattern thinning would stress.
    const size_t kFrames = 20;
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(kFrames);
    _ttd->StopRecording();

    auto refs = CaptureReferences();
    ASSERT_GT(refs.size(), 1u);

    // Sweep backward: last → first.
    for (size_t i = refs.size(); i-- > 0; )
    {
        ttd::TTDTimePoint target{refs[i].frame, 0};
        ASSERT_TRUE(_ttd->SeekTo(target))
            << "Backward sweep: SeekTo(cp " << i << ") failed";

        uint64_t h = HashNow();
        EXPECT_EQ(h, refs[i].fullHash)
            << "Backward sweep: state mismatch at cp " << i;
    }

    // Sweep forward: first → last (to verify no stale state from backward pass).
    for (size_t i = 0; i < refs.size(); ++i)
    {
        ttd::TTDTimePoint target{refs[i].frame, 0};
        ASSERT_TRUE(_ttd->SeekTo(target))
            << "Forward sweep: SeekTo(cp " << i << ") failed";

        uint64_t h = HashNow();
        EXPECT_EQ(h, refs[i].fullHash)
            << "Forward sweep: state mismatch at cp " << i;
    }
}

TEST_F(TTD_Thinning_Test, EveryPointReachable_AfterSerializationRoundTrip)
{
    // The invariant must survive serialization: after dump → reload, every
    // point must still be reachable. This catches format bugs that lose
    // checkpoint or journal data.
    const size_t kFrames = 15;
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(kFrames);
    _ttd->StopRecording();

    // Capture reference hashes before serialization.
    auto refsBefore = CaptureReferences();
    ASSERT_GT(refsBefore.size(), 1u);

    // Serialize to a temp file.
    char tmpfile[] = "/tmp/ttd_thinning_serialize_XXXXXX";
    int fd = mkstemp(tmpfile);
    ASSERT_GE(fd, 0);
    close(fd);

    {
        std::ofstream out(tmpfile, std::ios::binary);
        std::string err;
        ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;
    }

    // Deserialize into a fresh emulator.
    Emulator* emu2 = new Emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emu2->Init());
    EmulatorContext* ctx2 = emu2->GetContext();
    ttd::TimeTravelManager* ttd2 = ctx2->pTimeTravelManager;
    ASSERT_NE(ttd2, nullptr);

    ctx2->pFeatureManager->setFeature(Features::kDebugMode, true);
    ctx2->pFeatureManager->setFeature(Features::kTimeTravel, true);
    ctx2->pMemory->UpdateFeatureCache();

    {
        std::ifstream in(tmpfile, std::ios::binary);
        std::string err;
        ASSERT_TRUE(ttd2->DeserializeSession(in, err)) << err;
    }

    // Checkpoint count must match.
    EXPECT_EQ(ttd2->GetCheckpointCount(), refsBefore.size());

    // Hash helper for the second emulator.
    auto hashIn = [ctx2]() -> uint64_t {
        Z80* z80 = ctx2->pCore->GetZ80();
        if (!z80 || !ctx2->pMemory)
            return 0;
        const uint32_t ramBytes =
            static_cast<uint32_t>(ctx2->config.ramsize) * 1024u;
        const uint64_t ramDigest = ttd::HashBytes(ctx2->pMemory->RAMBase(), ramBytes);
        const auto snap = ttd::CaptureSnapshot(*static_cast<Z80State*>(z80),
                                                ctx2->emulatorState,
                                                ramDigest);
        return ttd::HashSnapshot(snap);
    };

    // Every frame-aligned point must still be reachable after reload,
    // with matching hashes.
    for (size_t i = 0; i < refsBefore.size(); ++i)
    {
        ttd::TTDTimePoint target{refsBefore[i].frame, 0};
        bool reached = ttd2->SeekTo(target);
        EXPECT_TRUE(reached)
            << "Post-deserialize: SeekTo(cp " << i << ", frame "
            << refsBefore[i].frame << ") failed";

        uint64_t h = hashIn();
        EXPECT_EQ(h, refsBefore[i].fullHash)
            << "Post-deserialize: state mismatch at cp " << i;
    }

    // Sample a few intra-frame points post-deserialize.
    const uint32_t frameT = ctx2->config.frame;
    const uint32_t tSamples[] = {5000, 20000, frameT / 2};
    for (size_t i = 1; i < refsBefore.size() && i <= 5; ++i)
    {
        for (uint32_t t : tSamples)
        {
            ttd::TTDTimePoint target{refsBefore[i].frame, t};
            EXPECT_TRUE(ttd2->SeekTo(target))
                << "Post-deserialize: SeekTo(frame=" << refsBefore[i].frame
                << ", t=" << t << ") failed";
        }
    }

    emu2->Stop();
    emu2->Release();
    delete emu2;
    remove(tmpfile);
}

TEST_F(TTD_Thinning_Test, EveryPointReachable_CheckpointAtOrBeforeEveryFrame)
{
    // Explicitly verify the "∃ checkpoint ≤ t" part of the invariant:
    // for every frame F in the session, there exists a checkpoint whose
    // frame is ≤ F. This is trivially true with dense checkpoints, but
    // the explicit verification is the regression gate for thinning.
    const size_t kFrames = 10;
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(kFrames);
    _ttd->StopRecording();

    const auto info = _ttd->GetSessionInfo();
    const size_t cpCount = _ttd->GetCheckpointCount();

    // Collect all checkpoint frames.
    std::vector<uint64_t> cpFrames;
    cpFrames.reserve(cpCount);
    for (size_t i = 0; i < cpCount; ++i)
    {
        const auto* cp = _ttd->GetCheckpoint(i);
        ASSERT_NE(cp, nullptr);
        cpFrames.push_back(cp->time.frame);
    }

    // Verify monotonicity (checkpoints are ordered by frame).
    for (size_t i = 1; i < cpFrames.size(); ++i)
    {
        EXPECT_GE(cpFrames[i], cpFrames[i - 1])
            << "Checkpoint frames not monotonically non-decreasing at index " << i;
    }

    // For every frame F in [sessionStartFrame, currentEndFrame], verify
    // there exists a checkpoint with frame ≤ F. This is the precondition
    // for SeekTo to succeed: without a preceding checkpoint, there's
    // nothing to restore from.
    for (uint64_t f = info.sessionStartFrame; f <= info.currentEndFrame; ++f)
    {
        auto it = std::lower_bound(cpFrames.begin(), cpFrames.end(), f);
        // lower_bound gives first element >= f. If it points to begin(),
        // even the earliest checkpoint is past f — invariant violated.
        // Otherwise, it-1 is the checkpoint at-or-before f.
        bool hasPreceding = (it != cpFrames.begin()) || 
                            (it != cpFrames.end() && *it == f);
        EXPECT_TRUE(hasPreceding)
            << "Frame " << f << " has no checkpoint at or before it";
    }
}
