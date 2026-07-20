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
