#pragma once

/// @file timetravelmanager.h
/// @brief TimeTravelManager — facade and capture orchestrator for Time-Travel Debugging.
///
/// Per parent TDD §7.1, §10.2. This class owns:
///   - The COW page store (TTDPageStore)
///   - The timeline (std::vector<TTDCheckpoint>)
///   - Session state (Idle / Recording / Detached)
///
/// It does NOT own:
///   - The dirty tracker (owned by Memory, hooked in MemoryWriteDebug per §6.2)
///   - The CPU/chipset structs (captured from the live EmulatorState/Z80State)
///   - Peripheral devices themselves (owned by SoundManager / EmulatorContext);
///     their serialized state is written into checkpoint blobs via the
///     TTDSerializable interface (P1.5 — AY/TurboSound wired; tape/FDC/Covox
///     land one at a time)
///
/// Recording lifecycle (full state machine per TDD §4.2):
///   - StartRecording() — Idle → Recording. Captures an initial baseline
///     checkpoint so the timeline always has at least one entry to seek to.
///   - StopRecording() — Recording → Idle (history retained). The user can
///     still browse/seek the timeline until InvalidateSession() clears it.
///   - InvalidateSession(reason) — any state → Idle, history cleared. Called
///     by session-invalidation hooks (Reset / Load* / speed change) in P1.6.
///
/// Threading (per TDD §7.2):
///   - OnFrameBoundary() runs on the emulator thread (called from MainLoop::
///     OnFrameEnd) — appends to the timeline, no locks.
///   - Start/Stop/Invalidate/Seek are called from the control thread while
///     the emulator is paused (existing pause discipline; no new concurrency).
///
/// v1 simplification — never-touched handling:
///   The TDD §6.3 "lazy baseline upgrade" optimization (capture pre-first-
///   write content so never-touched-in-session pages cost zero even on
///   extended-RAM machines) is deferred. For v1, the baseline capture at
///   StartRecording interns every model-RAM page (cost: 128 KB – 4 MB,
///   proportional to model RAM size, paid once per session). This is correct
///   and matches the design's restore semantics. The working-set-proportional
///   optimization can be added later as a fast path without changing the
///   public API.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

#include "emulator/platform.h"       // PlatformModulesEnum, MAX_RAM_PAGES
#include "common/modulelogger.h"    // ModuleLogger
#include "ttd_checkpoint.h"
#include "ttd_external_events.h"
#include "ttd_input_journal.h"
#include "ttd_page_store.h"

// Forward declarations — we don't pull emulator headers into this header.
// (EmulatorContext, Memory, Z80State, EmulatorState are all classes/structs
// defined in the emulator headers; here we only need pointer types.)
class EmulatorContext;
class Memory;
struct Z80State;
struct EmulatorState;
namespace ttd { class TTDDirtyTracker; }

namespace ttd {

/// @brief Session state machine values (TDD §4.2).
enum class TTDSessionState : uint8_t
{
    Idle       = 0,  ///< No active recording. History may or may not be present.
    Recording  = 1,  ///< Capture is active; OnFrameBoundary appends checkpoints.
    Detached   = 2,  ///< Emulator paused at a historical point (future seek state).
};

/// @brief Stable string identifier for a TTDSessionState.
///
/// The values ("idle" / "recording" / "detached") are part of the public
/// automation contract per parent TDD §10.4: WebAPI JSON, Lua tables, Python
/// attributes, and CLI tokens all use these exact spellings. Defined here
/// (rather than in each surface's own adapter) so the contract lives in one
/// place and is unit-testable from core-tests.
///
/// Keep in sync with the enum order above.
const char* TTDSessionStateToString(TTDSessionState state);

/// @brief Lightweight session summary returned by GetSessionInfo().
/// Matches the shape automation clients (WebAPI/Lua/CLI) consume per TDD §10.4.
struct TTDSessionInfo
{
    TTDSessionState state = TTDSessionState::Idle;
    uint64_t sessionStartFrame = 0;   ///< Frame counter at session start
    uint64_t currentEndFrame    = 0;  ///< Last captured frame (== current position when Recording)
    size_t   checkpointCount   = 0;
    size_t   pageStoreBytes    = 0;   ///< Capacity (allocated) — for budget checks
    size_t   pageStoreUsedBytes = 0;  ///< Live slot bytes
    uint64_t baselineFramesCaptured = 0;  ///< Diagnostic
};

class TimeTravelManager
{
public:
    /// @brief Construct the manager. Does NOT start recording.
    /// @param context  Emulator context (provides Memory, EmulatorState, Z80).
    explicit TimeTravelManager(EmulatorContext* context);
    ~TimeTravelManager();

    TimeTravelManager(const TimeTravelManager&) = delete;
    TimeTravelManager& operator=(const TimeTravelManager&) = delete;

    // -----------------------------------------------------------------------
    // Session lifecycle (control thread; emulator must be paused)
    // -----------------------------------------------------------------------

    /// @brief Begin recording. Captures the baseline checkpoint.
    /// Idempotent: calling while already Recording is a no-op.
    /// @return true if recording was started (or was already active).
    bool StartRecording();

    /// @brief Stop capturing new frames. History is retained and browsable.
    /// Idempotent: calling while Idle is a no-op.
    void StopRecording();

    /// @brief Drop all captured history and return to Idle.
    /// Called by session-invalidation hooks (Reset / Load* / speed change).
    /// The reason string is logged but not stored.
    void InvalidateSession(const char* reason);

    inline bool IsRecording() const { return _state == TTDSessionState::Recording; }
    inline TTDSessionState GetState() const { return _state; }

    TTDSessionInfo GetSessionInfo() const;

    // -----------------------------------------------------------------------
    // Capture (emulator thread only)
    // -----------------------------------------------------------------------

    /// @brief Capture a checkpoint at the current frame boundary.
    ///
    /// Called from MainLoop::OnFrameEnd (and the equivalent boundary in
    /// Emulator::RunTStates). No-op if not Recording. Cost when recording:
    ///   - Dirty pages: one 16 KB Intern per dirty page (typically 2–6/frame)
    ///   - Clean pages: one AddRef each (no memcpy)
    ///   - CPU + chipset: field copies (< 2 KB)
    void OnFrameBoundary();

    // -----------------------------------------------------------------------
    // Restore path (control thread; emulator must be paused)
    // -----------------------------------------------------------------------
    //
    // Phase 2 Item 1 (parent TDD §8.1 step 2). RestoreCheckpoint applies a
    // previously-captured checkpoint back to the live emulator: CPU + chipset
    // field copies, port-latch re-apply via Memory::UpdateZ80Banks (rebuilds
    // memory banking), RAM page content memcpy from the COW page store,
    // peripheral TTDLoadState dispatch, and Screen::InitFrame.
    //
    // Does NOT advance the emulator. After this returns, the live machine
    // state matches the checkpoint's frame boundary. Phase 2 Item 3 (SeekTo)
    // adds optional intra-frame silent replay on top.
    ///
    /// @param idx Timeline index to restore. Bounds-checked.
    /// @return true on success, false if idx is out of range or the manager
    ///         is not in a restorable state (Recording / Detached).
    bool RestoreCheckpointForTesting(size_t idx);

    // -----------------------------------------------------------------------
    // Silent replay mode (control thread; emulator must be paused)
    // -----------------------------------------------------------------------
    //
    // Phase 2 Item 2 (parent TDD §8.2 + Appendix C). Wraps the emulator's
    // existing RunTStates call so that replay from a restored checkpoint to
    // an intra-frame target is *observationally silent*: breakpoints skip,
    // analyzers dispatch no-op, keyboard matrix mutation blocked, recording
    // capture skipped, video frame refresh notifications dropped, audio host
    // buffer muted (device state still advances — critical for AY
    // determinism).
    //
    // The flag itself (`_context->ttdReplayActive`) is read by every
    // suppression site — see emulatorcontext.h. EnterReplayMode / ExitReplayMode
    // also save/restore the SoundManager mute state so the host audio output
    // can be muted independently of feature flags.
    //
    // Threading: same discipline as Restore — called on the control thread
    // with the emulator paused. Replay is driven by a follow-up RunTStates
    // call on the same thread, so the flag is set/cleared around it without
    // any cross-thread visibility concern.

    /// @brief Enter silent-replay mode.
    ///
    /// Sets `_context->ttdReplayActive = true`, saves and forces the
    /// SoundManager mute state. Idempotent: a second call while already in
    /// replay is a no-op (and does NOT overwrite the saved mute state, so
    /// nesting is safe).
    void EnterReplayMode();

    /// @brief Exit silent-replay mode.
    ///
    /// Clears `_context->ttdReplayActive`, restores the SoundManager mute
    /// state captured by EnterReplayMode. Idempotent: a call while not in
    /// replay is a no-op.
    void ExitReplayMode();

    /// @brief Query the replay-mode flag. Reads `_context->ttdReplayActive`.
    /// Defined out-of-line (EmulatorContext is only forward-declared here).
    bool IsReplayActive() const;

    // -----------------------------------------------------------------------
    // Input journal (Phase 2 Item 3; parent TDD §5 row #1)
    // -----------------------------------------------------------------------
    //
    // Captures keyboard matrix mutations with their TTDTimePoint. The seek
    // engine (Item 4) replays them at the recorded timestamps during
    // intra-frame replay instead of letting live input through (which is
    // blocked by the Item 2 suppression).
    //
    // Capture call sites live in DebugKeyboardManager::PressKey/ReleaseKey,
    // guarded by `IsRecording()` and `!IsReplayActive()`. The journal is
    // dropped together with the timeline on InvalidateSession/StartRecording
    // and truncated by Resume-from-past (Item 5).

    /// @brief Append a keyboard event to the journal.
    ///
    /// Called by DebugKeyboardManager::PressKey/ReleaseKey. The current
    /// TTDTimePoint is derived from EmulatorState (frame_counter + the
    /// intra-frame t-state). No-op when not Recording or when replay is
    /// active — but the caller already gates on those, so this method
    /// doesn't double-check.
    ///
    /// @param key   ZXKeysEnum value (callers cast from the typed enum).
    /// @param pressed true for press, false for release.
    void RecordInputEvent(uint8_t key, bool pressed);

    /// @brief Read-only access to the input journal. Used by the seek engine
    /// (Item 4) and by tests.
    inline const TTDInputJournal& GetInputJournal() const { return _inputJournal; }

    /// @brief Inject every event scheduled at `now` into the live keyboard.
    ///
    /// High-level wrapper around TTDInputJournal::InjectDueEvents — looks
    /// up the Keyboard pointer from EmulatorContext so the seek engine
    /// (Item 4) doesn't need to know about peripheral plumbing. Returns 0
    /// silently if no keyboard is attached.
    ///
    /// No-op when replay is not active (defensive — the seek engine should
    /// already be inside an EnterReplayMode / ExitReplayMode pair).
    size_t InjectDueInputEvents(const TTDTimePoint& now);

    // -----------------------------------------------------------------------
    // Seek engine (Phase 2 Item 4; parent TDD §8.1)
    // -----------------------------------------------------------------------
    //
    // SeekTo applies the closest-on-or-before checkpoint, then silently
    // replays forward to the requested intra-frame position. Step helpers
    // compose SeekTo with frame-counter arithmetic. On success the session
    // transitions to Detached.
    //
    // Preconditions (enforced):
    //   - Manager state is Recording or Detached (not Idle).
    //   - Target is within session bounds: (0,0) <= target <= last checkpoint.
    //   - Emulator is paused (caller's responsibility — these are control-
    //     thread entry points, matching RestoreCheckpointForTesting).

    /// @brief Step back exactly one frame, preserving the intra-frame position.
    ///
    /// Composition of SeekTo: reads the current position from EmulatorState
    /// and seeks to (frame-1, tInFrame). No-op (returns false) if the
    /// current position is at or before the first captured frame.
    bool StepBackFrame();

    /// @brief Step forward exactly one frame, preserving the intra-frame position.
    ///
    /// Composition of SeekTo: seeks to (frame+1, tInFrame). Fails if the
    /// target frame is beyond the last captured checkpoint — the seek
    /// engine cannot replay beyond recorded history.
    bool StepForwardFrame();

    /// @brief Read the current position as a TTDTimePoint.
    ///
    /// Convenience for callers (UI, step helpers, tests). Derived from
    /// EmulatorState: `frame = frame_counter`,
    /// `tInFrame = z80->t` (per-frame accumulator — see implementation
    /// note in the .cpp about why it's not `t_states % config.frame`).
    TTDTimePoint CurrentPosition() const;

    /// @brief Upper bound of the recorded timeline.
    ///
    /// Returns the time of the last checkpoint. Seeks to any point > this
    /// will fail. Returns {0,0} when the timeline is empty.
    TTDTimePoint SessionEndPosition() const;

    // -----------------------------------------------------------------------
    // External-event markers (Phase 2 Item 6; parent TDD §5.1)
    // -----------------------------------------------------------------------
    //
    // Sources of nondeterminism that aren't covered by an input journal in
    // v1 (tape control, disk writes, debugger-initiated state edits) get
    // a marker on the timeline. Markers are replay barriers: SeekTo refuses
    // to cross them silently and surfaces the marker to the caller via
    // TTDSeekResult. This keeps TTD honest — it never pretends to reproduce
    // what it cannot. Journals (input today, tape/disk later) progressively
    // convert marker classes into replayable events.
    //
    // Item 6 v1 ships the data structure, the capture API, and the
    // SeekTo barrier logic. Hook points in Tape / BetaDisk / debugger edit
    // paths land incrementally — each new hook is a one-line call to
    // RecordExternalEvent() guarded by IsRecording().

    /// @brief Record an external-event marker at the current position.
    ///
    /// Captures `time = CurrentPosition()` (frame + z80->t), `kind`, and a
    /// truncated copy of `reason` (up to 63 chars + NUL). No-op when not
    /// Recording — the caller's guard avoids double-checking, but this
    /// method is defensive anyway.
    ///
    /// @param kind   Source classification (UI / automation hint).
    /// @param reason Short human-readable description. May be nullptr.
    void RecordExternalEvent(TTDExternalEventKind kind, const char* reason);

    /// @brief Read-only access to the marker journal. Used by tests, the UI,
    /// and automation surfaces that surface the marker list.
    inline const TTDExternalEventJournal& GetExternalEvents() const { return _externalEvents; }

    // -----------------------------------------------------------------------
    // Seek engine — barrier-aware overload (Phase 2 Item 6)
    // -----------------------------------------------------------------------

    /// @brief Why a SeekTo call stopped where it did.
    ///
    /// Mirrors the automation contract per parent TDD §10.4 / §717:
    ///   halt_reason ∈ {target, external_event, out_of_range}
    enum class TTDSeekHaltReason : uint8_t
    {
        Target        = 0,  ///< Reached the requested target normally.
        ExternalEvent = 1,  ///< Stopped at an external-event marker (Item 6).
        OutOfRange    = 2,  ///< Target was beyond the session end.
    };

    /// @brief Struct returned by the barrier-aware SeekTo overload.
    ///
    /// `reached` is true iff the emulator is now positioned at the requested
    /// target. When false, `haltReason` explains why and (if ExternalEvent)
    /// `blockingMarker` describes the barrier.
    struct TTDSeekResult
    {
        bool              reached       = false;
        TTDTimePoint      arrivedAt     {};
        TTDSeekHaltReason haltReason    = TTDSeekHaltReason::Target;
        TTDExternalEvent  blockingMarker{};  ///< Valid iff haltReason == ExternalEvent.
    };

    /// @brief SeekTo with explicit result reporting.
    ///
    /// Same algorithm as the bool overload, plus marker-barrier detection:
    /// if intra-frame replay would cross an external-event marker, the seek
    /// stops at the marker's TTDTimePoint and `outResult.haltReason` is set
    /// to ExternalEvent. The bool return value is `outResult.reached`.
    ///
    /// Frame-aligned targets (tInFrame == 0) never cross markers — the
    /// chosen checkpoint already reflects any markers at or before that
    /// frame boundary, so no replay is needed.
    ///
    /// @param target    Where to seek to.
    /// @param outResult Written on both success and failure. May be nullptr
    ///                  (the bool overload passes nullptr and discards).
    /// @return true iff `outResult.reached` (target reached without barrier).
    bool SeekTo(const TTDTimePoint& target, TTDSeekResult* outResult);

    /// @brief Compatibility SeekTo — discards the result struct.
    ///
    /// Existing callers (StepBackFrame, StepForwardFrame, ResumeRecordingFrom,
    /// tests) keep working unchanged. New callers that care about halt_reason
    /// should use the overload above.
    inline bool SeekTo(const TTDTimePoint& target)
    {
        return SeekTo(target, /*outResult=*/nullptr);
    }

    // -----------------------------------------------------------------------
    // Resume-from-past (Phase 2 Item 5; parent TDD §8.3)
    // -----------------------------------------------------------------------
    //
    // When the user, having seeked to a point T < session end, wants to
    // continue execution from T: drop everything > T from the timeline and
    // the input journal, release page refs held by dropped checkpoints,
    // transition back to Recording. The next OnFrameBoundary will capture
    // a fresh checkpoint at frame T.frame + 1.
    //
    // Atomic with respect to the UI: the caller is expected to have paused
    // the emulator (existing pause discipline — same as RestoreCheckpoint-
    // ForTesting / SeekTo).

    /// @brief Resume recording from a historical position, truncating future.
    ///
    /// Algorithm (parent TDD §8.3):
    ///   1. Validate preconditions (state, bounds).
    ///   2. SeekTo(from) — ensures the emulator is positioned at `from`.
    ///      No-op-equivalent if already there (re-restore is deterministic).
    ///   3. Release page refs for every checkpoint cp where cp.time > from.
    ///   4. Erase those checkpoints from _timeline.
    ///   5. _inputJournal.DropAfter(from).
    ///   6. _state = Recording.
    ///
    /// @param from  Position to resume from. Must be within the current
    ///              recorded timeline bounds (<= SessionEndPosition()).
    /// @return true on success. False (with a logged warning) if state is
    ///         Idle, the timeline is empty, or `from` is out of bounds.
    bool ResumeRecordingFrom(const TTDTimePoint& from);

    // -----------------------------------------------------------------------
    // Test/diagnostic accessors
    // -----------------------------------------------------------------------

    /// @brief Number of checkpoints currently in the timeline.
    inline size_t GetCheckpointCount() const { return _timeline.size(); }

    /// @brief Read-only access to a timeline entry (bounds-checked).
    /// Returns nullptr if idx is out of range.
    const TTDCheckpoint* GetCheckpoint(size_t idx) const;

    /// @brief Read-only access to the page store (for tests / budget checks).
    inline const TTDPageStore& GetPageStore() const { return _pageStore; }

    /// @brief Number of model-RAM pages (set at StartRecording from the
    /// active model's RAM size).
    inline uint16_t GetModelRamPages() const { return _modelRamPages; }

private:
    // -----------------------------------------------------------------------
    // ModuleLogger wiring (matches the Emulator / Memory pattern).
    // -----------------------------------------------------------------------
    static const PlatformModulesEnum _MODULE    = PlatformModulesEnum::MODULE_DEBUGGER;
    static const uint16_t           _SUBMODULE  = 0x0000;  // No TTD-specific submodule enum yet
    ModuleLogger* _logger = nullptr;

    // -----------------------------------------------------------------------
    // Internal capture helpers
    // -----------------------------------------------------------------------

    /// @brief Snapshot CPU + chipset + RAM pages into a fresh checkpoint at
    /// the current frame boundary. Caller pushes it onto _timeline.
    void CaptureNow(TTDCheckpoint& out);

    /// @brief Intern every model-RAM page as the baseline. Used by the first
    /// capture of a session so subsequent frames can AddRef clean pages.
    void CaptureBaselineRamPages(std::vector<TTDPageRef>& outRamPages);

    /// @brief Update outRamPages for the next checkpoint: dirty pages get
    /// freshly Intern'd, clean pages AddRef the previous checkpoint's slot.
    /// Pages beyond _modelRamPages stay NEVER_TOUCHED.
    void UpdateRamPages(const std::vector<uint16_t>& dirtyPages,
                        const std::vector<TTDPageRef>& prevRamPages,
                        std::vector<TTDPageRef>& outRamPages);

    /// @brief Release every page ref held by a checkpoint (used when
    /// invalidating or thinning).
    void ReleaseCheckpointRefs(TTDCheckpoint& cp);

    /// @brief Read the active model's RAM page count from the Memory / config.
    /// Called once at StartRecording.
    uint16_t ResolveModelRamPages() const;

    // -----------------------------------------------------------------------
    // Internal restore helpers (Phase 2 Item 1; parent TDD §8.1 step 2)
    // -----------------------------------------------------------------------

    /// @brief Apply a captured checkpoint back to the live emulator.
    ///
    /// Field copies for CPU + chipset, rebuild memory banking from port
    /// latches, memcpy RAM pages from the page store, dispatch TTDLoadState
    /// on every peripheral, then Screen::InitFrame(). Does not advance the
    /// emulator. Caller (currently RestoreCheckpointForTesting; later
    /// SeekTo) is responsible for state transitions and bookkeeping.
    ///
    /// @param cp Checkpoint to apply. Read-only; no refs are taken or released.
    void RestoreCheckpoint(const TTDCheckpoint& cp);

    /// @brief Memcpy every referenced RAM page from the page store into the
    /// live Memory backing store. Pages marked NEVER_TOUCHED are skipped
    /// (their live RAM content IS the historical content). Pages beyond
    /// _modelRamPages are skipped (they're NEVER_TOUCHED by construction).
    void RestoreRamPages(const std::vector<TTDPageRef>& ramPages);

    // -----------------------------------------------------------------------
    // Internal seek helpers (Phase 2 Item 4; parent TTD §8.1 step 3)
    // -----------------------------------------------------------------------

    /// @brief Silent intra-frame replay from a restored frame boundary to a
    /// target t-state within the same frame.
    ///
    /// Precondition: RestoreCheckpoint(cp) was just called with
    /// `cp.time.frame == targetFrame`. The live emulator's z80.t is at the
    /// frame boundary (caller syncs to 0 before calling).
    ///
    /// Drives `Emulator::RunTStates` in chunks, breaking at each journaled
    /// input event scheduled within the interval so the event can be
    /// injected at its recorded TTDTimePoint. Wraps the whole loop in
    /// EnterReplayMode / ExitReplayMode so the Item 2 suppression matrix
    /// keeps replay observationally silent.
    ///
    /// @param targetFrame  Frame index (must match the restored checkpoint).
    /// @param targetTInFrame T-state offset within the frame to stop at.
    void ReplayWithinFrame(uint64_t targetFrame, uint32_t targetTInFrame);

    // -----------------------------------------------------------------------
    // Internal resume helpers (Phase 2 Item 5; parent TDD §8.3)
    // -----------------------------------------------------------------------

    /// @brief Drop every checkpoint with `cp.time > from` and release the
    /// page refs they hold. Used by ResumeRecordingFrom.
    ///
    /// No-op if every checkpoint has time <= `from` (the common case when
    /// `from` is exactly at the last captured frame boundary).
    ///
    /// Uses the same upper_bound comparator shape as SeekTo so the two
    /// methods agree on the meaning of "strictly after".
    void TruncateTimelineAfter(const TTDTimePoint& from);

    // -----------------------------------------------------------------------
    // Dependencies (non-owning)
    // -----------------------------------------------------------------------
    EmulatorContext* _context;
    Memory*          _memory = nullptr;
    TTDDirtyTracker* _dirtyTracker = nullptr;

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    TTDSessionState _state = TTDSessionState::Idle;

    /// The recorded timeline. Appended only on the emulator thread.
    std::vector<TTDCheckpoint> _timeline;

    /// Backing COW page pool.
    TTDPageStore _pageStore;

    /// Number of physical RAM pages on the active model (set at StartRecording).
    /// Pages in [0, _modelRamPages) are captured; pages in [_modelRamPages,
    /// MAX_RAM_PAGES) are NEVER_TOUCHED.
    uint16_t _modelRamPages = 0;

    /// Reusable scratch buffer for CollectAndClear (avoids per-frame alloc).
    std::vector<uint16_t> _dirtyScratch;

    /// Input journal — keyboard matrix mutations captured for replay (Item 3).
    /// Dropped on InvalidateSession/StartRecording; truncated by Item 5
    /// Resume-from-past.
    TTDInputJournal _inputJournal;

    /// External-event journal — replay barriers for nondeterminism sources
    /// that aren't input-journaled in v1 (Item 6). Same lifecycle as the
    /// input journal: dropped on Invalidate/Start, truncated by Resume.
    TTDExternalEventJournal _externalEvents;

    // -----------------------------------------------------------------------
    // Replay-mode state (Phase 2 Item 2; parent TDD §8.2)
    // -----------------------------------------------------------------------

    /// True while inside EnterReplayMode / ExitReplayMode — gates the
    /// save/restore of `_soundMuteBeforeReplay` so nested calls are safe.
    bool _inReplayMode = false;

    /// SoundManager mute state as it was before EnterReplayMode forced it
    /// true. Restored by ExitReplayMode. Only meaningful while `_inReplayMode`.
    bool _soundMuteBeforeReplay = false;
};

} // namespace ttd
