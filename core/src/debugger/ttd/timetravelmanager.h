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

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>
#include <string>

#include "emulator/platform.h"       // PlatformModulesEnum, MAX_RAM_PAGES
#include "common/modulelogger.h"    // ModuleLogger
#include "ttd_checkpoint.h"
#include "ttd_external_events.h"
#include "ttd_input_journal.h"
#include "ttd_write_journal.h"
#include "ttd_probe.h"
#include "ttd_codec_page_store.h"

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
    uint64_t baselineFramesCaptured = 0;  ///< Live page slots (distinct RAM snapshots in store)

    /// @brief Total heap footprint of the recorded session, in bytes.
    ///
    /// Real counter (not an estimate, not a percentage). Sums every
    /// allocation the session owns:
    ///   - page store backing (allocated vector capacity × page size)
    ///   - per-checkpoint struct + peripheral blob + page-ref vector
    ///   - input journal + external-event journal backing
    ///   - session-scope dirty-page scratch buffer
    ///
    /// This is the number to display when a user asks "how much memory is
    /// my recording consuming right now?". Distinct from pageStoreBytes
    /// (which is just the page-store vector) and pageStoreUsedBytes (which
    /// counts only live slots, not free-list capacity that's still
    /// allocated).
    size_t   sessionHeapBytes = 0;

    // --- Phase 5 codec telemetry (XOR+zstd-1 compression) ---
    uint64_t keyFrameCount    = 0;  ///< Number of I-frames captured
    uint64_t deltaFrameCount  = 0;  ///< Number of P-frames captured
    double   compressionRatio = 1.0; ///< kPageSize / mean(payload) across live slots
    size_t   livePayloadBytes = 0;  ///< Sum of compressed payload bytes (live slots)
};

class TimeTravelManager
{
public:
    /// @brief I-frame / P-frame discriminator.
    ///
    /// Per Phase 5 PoC: every K-th frame is a key frame (full RAM snapshot);
    /// frames in between are delta frames (only dirty pages, XOR-encoded).
    /// K=50 gives ~2 ms average seek and ~50 ms worst case (full chain walk).
    /// See docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md
    static constexpr uint32_t kKeyFrameInterval = 50;

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
    // Session serialization (.ttd format) — universal capability
    // -----------------------------------------------------------------------
    //
    // The .ttd binary format is the portable contract between every TTD
    // consumer: core tests, CLI tools, WebAPI wrappers, the Python analyzer,
    // and any third-party tool that generates a parser from the published
    // Kaitai schema (ttd.ksy).
    //
    // Read-only with respect to the live recording: SerializeSession does
    // not invalidate, thin, or otherwise mutate the timeline. The caller is
    // expected to have paused the emulator so the timeline is stable for
    // the duration of the serialize call.

    /// @brief Serialize the current timeline + page store to a .ttd stream.
    ///
    /// Universal: callable from core tests, the CLI, or a WebAPI handler.
    /// Read-only — does not invalidate or thin the live recording.
    ///
    /// @param out  Output stream. Written sequentially; no seeks.
    /// @param err  Filled with a human-readable message on failure.
    /// @return true on success, false on I/O error.
    ///
    /// Pre: caller holds the emulator lock (state is stable for the duration).
    ///      The timeline may be empty (produces a valid zero-checkpoint dump).
    bool SerializeSession(std::ostream& out, std::string& err) const;

    /// @brief Restore a timeline + page store from a .ttd stream.
    ///
    /// Replaces the current session entirely. Used by the round-trip test
    /// and by future replay/restore tools. Clears any existing timeline,
    /// resets the page store, then materializes the file's contents.
    ///
    /// @param in   Input stream. Read sequentially; no seeks.
    /// @param err  Filled with a human-readable message on failure.
    /// @return true on success, false on I/O or format error.
    ///
    /// Pre: caller has paused the emulator. The session state is set to
    ///      Idle after a successful load (the file does not carry live
    ///      recording state; callers that want Detached can call
    ///      SeekTo to position the emulator at any checkpoint).
    ///
    /// Refuses unknown future schema versions with a clear error message
    /// (see ttd_dump_format.h::kMaxSupportedSchemaVersion).
    bool DeserializeSession(std::istream& in, std::string& err);

    /// @brief In-memory capture/restore divergence self-test.
    ///
    /// Captures the current live state as a checkpoint, immediately restores
    /// it, and reports whether the architectural machine state (CPU + chipset
    /// + RAM content) matches. Single-frame, deterministic.
    ///
    /// Used by the analyzer to distinguish capture-side bugs from restore-
    /// side bugs without needing two emulators or a long timeline. If this
    /// fails on a single checkpoint, RestoreCheckpoint itself is broken.
    /// If it passes but seek shows drift after N frames, the issue is in
    /// capture or in multi-frame state evolution.
    struct SelfTestResult
    {
        bool   pre_post_match = false;  ///< True iff live state hashes match.
        uint64_t pre_hash     = 0;      ///< 64-bit hash before capture.
        uint64_t post_hash    = 0;      ///< 64-bit hash after restore.
        std::string notes;              ///< Human-readable summary / failure details.
    };
    SelfTestResult CaptureRestoreSelfTest();

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

    /// @brief Test whether OnFrameBoundary auto-paused the emulator and
    ///        clear the request.
    ///
    /// When the session is Detached (post-seek) and the emulator is
    /// resumed, OnFrameBoundary watches the live frame counter. The first
    /// boundary past SessionEndPosition() triggers an Emulator::Pause()
    /// call AND sets this flag. Production code never needs to read the
    /// flag — Pause() is sufficient — but tests that drive the emulator
    /// synchronously (where Pause() is a no-op because the emulator isn't
    /// async-running) need this flag to observe that the auto-pause path
    /// was reached.
    ///
    /// @return true iff an auto-pause request has fired since the last
    ///         call. The flag is also cleared by StartRecording /
    ///         InvalidateSession / SeekTo so each Detached→resume window
    ///         starts with a clean signal.
    bool ConsumeAutoPauseRequest();

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
    // Phase 4: Reverse search (parent TDD §9 + §10.4)
    // -----------------------------------------------------------------------
    //
    // Two-layer reverse-watchpoint engine:
    //   1. Write journal (§9.3): 256 MB ring of TTDWriteRecord, scanned
    //      backward for the fast path. Memory/port writes append to it from
    //      the existing MemoryWriteDebug / DecodePortOut hooks.
    //   2. Two-pass silent replay (§9.2): the fallback when the ring has
    //      wrapped past the query window. Restores each checkpoint interval
    //      with the access probe armed; the probe records every hit.
    //
    // On top of the search engine, StepBackInstruction / StepForwardInstruction
    // provide single-M1 navigation (TDD §10.2 + §16 row 2).
    
    /// @brief Hot-path capture: record a memory write.
    ///
    /// Called from Memory::MemoryWriteDebug when TTD is enabled and recording
    /// is active. Builds a TTDWriteRecord from the current frame/t-state
    /// (EmulatorState) + the write's address/value/PC/physical-page and
    /// appends it to the write journal. Also arms the access probe when a
    /// search is in flight (probe state lives in EmulatorContext).
    ///
    /// No-op when not Recording or when replay is active — replay-driven
    /// writes must NOT pollute the journal (they're reconstructions, not
    /// new history).
    void RecordMemoryWrite(uint16_t addr, uint8_t oldVal, uint8_t newVal,
                           uint16_t m1pc, uint8_t physPage);
    
    /// @brief Hot-path capture: record a port OUT (used for IO probe).
    ///
    /// Same threading/lifecycle as RecordMemoryWrite but for port writes
    /// (decoder::DecodePortOut path). Marked with isIo=1 in the record.
    void RecordIoWrite(uint16_t port, uint8_t value, uint16_t m1pc);
    
    /// @brief Reverse-search entry point (TDD §9.1).
    ///
    /// Returns the most recent TTDSearchResult matching the query before
    /// `query.beforeGlobalT`, or std::nullopt if no match exists in the
    /// recorded history. Honors external-event markers (TDD §5.1): if the
    /// search would cross a marker, returns std::nullopt and (if non-null)
    /// fills *outBlockingMarker with the barrier.
    ///
    /// Preconditions: emulator paused, state is Recording or Detached.
    std::optional<TTDSearchResult> FindLastAccess(
        const TTDSearchQuery& query,
        TTDExternalEvent* outBlockingMarker = nullptr);
    
    /// @brief Step back one instruction (TDD §10.2 + §16 row 2).
    ///
    /// Implemented as FindLastAccess(Execute, before=currentGlobalT) followed
    /// by SeekTo(result.time) on a hit. Returns false if there is no earlier
    /// instruction in the recorded history.
    bool StepBackInstruction();
    
    /// @brief Step forward one instruction (TDD §10.2).
    ///
    /// Only valid when Detached: runs a single M1 cycle via silent replay
    /// from the current position. Returns false when at or past the session
    /// end (no further history).
    bool StepForwardInstruction();

    // -----------------------------------------------------------------------
    // Phase 4 reverse execution (extends single-opcode step helpers with
    // multi-step / reverse-continue primitives).
    //
    // The single-opcode StepBackInstruction above internally calls
    // FindLastAccess(Execute) which restore+replays once per call. For N
    // backward opcodes, that's N independent restore+replay passes —
    // wasteful when the N target M1 cycles all live within (or near) the
    // same frame interval.
    //
    // The reverse execution primitives use a smarter strategy: do ONE
    // silent-replay pass over the interval [target_start, currentGlobalT],
    // recording every M1 cycle in a vector, then either index the Nth-
    /// from-end record (ReverseStepInstructions / ReverseStepTStates) or
    // scan backward for the first PC match (ReverseContinue).
    //
    // Strategy selection (constants below):
    //   - n <= kReverseSeqStepMaxN:       delegate to N × StepBackInstruction
    //                                     (no enumeration overhead)
    //   - kReverseSeqStepMaxN < n <= large: M1 enumeration + index/scan
    //
    // Thresholds are pinned by `core/benchmarks/debugger/ttd/
    // ttd_reverse_benchmark.cpp` — see Stage C of the reverse-execution
    // phase plan.
    // -----------------------------------------------------------------------

    /// @brief Adaptive per-strategy cutoffs.
    ///
    /// Tuned by `core/benchmarks/debugger/ttd/ttd_reverse_benchmark.cpp`.
    /// See `docs/inprogress/2026-07-19-time-travel/phase-4-reverse-execution.md`
    /// for the benchmark table and threshold rationale.
    ///
    /// @note Calibrated for Release builds. In Debug the per-call overhead
    ///       of A_seq (one restore+replay per step) is ~30 ms, so the
    ///       crossover to B_m1list is at N=2; in Release it's at N=4.
    ///       Production binaries run Release, so we use 4.
    static constexpr uint32_t kReverseSeqStepMaxN  = 4;   // N ≤ this → A_seq (repeated StepBackInstruction)
    static constexpr uint32_t kReverseM1ListLargeN = 64;  // N ≥ this → B_m1list is decisively faster (≥ 2×)

    /// @brief Step back N instructions (M1 boundaries) in one call.
    ///
    /// For n <= kReverseSeqStepMaxN:   delegates to repeated StepBackInstruction().
    /// For n > kReverseSeqStepMaxN:    single M1-enumeration pass + index Nth-from-end.
    ///
    /// @return true on success. False (with a warning log) if the manager
    ///         is Recording, the timeline is empty, the current position is
    ///         at session start, or fewer than n instructions exist before
    ///         the current position.
    bool ReverseStepInstructions(uint32_t n);

    /// @brief Step back N t-states, landing at the nearest M1 cycle
    ///        whose globalT <= (currentGlobalT - n).
    ///
    /// Z80 has no observable state between M1 cycles, so landing mid-
    /// instruction is meaningless; this primitive always lands on a clean
    /// instruction boundary.
    ///
    /// Internally: enumerate M1 records over [startGlobalT, currentGlobalT],
    /// where startGlobalT is comfortably below (currentGlobalT - n) to ensure
    /// the landing M1 is captured; pick the last M1 whose globalT <= target.
    ///
    /// @return true on success. False at session start, in Recording state,
    ///         or when no M1 record ≤ target exists.
    bool ReverseStepTStates(uint64_t n);

    /// @brief Result struct returned by ReverseContinue.
    struct TTDReverseContinueResult
    {
        bool           matched   = false;
        uint16_t       pc        = 0xFFFF;   ///< Valid iff matched.
        TTDTimePoint   arrivedAt{};          ///< Where the emulator landed.
        TTDExternalEvent blockingMarker{};   ///< Set iff a barrier halted the scan.
    };

    /// @brief Run backward until any PC in `breakpoints` matches.
    ///
    /// Enumerates every M1 cycle from session start (or first barrier) up to
    /// the current position in a single forward silent-replay pass, then
    /// scans the resulting vector backward for the first PC hit. The
    /// emulator is positioned at the hit (or at the blocking marker).
    ///
    /// This is the primitive GDB G3 will eventually wrap as
    /// `reverse-continue` over RSP.
    ///
    /// @param breakpoints  Set of PC values to match. Empty set returns
    ///                     {matched=false} immediately.
    /// @return See TTDReverseContinueResult.
    TTDReverseContinueResult ReverseContinue(const std::vector<uint16_t>& breakpoints);

    /// @brief Read-only accessor for the write journal (for serialization
    /// and tests).
    inline const TTDWriteJournal& GetWriteJournal() const { return _writeJournal; }
    
    // -----------------------------------------------------------------------
    // Test/diagnostic accessors
    // -----------------------------------------------------------------------
    
    /// @brief Number of checkpoints currently in the timeline.
    inline size_t GetCheckpointCount() const { return _timeline.size(); }
    
    /// @brief Read-only access to a timeline entry (bounds-checked).
    /// Returns nullptr if idx is out of range.
    const TTDCheckpoint* GetCheckpoint(size_t idx) const;

    /// @brief Read-only access to the page store (for tests / budget checks).
    inline const TTDCodecPageStore& GetPageStore() const { return _pageStore; }

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
    ///
    /// @param isKeyFrame  When true, every model RAM page is re-interned as
    ///                   Full (I-frame path). When false, only dirty pages
    ///                   are touched (P-frame path).
    void UpdateRamPages(const std::vector<uint16_t>& dirtyPages,
                        const std::vector<TTDPageRef>& prevRamPages,
                        std::vector<TTDPageRef>& outRamPages,
                        bool isKeyFrame);

    /// @brief Release every page ref held by a checkpoint (used when
    /// invalidating or thinning).
    void ReleaseCheckpointRefs(TTDCheckpoint& cp);

    /// @brief Read the active model's RAM page count from the Memory / config.
    /// Called once at StartRecording.
    uint16_t ResolveModelRamPages() const;

    /// @brief Compute the real heap footprint of the recorded session.
    ///
    /// Sums every allocation the session owns (page store backing,
    /// per-checkpoint struct + peripheral blobs + page-ref vectors,
    /// input/external-event journals, session-scope dirty scratch).
    /// Used by GetSessionInfo so callers (WebAPI/UI) get a single number
    /// that reflects actual memory consumption — not the misleading
    /// page-store percentage (which is always ~100% because the COW store
    /// auto-grows to fit the working set).
    size_t EstimateSessionHeapBytes() const;

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

    /// @brief Internal seek implementation without the Recording-state guard.
    ///
    /// Used by public SeekTo (which adds the guard) and by ResumeRecordingFrom
    /// (which legitimately seeks during Recording — it controls the timeline
    /// truncation itself, so the sorted invariant is preserved).
    bool SeekToInternal(const TTDTimePoint& target, TTDSeekResult* outResult);

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
    // Phase 4 reverse execution: M1 enumeration helper (private).
    // -----------------------------------------------------------------------
    //
    // Enumerates every M1 cycle in [startGlobalT, endGlobalT) into outM1s.
    // Walks checkpoint intervals backward from the one containing endGlobalT
    // until reaching the one containing startGlobalT (or a barrier). For
    // each interval: restore the checkpoint, arm the Execute probe with
    // full address range, silent-replay forward to the interval end, extract
    // hits, prepend them to outM1s in time order. Stops at the first marker
    // barrier; the caller (ReverseStep*/ReverseContinue) decides what to do
    // with the partial results.
    //
    // Note: doesn't SeekTo anywhere — the caller consumes the M1 list and
    // then SeekTos the chosen record's globalT. The probe is disarmed on
    // return (matches FindLastAccess discipline).
    //
    // Returns the blocking marker (if any) via outBlockingMarker. Returns
    // the globalT of the earliest interval scanned via outEarliestScannedGlobalT
    // so ReverseStepInstructions can tell the difference between "nothing in
    // range" and "the range was clipped by a barrier / session start".
    struct EnumerateResult
    {
        const TTDExternalEvent* barrier = nullptr;            ///< Non-owning; valid only during the caller's stack frame.
        uint64_t earliestScannedGlobalT = 0;                  ///< Lowest globalT actually inspected.
    };
    EnumerateResult EnumerateM1InRange(uint64_t startGlobalT,
                                       uint64_t endGlobalT,
                                       std::vector<TTDM1Record>& outM1s,
                                       TTDExternalEvent* outBlockingMarkerStorage);

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

    /// Backing codec page store (4 KB pages, XOR+zstd-1 compression).
    TTDCodecPageStore _pageStore;

    /// Last captured keyframe index. P-frames between this and the next
    /// I-frame restore by walking deltas from this anchor. Updated on
    /// every OnFrameBoundary when an I-frame is emitted.
    uint64_t _lastKeyFrameIdx = 0;

    /// Force the next OnFrameBoundary capture to be an I-frame regardless
    /// of the periodic interval. Set by Reset / Load / debugger-edit hooks
    /// (anything that introduces non-deterministic state the codec can't
    /// reconstruct from deltas).
    bool _forceNextKeyFrame = true;

    /// Materialized-RAM cache: when restoring P-frames in sequence, we
    /// keep the most-recent fully-reconstructed RAM image here so the next
    /// restore can walk only the *new* deltas from the cached anchor
    /// instead of decompressing the full delta chain back to the I-frame.
    ///
    /// Invalidation: any write to live RAM (capture path, live run)
    /// invalidates this. Only valid while _state == Detached.
    struct MaterializedRamCache {
        bool        valid = false;
        uint64_t    frame = 0;      ///< Frame index that this cache represents
        std::vector<uint8_t> ram;   ///< _modelRamPages × PAGE_SIZE bytes
    };
    MaterializedRamCache _ramCache;

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

    /// Write journal — fast-path accelerator for FindLastAccess (Phase 4;
    /// parent TDD §9.3). 256 MB ring of 12-byte TTDWriteRecords. Appended
    /// from MemoryWriteDebug / DecodePortOut hooks. Same lifecycle as the
    /// other journals: dropped on Invalidate/Start, truncated by Resume.
    TTDWriteJournal _writeJournal;

    // -----------------------------------------------------------------------
    // Replay-mode state (Phase 2 Item 2; parent TDD §8.2)
    // -----------------------------------------------------------------------

    /// True while inside EnterReplayMode / ExitReplayMode — gates the
    /// save/restore of `_soundMuteBeforeReplay` so nested calls are safe.
    bool _inReplayMode = false;

    /// SoundManager mute state as it was before EnterReplayMode forced it
    /// true. Restored by ExitReplayMode. Only meaningful while `_inReplayMode`.
    bool _soundMuteBeforeReplay = false;

    // -----------------------------------------------------------------------
    // Auto-pause at session end (Detached state)
    // -----------------------------------------------------------------------
    ///
    /// Set by OnFrameBoundary when state == Detached and the live frame
    /// counter has just exceeded SessionEndPosition(). Read and cleared by
    /// ConsumeAutoPauseRequest(). Atomic because OnFrameBoundary runs on
    /// the emulator thread while callers (tests, UI) typically read from
    /// the control thread.
    std::atomic<bool> _autoPauseRequested{false};

    // -----------------------------------------------------------------------
    // Feature-flag stewardship
    // -----------------------------------------------------------------------
    //
    // StartRecording requires both Features::kDebugMode (so Core uses
    // UseDebugMemoryInterface, which routes writes through MemoryWriteDebug
    // where TTDDirtyTracker::MarkDirty is invoked) and Features::kTimeTravel
    // (so Memory's cached _feature_ttd_enabled flag is true).
    //
    // If either is OFF when StartRecording is called, TTD flips it ON via
    // FeatureManager::setFeature (which cascades through onFeatureChanged
    // -> UseDebugMemoryInterface + Memory::UpdateFeatureCache). StopRecording
    // restores the prior state, but only for flags we actually toggled —
    // pre-existing user/debugger debug mode is left intact.
    //
    // Toggled flag (true == we turned it ON, so we turn it back OFF on stop).
    bool _toggledDebugModeOn = false;
    bool _toggledTimeTravelOn = false;
};

} // namespace ttd
