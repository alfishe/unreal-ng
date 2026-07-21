#pragma once
//
// ttd_divergence_harness.h — Phase 2 Item 7 (parent TDD §5, §15.1)
//
// Test-only helper that runs a workload twice (live + replay-from-checkpoint)
// and asserts bit-identical per-frame machine state. The oracle for the
// entire TTD engine: every seek / restore / replay path must produce hashes
// matching the corresponding live frame.
//
// Usage (typical corpus test):
//
//     TTDDivergenceHarness h(emulator);
//     h.LoadSnapshot("data/testsoft/AccuracyCoinZX/accuracy_coin.sna");
//     auto live = h.RunLiveAndCapture(/*frames=*/200);
//     ASSERT_TRUE(h.StartRecordingAndCaptureTimeline(live.Size()));
//     for (size_t i : h.PickSampleFrames(live.Size(), /*step=*/10))
//     {
//         SCOPED_TRACE("frame " + std::to_string(i));
//         EXPECT_TRUE(h.VerifyReplayMatchesLive(i, live));
//     }
//
// Lives in tests/_helpers so all corpus test files share one implementation.
//

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/machine_state_hash.h"

// Forward declarations to keep heavy emulator headers out.
class Emulator;
class EmulatorContext;
struct Z80State;
struct EmulatorState;
class Memory;

namespace ttd {

/// Per-frame capture of architectural state at a frame boundary.
/// Records the digest of all RAM pages in use, the architectural snapshot,
/// and the resulting composite hash. Used both for the live run and as the
/// expected value when replaying.
struct DivergenceFrame
{
    uint64_t               frameCounter = 0;
    uint64_t               t_states     = 0;
    uint64_t               ram_digest   = 0;
    MachineStateSnapshot  snapshot{};
    uint64_t               hash         = 0;  // HashSnapshot(snapshot)
};

/// A captured sequence of per-frame state from a live run.
struct DivergenceHistory
{
    std::vector<DivergenceFrame> frames;

    void   Reserve(size_t n);
    size_t Size() const;
    bool   Empty() const;
    void   Clear();
};

/// @brief Test harness for TTD divergence checks.
///
/// All methods assume the caller has set up the emulator on a single thread
/// and is NOT driving it concurrently (the run-control claim is the
/// production answer; for tests, ownership is implicit).
class TTDDivergenceHarness
{
public:
    /// Constructs the harness around an existing emulator instance.
    /// The harness does NOT take ownership — caller keeps the emulator alive.
    explicit TTDDivergenceHarness(Emulator* emulator);

    // -----------------------------------------------------------------
    // Workload setup
    // -----------------------------------------------------------------

    /// Load a snapshot file (.sna / .z80) into the emulator. Returns false
    /// on file-not-found or loader validation failure. Path is resolved
    /// via TestPathHelper when relative.
    bool LoadSnapshot(const std::string& relativeOrAbsolutePath);

    // -----------------------------------------------------------------
    // Live run + per-frame hash capture
    // -----------------------------------------------------------------

    /// @brief Run `frames` frames live and capture a per-frame divergence
    /// record at every frame boundary.
    ///
    /// Emulator must be paused (or freshly initialised) when called. The
    /// harness will not touch TTD state — this is the "ground truth" run.
    ///
    /// @param frames  Number of frames to execute.
    /// @return DivergenceHistory with `frames` entries.
    DivergenceHistory RunLiveAndCapture(size_t frames);

    /// @brief Extract per-frame hashes from the current TTD timeline.
    ///
    /// Each checkpoint is hashed via the same CaptureSnapshot + HashSnapshot
    /// path used by RunLiveAndCapture, so the two histories are directly
    /// comparable. Used as the "expected" for VerifyReplayMatchesLive when
    /// you want to test the restore path against the capture path (the
    /// standard divergence oracle pattern).
    ///
    /// @return DivergenceHistory with one entry per checkpoint.
    DivergenceHistory ExtractHashesFromTimeline();

    // -----------------------------------------------------------------
    // TTD recording + checkpoint capture
    // -----------------------------------------------------------------

    /// @brief Start TTD recording and run `frames` frames, capturing
    /// checkpoints into the timeline as usual.
    ///
    /// Call after RunLiveAndCapture so the workload matches exactly. The
    /// harness will use the same number of frames as the live run.
    ///
    /// @return true iff StartRecording succeeded and timeline populated.
    bool StartRecordingAndCaptureTimeline(size_t frames);

    // -----------------------------------------------------------------
    // Replay verification
    // -----------------------------------------------------------------

    /// @brief SeekTo frame `frameIndex` (frame-aligned, tInFrame=0) and
    /// verify the resulting live state matches the live-captured record.
    ///
    /// Algorithm:
    ///   1. SeekTo({frame, 0})
    ///   2. Compute current snapshot + ram_digest
    ///   3. Compare hash + digest + key snapshot fields to expected
    ///
    /// @param frameIndex  Index into the live history (0-based).
    /// @param expected    The live history captured earlier.
    /// @param[out] failureMsg If returning false, filled with a description
    ///                       of the first mismatched field.
    /// @return true iff every field matches.
    bool VerifyReplayMatchesLive(size_t frameIndex,
                                 const DivergenceHistory& expected,
                                 std::string* failureMsg = nullptr);

    // -----------------------------------------------------------------
    // Sample-frame selection
    // -----------------------------------------------------------------

    /// @brief Pick a deterministic set of frame indices to sample.
    ///
    /// @param totalFrames  Total frames available.
    /// @param step         Sampling interval (e.g. 10 → frames 0, 10, 20, ...).
    /// @return Indices to verify. Always includes the last frame.
    std::vector<size_t> PickSampleFrames(size_t totalFrames, size_t step) const;

private:
    // -------------------------------------------------------------
    // Internal capture helper — called at each frame boundary.
    // -------------------------------------------------------------
    DivergenceFrame CaptureCurrentFrame();

    // -------------------------------------------------------------
    // RAM digest — hashes every RAM page the model has. Matches the
    // Sprint 0 benchmark's HashRAM pattern so divergence numbers are
    // directly comparable.
    // -------------------------------------------------------------
    uint64_t HashRamInUse() const;

    // -------------------------------------------------------------
    // Re-apply the loaded snapshot so the next run starts from the
    // identical initial state. Called between the live-run phase and
    // the recording phase so frame_counter numbers line up 1:1.
    // -------------------------------------------------------------
    bool ReloadSnapshotForReplayPhase();

    Emulator*         _emulator    = nullptr;
    EmulatorContext*  _context     = nullptr;
    Memory*           _memory      = nullptr;

    // The path passed to the last successful LoadSnapshot() call, so
    // StartRecordingAndCaptureTimeline can re-load it and reset the
    // frame_counter to the same starting value as the live run.
    std::string       _lastSnapshotPath;
    bool              _snapshotLoaded = false;
};

} // namespace ttd
