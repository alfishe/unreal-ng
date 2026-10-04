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
///   - OnFrameBoundary() runs on the thread driving the frame (called from
///     MainLoop::CompleteFrame) — appends to the timeline, no locks.
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
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <string>

#include "emulator/platform.h"       // PlatformModulesEnum, MAX_RAM_PAGES
#include "emulator/io/keyboard/keyboard.h"  // Keyboard::InputState (display sandbox)
#include "common/modulelogger.h"    // ModuleLogger
#include "emulator/sound/soundmanager.h"  // SoundManager::HostOutputHold (replay hold)
#include "ttdcheckpoint.h"
#include "ttdexternalevents.h"
#include "ttdfileinfo.h"
#include "ttdbookmarks.h"
#include "ttdinputjournal.h"
#include "ttdwritejournal.h"
#include "ttdprobe.h"
#include "ttdcodecpagestore.h"
#include "ttdcoverageindex.h"
#include "ttdperipheralregistry.h"
#include "ttdportjournal.h"
#include "ttdportsearch.h"
#include "timetravelframecache.h"

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

/// @brief Recording mode: which surface owns the session lifecycle.
///
/// Session (default) — the classic record/browse/seek/resume/.ttd flow
/// used by the scrubber, WebAPI and CLI: StartRecording wipes and
/// re-baselines, GetFrameCache is replay-scope only (never while
/// Recording).
///
/// DebuggerLive — entered by debugger sessions (DeZog DZRP/ZRCP via the
/// shared adapter): recording is continuous across browse cycles, and
/// GetFrameCache may build while Recording when the emulator is paused
/// (mode-scoped invariant relaxation; SeekTo/StepBack stay forbidden
/// while Recording). See docs/inprogress/2026-08-27-dezog-integration/
/// reverse-debugging.md §6.
enum class TTDRecordMode : uint8_t
{
    Session      = 0,  ///< Classic .ttd session (scrubber/WebAPI/CLI).
    DebuggerLive = 1,  ///< Always-recording live history for debuggers.
};

/// @brief Lightweight session summary returned by GetSessionInfo().
/// Matches the shape automation clients (WebAPI/Lua/CLI) consume per TDD §10.4.
/// Timings of the last capture and the last restore, in nanoseconds (the
/// benchmark harness, PLAN #40 Phase 0, Step 2, BM-2 / BM-6). Two clock reads per frame
/// while recording and a handful per restore - free next to a frame's work
/// Work the last frame capture did, counted rather than timed. For a
/// replayable workload the counts repeat exactly on every host and under any
/// load, so the TTD CI gate (core/tests/debugger/ttd/bench) compares them
/// against its baseline like byte counts: a capture path that starts walking,
/// copying or compressing more shows up without a clock
struct TTDCaptureWork
{
    uint64_t pagesVisited = 0;        ///< RAM pages the capture walked
    uint64_t deltaBaseBytes = 0;      ///< bytes copied into the delta base (_prevPageCache)
    uint64_t deviceBlobBytes = 0;     ///< device-state bytes stored
    uint64_t bytesScanned = 0;        ///< page store: bytes XOR'd and zero-checked
    uint64_t compressCalls = 0;       ///< page store: zstd calls
    uint64_t compressInputBytes = 0;  ///< page store: bytes handed to zstd
    uint64_t slotsDecoded = 0;        ///< page store: chain links decoded
};

struct TTDPerfCounters
{
    uint64_t lastCaptureNs = 0;              ///< checkpoint capture of the last frame (incl. coverage seal)
    uint64_t lastRestoreCpuChipsetNs = 0;    ///< CPU registers + chipset latches + frame timing
    uint64_t lastRestoreDevicesNs = 0;       ///< peripheral blobs (model state, sound, disk, ...)
    uint64_t lastRestoreMemoryNs = 0;        ///< bank rebuild + RAM pages from the page store
    uint64_t lastRestoreScreenNs = 0;        ///< screen state resync
    uint64_t lastReplayNs = 0;               ///< intra-frame re-execution of the last seek (0 = frame-aligned)
    uint64_t lastPresentNs = 0;              ///< picture of the last seek's position (ComposeDisplay + publish)
    TTDCaptureWork lastCaptureWork;          ///< counted work of the last capture (deterministic)
    uint64_t lastRestoreTotalNs() const
    {
        return lastRestoreCpuChipsetNs + lastRestoreDevicesNs + lastRestoreMemoryNs + lastRestoreScreenNs;
    }
};

/// Where the session heap goes (sessionHeapBytes = Total()). Allocated
/// bytes. The page-store payloads are split exactly into content
/// (ramPayload) and unused allocation (ramPayloadSlack), two parts of the
/// sum; the other *Slack fields name the unused allocation inside the part
/// before them and are not added again
struct TTDHeapBreakdown
{
    size_t pageStoreTable = 0;    ///< slot table, free list, decode scratch
    size_t ramPayload = 0;        ///< compressed piece content (live slots)
    size_t ramPayloadSlack = 0;   ///< allocated payload capacity beyond the content (and free slots')
    size_t checkpoints = 0;       ///< the checkpoint structs themselves
    size_t pageRefs = 0;          ///< per-checkpoint page reference tables (capacity)
    size_t deviceBlobs = 0;       ///< per-checkpoint device-state blobs (capacity)
    size_t inputJournals = 0;     ///< input + external-event journals, dirty-page scratch
    size_t writeJournal = 0;      ///< committed ring chunks
    size_t writeJournalSlack = 0; ///< committed chunk space not holding a record yet
    size_t coverage = 0;
    size_t coverageSlack = 0;     ///< unused capacity of compressed coverage blocks
    size_t portReads = 0;
    size_t portWrites = 0;
    size_t portJournalSlack = 0;  ///< unused capacity of compressed port blocks (reads + writes)
    size_t frameCache = 0;

    /// ramPayloadSlack is a part of its own; the other slack fields are not added again
    size_t Total() const
    {
        return pageStoreTable + ramPayload + ramPayloadSlack + checkpoints + pageRefs + deviceBlobs + inputJournals +
               writeJournal + coverage + portReads + portWrites + frameCache;
    }
};

struct TTDSessionInfo
{
    TTDSessionState state = TTDSessionState::Idle;
    uint64_t sessionStartFrame = 0;   ///< Frame counter at session start
    uint64_t currentEndFrame    = 0;  ///< Last captured frame (== current position when Recording)
    size_t   checkpointCount   = 0;
    size_t   pageStoreBytes    = 0;   ///< Capacity (allocated) — for budget checks
    size_t   pageStoreUsedBytes = 0;  ///< Live slot bytes
    uint64_t historyLimitFrames = 0;  ///< Oldest frames released beyond this many checkpoints (0 = no limit)
    uint64_t historyLimitBytes  = 0;  ///< ... or beyond this many bytes of checkpoint data (0 = no limit)
    uint64_t evictedCheckpoints = 0;  ///< Checkpoints the history limit released since the session started
    uint64_t historyBytes       = 0;  ///< What the byte limit measures (TimeTravelManager::HistoryBytes)
    uint64_t baselineFramesCaptured = 0;  ///< Live page slots (distinct RAM snapshots in store)

    /// @brief Total heap footprint of the recorded session, in bytes.
    ///
    /// Real counter (not an estimate, not a percentage). Sums every
    /// allocation the session owns:
    ///   - page store: slot table + every compressed page payload
    ///   - per-checkpoint struct + peripheral blob + page-ref vector
    ///   - input journal + external-event journal backing
    ///   - session-scope dirty-page scratch buffer
    ///   - write journal (committed ring chunks), coverage index, frame cache
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

    bool writeJournalEnabled = false;  ///< True if write journal is active (for FindLast)

    /// The journal holds every write/port write of the session since its start,
    /// so write/io find-last answers from it instead of replaying.
    bool writeJournalComplete = false;
    /// The journal ring dropped its oldest records: a "no match" from it is not
    /// final and falls back to replay (a match is still exact).
    bool writeJournalWrapped = false;
    /// Why the journal stopped covering the session; empty while complete.
    std::string journalGapReason;
    /// Where it stopped (known for gaps made live; not for a loaded file).
    bool journalGapHasPosition = false;
    TTDTimePoint journalGapAt{};

    // --- Provenance -------------------------------------------------------
    //
    // "Is this something I just recorded, or something I opened?" is the first
    // question when a session is handed around, and nothing in the numbers
    // above answers it: a loaded session and a live one look identical.

    /// True when the timeline came from a file rather than from live capture.
    bool loadedFromFile = false;

    /// Path the session was loaded from. Empty for live recordings.
    std::string sourcePath;

    /// Wall-clock capture time recorded in the file (ms since Unix epoch).
    /// Zero for a live recording, which has not been written anywhere yet.
    uint64_t capturedAtUnixMs = 0;

    // --- Machine ----------------------------------------------------------

    uint8_t  modelId = 0;         ///< eModel value the session belongs to
    uint16_t modelRamPages = 0;   ///< Exclusive RAM page-index bound (see TDD 6.2a)

    /// The machine the session was recorded on, as a .ttd file states it
    /// (ttdfileinfo.h): model, ROM signature (the file's when loaded, the live
    /// ROM's otherwise), the fitted devices of the baseline checkpoint and the
    /// General Sound / TurboSound slot devices among them. Empty (modelId 0,
    /// no devices) while there is no session.
    ttd::TTDRecordedMachine machine;

    /// Symbolic id of the instance that recorded a loaded session (the file's
    /// emulator_id); empty for a live recording.
    std::string recordedBy;

    // --- Sections ---------------------------------------------------------

    uint64_t writeJournalRecords = 0;  ///< Records currently held in the ring
    size_t   writeJournalBytes = 0;    ///< Their in-memory footprint

    /// Coverage index: frames indexed and bytes held. Zero when the session
    /// carries no index — correct but slower for reverse queries.
    size_t coverageIndexFrames = 0;
    size_t coverageIndexBytes = 0;

    /// Advisory bookmarks currently held (TD-4). Zero is a session without
    /// annotations — complete and correct.
    size_t bookmarkCount = 0;

    /// Replay inputs of the session: input events (keyboard, mouse, GS host
    /// stimuli) and external-event markers (replay barriers) currently held.
    size_t inputEventCount = 0;
    size_t externalEventCount = 0;

    /// False only for a session loaded from a file written before inputs and
    /// markers were saved (.ttd header bits 6-7 absent): replay inside a frame
    /// runs without the recorded input and may differ from the recording, and
    /// seeks may cross points replay cannot reproduce. Restoring a checkpoint
    /// itself stays exact.
    bool inputHistoryComplete = true;

    /// Port-read journal (ttd-port-read-journal.md): true when the session
    /// holds every IN result of its history, so replay feeds the CPU recorded
    /// values and needs no media or host device. False on configurations the
    /// first version does not isolate (portJournalOffReason says which) and
    /// for files without the section.
    bool portJournalActive = false;
    std::string portJournalOffReason;
    uint64_t portReadCount = 0;        ///< IN results recorded
    uint64_t portWriteCount = 0;       ///< OUTs recorded
    size_t portJournalBytes = 0;       ///< both journals' size in a .ttd file
    /// Replayed reads whose live device answer differed from the recording
    /// (the CPU got the recorded value): a changed or missing medium, a host
    /// device. portReplayDivergences counts replayed INs and OUTs at another
    /// time, from another instruction or to another port, and OUTs of another
    /// value: execution itself left the recording (expected 0)
    uint64_t portReplayValueMismatches = 0;
    uint64_t portReplayDivergences = 0;

    /// Why the last session with history was dropped (the InvalidateSession
    /// reason, e.g. a device TTD cannot follow ending a recording); empty
    /// when none was dropped in this run.
    std::string lastDropReason;

    /// Why time travel is not available for this instance at all (for example
    /// a member of a ZX-Poly machine); empty when it is available.
    std::string unavailableReason;
};

/// @brief Actions that would end, wipe or corrupt a recording in progress.
/// While TTD records they are refused (TimeTravelManager::RecordingGuard):
/// stop the recording first. A machine reset is not one of them - it stops
/// the recording and keeps the history.
enum class TTDGuardedAction : uint8_t
{
    LoadSnapshot,        ///< replaces the whole machine state
    LoadTape,            ///< a new medium
    LoadDisk,            ///< a new medium
    CreateDisk,          ///< a new medium
    LoadRom,             ///< the code every checkpoint relies on
    Invalidate,          ///< discards the session
    DisableTimeTravel,   ///< capture stops mid-session
    DisableDebugMode,    ///< writes stop reaching the history
    ChangeWriteJournal,  ///< a recording keeps the journal mode it started with
    SwitchGsCard,        ///< a General Sound personality switch changes the device set (FR-4)
    CdFrontPanel         ///< a CD drive's play / pause / stop / volume from outside the guest: not in the journal
};

/// @brief String conversion for TTDCoverageKind.
inline const char* TTDCoverageKindToString(TTDCoverageKind kind)
{
    switch (kind)
    {
        case TTDCoverageKind::Executed: return "executed";
        case TTDCoverageKind::Written:  return "written";
        case TTDCoverageKind::Read:     return "read";
        default:                        return "unknown";
    }
}

/// @brief Parse TTDCoverageKind from string ("executed"/"exec", "written"/"write", "read").
inline bool TTDCoverageKindFromString(const std::string& str, TTDCoverageKind& outKind)
{
    if (str == "executed" || str == "exec" || str == "execute")
    {
        outKind = TTDCoverageKind::Executed;
        return true;
    }
    if (str == "written" || str == "write")
    {
        outKind = TTDCoverageKind::Written;
        return true;
    }
    if (str == "read")
    {
        outKind = TTDCoverageKind::Read;
        return true;
    }
    return false;
}

/// @brief Result of a coverage probe query (TD-7 §3.1.1).
struct TTDCoverageProbeResult
{
    uint64_t frame = 0;
    TTDCoverageKind kind = TTDCoverageKind::Executed;
    uint16_t addrFrom = 0;
    uint16_t addrTo = 0xFFFF;
    std::optional<PhysPage> physPage;  ///< 0..255, or kPhysPageNone for the ROM/no-page bucket
    bool touched = false;
    bool indexAvailable = false;
};

/// @brief Result of a coverage scan query (TD-7 §3.1.2).
struct TTDCoverageScanResult
{
    TTDCoverageKind kind = TTDCoverageKind::Executed;
    uint16_t addrFrom = 0;
    uint16_t addrTo = 0xFFFF;
    std::optional<PhysPage> physPage;  ///< 0..255, or kPhysPageNone for the ROM/no-page bucket
    uint64_t scannedFrames = 0;
    uint64_t matchingFrames = 0;
    std::vector<uint64_t> frames;
    uint64_t firstMatch = 0;
    uint64_t lastMatch = 0;
    uint64_t coveredFrom = 0;                ///< First frame the index covers; the query window is clamped to it
    uint64_t coveredTo = 0;                  ///< Last frame the index covers
    bool truncated = false;
    bool indexAvailable = false;
};

/// @brief One bucket in a coverage summary query (TD-7 §3.1.3).
struct TTDCoverageSummaryBucket
{
    uint64_t frameStart = 0;
    uint64_t frameEnd = 0;
    uint32_t executedDistinct = 0;
    uint32_t writtenDistinct = 0;
    uint32_t readDistinct = 0;
    bool hasKeyframe = false;
};

/// @brief Result of a coverage summary query (TD-7 §3.1.3).
struct TTDCoverageSummaryResult
{
    uint64_t fromFrame = 0;
    uint64_t toFrame = 0;
    uint64_t coveredFrom = 0;                ///< First frame any covered kind covers
    uint64_t coveredTo = 0;                  ///< Last frame any covered kind covers
    uint64_t bucketSize = 50;
    size_t bucketCount = 0;
    std::vector<TTDCoverageSummaryBucket> buckets;
    bool indexAvailable = false;
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

    /// @brief Make time travel unavailable for this instance, with the reason a
    /// user sees: StartRecording (and the debugger live history built on it)
    /// and DeserializeSession refuse while it is set. Any session held is
    /// dropped. An empty reason makes it available again.
    void SetUnavailableReason(const std::string& reason);
    const std::string& GetUnavailableReason() const { return _unavailableReason; }

    /// @brief Stop capturing new frames. History is retained and browsable.
    /// Idempotent: calling while Idle is a no-op.
    void StopRecording();

    /// @brief Drop all captured history and return to Idle.
    /// Called by session-invalidation hooks (Reset / Load* / speed change).
    /// The reason is logged and kept as TTDSessionInfo::lastDropReason.
    void InvalidateSession(const char* reason);

    /// @brief Whether `action` may run now. While a user recording runs, every
    /// TTDGuardedAction is refused - stop the recording first. A debugger's
    /// live history (DebuggerLive) is not protected: an outside change drops it
    /// and the debugger restarts it.
    /// @return empty when allowed; otherwise the reason, one sentence a user can
    /// act on. Every automation surface shows it verbatim, and the core paths
    /// that perform the action refuse with it too.
    std::string RecordingGuard(TTDGuardedAction action) const;

    /// @brief InvalidateSession requested from inside emulation, by a device
    /// whose state TTD cannot follow yet (storage: the SD card, IDE).
    /// Applied at the next OnFrameBoundary, before that frame's checkpoint is
    /// captured, on the frame thread - never from inside a port handler.
    /// No-op unless Recording. `reason` must outlive the call (a literal).
    void RequestInvalidation(const char* reason);
    inline bool IsInvalidationPending() const { return _pendingInvalidation.load(std::memory_order_acquire) != nullptr; }

    /// Any thread: the state is atomic (observers poll it while the machine runs)
    inline bool IsRecording() const { return _state.load(std::memory_order_acquire) == TTDSessionState::Recording; }

    inline TTDSessionState GetState() const { return _state.load(std::memory_order_acquire); }
    inline TTDRecordMode GetRecordMode() const { return _recordMode; }
    inline bool IsDebuggerLive() const { return _recordMode == TTDRecordMode::DebuggerLive; }

    /// @brief Enter DebuggerLive mode (debugger session live history).
    ///
    /// Adopts whatever recording state exists instead of wiping:
    ///  - already Recording (a Session-mode start hijacked by an attach):
    ///    keep the timeline, just switch the mode flag;
    ///  - Idle with history and no unrecorded gap: ResumeRecordingLive()
    ///    (append after the recorded end);
    ///  - Idle empty or gapped: fresh StartRecording-style baseline.
    ///
    /// @return true when recording is active in DebuggerLive mode on
    ///         return.
    bool BeginDebuggerLiveHistory();

    /// @brief Leave DebuggerLive mode. Recording stops, the timeline is
    ///        KEPT as normal Idle-with-history for the scrubber/.ttd
    ///        flows. Idempotent (no-op when not in DebuggerLive).
    void EndDebuggerLiveHistory();

    /// @brief The session summary, computed from the live session structures.
    ///
    /// Thread contract (TDD section 7.2): only the thread that drives the
    /// session may call it - the machine's thread, or a control thread while
    /// the machine is paused (the same rule as every other session operation).
    /// It walks the timeline, the page store and the journals, which the
    /// machine's thread reallocates while it records. Every call also
    /// publishes its result for GetPublishedSessionInfo().
    TTDSessionInfo GetSessionInfo() const;

    /// @brief The last published session summary - for observers on any thread
    /// at any time (UI tooltips, status bar, polling timers).
    ///
    /// Never touches the live session: it copies a snapshot under its own
    /// small mutex. Published by the thread that drives the session: at every
    /// session operation (start, stop, invalidate, load, seek, resume, history
    /// limit), on every GetSessionInfo() call, and at frame boundaries while a
    /// session is active (at most every kPublishIntervalMs, and only once an
    /// observer has asked since the last publication). So the numbers lag the
    /// running machine by at most one interval plus a frame; while the machine
    /// is paused they are exact.
    TTDSessionInfo GetPublishedSessionInfo() const;
    static constexpr uint32_t kPublishIntervalMs = 100;

    /// @brief The session summary for automation status reads, from any thread.
    ///
    /// Live (GetSessionInfo) when nothing else can be changing the session:
    /// the caller is the machine's thread; or no thread executes the machine
    /// and no other control operation is in progress (the control lock is
    /// free) and the session is not recording (a recording machine could be
    /// resumed by another thread mid-read). Otherwise the published snapshot,
    /// which is exact for a parked recording (the machine's thread publishes
    /// as it parks, see OnMachineParking) and at most kPublishIntervalMs plus
    /// a frame old while it runs. Never blocks, never pauses the machine.
    TTDSessionInfo ReadSessionInfo() const;

    /// @brief The machine's thread, about to park (pause): publish the
    /// recording's summary so status reads while paused are exact
    void OnMachineParking();

    /// @brief History limit: while recording, the oldest checkpoints are
    /// released once the timeline holds more than `maxFrames` checkpoints or
    /// more than `maxBytes` of checkpoint data (page store + device blobs).
    /// 0 = no limit for that measure (the default for both). What stays is a
    /// complete, shorter session: every checkpoint decodes on its own (a delta
    /// page holds a reference on its base page), the journals are cut to the
    /// new start, and a session saved after an eviction starts there.
    void SetHistoryLimit(uint64_t maxFrames, uint64_t maxBytes);
    /// Bytes of checkpoint data held now: the page store's slots and every
    /// checkpoint's device blobs (what the byte limit measures)
    uint64_t HistoryBytes() const;

    /// @brief Called by FeatureManager when feature flags change.
    /// Deallocates write journal when TimeTravel feature is disabled.
    void UpdateFeatureCache();

    // -----------------------------------------------------------------------
    // Session configuration (v2 optimizations)
    // -----------------------------------------------------------------------

    /// @brief Enable/disable write journal capture.
    ///
    /// **Gaming mode** (`enable = false`):
    ///   - Smaller .ttd files (~90% reduction)
    ///   - Checkpoint scrubbing and rewind work normally
    ///   - Reverse-watchpoint queries fall back to checkpoint replay (slower)
    ///   - Best for: recording gameplay, demos, general time-travel
    ///
    /// **Development mode** (`enable = true`, default):
    ///   - Full write journal with every memory/port write (12 bytes each)
    ///   - Fast reverse-watchpoint queries ("where was X last written?")
    ///   - Best for: debugging, step-back analysis, reverse debugging
    ///
    /// Must be called before StartRecording() to take effect. Refused (false)
    /// while a user recording runs: it keeps the mode it started with
    /// (RecordingGuard(ChangeWriteJournal) explains; a debugger's live history
    /// is not protected). Changing it on a stopped session that holds history
    /// leaves a gap in the journal (reported in the session status), so reverse
    /// queries on that session fall back to replay. Switching it off with no
    /// session frees the pre-allocated journal.
    bool SetEnableWriteJournal(bool enable);
    bool GetEnableWriteJournal() const { return _enableWriteJournal; }

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
    /// (see ttddumpformat.h::kMaxSupportedSchemaVersion).
    bool DeserializeSession(std::istream& in, std::string& err);

private:
    /// The port journals of a file read by SearchPortEventsInFile
    struct FilePortJournals
    {
        TTDPortJournal reads{TTDPortJournal::Direction::Read};
        TTDPortJournal writes{TTDPortJournal::Direction::Write};
    };
    /// DeserializeSession's body. With `journalsOnly` the file is parsed and
    /// checked the same way but nothing is committed: its port journals are
    /// handed out, and the machine-compatibility checks are skipped
    bool DeserializeSessionImpl(std::istream& in, std::string& err, FilePortJournals* journalsOnly);

public:

    /// @brief Session-kind guard decision core (TSFM design §8.2).
    ///
    /// Pure decision: does a recorded session's TurboSound-slot blob set
    /// agree with the live slot device? The slot has exactly two
    /// inhabitants - the legacy two-AY device (PeripheralId::TurboSound, 0)
    /// and TSFM (PeripheralId::TSFM, 4). A session recorded with one,
    /// loaded into an instance running the other, would restore NEITHER
    /// device (RestoreAll counts the live one under missingBlobs and leaves
    /// it holding pre-load state) - a silent divergence, so the load is
    /// refused instead. A session with no slot blob at all (recorded on a
    /// machine without the device) is not a mismatch.
    ///
    /// Exposed as a public static for unit tests; DeserializeSession applies
    /// it to the baseline checkpoint's blob map.
    ///
    /// @param sessionBlobs   Baseline checkpoint peripheral blob map
    ///                       (keyed by PeripheralId).
    /// @param liveSlotDevice The device currently in the TurboSound slot.
    /// @return true when the kinds agree (or the session has no slot blob).
    static bool TurboSoundSessionKindMatches(
        const std::unordered_map<uint8_t, std::vector<uint8_t>>& sessionBlobs,
        const TTDSerializable& liveSlotDevice);

    /// @brief Record where a just-deserialized session came from.
    /// Callers that loaded from a path should set it so GetSessionInfo can
    /// report provenance; streams with no path leave it empty.
    void SetSessionSourcePath(const std::string& path);

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
    /// Called from MainLoop::CompleteFrame - the one frame boundary every run
    /// path (main loop and all Emulator::Run*) goes through - AFTER the new
    /// frame's start (frame-start hooks, CPU frame geometry): checkpoint N
    /// is the machine ready to execute frame N's first instruction, the same
    /// frame phase as every other capture point (a recording's baseline taken
    /// at a pause), so a restore never runs any frame hook. Host input
    /// injected at the boundary comes after the capture and is replayed from
    /// the input journal. No-op if not Recording. Cost when recording:
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
    // also hold the host audio output (SoundManager::HostOutputHold, reason
    // TtdReplay): nothing reaches the speakers, the user's mute is untouched.
    //
    // Threading: same discipline as Restore — called on the control thread
    // with the emulator paused. Replay is driven by a follow-up RunTStates
    // call on the same thread, so the flag is set/cleared around it without
    // any cross-thread visibility concern.

    /// @brief Enter silent-replay mode.
    ///
    /// Sets `_context->ttdReplayActive = true` and holds the host audio
    /// output. Idempotent: a second call while already in replay is a no-op
    /// (no second hold, so nesting is safe).
    void EnterReplayMode();

    /// @brief Exit silent-replay mode.
    ///
    /// Clears `_context->ttdReplayActive` and releases the host audio hold.
    /// Idempotent: a call while not in replay is a no-op.
    void ExitReplayMode();

    /// @brief EnterReplayMode for a scope: ExitReplayMode on Exit() or, at the
    /// latest, when the scope ends - an exception or early return inside a
    /// replay can no longer leave replay mode (and its host audio hold) on.
    /// Same semantics as the explicit pair: Exit leaves replay mode even when
    /// an outer caller entered it first.
    class ReplayModeScope
    {
    public:
        explicit ReplayModeScope(TimeTravelManager& manager) : _manager(&manager) { manager.EnterReplayMode(); }
        ~ReplayModeScope() { Exit(); }
        ReplayModeScope(const ReplayModeScope&) = delete;
        ReplayModeScope& operator=(const ReplayModeScope&) = delete;
        void Exit()
        {
            if (TimeTravelManager* manager = _manager)
            {
                _manager = nullptr;
                manager->ExitReplayMode();
            }
        }

    private:
        TimeTravelManager* _manager;
    };

    /// @brief Query the replay-mode flag. Reads `_context->ttdReplayActive`.
    /// Defined out-of-line (EmulatorContext is only forward-declared here).
    bool IsReplayActive() const;

    // -----------------------------------------------------------------------
    // Input journal (Phase 2 Item 3; parent TDD §5 row #1)
    // -----------------------------------------------------------------------
    //
    // Captures input device mutations (keyboard, Kempston Mouse, General Sound
    // host stimuli) with their TTDTimePoint. Playback (ServiceInput) applies
    // them at the recorded times through ApplyInputEvent (ttdinputapply.h),
    // the same path live input takes, while live input is refused
    // (OwnsInput).
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

    /// @brief Journal a Kempston Mouse mutation (same contract as RecordInputEvent:
    /// callers gate on IsRecording() and !IsReplayActive(), and call BEFORE applying).
    void RecordMouseMove(int dx, int dy);
    void RecordMouseButtons(uint8_t activeLowMask);
    void RecordMouseWheel(int steps);
    void RecordMouseCounters(uint8_t x, uint8_t y);

    /// @brief Journal a whole-matrix keyboard reset (release of every key)
    void RecordKeyboardReset();

    /// @brief Read-only access to the input journal (playback cursor, tests).
    inline const TTDInputJournal& GetInputJournal() const { return _inputJournal; }

    /// @brief Read-only access to the port-read journal (tests, status).
    inline const TTDPortJournal& GetPortReadJournal() const { return _portReads; }
    inline const TTDPortJournal& GetPortWriteJournal() const { return _portWrites; }

    /// @brief "When did the program ..." over the port journals
    /// (ttdportsearch.h): no replay, works on loaded files. Fails with a reason
    /// when the session has no port journals, or while a recording is running
    /// (pause it: the journals are only written by the running emulation)
    TTDPortSearchResult SearchPortEvents(const TTDPortQuery& q) const;

    /// @brief The same search over a .ttd file on disk, without loading it:
    /// the current session and machine are not touched. The whole file is
    /// read and checked like a load (CRCs, section layout); the checks that
    /// tie a session to this machine (model, ROM set, sound slots) are skipped
    /// - the journals do not need them. Fails with a reason for an unreadable
    /// file or one without port journals
    TTDPortSearchResult SearchPortEventsInFile(const std::string& path, const TTDPortQuery& q);

    // -----------------------------------------------------------------------
    // Input ownership: live input vs the recorded journal
    // -----------------------------------------------------------------------
    //
    // While the machine executes recorded history, its input comes from the
    // journal ONLY - applied straight to the keyboard matrix / mouse at the
    // recorded instruction boundaries - and every live source (host UI via
    // MessageCenter, automation) is refused. Outside history, live input is
    // applied on the machine's own thread at an instruction boundary and
    // journaled there while recording, so the recorded time is the moment
    // the program could first see the change.

    /// @brief True while the journal owns input: a seek replay is running, or
    /// the machine sits Detached inside the recorded session (seeked into the
    /// past and possibly running forward through it). Live input is refused.
    bool OwnsInput() const;

    /// @brief The one entry point for live input (any thread). Refused (false)
    /// while OwnsInput(). While the emulator loop runs, the event is queued
    /// and applied - and journaled when recording - by the machine's thread at
    /// the next instruction boundary (ServiceInput); in synchronous mode (loop
    /// not running, the caller is the only thread) it is applied at once.
    /// `ev.time` is ignored: the time is stamped when the event is applied.
    bool SubmitLiveInput(const TTDInputEvent& ev);

    /// @brief SubmitLiveInput for a NetEvent: its network record and bytes are
    /// copied; the journal keeps them while recording, and the virtual network
    /// reads them when the event is applied.
    bool SubmitLiveInput(const TTDInputEvent& ev, const TTDNetInput& net, const uint8_t* payload, uint32_t length);

    /// @brief A lockstep group (the ZX-Poly master) takes live input itself, to
    /// give it to every member at one frame boundary. SubmitLiveInput hands each
    /// event to `interceptor` first; when it returns true the event is consumed
    /// there. Events it declines (false) take the normal path. An empty function
    /// removes it
    void SetLiveInputInterceptor(std::function<bool(const TTDInputEvent&)> interceptor);

    /// @brief Run `task` on the machine's thread - for automation actions that
    /// touch a device the executing thread may be using (a NeoGS SD card
    /// insert / eject, a flash save). Same delivery as SubmitLiveInput: queued
    /// for the next instruction boundary while the emulator loop runs (while
    /// paused: when execution continues), run at once otherwise. Not
    /// journaled: a task is not replayable input. Refused while OwnsInput().
    enum class MachineTaskResult : uint8_t { RanNow, Queued, Refused };
    MachineTaskResult SubmitMachineTask(std::function<void()> task);

    /// @brief Executing thread, before every instruction (Z80::StepInstruction;
    /// cheap gate: EmulatorContext::kStepWorkTtdInput in stepWork): play due journal events,
    /// then apply queued live input.
    void ServiceInput();

    /// @brief The machine left the recorded timeline from outside (reset):
    /// a Detached session returns to Idle and journal playback stops.
    void OnMachineReset();

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
    /// EmulatorState: `frame = frame_counter`, `tInFrame` = z80->t in TTD
    /// time units (TInFrameNow).
    TTDTimePoint CurrentPosition() const;

    /// @brief TTD time. A position's tInFrame counts T-states at the model's
    /// TOP CPU clock (EmulatorState::ttd_clock_units per base T-state: 1 on
    /// models without a hardware turbo, where it equals z80.t). z80.t alone
    /// counts at the current clock and is rescaled when a hardware turbo
    /// switches mid-frame, so after a switch down it repeats values of the
    /// same frame; in these units every instant has one value and time only
    /// grows (B4).
    uint32_t TInFrameNow() const;

    /// @brief TTD time units in one frame (config.frame at the top clock).
    /// Constant for a session whatever turbo is engaged.
    uint32_t FrameSpan() const;

    /// @brief A position as one absolute count of TTD time units since frame 0
    /// (the write journal's globalT; find-last's beforeGlobalT)
    uint64_t GlobalT(const TTDTimePoint& at) const { return at.frame * FrameSpan() + at.tInFrame; }

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
    // Clip export (ZX DLSS reference material; implementation ttdclipexport.cpp)
    // -----------------------------------------------------------------------

    struct TTDClipExportOptions
    {
        uint64_t fromFrame = 0;
        uint64_t toFrame = 0;
        std::string directory;      ///< created if missing
        uint32_t chunkFrames = 500;  ///< frames per zstd chunk
        int zstdLevel = 3;
    };

    struct TTDClipExportResult
    {
        bool ok = false;
        std::string error;
        uint64_t frames = 0;
        uint64_t bytesWritten = 0;
        bool planeB = false;  ///< plane B written (feature zxdlss on)
        uint32_t width = 0;
        uint32_t height = 0;
        double seconds = 0.0;
    };

    /// @brief Write every frame of [fromFrame, toFrame] as its final picture -
    /// the same picture positioning by frame number shows - into `directory`:
    /// rgba_NNNN.zst (RGBA8), planeb_NNNN.zst (plane B, when the zxdlss feature
    /// is on), meta.jsonl (frame, #7FFD, displayed screen, border at the frame's
    /// start) and clip.json (geometry, encodings). One call instead of one
    /// seek + capture round trip per frame. Leaves the machine positioned and
    /// displayed at toFrame. The emulator must be paused; refused while recording.
    TTDClipExportResult ExportClip(const TTDClipExportOptions& options);

    /// One composed frame handed to a VisitComposedFrames callback. The
    /// pointers are valid only during the callback.
    struct TTDComposedFrame
    {
        uint64_t frame = 0;
        const uint8_t* rgba = nullptr;    ///< width x height RGBA8 (the framebuffer)
        size_t rgbaBytes = 0;
        const uint16_t* planeB = nullptr; ///< width x height plane B, nullptr when zxdlss is off
        size_t planeBCount = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint8_t p7FFD = 0;                ///< at the frame's start
        uint8_t activeScreen = 0;
        uint8_t border = 0;
    };
    using TTDFrameVisitor = std::function<bool(const TTDComposedFrame&)>;  ///< false stops the walk

    /// @brief Walk [fromFrame, toFrame] and hand every frame's final picture
    /// (and plane B) to `visit` - the walk ExportClip writes to disk, for tools
    /// that process the frames in memory (tools/verification/zxdlss renders
    /// TTD files through de-flicker algorithms). Same preconditions as
    /// ExportClip; leaves the machine displayed at the last frame visited.
    /// @return empty on success, otherwise the reason
    std::string VisitComposedFrames(uint64_t fromFrame, uint64_t toFrame, const TTDFrameVisitor& visit);

    // -----------------------------------------------------------------------
    // Agent bookmarks (TD-4; ttd-coverage-evaluation.md §TD-4)
    // -----------------------------------------------------------------------
    //
    // Advisory named timeline annotations — the "note to self" that survives
    // seeks: mark the unpack entry once, then return by label no matter how
    // far find-last / reverse-continue / step-back wandered. Stored in
    // TTDBookmarkJournal BESIDE the external-event journal, never inside it:
    // a bookmark observes the timeline, it is not a replay barrier, and
    // SeekTo never halts on one (halt_reason has no "bookmark" value).
    //
    // Lifecycle mirrors the other journals: cleared on StartRecording /
    // InvalidateSession / DeserializeSession, clipped by
    // ResumeRecordingFrom, persisted in the .ttd file as a flag-gated
    // section (ttd::dump::kFlagsHasBookmarks).

    /// @brief Add a bookmark at an explicit position.
    ///
    /// Manager-level validation on top of the journal's label rules: the
    /// timeline must be non-empty and `time` must lie within the recorded
    /// bounds (<= SessionEndPosition()) — a bookmark pointing past the end
    /// can never be sought to and is refused at creation instead.
    ///
    /// Callable in any session state (a bookmark added while Recording
    /// points at history that exists; adding while Detached/Idle annotates
    /// the browsed timeline).
    ///
    /// @return false with *err filled on invalid label / duplicate label /
    /// out-of-bounds position.
    bool AddBookmark(const TTDTimePoint& time, const std::string& label,
                     std::string* err = nullptr);

    /// @brief All bookmarks, time-sorted (thread-safe snapshot copy).
    std::vector<TTDBookmark> GetBookmarks() const;

    /// @brief Resolve a label to its bookmark. False when unknown.
    bool FindBookmark(const std::string& label, TTDBookmark& out) const;

    /// @brief Remove a bookmark by label. False when the label is unknown.
    bool RemoveBookmark(const std::string& label);

    /// @brief Seek to a bookmark's position by label.
    ///
    /// Pure composition: FindBookmark + SeekTo. The returned result is
    /// exactly what a direct SeekTo to the same timepoint would produce —
    /// in particular a marker between the restore checkpoint and the target
    /// still reports halt_reason "external_event", and a bookmark itself
    /// never appears as a halt reason (advisory by construction).
    ///
    /// @param label     Bookmark to seek to.
    /// @param outResult Seek outcome (may be nullptr).
    /// @param err       Filled with "unknown bookmark ..." on a bad label.
    /// @return          outResult->reached (false on unknown label).
    bool SeekToBookmark(const std::string& label, TTDSeekResult* outResult,
                        std::string* err = nullptr);

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

    /// @brief Resume live recording from the CURRENT position, keeping history.
    ///
    /// The non-destructive counterpart to StartRecording for the
    /// browse-while-paused flow (debugger history views): StopRecording keeps
    /// the timeline for browsing, and this transitions Idle → Recording again
    /// WITHOUT clearing anything, so the next OnFrameBoundary appends strictly
    /// after the existing timeline end and the recorded history keeps growing
    /// across browse/resume cycles (StartRecording would wipe it).
    ///
    /// Valid only when the live machine never left the present (no seek/
    /// scrub) AND no unrecorded gap exists: the current position must be
    /// in the SAME frame as the recorded end (timeline.back()). A
    /// mid-frame present in that frame is fine — unlike
    /// ResumeRecordingFrom there is no target to hit and nothing to
    /// truncate; the partial frame simply continues where it paused (the
    /// emulator state is continuous across the browse). A frame gap means
    /// the emulator ran while recording was stopped; those frames have no
    /// checkpoints and no journaled writes, so replaying across the gap
    /// would silently produce wrong state — refuse and let the caller fall
    /// back to StartRecording.
    ///
    /// @return true on success (or already Recording — idempotent). False
    ///         (with a logged warning) if state is Detached (use
    ///         ResumeRecordingFrom), the timeline is empty (caller should
    ///         StartRecording), or a frame gap was detected
    ///         (sorted-invariant/unrecordable-gap guard).
    bool ResumeRecordingLive();

    // -----------------------------------------------------------------------
    // Phase 4: Reverse search (parent TDD §9 + §10.4)
    // -----------------------------------------------------------------------
    //
    // Two-layer reverse-watchpoint engine:
    //   1. Write journal (§9.3): 64 MB ring of TTDWriteRecord, scanned
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
    /// @brief Record an instruction fetch in the coverage index.
    ///
    /// Called from the Z80 M1 cycle, which gates on
    /// EmulatorContext::ttdCoverageActive first. Separate from the write path
    /// because instruction fetches are not journalled - the coverage set is the
    /// only record that a frame executed a given address, and therefore the
    /// only thing that lets a reverse breakpoint skip frames instead of
    /// replaying them.
    /// @brief Record a memory read in the coverage index.
    ///
    /// Reads are the one access kind with no journal at all, so without this a
    /// reverse read-watchpoint has nothing to prune with and must replay every
    /// frame. Inline for the same reason as the execute path: reads outnumber
    /// writes roughly 3:1, so the call itself would dominate.
    inline void RecordReadCoverage(PhysPage physPage, uint16_t addr)
    {
        if (_state != TTDSessionState::Recording)
            return;

        // Accesses with no RAM page (ROM, cache) are recorded under the
        // kPhysPageNone bucket rather than dropped. Dropping them made the
        // index claim frames were empty when they in fact held ROM accesses,
        // and pruning then deleted real search hits. Every ROM page collapses
        // into one bucket, which over-approximates — extra replays, never a
        // lost answer.
        _coverageIndex.Record(TTDCoverageKind::Read, MakeCoverageKey(physPage, addr));
    }

    inline void RecordExecutedCoverage(PhysPage physPage, uint16_t pc)
    {
        // Inline for the same reason TTDCoverageIndex::Record is: this sits on
        // the instruction-fetch path, so the call itself was the cost.
        if (_state != TTDSessionState::Recording)
            return;

        // ROM execution is recorded under the kPhysPageNone bucket, not
        // dropped — see RecordReadCoverage for why.
        _coverageIndex.Record(TTDCoverageKind::Executed, MakeCoverageKey(physPage, pc));
    }

    /// @brief Read-only access to the coverage index (control thread).
    inline const TTDCoverageIndex& GetCoverageIndex() const { return _coverageIndex; }

    /// @brief Is per-frame coverage collection enabled for new sessions?
    inline bool IsCoverageIndexEnabled() const { return _enableCoverageIndex; }
    void SetEnableCoverageIndex(bool enable);

private:
    /// @brief Make the current position visible: compose its picture
    /// (ComposeDisplay) and publish it.
    ///
    /// The single display step of every user-facing navigation (public SeekTo
    /// and the operations built on it, StepForwardInstruction). Internal
    /// restores during search, reverse execution and frame-cache builds never
    /// reach it, so they cannot touch the display.
    /// @param frameTarget true when the user positioned by frame number
    void PresentPosition(bool frameTarget);

    /// @brief Flush the video delay line and post NC_VIDEO_FRAME_REFRESH so
    /// every observer (Qt present queue, WebAPI capture, viewers) sees the
    /// framebuffer PresentPosition composed.
    void PublishSeekedFrame();

public:

private:
    /// @brief May the coverage index be used to skip frames for this query?
    ///
    /// Only Read and Execute searches reach the replay loop at all (Write and
    /// Io are answered by the journal), and pruning is sound only when the
    /// query's Z80 address range collapses to one non-wrapping offset interval
    /// inside a 16 KB page. A range spanning a page boundary, or wider than a
    /// page, could match any offset, so it is left unpruned rather than
    /// approximated.
    inline bool CanPruneByCoverage(const TTDSearchQuery& q) const
    {
        if (!_enableCoverageIndex)
            return false;
        if (q.access != TTDAccessType::Read && q.access != TTDAccessType::Execute)
            return false;
        if (q.addrTo < q.addrFrom)
            return false;
        if (static_cast<uint32_t>(q.addrTo - q.addrFrom) >= 0x4000u)
            return false;  // Wider than a page — every offset is possible.

        // Reject ranges that wrap across a 16 KB boundary: their offsets are two
        // disjoint intervals, which FrameMayContain does not model.
        return (q.addrFrom & 0x3FFF) <= (q.addrTo & 0x3FFF);
    }

public:

    void RecordMemoryWrite(uint16_t addr, uint8_t oldVal, uint8_t newVal,
                           uint16_t m1pc, PhysPage physPage);
    
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
    /// fills *outBlockingMarker with the barrier. *outWindow (if non-null)
    /// receives the part of history the search examined (TD-8).
    ///
    /// Preconditions: emulator paused, state is Recording or Detached.
    std::optional<TTDSearchResult> FindLastAccess(
        const TTDSearchQuery& query,
        TTDExternalEvent* outBlockingMarker = nullptr,
        TTDSearchWindow* outWindow = nullptr);

    /// @brief Probe coverage for a specific frame and address range (TD-7 §3.1.1).
    TTDCoverageProbeResult QueryCoverageProbe(
        uint64_t frame,
        TTDCoverageKind kind,
        uint16_t addrFrom,
        uint16_t addrTo,
        std::optional<PhysPage> physPage = std::nullopt) const;

    /// @brief Scan frames in [fromFrame, toFrame] touching range (TD-7 §3.1.2).
    TTDCoverageScanResult QueryCoverageScan(
        uint64_t fromFrame,
        uint64_t toFrame,
        TTDCoverageKind kind,
        uint16_t addrFrom,
        uint16_t addrTo,
        std::optional<PhysPage> physPage = std::nullopt,
        size_t limit = 200) const;

    /// @brief Activity heatmap over [fromFrame, toFrame] (TD-7 §3.1.3).
    TTDCoverageSummaryResult QueryCoverageSummary(
        uint64_t fromFrame,
        uint64_t toFrame,
        std::optional<TTDCoverageKind> kind = std::nullopt,
        uint64_t bucketSize = 0,
        size_t limit = 100) const;
    
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
        TTDSearchWindow  window{};           ///< Part of history the scan examined (TD-8).
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
    inline const TTDWriteJournal* GetWriteJournal() const { return _writeJournal.get(); }

    // -----------------------------------------------------------------------
    // Per-frame decode cache (reverse-browsing accelerator, §5 of the DeZog
    // reverse-debugging design). Populated only during a replay pass; freed on
    // leaving the replay/browse scope. See timetravelframecache.h.
    // -----------------------------------------------------------------------

    /// @brief Get the decode cache for `frame`, building it on demand.
    ///
    /// The first call for a frame replays that frame once (Detached) with an
    /// instruction-capture hook, filling the cache; subsequent calls for the
    /// same frame return it directly. The emulator state and position are
    /// left unchanged: the live machine state is snapshotted before the
    /// build replay and restored verbatim after (exact and marker-safe —
    /// a seek-back cannot cross external-event markers).
    ///
    /// @return the cache, or nullptr if unavailable (Recording state, empty
    ///         timeline, or the frame is out of recorded range).
    ///
    /// Pre: emulator paused, state is Detached or Idle-with-history (NOT
    ///      Recording — call StopRecording first). Control thread only.
    const TTDFrameCache* GetFrameCache(uint64_t frame);

    /// @brief Free the per-frame cache and release its memory. Called on any
    /// transition back to the present (StartRecording / ResumeRecordingFrom /
    /// InvalidateSession) and available to callers leaving the browse scope.
    void ClearFrameCache();

    /// @brief Heap footprint of the currently-held frame cache (0 if none).
    inline size_t GetFrameCacheBytes() const
    {
        return _frameCache ? _frameCache->Bytes() : 0;
    }

    /// @brief Frame number currently cached, or UINT64_MAX if none.
    inline uint64_t GetCachedFrame() const
    {
        return _frameCache ? _frameCache->frame : UINT64_MAX;
    }
    
    // -----------------------------------------------------------------------
    // Test/diagnostic accessors
    // -----------------------------------------------------------------------
    
    /// @brief Number of checkpoints currently in the timeline.
    inline size_t GetCheckpointCount() const { return _timeline.size(); }
    
    /// @brief Earliest frame covered by the timeline (0 when empty).
    ///
    /// Frames below this have no checkpoints: seeks/builds targeting them
    /// always fail, so callers walking backward (e.g. the DeZog history
    /// index resolver) can stop here instead of probing every frame down
    /// to 0.
    inline uint64_t GetEarliestRecordedFrame() const
    {
        return _timeline.empty() ? 0 : _timeline.front().time.frame;
    }
    
    /// @brief Read-only access to a timeline entry (bounds-checked).
    /// Returns nullptr if idx is out of range.
    const TTDCheckpoint* GetCheckpoint(size_t idx) const;

    /// @brief Read-only access to the page store (for tests / budget checks).
    inline const TTDCodecPageStore& GetPageStore() const { return _pageStore; }

    /// @brief Last capture / restore timings (benchmark harness, BM-2 / BM-6).
    inline const TTDPerfCounters& GetPerfCounters() const { return _perf; }

    /// Model-specific peripheral serializers registered for this session.
    /// Exposed so the divergence hash can mix in their contribution without
    /// the hash code knowing which machine is running.
    inline const TTDPeripheralRegistry& GetPeripheralRegistry() const { return _peripherals; }

    /// @brief Re-point a peripheral slot at a new object, for an owner that
    /// replaces its own device instance at runtime.
    ///
    /// Currently: General Sound personality switching. SoundManager::
    /// switchGeneralSoundCard deletes the outgoing card and installs a new
    /// one under the same GeneralSoundCard* the SoundManager exposes.
    /// RegisterModelPeripherals only runs at StartRecording/session load, so
    /// a switch that happens while a recording is already active would
    /// otherwise leave the registry holding a dangling pointer to the
    /// just-deleted card - the next checkpoint's TTDSaveState call would be a
    /// use-after-free. The owner calls this immediately after the swap
    /// (whether or not TTD is currently recording - the registry array
    /// persists after StopRecording too, ready for the next
    /// StartRecording/seek, so a stale pointer left uncorrected here would
    /// resurface later even outside an active recording).
    ///
    /// @p newId is the slot to (re-)register @p device under - the device's
    /// own `TTDPeripheralId()`, not necessarily the slot the previous device
    /// occupied: the GS personalities register under different ids
    /// (GeneralSound for LLE, GeneralSoundLightweight for LW - same pattern
    /// as the TurboSound/TSFM slot split in RegisterModelPeripherals), so a
    /// personality switch moves the registration to a different slot rather
    /// than reusing the old one. @p oldId, if different from @p newId, is
    /// unregistered first - leaving it registered would keep offering a
    /// blob-less capture for a slot nothing occupies anymore, and would leave
    /// a stale pointer in that slot exactly as bad as the use-after-free this
    /// method exists to prevent had @p device simply been registered under
    /// the wrong slot instead. Pass the same value for both when the device
    /// keeps its slot (the common case for every other peripheral type).
    ///
    /// A null TimeTravelManager pointer (TTD unavailable) is a caller error
    /// to guard against, not this method's job. An @p oldId with no existing
    /// registration (e.g. no GS card was fitted at RegisterModelPeripherals
    /// time) is harmless - Unregister on an absent id is a no-op.
    inline void UpdatePeripheral(PeripheralId oldId, PeripheralId newId, TTDSerializable* device)
    {
        if (oldId != newId)
            _peripherals.Unregister(oldId);
        _peripherals.Register(newId, device);
    }

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
    static bool IsPageAllZero(const uint8_t* page);

    void UpdateRamPages(const std::vector<uint16_t>& dirtyPages,
                        const std::vector<TTDPageRef>& prevRamPages,
                        std::vector<TTDPageRef>& outRamPages,
                        bool isKeyFrame);

    /// @brief Release every page ref held by a checkpoint (used when
    /// invalidating or thinning).
    void ReleaseCheckpointRefs(TTDCheckpoint& cp);

    /// @brief Update the previous-page cache with current RAM contents.
    ///
    /// Called after each checkpoint capture. The cache stores uncompressed
    /// RAM so the next frame can compute XOR deltas without decompressing
    /// the slots from the previous checkpoint. This eliminates the O(chain_depth)
    /// recursive decompression that would otherwise occur on every dirty page.
    void UpdatePrevPageCache();

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
 public:
    /// The session heap by part (TTD benchmark BM-4 split)
    TTDHeapBreakdown GetHeapBreakdown() const;

 private:

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
    /// @param targetTInFrame Position within the frame to stop at, in TTD
    ///        time units (FrameSpan() = the whole frame).
    void ReplayWithinFrame(uint64_t targetFrame, uint32_t targetTInFrame);

    /// @brief Run the CPU until the current frame reaches `targetTInFrame`
    /// (TTD time units) or ends. A hardware turbo may switch on the way, so
    /// the T-state budget is re-derived after each run.
    void RunToTInFrame(uint32_t targetTInFrame);

    /// @brief Compose the picture for the current position (display rule,
    /// docs/inprogress/2026-09-28-ttd-positioning-and-display/design.md §3).
    ///
    /// - Frame target (positioned by frame number): the frame's FINAL
    ///   picture — its own T-states replayed from its checkpoint to its end.
    /// - Time target (frame f, T-state T): what the beam rendered from the
    ///   start of f up to T, over frame f-1's final picture for the part not
    ///   drawn yet — exactly the framebuffer of a live machine paused there.
    ///
    /// Runs in a sandbox: live state (CPU, RAM, peripherals, input cursor,
    /// keyboard, framebuffer) is saved first and restored afterwards, then
    /// the composed pixels are written. Machine state is never changed.
    /// Checkpoint restores themselves never paint (ResyncScreenState), so
    /// this is the only place that decides what a TTD position shows.
    void ComposeDisplay(bool frameTarget);

    /// @brief Replay the rest of the current frame up to its end through the
    /// normal CPU/video pipeline (the frame-end processing runs, so the
    /// frame's final picture is in the framebuffer). Caller owns replay mode.
    void RunToFrameEnd();

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

    /// @brief Release the `count` oldest checkpoints (at least two stay) and
    /// cut the input, marker, bookmark and port journals to the new start.
    void EvictOldest(size_t count);
    /// @brief Apply the history limit after a checkpoint was captured.
    void EnforceHistoryLimit();

    // -----------------------------------------------------------------------
    // Dependencies (non-owning)
    // -----------------------------------------------------------------------
    EmulatorContext* _context;
    Memory*          _memory = nullptr;
    TTDDirtyTracker* _dirtyTracker = nullptr;

    // -----------------------------------------------------------------------
    // Per-frame decode cache
    // -----------------------------------------------------------------------
    /// Shortest Z80 instruction length in t-states (e.g. NOP = 4). Bounds the
    /// number of instructions — hence records — a single frame can hold.
    static constexpr uint32_t kMinInstructionTStates = 4;

    /// Safety margin (in records) added to the reserved frame capacity. An
    /// instruction can start just before the frame boundary and run up to the
    /// longest Z80 opcode past it, so the true instruction count is a ceiling,
    /// not a floor; this margin plus ceiling-division guarantees the fill never
    /// reallocates even at that boundary edge (and across an injected interrupt).
    static constexpr uint32_t kFrameReserveMargin = 8;

    std::unique_ptr<TTDFrameCache> _frameCache;
    /// While true, the memory/port write hooks append accesses to the
    /// instruction currently being captured (set only during a build replay).
    bool _frameCaptureActive = false;
    /// Cache currently being filled (valid only while _frameCaptureActive).
    TTDFrameCache* _capturingCache = nullptr;

    /// @brief Replay `frame` once and fill `out` with one record per M1.
    void BuildFrameCache(uint64_t frame, TTDFrameCache& out);
    /// @brief M1 hook body: snapshot CPU/opcodes/sp/slots into a new entry.
    void CaptureM1(uint16_t pc);

    /// @brief Exact live machine state: everything a frame-cache build's
    /// replay can disturb. GetFrameCache captures this before building and
    /// restores it verbatim after, so the caller's position and memory stay
    /// intact even when external-event markers block a replay-based restore
    /// (their effects are not reproducible).
    struct LiveStateSnapshot
    {
        TTDCpuState     cpu{};
        TTDChipsetState chipset{};
        uint32_t        z80TInFrame = 0;   ///< z80.t (host-side, not in TTDCpuState)
        std::vector<uint8_t> ram;          ///< model RAM, _modelRamPages × 16 KB
        /// Peripheral state, keyed by PeripheralId — same representation the
        /// checkpoints use, produced by the same registry.
        std::unordered_map<uint8_t, std::vector<uint8_t>> peripheralBlobs;
        size_t inputCursor = 0;            ///< journal playback cursor (ServiceInput)
        bool   inputPlaybackArmed = false;
        /// Port-read journal mode and position (a throwaway replay moves them)
        TTDPortJournal::Mode portReadMode = TTDPortJournal::Mode::Off;
        uint64_t portReadCursor = 0;
        TTDPortJournal::Mode portWriteMode = TTDPortJournal::Mode::Off;
        uint64_t portWriteCursor = 0;
        /// Keyboard matrix + counters: journal playback inside a sandbox
        /// replay presses/releases keys on the live device.
        Keyboard::InputState keyboard{};
        bool   hasKeyboard = false;
        /// Framebuffer pixels: a sandbox replay renders into the live
        /// framebuffer; restoring hands the caller's picture back untouched.
        std::vector<uint8_t> framebuffer;
        std::vector<uint16_t> planeB;      ///< ZX DLSS plane B, rendered in the same pass as the pixels
        uint32_t screenPrevTstate = 0;     ///< renderer draw cursor (Screen::_prevTstate)
    };

    /// Reused across builds; vector capacities are retained, so the sizable
    /// part (the RAM copy, ≤ ~1 MB) is allocated at most once per browse.
    LiveStateSnapshot _liveSnapshot;

    /// @brief Capture the live machine state into `out` (pure copies; no
    /// timeline or dirty-tracker effects).
    void SaveLiveState(LiveStateSnapshot& out);

    /// @brief Restore a SaveLiveState snapshot verbatim, mirroring
    /// RestoreCheckpoint's ordering: CPU → chipset → bank rebuild → RAM →
    /// peripherals → screen resync.
    void RestoreLiveState(const LiveStateSnapshot& snap);

    /// @brief Re-derive the screen renderer's cached state (video mode,
    /// active screen bank, border color, frame-local counters) from
    /// emulatorState after a restore that bypassed the port decoder
    /// (TDD §8.1 step 2e). Never writes framebuffer pixels — what a position
    /// shows is decided by ComposeDisplay alone.
    void ResyncScreenState();

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    /// Written only by the thread that drives the session, read by observers
    /// on any thread (IsRecording / GetState)
    std::atomic<TTDSessionState> _state{TTDSessionState::Idle};

    /// GetPublishedSessionInfo(): the snapshot observers read instead of the
    /// live session (TDD section 7.2: "a small mutex-protected summary struct").
    /// _published is guarded by _publishedMutex; everything else is touched by
    /// the session-driving thread only
    void PublishSessionInfo(const TTDSessionInfo& info) const;
    void PublishSessionInfo() const { (void)GetSessionInfo(); }
    /// Frame boundary: publish when an observer asked and the interval passed
    void MaybePublishAtFrameBoundary();
    mutable std::mutex _publishedMutex;
    mutable TTDSessionInfo _published;
    mutable std::atomic<bool> _publishRequested{false};
    /// Every public operation that reads or changes the session holds one for
    /// its whole run (TDD section 7.2, "control thread, emulator paused").
    /// On the machine's own thread it does nothing but publish after a change.
    /// On any other thread it
    ///   - takes the control lock (_controlMutex): one control operation at a
    ///     time, and ReadSessionInfo never computes beside one;
    ///   - parks the machine while a session is active (Recording: its thread
    ///     appends to the timeline and journals; Detached: it replays them),
    ///     and resumes it afterwards if it parked it here;
    ///   - after a Change, the outermost operation publishes the summary.
    /// An Idle session is not touched by a running machine, so it is not parked.
    class SessionOperation
    {
    public:
        enum class Kind : uint8_t { Read, Change };
        SessionOperation(const TimeTravelManager& manager, Kind kind);
        ~SessionOperation();
        SessionOperation(const SessionOperation&) = delete;
        SessionOperation& operator=(const SessionOperation&) = delete;

    private:
        const TimeTravelManager& _manager;
        Kind _kind;
        bool _locked = false;
        bool _parked = false;
    };
    bool OnMachineThread() const;
    mutable std::recursive_mutex _controlMutex;
    mutable int _operationDepth = 0;  ///< nesting on the lock holder's thread (guarded by _controlMutex)
    std::chrono::steady_clock::time_point _lastPublish{};
    /// ROM signature of a live session, taken with its baseline (the ROM the
    /// recording relies on); GetSessionInfo no longer hashes the ROM per call
    uint64_t _liveRomSignature = 0;

    /// Recording mode (Session vs DebuggerLive). See TTDRecordMode.
    TTDRecordMode _recordMode = TTDRecordMode::Session;

    /// The recorded timeline. Appended only on the emulator thread.
    std::vector<TTDCheckpoint> _timeline;

    /// Backing codec page store (4 KB pages, XOR+zstd-1 compression).
    TTDCodecPageStore _pageStore;

    /// Model-specific peripheral serializers. The framework never names a
    /// machine: it registers whatever the active model provides (see
    /// RegisterModelPeripherals) and thereafter only calls TTDSerializable.
    TTDPerfCounters _perf;
    TTDCaptureWork _captureWork;   ///< filled while CaptureNow runs, published in _perf
    TTDPeripheralRegistry _peripherals;

    /// Serializers owned by this manager for the lifetime of a session. Held
    /// as a vector of base pointers so adding a model costs one factory line
    /// in RegisterModelPeripherals and nothing else.
    std::vector<std::unique_ptr<TTDSerializable>> _ownedPeripherals;

    /// Build and register the serializers the active model needs. Called on
    /// StartRecording; cleared by ReleaseModelPeripherals on stop.
    /// Build and register the serializers the active model declares.
    /// @param err optional; set to a human-readable reason on failure
    /// @return false when the model declares state no serializer covers - the
    ///         caller must then refuse to record rather than drop that state
    bool RegisterModelPeripherals(std::string* err = nullptr);
    void ReleaseModelPeripherals();

    /// Fingerprint of the loaded ROM set, stored in the .ttd header so playback
    /// can refuse a session recorded against different ROMs. On Scorpion the
    /// ProfROM image decides what a plane id even means, so replaying against
    /// another image would silently produce wrong pages rather than an error.
    uint64_t ComputeRomSignature() const;

    /// Last captured keyframe index. P-frames between this and the next
    /// I-frame restore by walking deltas from this anchor. Updated on
    /// every OnFrameBoundary when an I-frame is emitted.
    uint64_t _lastKeyFrameIdx = 0;
    /// SetHistoryLimit (0 = no limit). Atomic: a control thread sets them while
    /// the machine's thread enforces them after every capture
    std::atomic<uint64_t> _historyLimitFrames{0};
    std::atomic<uint64_t> _historyLimitBytes{0};
    uint64_t _evictedCheckpoints = 0;   ///< released by the limit in this session
    uint64_t _blobBytes = 0;            ///< device blob bytes of every checkpoint in _timeline (kept with it)
    static uint64_t BlobBytes(const TTDCheckpoint& cp);

    /// Force the next OnFrameBoundary capture to be an I-frame regardless
    /// of the periodic interval. Set when the session is replaced
    /// (InvalidateSession, a file load) so the next capture anchors a fresh
    /// key frame.
    bool _forceNextKeyFrame = true;

    /// Exclusive page-index bound for the active model (set at StartRecording).
    /// Pages in [0, _modelRamPages) are captured; pages in [_modelRamPages,
    /// MAX_RAM_PAGES) are NEVER_TOUCHED.
    ///
    /// This is a BOUND, not a page count: models that map their RAM at
    /// non-contiguous page numbers need a bound above their page count. The 48K
    /// machine has 3 pages but uses page numbers {0, 2, 5}, so its bound is 6
    /// and three slots inside the range stay unused. See ResolveModelRamPages().
    uint16_t _modelRamPages = 0;

    /// Latches the first "dirty page outside the captured range" warning so a
    /// mis-sized model reports once per session instead of every frame.
    bool _dirtyPageOverflowReported = false;

    /// Where this session came from. Set by DeserializeSession, cleared by
    /// StartRecording and InvalidateSession, so GetSessionInfo can tell a
    /// loaded recording from a live one.
    bool        _loadedFromFile = false;
    /// See TTDSessionInfo::inputHistoryComplete: false while the session came
    /// from a file without the input-journal / external-event sections.
    bool        _inputHistoryComplete = true;
    /// The last SeekToInternal ended on a checkpoint without replaying past
    /// it (ResumeRecordingFrom: the resume frame is then collected whole)
    bool        _seekLandedOnCheckpoint = false;
    std::string _sourcePath;
    uint64_t    _capturedAtUnixMs = 0;
    uint8_t     _sessionModelId = 0;
    uint64_t    _loadedRomSignature = 0;  ///< The loaded file's rom_signature
    std::string _loadedRecordedBy;        ///< The loaded file's emulator_id

    /// Per-frame coverage sets backing reverse-search frame skipping.
    TTDCoverageIndex _coverageIndex;
    bool _enableCoverageIndex = true;

    /// Previous-checkpoint page cache for XOR delta computation during capture.
    ///
    /// OPTIMIZATION: During forward recording, computing XOR deltas requires
    /// the previous page content. Without caching, InternXor must decompress
    /// the previous slot (and its entire delta chain back to the I-frame),
    /// causing O(chain_depth) decompression per dirty page per frame.
    ///
    /// With this cache, we store the uncompressed 4KB sub-pages from the
    /// previous checkpoint. The cache is:
    ///   - Populated after each checkpoint capture (current RAM → cache)
    ///   - Used by UpdateRamPages to call InternXorCached instead of InternXor
    ///   - Invalidated on session start/stop/invalidate
    ///
    /// Layout: [page0_sub0, page0_sub1, page0_sub2, page0_sub3, page1_sub0, ...]
    /// Size: _modelRamPages * 4 * 4KB = _modelRamPages * 16KB (e.g., 128KB for 128KB model)
    std::vector<uint8_t> _prevPageCache;
    bool _prevPageCacheValid = false;

    /// Reusable scratch buffer for CollectAndClear (avoids per-frame alloc).
    std::vector<uint16_t> _dirtyScratch;

    /// Input journal — keyboard matrix mutations captured for replay (Item 3).
    /// Dropped on InvalidateSession/StartRecording; truncated by Item 5
    /// Resume-from-past.
    TTDInputJournal _inputJournal;

    /// Journal playback: next event to apply while the machine executes
    /// recorded history (armed by a navigation restore, see ArmInputPlayback)
    size_t _inputCursor = 0;
    bool _inputPlaybackArmed = false;

    /// Live input and machine tasks waiting for the machine's thread
    /// (SubmitLiveInput, SubmitMachineTask)
    std::mutex _pendingInputMutex;
    std::mutex _liveInputInterceptorMutex;
    std::function<bool(const TTDInputEvent&)> _liveInputInterceptor;    // see SetLiveInputInterceptor
    struct PendingInput
    {
        TTDInputEvent ev;
        bool hasNet = false;
        TTDNetInput net;
        std::vector<uint8_t> payload;
    };
    std::vector<PendingInput> _pendingInput;
    std::vector<std::function<void()>> _pendingTasks;

    /// Position the playback cursor at the restored machine time (events
    /// journaled at exactly that time are applied by the next instruction)
    void ArmInputPlayback();
    void DisarmInputPlayback();

    /// Apply one live event on the machine's thread, journaling it first
    /// (stamped with the current time) while recording
    void ApplyLiveInput(TTDInputEvent ev, const TTDNetInput* net = nullptr, const uint8_t* payload = nullptr,
                        uint32_t length = 0);
    bool SubmitLiveInputImpl(const TTDInputEvent& ev, const TTDNetInput* net, const uint8_t* payload, uint32_t length);

    /// Refresh EmulatorContext::kStepWorkTtdInput (the per-step gate)
    void UpdateInputWorkFlag();
    /// Live input other threads queued: applied in order (and journaled), or dropped when the journal owns input
    void DrainPendingLiveInput();

    /// RestoreCheckpoint + ArmInputPlayback + port-journal playback from the
    /// checkpoint's cursor: every restore that navigates history (seek,
    /// reverse queries) - not the capture/restore self-test
    void RestoreCheckpointForReplay(const TTDCheckpoint& cp);

    /// Port-read journal of the session (ttd-port-read-journal.md) and whether
    /// it holds every IN of the history (_portJournalOffReason says why not)
    TTDPortJournal _portReads{TTDPortJournal::Direction::Read};
    TTDPortJournal _portWrites{TTDPortJournal::Direction::Write};
    bool _portJournalValid = false;
    std::string _portJournalOffReason;

    /// Why the current configuration cannot record an isolating port-read
    /// journal; nullptr when it can
    const char* PortJournalUnsupportedReason() const;
    /// Give up the journal for this session (a gap in it): replay falls back
    /// to the live devices
    void DropPortJournal(const char* reason);
    /// Point EmulatorContext::ttdPortReads / ttdPortWrites at the journals while they record
    /// or plays, null otherwise
    void SyncPortJournalHook();

    /// External-event journal — replay barriers for nondeterminism sources
    /// that aren't input-journaled in v1 (Item 6). Same lifecycle as the
    /// input journal: dropped on Invalidate/Start, truncated by Resume.
    TTDExternalEventJournal _externalEvents;

    /// Advisory bookmarks (TD-4) — named annotations BESIDE the barrier
    /// journal above, never inside it. Same lifecycle: dropped on
    /// Invalidate/Start, truncated by Resume, persisted in the .ttd file.
    TTDBookmarkJournal _bookmarks;

    /// Write journal — fast-path accelerator for FindLastAccess (Phase 4;
    /// parent TDD §9.3). 64 MB ring of 12-byte TTDWriteRecords (~5.5M records,
    /// ~50 sec at max intensity). Appended from MemoryWriteDebug / DecodePortOut
    /// hooks. Same lifecycle as other journals: dropped on Invalidate/Start.
    /// Lazily allocated on first StartRecording() when _enableWriteJournal is true.
    std::unique_ptr<TTDWriteJournal> _writeJournal;

    /// Whether to capture write journal entries. When false, journal is empty
    /// and reverse-watchpoint queries fall back to checkpoint replay.
    /// Set via SetEnableWriteJournal() before StartRecording().
    bool _enableWriteJournal = true;

    /// True while the journal holds every write of the session since its start
    /// (journaling on at StartRecording, never switched, no unrecorded run
    /// between a stop and a live resume). Only then may FindLastAccess answer
    /// from it; a loaded file's journal cannot vouch for this and replays.
    bool _journalGapless = false;
    /// See TTDSessionInfo::lastDropReason
    std::string _lastDropReason;
    std::string _unavailableReason;    // see SetUnavailableReason
    /// Why and where _journalGapless dropped, for the session status (MarkJournalGap)
    std::string  _journalGapReason;
    bool         _journalGapHasPosition = false;
    TTDTimePoint _journalGapAt{};
    /// The journal stops covering the session: clear _journalGapless, remember
    /// why/where, warn once. No-op when it was already incomplete.
    void MarkJournalGap(const char* reason, bool hasPosition = true);
    /// No session any more (or a fresh one): forget the previous gap
    void ClearJournalGap();
    /// Position at StopRecording, to tell whether the machine ran before a live resume
    uint64_t _recordingStoppedAtT = 0;

    // -----------------------------------------------------------------------
    // Replay-mode state (Phase 2 Item 2; parent TDD §8.2)
    // -----------------------------------------------------------------------

    /// True while inside EnterReplayMode / ExitReplayMode, so nested calls are safe
    bool _inReplayMode = false;

    /// The host audio hold of the replay (reason TtdReplay): taken by EnterReplayMode after the replay flag is
    /// set, released by ExitReplayMode before it clears it (and with the manager). Replay runs as fast as the
    /// host goes; the user's master mute is never touched
    SoundManager::HostOutputHold _replayHostHold;
    /// Z80 debug mode before replay engaged the debug memory path (restored on exit)
    bool _debugModeBeforeReplay = false;

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
    // RequestInvalidation reason, consumed by OnFrameBoundary (nullptr = none)
    std::atomic<const char*> _pendingInvalidation{nullptr};

    // -----------------------------------------------------------------------
    // Feature-flag stewardship
    // -----------------------------------------------------------------------
    //
    // StartRecording requires both Features::kDebugMode (so Core uses
    // SelectMemoryInterface, which routes writes through MemoryWriteDebug
    // where TTDDirtyTracker::MarkDirty is invoked) and Features::kTimeTravel
    // (so Memory's cached _feature_ttd_enabled flag is true).
    //
    // If either is OFF when StartRecording is called, TTD flips it ON via
    // FeatureManager::setFeature (which cascades through onFeatureChanged
    // -> SelectMemoryInterface + Memory::UpdateFeatureCache). StopRecording
    // restores the prior state, but only for flags we actually toggled —
    // pre-existing user/debugger debug mode is left intact.
    //
    // Toggled flag (true == we turned it ON, so we turn it back OFF on stop).
    bool _toggledDebugModeOn = false;
    bool _toggledTimeTravelOn = false;

    // --- Recording acceleration lock ------------------------------------
    // A recording must capture the code running at real speed. Entering
    // Recording (by any path) engages the lock: host speed multiplier forced
    // to 1x (the emulated hardware turbo is guest behavior and stays), and
    // FeatureManager masks/blocks turbo mode and the tape/disk shortcuts. It is
    // held through Detached - replaying history with a trap or a dilated clock
    // would diverge - and released only when the session returns to Idle.

    /// Every _state write goes through here so no transition bypasses the lock
    void SetState(TTDSessionState next);
    /// Capture needs 'timetravel' (Memory's TTD gate) and 'debugmode' (the
    /// debug write path that marks pages dirty): switch on whichever is off and
    /// remember it for StopRecording. Every way into Recording calls it; the
    /// CPU must be parked (the debugmode switch swaps the memory interface).
    void EngageCaptureFeatures();
    void EngageRecordingLock();
    void ReleaseRecordingLock();

    bool _recordingLockEngaged = false;
    uint8_t _savedHostSpeedMultiplier = 1;  // restored on release
};

} // namespace ttd
