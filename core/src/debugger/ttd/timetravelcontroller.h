#pragma once

/// @file timetravelcontroller.h
/// @brief TimeTravelController — the engine's playback controller (Phase 5, item 2): a
/// copy of v1's manager (deleted in Phase 6) that records into and restores from its own
/// TimeTravelEngine: the engine holds the history (checkpoints, pieces, events,
/// bus and write journals) and writes the session file (Phase 5, C1-C4).
///
/// Per parent TDD §7.1, §10.2. This class owns:
///   - The engine (TimeTravelEngine) - the history's store
///   - The timeline: one side record per engine checkpoint (time, CPU, chipset)
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
/// Never-touched handling:
///   The TDD §6.3 "lazy baseline upgrade" optimization (capture pre-first-
///   write content so never-touched-in-session pages cost zero even on
///   extended-RAM machines) is deferred. The baseline capture at
///   StartRecording stores every model-RAM page (cost: 128 KB – 4 MB,
///   proportional to model RAM size, paid once per session). This is correct
///   and matches the design's restore semantics. The working-set-proportional
///   optimization can be added later as a fast path without changing the
///   public API.

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <map>
#include <mutex>
#include <optional>
#include <vector>
#include <set>
#include <string>
#include <thread>

#include "emulator/execution/executionarbiter.h"
#include "emulator/platform.h"       // PlatformModulesEnum, MAX_RAM_PAGES
#include "emulator/io/keyboard/keyboard.h"  // Keyboard::InputState (display sandbox)
#include "emulator/io/controllers/ihapticfeed.h"  // controllers::HapticProbe (probe-frames)
#include "common/modulelogger.h"    // ModuleLogger
#include "debugger/ttd/timetravelhooks.h"  // ITimeTravelHooks, TTDSessionState, TTDGuardedAction
#include "debugger/ttd/ttdcursorcell.h"   // the published cursor
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/engine/ttdbranchtable.h"
#include "debugger/ttd/ttdsessiontypes.h"  // the session's status, results and options, shared with the controller
#include "emulator/sound/soundmanager.h"  // SoundManager::HostOutputHold (replay hold)
#include "emulator/video/screenmemoryspan.h"  // the picture's memory at a boundary (timeline `screen`)
#include "ttdcheckpoint.h"
#include "ttdexternalevents.h"
#include "ttdfileinfo.h"
#include "ttdbookmarks.h"
#include "ttdsessionfacts.h"
#include "ttdinputjournal.h"
#include "ttdmachineperipherals.h"
#include "ttdv1events.h"
#include "engine/ttdrestoreresult.h"
#include "engine/ttdwriteindex.h"
#include "engine/ttdrecordingwriter.h"
#include "ttdrecordingfolders.h"
#include "emulator/media/mediareadjournal.h"
#include "ttdwritejournal.h"
#include "ttdprobe.h"
#include "ttdcausechain.h"
#include "ttdbacktrace.h"
#include "ttdfindevent.h"
#include "ttdexplainframe.h"
#include "ttddiffmoments.h"
#include "ttdreplaycheck.h"
#include "ttdcoverageindex.h"
#include "ttdtimeline.h"
#include "ttdperipheralregistry.h"
#include "ttdportjournal.h"
#include "ttdportsearch.h"
#include "timetravelframecache.h"
#include "debugger/ttd/engine/ttdregiontracker.h"

// Forward declarations — we don't pull emulator headers into this header.
// (EmulatorContext, Memory, Z80State, EmulatorState are all classes/structs
// defined in the emulator headers; here we only need pointer types.)
class EmulatorContext;
class Memory;
struct Z80State;
struct EmulatorState;
namespace ttd { class TTDDirtyTracker; class TimeTravelEngine; }

namespace ttd {

/// The recording folder's free space (storage.md §4.2, S-7)
enum class TTDDiskSpace : uint8_t
{
    Ok,
    Low,    ///< under max(1 GB, 10 minutes at the measured rate): a warning, the files go on
    Full,   ///< under kTTDDiskStopBytes: the files are finished whole, no more are written; memory records on
};
constexpr uint64_t kTTDDiskStopBytes = 128ull << 20;
constexpr uint64_t kTTDDiskWarnBytes = 1ull << 30;

class TimeTravelController final : public ITimeTravelHooks, public ITTDWriteSink
{
public:
    /// @brief Construct the manager. Does NOT start recording.
    /// @param context  Emulator context (provides Memory, EmulatorState, Z80).
    explicit TimeTravelController(EmulatorContext* context);
    ~TimeTravelController();

    TimeTravelController(const TimeTravelController&) = delete;
    TimeTravelController& operator=(const TimeTravelController&) = delete;

    // -----------------------------------------------------------------------
    // Session lifecycle (control thread; emulator must be paused)
    // -----------------------------------------------------------------------

    /// @brief Begin recording. Captures the baseline checkpoint.
    /// Idempotent: calling while already Recording is a no-op.
    /// @return true if recording was started (or was already active).
    bool StartRecording();
    /// Why the last StartRecording() refused (empty after a start that succeeded): the automation surfaces report it
    const std::string& LastStartError() const { return _lastStartError; }

    /// @brief Make time travel unavailable for this instance, with the reason a
    /// user sees: StartRecording (and the debugger live history built on it)
    /// and DeserializeSession refuse while it is set. Any session held is
    /// dropped. An empty reason makes it available again.
    void SetUnavailableReason(const std::string& reason) override;
    const std::string& GetUnavailableReason() const { return _unavailableReason; }

    /// @brief Stop capturing new frames. History is retained and browsable.
    /// Idempotent: calling while Idle is a no-op.
    void StopRecording() override;

    /// @brief Drop all captured history and return to Idle.
    /// Called by session-invalidation hooks (Reset / Load* / speed change).
    /// The reason is logged and kept as TTDSessionInfo::lastDropReason.
    void InvalidateSession(const char* reason);

    /// ITimeTravelHooks: the session effects of the coordinator's operations (ttdsessionport.cpp)
    void EndSession(const char* reason) override;
    void EndSessionFor(TTDChange change, const char* reason) override;
    bool HasHistory() const override { return !_line->timeline.empty(); }
    const TTDInputJournal& InputJournal() const override { return _line->inputJournal; }

    /// @brief Whether `action` may run now. While an explicit recording runs
    /// (one the user started), a media change is refused - stop the recording
    /// first. A background recording (the black box, a debugger's history) is
    /// not protected: the change ends its session, the next one starts at once
    /// (owner decision 2026-10-07).
    /// @return empty when allowed; otherwise the reason, one sentence a user can
    /// act on. Every automation surface shows it verbatim, and the core paths
    /// that perform the action refuse with it too.
    std::string RecordingGuard(TTDGuardedAction action) const override;

    /// @brief InvalidateSession requested from inside emulation, by a device
    /// whose state TTD cannot follow yet (storage: the SD card, IDE).
    /// Applied at the next OnFrameBoundary, before that frame's checkpoint is
    /// captured, on the frame thread - never from inside a port handler.
    /// No-op unless Recording. `reason` must outlive the call (a literal).
    void RequestInvalidation(const char* reason);
    inline bool IsInvalidationPending() const { return _pendingInvalidation.load(std::memory_order_acquire) != nullptr; }

    /// Any thread: the state is atomic (observers poll it while the machine runs)
    inline bool IsRecording() const override { return _state.load(std::memory_order_acquire) == TTDSessionState::Recording; }

    inline TTDSessionState GetState() const override { return _state.load(std::memory_order_acquire); }
    /// The current session is a background one (the black box, a debugger's
    /// history): it holds no lock and refuses nothing; an outside change ends it
    inline bool IsBackgroundSession() const { return _backgroundSession; }

    /// @brief A debugger needs a history (DeZog, owner decision 2026-10-07): the
    /// recording that runs is used - an explicit one stays explicit; with none,
    /// a background recording starts (it goes on after the retained history
    /// when it can, as a fresh session otherwise). Idempotent: called on every
    /// resume and step. False while browsing a stopped session (Detached).
    bool HoldBackgroundRecording();

    /// @brief The debugger is gone: a background recording it started stops
    /// (history kept, as for the scrubber and the .ttd flows) unless the black
    /// box keeps it. Idempotent
    void ReleaseBackgroundRecording();

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
    TTDSessionInfo GetPublishedSessionInfo() const override;
    static constexpr uint32_t kPublishIntervalMs = 100;
    static constexpr uint32_t kFreshPublishWaitMs = 250;

    /// @brief The session summary for automation status reads, from any thread.
    ///
    /// Live (GetSessionInfo) when nothing else can be changing the session:
    /// the caller is the machine's thread; or no thread executes the machine
    /// and no other control operation is in progress (the control lock is
    /// free) and the session is not recording (a recording machine could be
    /// resumed by another thread mid-read). Otherwise the published snapshot,
    /// which is exact for a parked recording (the machine's thread publishes
    /// as it parks, see OnMachineParking). While the machine runs, a snapshot
    /// older than kPublishIntervalMs is refreshed first: the read waits for the
    /// machine's next frame boundary (one frame, at most kFreshPublishWaitMs) -
    /// so the status follows a running recording instead of the last time
    /// somebody looked. Never pauses the machine.
    TTDSessionInfo ReadSessionInfo() const override;
    /// The published cursor (TTDCursor, ttdcursorcell.h): written at every frame boundary (not by a control thread's
    /// throwaway replays) and by every GetSessionInfo (so at the end of every operation); read lock-free
    TTDCursor PublishedCursor() const override;
    /// D8: the recording is paused for browsing (state detached); see PauseRecordingForBrowsing
    bool IsRecordingPaused() const { return _recordingPaused; }
    /// A start asked while the machine stood inside another CPU's run (Emulator::IsParkedInsideCardRun): the
    /// recording begins at the next frame boundary, its baseline the frame's start (debuggers.md §7)
    bool IsStartPending() const { return _startPending.load(std::memory_order_acquire); }

    /// @brief The machine's thread, about to park (pause): publish the
    /// recording's summary so status reads while paused are exact
    void OnMachineParking() override;

    /// @brief History limit: while recording, the oldest history is released
    /// in whole segments (D41) - the engine keeps at least the last
    /// `maxFrames` frames (a ring; its segments are an eighth of the window,
    /// so it holds up to 1/8 more), and drops segments while the store holds
    /// more than `maxBytes`. 0 = no limit for that measure (the default for
    /// both). What stays is a complete, shorter session starting at a
    /// segment's baseline; the journals are cut to the new start.
    void SetHistoryLimit(uint64_t maxFrames, uint64_t maxBytes);
    /// Bytes of history held now: the engine's piece store and the first
    /// checkpoint's device blobs (what the byte limit measures)
    uint64_t HistoryBytes() const;

    /// Record into @p engine instead of the controller's own (the "shadow"
    /// engine, a name from the TTD v2 migration, Phase 1, Step 4): every capture
    /// is handed to it - the same dirty pages, live memory, CPU and device
    /// state. When the history is cleared, loaded or cut short, the engine
    /// starts a new session; after a restore it rescans all pieces once. Null
    /// detaches and leaves the engine's session as it is.
    /// Tests and benchmarks only
    void SetShadowEngine(TimeTravelEngine* engine);
    /// Shadow mode (Phase 4): write the shadow engine's session as it records,
    /// a file per segment, into a recording folder under @p root (empty, the
    /// default: no files). A stop finishes the files; invalidating the session
    /// deletes its folder. For tests and the benchmark until Phase 5
    void SetShadowRecordingRoot(const std::string& root) { _shadowRecordingRoot = root; }
    /// The folder the shadow session is written to (empty when none)
    std::string ShadowRecordingFolder() const;
    /// The folder's free space as last checked (every 25 captures and at each new file)
    TTDDiskSpace ShadowDiskSpace() const { return _shadowDiskSpace; }
    /// Tests: the free bytes of the folder's volume (default: std::filesystem::space)
    void SetFreeSpaceProbeForTesting(std::function<uint64_t(const std::string&)> probe)
    {
        _freeSpaceProbe = std::move(probe);
    }
    TimeTravelEngine* GetShadowEngine() const { return _shadowEngine; }
    /// The engine this controller records into and restores from (Phase 5, C1)
    TimeTravelEngine& GetEngine() { return *_engine; }
    const TimeTravelEngine& GetEngine() const { return *_engine; }

    /// Seeks restore from @p engine's checkpoints and replay its event log and
    /// bus journals (a session fed into another engine, tests). Null: the
    /// session's own engine, the default
    void SetReplaySource(TimeTravelEngine* engine);
    TimeTravelEngine* GetReplaySource() const { return _replayEngine; }
    /// The settings and media check of the last restore from the replay
    /// engine (Phase 3, Step 4; FR-14): ConfigurationDiffers for each setting
    /// the session was recorded with that this machine lacks (a replay:
    /// NotBitExact; the model or RAM size: Degraded), MediaVersionDiffers for
    /// a medium that changed since the checkpoint and cannot go back. Exact
    /// when everything matches. The restore and the replay run either way
    const TTDRestoreResult& LastEngineCheck() const { return _lastEngineCheck; }

    /// @brief Called by FeatureManager when feature flags change.
    /// Deallocates write journal when TimeTravel feature is disabled.
    void UpdateFeatureCache() override;
    void StopForFeatureChange(const char* feature) override;
    bool LocksAcceleration() const override { return !_blackBox; }
    std::string SpeedLock() const override;
    bool RecordsShortcuts() const override { return true; }
    void OnAccelerationChanging(bool accelerating) override;

    /// @brief The black box (Phase 5, Step 3; D29): a recording that keeps the
    /// last @p minutes and never holds the machine at real speed - before an
    /// acceleration (host speed above 1x, turbo mode, turbo tape's warp) it
    /// stops, its history kept, and once nothing accelerates a new session
    /// starts. The tape trap is a recorded edit; fast disk is held (its FDC timing is no edit). Real-time clocks still run on emulated time. Applies to
    /// the next StartRecording; off: an explicit recording (the lock)
    void SetBlackBox(bool on, uint32_t minutes = 5);
    /// The recording session as a refusal names it: "#3, started at frame 1200" (the instance's recordings
    /// counted from 1). Empty while nothing records
    std::string RecordingSessionLabel() const;
    bool IsBlackBox() const { return _blackBox; }
    /// The black box's window in minutes; a running black box keeps the new window at once
    void SetBlackBoxMinutes(uint32_t minutes);
    /// Whether host speed or turbo mode is on now
    bool AccelerationActive() const;

    // -----------------------------------------------------------------------
    // Session configuration (v2 optimizations)
    // -----------------------------------------------------------------------

    /// @brief Switch the write journal on or off (D40). Off by default.
    ///
    /// The journal records every memory write (time, address, value, PC,
    /// page) so "who wrote address X last" answers at once. Port writes are
    /// not in it: the port journal, recorded in every session, has them.
    /// Allowed at any moment, also during a recording and inside a frame (a
    /// breakpoint handler on the emulation thread may call it): switching on
    /// opens a journal segment at the current instruction, switching off
    /// closes it. Outside the segments a write search uses the coverage index
    /// and replays one frame. Switching it off with no session frees the
    /// pre-allocated journal.
    bool SetEnableWriteJournal(bool enable);
    /// The same from a control thread (automation, UI) while the machine may
    /// be running: pauses it, switches at the instruction it stopped on, and
    /// resumes it. The emulation thread itself (a breakpoint handler) calls
    /// SetEnableWriteJournal directly
    bool SwitchWriteJournal(bool enable);
    bool GetEnableWriteJournal() const { return _enableWriteJournal; }

    /// @brief The write journal ring's size in bytes (0: the default, 64 MB,
    /// 8,388,608 records). Memory is committed as the ring fills, so a large
    /// ring costs only what it holds. Takes effect at the next recording;
    /// refused (false) while a session exists (recorded or loaded). Experiments that need a session's
    /// whole write history (E7) record with a ring that does not wrap
    bool SetWriteJournalCapacity(size_t bytes);
    size_t GetWriteJournalCapacity() const { return _writeJournalBytes; }

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

    /// @brief Write the session to @p out in the engine's session file format
    /// (Phase 5, C4b): the engine's data, and as holder streams the facts a
    /// loader checks (model, ROM set, recorded devices), the coverage index and
    /// the bookmarks. While recording, the engine first takes the journals of
    /// the current frame. The caller has paused the emulator.
    /// With branches the file holds every live branch; @p lineageOnly writes the active branch's lineage alone as a
    /// linear file (TTD branches storage.md §5): the trunk up to the first fork, each branch up to the next, the
    /// lineage's bookmarks on the trunk, no coverage index (each branch's starts at its fork)
    /// @return false with @p err set when there is no session or a write fails
    bool SerializeSession(std::ostream& out, std::string& err, bool lineageOnly = false);

    /// @brief Replace the session by the one in @p in, an engine session file.
    /// Refused, leaving the current session as it was, when the file is
    /// unreadable or a (deleted) v1 file, recorded on another model or ROM set,
    /// or with devices this machine lacks. The session is Idle afterwards;
    /// SeekTo browses it, ResumeRecordingFrom continues it
    bool DeserializeSession(std::istream& in, std::string& err);
    /// DeserializeSession from a file, read where the loader asks: the file is
    /// not copied into memory first ("Cannot open file: <path>" when it cannot be opened)
    bool DeserializeSessionFile(const std::string& path, std::string& err);

private:
    /// The engine this controller records into and restores from (Phase 5, C1): created
    /// with the controller and destroyed last (declared first), after everything that
    /// points into it
    std::unique_ptr<TimeTravelEngine> _engine;

    /// Load @p source into a fresh engine, checked against this machine and
    /// bound to it; nothing of the current session changes
    /// The load from any byte source (a file, memory)
    bool DeserializeSessionFrom(const ITTDByteSource& source, std::string& err);
    std::unique_ptr<TimeTravelEngine> LoadEngineSession(const ITTDByteSource& source, TTDSessionFacts& facts,
                                                        std::vector<uint8_t>& coverage, TTDBookmarkJournal& bookmarks,
                                                        std::string& err);
    /// Make @p loaded the session: the side table, the controller's journals and indexes from it
    /// A loaded file's branches (TTD branches P2): per branch line its record, coverage and bookmarks, the
    /// session's branch state; LoadEngineSession fills it, CommitLoadedSession builds the tree from it
    struct LoadedBranches
    {
        std::map<uint32_t, std::vector<uint8_t>> records;
        std::map<uint32_t, std::vector<uint8_t>> coverage;
        std::map<uint32_t, std::vector<TTDBookmark>> bookmarks;
        std::vector<uint8_t> state;
    };
    LoadedBranches _loadedBranches;
    /// The loaded file's branches into the tree and their controller lines, each built by @p buildLine on its
    /// own lineage; the saved active branch becomes the active one
    void CommitLoadedBranches(const std::function<void(const std::vector<uint8_t>&)>& buildLine);
    /// Save with branches: each branch's record and coverage, the session's branch state
    void AddBranchesToSave(TTDSessionSaveParams& params);
    /// Save the active lineage alone: the engine's lineage file, the lineage's bookmarks moved to the trunk
    void AddLineageToSave(TTDSessionSaveParams& params);
    void CommitLoadedSession(std::unique_ptr<TimeTravelEngine> loaded, const TTDSessionFacts& facts,
                             const std::vector<uint8_t>& coverage, TTDBookmarkJournal& bookmarks);

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
    void OnFrameBoundary() override;

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

    /// @brief The memory work of the last checkpoint restore: inside a KnownMemoryScope only the written and the
    /// changed pieces are decoded
    const TTDRestoreStats& LastMemoryRestoreStats() const { return _lastMemoryRestore; }

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
        explicit ReplayModeScope(TimeTravelController& manager) : _manager(&manager) { manager.EnterReplayMode(); }
        ~ReplayModeScope() { Exit(); }
        ReplayModeScope(const ReplayModeScope&) = delete;
        ReplayModeScope& operator=(const ReplayModeScope&) = delete;
        void Exit()
        {
            if (TimeTravelController* manager = _manager)
            {
                _manager = nullptr;
                manager->ExitReplayMode();
            }
        }

    private:
        TimeTravelController* _manager;
    };

    /// @brief A run of replays that only the replay executes between (a verb's frame loop, a seek and its
    /// picture): inside it a checkpoint restore trusts what the previous restore left in memory, apart from the
    /// pieces the trackers marked since (_engineMemoryKnown). Nests; the outermost end forgets
    class KnownMemoryScope
    {
    public:
        explicit KnownMemoryScope(TimeTravelController& manager) : _manager(manager) { ++_manager._knownMemoryDepth; }
        ~KnownMemoryScope()
        {
            if (--_manager._knownMemoryDepth == 0)
                _manager._engineMemoryKnown = false;
        }
        KnownMemoryScope(const KnownMemoryScope&) = delete;
        KnownMemoryScope& operator=(const KnownMemoryScope&) = delete;

    private:
        TimeTravelController& _manager;
    };

    /// @brief Query the replay-mode flag. Reads `_context->ttdReplayActive`.
    /// Defined out-of-line (EmulatorContext is only forward-declared here).
    bool IsReplayActive() const override;

    /// The bytes a tool edit changes in memory the CPU sees, as a replay applies the edit: a fast tape load or a
    /// script's poke reaches memory past the bus, so the analysis replay (replaytracer.cpp) folds these as the
    /// writes of the instruction that made the edit. Only bytes whose value changes are reported for a page image
    using EditWriteObserver = std::function<void(uint16_t z80, uint8_t value)>;
    void SetEditWriteObserver(EditWriteObserver observer) { _editWriteObserver = std::move(observer); }

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
    void RecordInputEvent(uint8_t key, bool pressed) override;

    /// @brief Journal a Kempston Mouse mutation (same contract as RecordInputEvent:
    /// callers gate on IsRecording() and !IsReplayActive(), and call BEFORE applying).
    void RecordMouseMove(int dx, int dy) override;
    void RecordMouseButtons(uint8_t activeLowMask) override;
    void RecordMouseWheel(int steps) override;
    void RecordMouseCounters(uint8_t x, uint8_t y);

    /// @brief Journal a whole-matrix keyboard reset (release of every key)
    void RecordKeyboardReset();

    /// @brief Read-only access to the input journal (playback cursor, tests).
    inline const TTDInputJournal& GetInputJournal() const { return _line->inputJournal; }

    /// @brief The session's IN / OUT journals: the engine's bus journals, recorded straight into
    /// (port-journals-on-engine.md)
    inline const TTDPortJournal& GetPortReadJournal() const { return _engine->BusReads(); }
    inline const TTDPortJournal& GetPortWriteJournal() const { return _engine->BusWrites(); }

    /// @brief "When did the program ..." over the port journals
    /// (ttdportsearch.h): no replay, works on loaded files. Fails with a reason
    /// when the session has no port journals, or while a recording is running
    /// (pause it: the journals are only written by the running emulation)
    TTDPortSearchResult SearchPortEvents(const TTDPortQuery& q) const override;

    /// @brief The same search over a .ttd file on disk, without loading it:
    /// the current session and machine are not touched. The whole file is
    /// read and checked like a load (CRCs, section layout); the checks that
    /// tie a session to this machine (model, ROM set, sound slots) are skipped
    /// - the journals do not need them. Fails with a reason for an unreadable
    /// file or one without port journals
    TTDPortSearchResult SearchPortEventsInFile(const std::string& path, const TTDPortQuery& q);

    /// @brief The machine at each access of `hits` (port-events' context, phase 2 D5): the registers at the M1
    /// of the IN / OUT instruction (before it ran) and the word at SP then. Replays to each access and puts the
    /// machine back verbatim, as ReadCpuView does; a running recording pauses for browsing. One entry per hit
    std::vector<TTDPortContext> PortAccessContexts(const std::vector<TTDPortHit>& hits);

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
    bool OwnsInput() const override;

    /// @brief The one entry point for live input (any thread). Refused (false)
    /// while OwnsInput(). While the emulator loop runs, the event is queued
    /// and applied - and journaled when recording - by the machine's thread at
    /// the next instruction boundary (ServiceInput); in synchronous mode (loop
    /// not running, the caller is the only thread) it is applied at once.
    /// `ev.time` is ignored: the time is stamped when the event is applied.
    bool SubmitLiveInput(const TTDInputEvent& ev) override;

    /// @brief SubmitLiveInput for a NetEvent: its network record and bytes are
    /// copied; the journal keeps them while recording, and the virtual network
    /// reads them when the event is applied.
    bool SubmitLiveInput(const TTDInputEvent& ev, const TTDNetInput& net, const uint8_t* payload, uint32_t length) override;

    /// @brief A lockstep group (the ZX-Poly master) takes live input itself, to
    /// give it to every member at one frame boundary. SubmitLiveInput hands each
    /// event to `interceptor` first; when it returns true the event is consumed
    /// there. Events it declines (false) take the normal path. An empty function
    /// removes it
    void SetLiveInputInterceptor(std::function<bool(const TTDInputEvent&)> interceptor) override;

    /// @brief Run `task` on the machine's thread - for automation actions that
    /// touch a device the executing thread may be using (a NeoGS SD card
    /// insert / eject, a flash save). Same delivery as SubmitLiveInput: queued
    /// for the next instruction boundary while the emulator loop runs (while
    /// paused: when execution continues), run at once otherwise. Not
    /// journaled: a task is not replayable input. Refused while OwnsInput().
    using MachineTaskResult = TTDMachineTaskResult;
    MachineTaskResult SubmitMachineTask(std::function<void()> task) override;

    /// @brief Executing thread, before every instruction (Z80::StepInstruction;
    /// cheap gate: EmulatorContext::kStepWorkTtdInput in stepWork): play due journal events,
    /// then apply queued live input.
    void ServiceInput() override;
    uint32_t InputIdleLimit() const override;

    /// RZX playback while recording (Phase 3, Step 2): the CPU reports each
    /// RZX frame end at the end of the step that ended it (@p rzxFrame: the
    /// frames done after it, @p interrupt: the step took the interrupt); the
    /// playback reports its end. Facts for the shadow engine; nothing while
    /// not recording or while a replay runs
    void NoteRzxFrameEnd(uint64_t rzxFrame, bool interrupt) override;
    void NoteReplaySource(TTDReplaySource source) override;

    /// @brief The machine left the recorded timeline from outside (reset):
    /// a Detached session returns to Idle and journal playback stops.
    void OnMachineReset() override;

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
    uint64_t CurrentFrame() const override { return CurrentPosition().frame; }

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

    /// @brief A position as one absolute count of TTD time units: machine time
    /// on the engine's frame table (Phase 5, C3), so a frame of another length
    /// (Sprinter 320 / 312 lines) keeps the order. The write journal's globalT,
    /// find-last's beforeGlobalT, the journal segments and the engine's events
    /// share it. Without a session: frame x FrameSpan() + tInFrame
    uint64_t GlobalT(const TTDTimePoint& at) const;
    /// GlobalT of a moment in @p frame, the start of the frame cached: a journaled write asks for the frame being
    /// recorded at every store, and the frame table's lookups cost more than the record itself. The start depends
    /// only on the frame, the table (its generation) and the frame span, so the cache is keyed by them
    uint64_t GlobalTInFrame(uint64_t frame, uint32_t tInFrame) const;
    struct FrameStartCache
    {
        uint64_t frame = UINT64_MAX;
        uint64_t generation = 0;
        uint32_t span = 0;
        bool open = false;
        uint64_t start = 0;
        const TTDFrameTable* table = nullptr;   ///< the lineage line's table the start came from
    };
    mutable FrameStartCache _frameStartCache;
    /// @brief The position of machine time @p globalT (the inverse of GlobalT)
    TTDTimePoint TimePointAt(uint64_t globalT) const;
    /// @brief The first barrier in (@p from, @p to] on the engine's event log,
    /// as a marker (TTDExternalEvent, C3): what a replay cannot reproduce. Tape control and
    /// debugger edits with their bytes are input the replay applies, not barriers
    std::optional<TTDExternalEvent> FirstBarrierBetween(const TTDTimePoint& from, const TTDTimePoint& to) const;
    /// @brief The engine gets what the controller's journals hold since the last boundary
    /// (bus records, input, markers, facts): at a stop, and before a replay
    /// while recording
    void FlushToEngine();
    /// @brief The engine's write index takes the live ring's records and its
    /// spans, and the ring is emptied: it holds one frame's writes at most
    /// (at each boundary, at a stop)
    void DrainWritesToEngine();
    uint64_t _journalLostUpTo = 0;   ///< a frame overflowed the ring: the journal covers only after this

    /// @brief Upper bound of the recorded timeline.
    ///
    /// Returns the time of the last checkpoint. Seeks to any point > this
    /// will fail. Returns {0,0} when the timeline is empty. While a recording
    /// runs, another thread gets the end the machine's thread noted (no park)
    TTDTimePoint SessionEndPosition() const;

    /// @brief Where "frame N" without a T-state lands (D13): the end of frame
    /// N, {N+1, 0}, when the history reaches it; otherwise the history's end
    /// if that lies inside frame N (a recording's present counts as its end).
    /// A frame past the history gives {N+1, 0}, which a seek refuses
    TTDTimePoint FrameEndPosition(uint64_t frame) const;
    /// @brief Frame N's length in tInFrame units (the frame table's, so a
    /// frame whose length changed reports its own); FrameSpan() outside the session
    uint32_t FrameLength(uint64_t frame) const;

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
    // Sources of nondeterminism that the input journal does not cover (tape
    // control, disk writes, debugger-initiated state edits) get a marker on
    // the timeline. Markers are replay barriers: SeekTo refuses
    // to cross them silently and surfaces the marker to the caller via
    // TTDSeekResult. This keeps TTD honest — it never pretends to reproduce
    // what it cannot. Journals (input today, tape/disk later) progressively
    // convert marker classes into replayable events.
    //
    // Item 6 shipped the data structure, the capture API, and the
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
    void RecordExternalEvent(TTDExternalEventKind kind, const char* reason) override;
    void NoteMediaChange(const char* slot, const char* what) override;
    /// The same, stamped at @p at (a tool edit's event sits where the edit began)
    void RecordExternalEventAt(TTDExternalEventKind kind, const char* reason, const TTDTimePoint& at);

    /// A tool's edit of the machine while recording (Emulator::EditMemoryFromTool,
    /// Phase 3): BeginToolEdit before the edit, EndToolEdit after it. The edit
    /// is a DebuggerEdit marker in the marker journal and, with its bytes - the
    /// RAM pages and device-memory pieces written since the last checkpoint,
    /// and every device state the edit changed - an input event the engine's
    /// replay applies (no barrier)
    void BeginToolEdit() override;
    void EndToolEdit(const char* source) override;
    /// A logic callback's writes (ScriptCallbackManager::ApplyEdits): an edit with only those bytes and, with @p cpu,
    /// the CPU, which the engine's replay applies at the same boundary
    bool RecordsScriptEdits() const override { return true; }
    void RecordScriptEdit(const char* source, const std::vector<std::pair<uint16_t, uint8_t>>& pokes,
                          bool cpu) override;
    /// A card hook's writes (GSCardTarget): one edit of kind-10 records at the main CPU's position; the replay hands
    /// them to the card's target a frame at a time (ServiceInput), since the card runs behind the main CPU
    void RecordCardEdits(const char* source, const std::vector<TTDCardEdit>& edits) override;

    /// The bytes of the tool edit recorded as marker @p markerIndex (empty: none)
    const std::unordered_map<size_t, std::vector<uint8_t>>& ToolEditPayloads() const { return _line->toolEditPayloads; }

    /// @brief Read-only access to the marker journal. Used by tests, the UI,
    /// and automation surfaces that surface the marker list.
    inline const TTDExternalEventJournal& GetExternalEvents() const { return _line->externalEvents; }

    // -----------------------------------------------------------------------
    // Seek engine — barrier-aware overload (Phase 2 Item 6)
    // -----------------------------------------------------------------------

    using TTDSeekHaltReason = ttd::TTDSeekHaltReason;

    using TTDSeekResult = ttd::TTDSeekResult;

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

    using TTDClipExportOptions = ttd::TTDClipExportOptions;

    using TTDClipExportResult = ttd::TTDClipExportResult;

    /// @brief Write every frame of [fromFrame, toFrame] as its final picture -
    /// the same picture positioning by frame number shows - into `directory`:
    /// rgba_NNNN.zst (RGBA8), planeb_NNNN.zst (plane B, when the zxdlss feature
    /// is on), meta.jsonl (frame, #7FFD, displayed screen, border at the frame's
    /// start) and clip.json (geometry, encodings). One call instead of one
    /// seek + capture round trip per frame. Leaves the machine positioned and
    /// displayed at toFrame. The emulator must be paused; refused while recording.
    TTDClipExportResult ExportClip(const TTDClipExportOptions& options);

    using TTDComposedFrame = ttd::TTDComposedFrame;
    using TTDFrameVisitor = ttd::TTDFrameVisitor;

    /// @brief Walk [fromFrame, toFrame] and hand every frame's final picture
    /// (and plane B) to `visit` - the walk ExportClip writes to disk, for tools
    /// that process the frames in memory (tools/verification/zxdlss renders
    /// TTD files through de-flicker algorithms). Same preconditions as
    /// ExportClip; leaves the machine displayed at the last frame visited.
    /// @return empty on success, otherwise the reason
    std::string VisitComposedFrames(uint64_t fromFrame, uint64_t toFrame, const TTDFrameVisitor& visit);
    /// VisitComposedFrames as one operation of the session (its lock, the CPU held), for a caller outside the TTD
    /// (capture::ExportTtdClip writes the frames as a video file)
    std::string VisitComposedFramesAsOperation(uint64_t fromFrame, uint64_t toFrame, const TTDFrameVisitor& visit);

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
    // ResumeRecordingFrom, persisted in the .ttd file as the "bookmarks"
    // holder stream (ttdcontrollersessionfile.cpp).

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

    /// @brief Add a bookmark with its note, tags and watched ranges (agent notes, 2026-11-03). With
    /// @p captureContext the context (registers, the instruction at PC, its page, the paging latch) and the watched
    /// bytes are taken at the bookmark's moment: the current position as the machine stands, another moment by one
    /// silent seek with the machine put back (a live recording pauses for browsing then). A moment the replay cannot
    /// reach leaves the context invalid and the bytes empty; the bookmark is still added. The same position checks
    /// as above, and the bounds of ValidateBookmark.
    bool AddAgentBookmark(TTDBookmark bookmark, bool captureContext, std::string* err);

    /// @brief Change a bookmark's note, tags or label under the journal's lock (TTDBookmarkJournal::Edit).
    bool EditBookmark(const std::string& label, const std::function<void(TTDBookmark&)>& edit, std::string* err);

    /// @brief All bookmarks, time-sorted (thread-safe snapshot copy).
    std::vector<TTDBookmark> GetBookmarks() const;
    /// @brief Every bookmark of the session, on any branch (GetBookmarks lists the active lineage's: TTD branches §5.1)
    std::vector<TTDBookmark> GetAllBookmarks() const;

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

    /// @brief Resume recording from a historical position; the old future is kept.
    ///
    /// Algorithm (parent TDD §8.3, TTD branches architecture.md §4.5):
    ///   1. Validate preconditions (state, bounds).
    ///   2. SeekTo(from) — ensures the emulator is positioned at `from`.
    ///      No-op-equivalent if already there (re-restore is deterministic).
    ///   3. History after `from`: a new branch forks there (ForkBranchAt), the old future stays on its parent.
    ///      At the active branch's end: the line is cut at the resume point (its partial last frame) and goes on.
    ///   4. The new line's journals, markers, bookmarks and coverage after the resume point are dropped.
    ///   5. _state = Recording.
    ///
    /// @param from  Position to resume from. Must be within the current
    ///              recorded timeline bounds (<= SessionEndPosition()).
    /// @return true on success. False (with a logged warning) if state is
    ///         Idle, the timeline is empty, or `from` is out of bounds.
    bool ResumeRecordingFrom(const TTDTimePoint& from);

    /// region <Branches> (docs/inprogress/2026-11-08-ttd-branches/architecture.md §4-5)

    /// The session's branches: identities, metadata, the tree. The trunk alone until a resume (or an edit) in the
    /// past keeps the old future: a fork at that moment, its branch active from then on
    const TTDBranchTable& Branches() const { return _branches; }
    /// The active branch: the end of the lineage shown, continued at its end
    uint32_t ActiveBranch() const { return _tip; }
    /// The branch the machine stands on: the active lineage's owner of the current moment
    uint32_t ViewedBranch() const { return _viewId; }
    /// Make @p branch active and stand at @p at on its lineage (default: its last visited moment, else its end).
    /// A recording pauses for browsing first
    bool SwitchBranch(uint32_t branch, const std::optional<TTDTimePoint>& at, std::string& error);
    /// Delete @p branch and its descendants (TTDBranchTable::Delete: never the trunk or the active lineage, pinned
    /// ones only with @p force); their history leaves memory. @p removed: the ids deleted
    bool DeleteBranch(uint32_t branch, bool force, std::vector<uint32_t>& removed, std::string& error);
    /// Edit @p branch's name, note, tags or pin (the table's rules: a name stays unique among the live branches)
    bool UpdateBranch(uint32_t branch, const TTDBranchUpdate& update, std::string& error);
    /// Who asks for the next fork and why (createdBy, run, tags, note, name; reason when empty: resume): taken by
    /// the next fork only
    void SetNextBranchMeta(TTDBranchMeta meta)
    {
        _nextBranchMeta = std::move(meta);
        _nextBranchMetaSet = true;
    }
    /// branch-create (surfaces.md §5.1): the next resume forks a branch with @p meta even at the active branch's
    /// end, where a resume only goes on. Taken by the next resume; CancelBranchCreate drops it when none came
    void PrepareBranchCreate(TTDBranchMeta meta)
    {
        SetNextBranchMeta(std::move(meta));
        _forceFork = true;
    }
    void CancelBranchCreate()
    {
        _forceFork = false;
        _nextBranchMetaSet = false;
    }
    /// The lineage spans of the active branch (trunk first)
    std::vector<TTDLineageSpan> ActiveLineage() const { return _branches.Lineage(_tip); }
    /// The branches as last published (no fresh publish asked)
    TTDBranchesPublished PublishedBranches() const override;

    /// endregion </Branches>
    /// The media a recording continued from @p from could not set back there (M5, F11): another medium in the
    /// slot, or a version its undo log does not reach. One MediaVersionDiffers issue per slot, detail
    /// "<slot>: <why>"; empty when every medium goes back. Changes nothing
    std::vector<TTDRestoreIssue> PlanCut(const TTDTimePoint& from) const;

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
        _line->coverageIndex.Record(TTDCoverageKind::Read, MakeCoverageKey(physPage, addr));
    }

    inline void RecordExecutedCoverage(PhysPage physPage, uint16_t pc)
    {
        // Inline for the same reason TTDCoverageIndex::Record is: this sits on
        // the instruction-fetch path, so the call itself was the cost.
        if (_state != TTDSessionState::Recording)
            return;

        // ROM execution is recorded under the kPhysPageNone bucket, not
        // dropped — see RecordReadCoverage for why.
        _line->coverageIndex.Record(TTDCoverageKind::Executed, MakeCoverageKey(physPage, pc));
    }

    /// @brief Read-only access to the coverage index (control thread).
    inline const TTDCoverageIndex& GetCoverageIndex() const { return _line->coverageIndex; }

    /// @brief Is per-frame coverage collection enabled for new sessions?
    inline bool IsCoverageIndexEnabled() const { return _enableCoverageIndex; }
    void SetEnableCoverageIndex(bool enable);
    /// EmulatorContext::ttdCoverage: this session's coverage index while it records with
    /// coverage on, null otherwise - the core's inline record needs no state check
    void SyncCoverageSink();

private:
    /// @brief Make the current position visible: compose its picture
    /// (ComposeDisplay) and publish it.
    ///
    /// The single display step of every user-facing navigation (public SeekTo
    /// and the operations built on it, StepForwardInstruction). Internal
    /// restores during search, reverse execution and frame-cache builds never
    /// reach it, so they cannot touch the display.
    /// @param frameTarget true at a frame boundary: the frame that ended there
    void PresentPosition(bool frameTarget);

    /// @brief Flush the video delay line and post NC_VIDEO_FRAME_REFRESH so
    /// every observer (Qt present queue, WebAPI capture, viewers) sees the
    /// framebuffer PresentPosition composed.
    void PublishSeekedFrame();

public:

private:
    /// @brief May the coverage index be used to skip frames for this query?
    ///
    /// Read, Execute and Write searches reach the replay loop (a Write only
    /// for frames outside the write journal's segments; Io answers from the
    /// port journal), and pruning is sound only when the
    /// query's Z80 address range collapses to one non-wrapping offset interval
    /// inside a 16 KB page. A range spanning a page boundary, or wider than a
    /// page, could match any offset, so it is left unpruned rather than
    /// approximated.
    /// May frame `frame` hold an access q matches (CanPruneByCoverage first): the coverage keys name the byte's true
    /// offset in its page (Memory::TtdKeyOf), so on 8K slots a query with no known half asks both halves
    bool CoverageMayContain(TTDCoverageKind kind, uint64_t frame, const TTDSearchQuery& q) const;

    inline bool CanPruneByCoverage(const TTDSearchQuery& q) const
    {
        if (!_enableCoverageIndex)
            return false;
        if (q.access != TTDAccessType::Read && q.access != TTDAccessType::Execute && q.access != TTDAccessType::Write)
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
                           uint16_t m1pc, PhysPage physPage, uint8_t flip = 0) override;
    
    /// @brief Hot-path capture: record a port OUT (used for IO probe).
    ///
    /// Same threading/lifecycle as RecordMemoryWrite but for port writes
    /// (decoder::DecodePortOut path). Marked with isIo=1 in the record.
    void RecordIoWrite(uint16_t port, uint8_t value, uint16_t m1pc) override;
    
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

    /// @brief Regenerate one recorded frame's memory writes by replaying it
    /// (TTD v2 Phase 3, Step 7: the write journal as an index derived from a
    /// sealed replay). Restores the frame's checkpoint and replays the whole
    /// frame with every memory write collected, in execution order (time,
    /// address, value, PC, physical page), as the write journal recorded
    /// them. The machine is left at the frame's end.
    /// @return false when the session has no checkpoint of @p frame or none
    ///         after it, while recording, or when a barrier marker lies in the frame
    bool RegenerateFrameWrites(uint64_t frame, std::vector<TTDSearchResult>& out);

    /// @brief Build the write journal for a span of recorded history by
    /// replaying it (D40, Phase 3 J2). Every frame that overlaps machine time
    /// (fromT, toT] and is not inside a journal segment yet is replayed with
    /// every memory write collected; the writes join the journal in time
    /// order and the frames join its segments. Slow - about 2-4 ms per frame,
    /// like RZX playback - so @p progress reports each frame and can stop
    /// the build (the frames built so far are kept). The session's last
    /// frame (no checkpoint after it) and frames holding a marker without
    /// its data (a barrier) are not built. Refused while recording. The machine returns
    /// to where it stood (positioned in history, as after any search).
    TTDJournalBuildResult BuildWriteJournal(uint64_t fromT, uint64_t toT,
                                            const TTDJournalBuildProgress& progress = nullptr);
    /// The same by frame numbers: frames @p fromFrame to @p toFrame, both
    /// included (UINT64_MAX: to the session end)
    TTDJournalBuildResult BuildWriteJournalFrames(uint64_t fromFrame, uint64_t toFrame,
                                                  const TTDJournalBuildProgress& progress = nullptr);
    using JournalBuildState = ttd::TTDJournalBuildState;
    JournalBuildState GetJournalBuildState() const
    {
        return {_journalBuildActive.load(), _journalBuildDone.load(), _journalBuildTotal.load()};
    }
    /// Any thread: the running build stops after its current frame (what it
    /// built is kept). No build running: nothing happens
    void CancelJournalBuild() { _journalBuildCancel.store(true); }

    /// @brief Probe coverage for a specific frame and address range (TD-7 §3.1.1).
    TTDCoverageProbeResult QueryCoverageProbe(
        uint64_t frame,
        TTDCoverageKind kind,
        uint16_t addrFrom,
        uint16_t addrTo,
        std::optional<PhysPage> physPage = std::nullopt) const;

    /// @brief Scan frames in [fromFrame, toFrame] touching range (TD-7 §3.1.2).
    ///
    /// The list keeps the first `limit` matches in scan order: oldest first, or newest first with
    /// `newestFirst`. With `asRanges` it holds runs of consecutive matching frames and `limit` counts runs.
    /// The counts and first / last match cover the whole window either way.
    TTDCoverageScanResult QueryCoverageScan(
        uint64_t fromFrame,
        uint64_t toFrame,
        TTDCoverageKind kind,
        uint16_t addrFrom,
        uint16_t addrTo,
        std::optional<PhysPage> physPage = std::nullopt,
        size_t limit = 200,
        bool newestFirst = false,
        bool asRanges = false) const;

    /// Most items a coverage scan list returns (frames or runs)
    static constexpr size_t kCoverageScanLimitMax = 10000;

    /// @brief The keys of [fromFrame, toFrame] touched in at most @p maxFrames frames, as blocks (D1 of
    /// docs/inprogress/2026-10-28-ttd-analysis-feedback/phase-2-plan.md): one pass over the index, no replay.
    ///
    /// A block joins neighbouring keys of one page (executed: up to 4 bytes apart, an instruction's length;
    /// written and read: adjacent) touched in the same frames. The range and page filter as in QueryCoverageScan.
    /// The list keeps the first `limit` blocks, rarest first; the counts cover the whole window.
    TTDCoverageRareResult QueryCoverageRare(
        uint64_t fromFrame,
        uint64_t toFrame,
        TTDCoverageKind kind,
        uint16_t addrFrom,
        uint16_t addrTo,
        std::optional<PhysPage> physPage = std::nullopt,
        uint32_t maxFrames = 1,
        size_t limit = 200) const;

    /// Most frames a key may have to count as rare
    static constexpr uint32_t kCoverageRareMaxFrames = 1000;

    /// @brief Activity heatmap over [fromFrame, toFrame] (TD-7 §3.1.3).
    TTDCoverageSummaryResult QueryCoverageSummary(
        uint64_t fromFrame,
        uint64_t toFrame,
        std::optional<TTDCoverageKind> kind = std::nullopt,
        uint64_t bucketSize = 0,
        size_t limit = 100) const;

    /// @brief The timeline summary (TD-5, docs/inprogress/2026-10-31-ttd-timeline-summary): per bucket of frames
    /// the facts the session holds, and the phases they make. Reads only what the recording stores (no replay);
    /// a fact it cannot give is named in `missing`
    TTDTimelineResult QueryTimeline(const TTDTimelineOptions& options) const;

    /// @brief The cause chain (docs/inprogress/2026-11-02-ttd-cause-chain): where the value of a memory byte or a
    /// register at a moment came from, as a tree of links back to constants, port reads and the session's initial
    /// memory. Replays the frames it needs with the instruction and bus hooks and puts the machine back exactly
    /// where it stood; a live recording pauses for browsing
    TTDCauseResult QueryCauseChain(const TTDCauseQuery& query);

    /// @brief The call stack at a moment (docs/inprogress/2026-11-07-ttd-backtrace/README.md): the return addresses on
    /// the stack above SP, innermost first, each with the CALL / RST / interrupt that pushed it and when. Each stack
    /// word is named by its last write before the moment (the write journal, the frame's trace); the machine is put
    /// back exactly where it stood; a live recording pauses for browsing
    TTDBacktraceResult QueryBacktrace(const TTDBacktraceQuery& query);

    /// @brief Find event (docs/inprogress/2026-11-02-ttd-find-event): the first (or last) moments in a frame window
    /// where a described event happened - a memory value meets a condition, the screen changed as a whole, an address
    /// ran, a port access, a border change, a key. Frames are filtered from what the session stores; only the hit
    /// frames are replayed. The machine is put back where it stood; a live recording pauses for browsing
    TTDFindEventResult QueryFindEvent(const TTDFindEventQuery& query);

    /// @brief Explain frame (docs/inprogress/2026-11-02-ttd-explain-frame): what one frame did - its time split, the
    /// routines by time, the interrupts, paging, ports per device, the memory changed with its writers, the screen,
    /// the border, the keys and the markers - from one replay of the frame with the instruction and bus hooks. The
    /// machine is put back where it stood; a live recording pauses for browsing
    TTDExplainFrameResult QueryExplainFrame(const TTDExplainFrameQuery& query);

    /// @brief What differs between two moments (docs/inprogress/2026-11-03-ttd-diff-moments/design.md): the
    /// registers, the latches and the slot mapping, the RAM in ranges per region with the last write in between, the
    /// screen and border, the devices, and the time, keys and markers in between. Replays to each moment and puts the
    /// machine back where it stood.
    TTDDiffMomentsResult QueryDiffMoments(const TTDDiffMomentsQuery& query);

    /// @brief Replay check (docs/inprogress/2026-11-05-ttd-replay-check/README.md): each frame of the range replayed
    /// from its checkpoint to its end and compared with the next checkpoint, the state the live machine had there:
    /// the CPU, the chipset, the RAM per page, every registered device, the engine's device regions. Frames with a
    /// barrier inside or a cut at their end are skipped and counted. The machine is put back where it stood; a live
    /// recording pauses for browsing
    TTDReplayCheckResult QueryReplayCheck(const TTDReplayCheckQuery& query);

    /// @brief check_seeks (the replay check's README, section 3): each seek also runs the replay check of the frame
    /// it lands in, before the picture is composed; the seek's result carries the outcome and the session info counts
    /// checks and divergences. Off by default: a check costs a frame's replay and two state copies
    void SetCheckSeeks(bool on) { _checkSeeks.store(on, std::memory_order_relaxed); }
    bool GetCheckSeeks() const { return _checkSeeks.load(std::memory_order_relaxed); }
    
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

    /// @brief Step back over (the debuggers' Shift+F8): one instruction back, but when that instruction is a return
    /// (RET, RET cc taken, RETI, RETN) that brought the stack up to where it is now, back past the whole routine to
    /// the instruction at this stack level before it: the CALL / RST that entered it, or the instruction an interrupt
    /// came after. The routine is found by the stack level of each instruction's start (SP), so nested calls,
    /// interrupts inside it and recursion are passed over too. There is no reverse step out (owner 2026-11-12).
    ttd::TTDReverseStepOverResult ReverseStepOver();

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

    using TTDReverseContinueResult = ttd::TTDReverseContinueResult;

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

    /// @brief Every access matching @p q, newest first, back from q.beforeGlobalT (UINT64_MAX: now).
    ///
    /// The list mode of FindLastAccess: write-journal segments answer at once, other frames are replayed from
    /// their checkpoints (pruned by the coverage index). With changesOnly a write counts only when it changes
    /// the byte: the byte it replaced is the previous write to the same page and offset, or the byte at the
    /// checkpoint the stretch starts from. The machine stays exactly where it stood.
    TTDFindResult FindAccesses(const TTDSearchQuery& q, const TTDFindOptions& options);
    /// FindAccesses on the line being viewed (FindAccesses walks the active lineage with it)
    TTDFindResult FindAccessesOnLine(const TTDSearchQuery& q, const TTDFindOptions& options);

    /// @brief The Z80's view of memory at @p at: @p length bytes from Z80 address @p offset and the four
    /// windows' mapping. Replays there silently and puts the machine back exactly where it stood (like a frame
    /// cache build). Fails on a replay barrier before the point, or a point outside the history.
    TTDCpuView ReadCpuView(const TTDTimePoint& at, uint16_t offset, size_t length);

    /// @brief ReverseContinue with bounds, a page filter and the all mode.
    ///
    /// Single mode: the newest hit in (fromFrame, before], the machine moved there. All mode: up to `limit` hits,
    /// newest first; the machine is replayed from checkpoints and put back exactly where it stood.
    TTDReverseContinueResult ReverseContinue(const TTDReverseContinueQuery& query);

    /// Most hits an all-mode reverse search or find-last returns
    static constexpr size_t kReverseHitLimitMax = 10000;

    /// @brief In which frames of [fromFrame, toFrame] each of @p probes fired (feedback item 7 of
    /// docs/inprogress/2026-10-28-ttd-analysis-feedback): the probes of a game profile (exec, read, write, port in /
    /// out, a page, a port mask, an `if:` condition), armed as taps of the replay's own sink, so they match as they
    /// do in real time. Each complete recorded frame of the window is replayed from its checkpoint, unless the
    /// coverage index proves it holds no hit of a memory probe; a frame a barrier cuts is skipped and named. The
    /// live haptics, the user's breakpoints and the deck hear nothing; the machine is put back exactly where it
    /// stood. At most HapticFrame::kMaxProbes probes; the list keeps each probe's first @p limit frames.
    TTDProbeFramesResult ProbeFrames(const std::vector<controllers::HapticProbe>& probes, uint64_t fromFrame,
                                     uint64_t toFrame, size_t limit = 1000);

    /// Most frames a probe-frames list returns per probe
    static constexpr size_t kProbeFramesLimitMax = 10000;

    /// @brief Read-only accessor for the write journal (for serialization
    /// and tests).
    inline const TTDWriteJournal* GetWriteJournal() const { return _line->writeJournal.get(); }

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
    
    /// @brief Number of checkpoints currently in the timeline: the active lineage's (trunk first; a fork
    /// checkpoint counted once, on the branch)
    inline size_t GetCheckpointCount() const
    {
        return _tip == kTTDTrunk ? _lines.front()->timeline.size() : LineageCheckpointCount();
    }
    
    /// @brief Earliest frame covered by the timeline (0 when empty).
    ///
    /// Frames below this have no checkpoints: seeks/builds targeting them
    /// always fail, so callers walking backward (e.g. the DeZog history
    /// index resolver) can stop here instead of probing every frame down
    /// to 0.
    inline uint64_t GetEarliestRecordedFrame() const
    {
        return _lines.front()->timeline.empty() ? 0 : _lines.front()->timeline.front().time.frame;
    }
    
    /// @brief Read-only access to a timeline entry (bounds-checked).
    /// Returns nullptr if idx is out of range.
    const TTDCheckpoint* GetCheckpoint(size_t idx) const;
    /// Where a checkpoint's CPU stands in machine time: its frame boundary plus
    /// the last instruction's overshoot (a frame's writes are after it)
    uint64_t CheckpointStartT(const TTDCheckpoint& cp) const;


    /// @brief Last capture / restore timings (benchmark harness, BM-2 / BM-6).
    inline const TTDPerfCounters& GetPerfCounters() const { return _perf; }

    /// Model-specific peripheral serializers registered for this session.
    /// Exposed so the divergence hash can mix in their contribution without
    /// the hash code knowing which machine is running.
    inline const TTDPeripheralRegistry& GetPeripheralRegistry() const { return _peripherals; }

    /// @brief A device was swapped for another one (the General Sound card's personality switch): the registry
    /// drops every device of @p oldType and takes @p device under its own key (the lightweight GS: marked not
    /// recorded). Keeps the registry free of a stale pointer to the outgoing device (whether or not TTD is
    /// recording: the registry persists for the next StartRecording / seek). See SwitchMachinePeripheral
    inline void UpdatePeripheral(TTDDeviceType oldType, TTDSerializable* device) override
    {
        SwitchMachinePeripheral(_context, _peripherals, oldType, device);
    }

    /// @brief Number of model-RAM pages (set at StartRecording from the
    /// active model's RAM size).
    inline uint16_t GetModelRamPages() const { return _modelRamPages; }

    /// Devices fitted but deliberately not recorded: the loaded file's when the
    /// session came from one, else the live registry's
    const std::vector<TTDDeviceKey>& NotRecorded() const
    {
        return _loadedFromFile ? _loadedNotRecorded : _peripherals.NotRecorded();
    }

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
    /// the current frame boundary. Caller pushes it onto _line->timeline. False when
    /// the engine (the history's store) did not take it
    bool CaptureNow(TTDCheckpoint& out);




    /// @brief Release every page ref held by a checkpoint (used when
    /// invalidating or thinning).


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

    /// @brief The pieces a replay may have written since the engine last restored memory: the RAM pages and the
    /// device pieces their trackers marked (taken and cleared), every piece of a region no tracker watches. Valid
    /// only while _engineMemoryKnown holds
    TTDWrittenFn TakeWrittenSinceRestore();
    /// @brief The same marks, kept: the next TakeWrittenSinceRestore adds its own to them (a replay check reads the
    /// frame's writes, the next restore still needs them)
    TTDWrittenFn PeekWrittenSinceRestore();

    /// @brief ReadCpuView at a checkpoint (a frame start the restore lands on) from the store: the four windows
    /// the capture mapped, their pages decoded, the machine untouched. False where only a replay can tell (no
    /// window map, a window outside the stored regions and ROM, a point past the checkpoint)
    bool ReadCpuViewFromStore(const TTDTimePoint& at, uint16_t offset, size_t length, TTDCpuView& view);


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

    /// The cause chain's frame trace: the frame of checkpoint @p cpIndex replayed up to @p endT with the instruction
    /// and bus hooks (ttdcontrollercausechain.cpp). The caller saved the live machine
    std::shared_ptr<TTDFrameTrace> TraceCauseFrame(size_t cpIndex, uint32_t endT);

    /// @brief Run the CPU until the current frame reaches `targetTInFrame`
    /// (TTD time units) or ends. A hardware turbo may switch on the way, so
    /// the T-state budget is re-derived after each run.
    void RunToTInFrame(uint32_t targetTInFrame);

    /// @brief Compose the picture for the current position (display rule,
    /// docs/inprogress/2026-09-28-ttd-positioning-and-display/design.md §3).
    ///
    /// - Frame target: a frame's FINAL picture — its own T-states replayed
    ///   from its checkpoint to its end. @p frame: the frame that ended at the
    ///   current position (a frame boundary, D13: "frame N" is {N+1, 0});
    ///   the clip export passes the current frame instead (kCurrentFrame).
    /// - Time target (frame f, T-state T): what the beam rendered from the
    ///   start of f up to T, over frame f-1's final picture for the part not
    ///   drawn yet — exactly the framebuffer of a live machine paused there.
    ///
    /// Runs in a sandbox: live state (CPU, RAM, peripherals, input cursor,
    /// keyboard, framebuffer) is saved first and restored afterwards, then
    /// the composed pixels are written. Machine state is never changed.
    /// Checkpoint restores themselves never paint (ResyncScreenState), so
    /// this is the only place that decides what a TTD position shows.
    static constexpr uint64_t kEndedFrame = UINT64_MAX;     ///< ComposeDisplay: the frame that ended here
    static constexpr uint64_t kCurrentFrame = UINT64_MAX - 1;   ///< ComposeDisplay: the current frame to its end
    void ComposeDisplay(bool frameTarget, uint64_t frame = kEndedFrame);

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
    /// EnumerateM1InRange on the viewed line only (EnumerateM1InRange walks the lineage with it)
    EnumerateResult EnumerateM1InRangeOnLine(uint64_t startGlobalT, uint64_t endGlobalT,
                                             std::vector<TTDM1Record>& outM1s,
                                             TTDExternalEvent* outBlockingMarkerStorage);

    // -----------------------------------------------------------------------
    // Internal resume helpers (Phase 2 Item 5; parent TDD §8.3)
    // -----------------------------------------------------------------------

    /// @brief Drop every checkpoint after `from` and everything the engine
    /// recorded after `cut` (in `from`'s frame; records at it stay), so the
    /// recording continues from there (TimeTravelEngine::TruncateAfter). Used
    /// by ResumeRecordingFrom. No-op when `from` is at the last checkpoint and
    /// nothing was recorded after `cut`.
    void TruncateTimelineAfter(const TTDTimePoint& from, const TTDTimePoint& cut);

    /// Branches (ttdcontrollerbranches.cpp)
    TTDBranchTable _branches;
    uint32_t _tip = 0;      ///< the active branch
    uint32_t _viewId = 0;   ///< the line read and written now (an ancestor's while standing in its part)
    TTDBranchMeta _nextBranchMeta;
    bool _nextBranchMetaSet = false;
    bool _forceFork = false;   ///< PrepareBranchCreate: the next resume forks at the end too
    /// The table's last change (the ttd.branches topic); NoteBranchEvent numbers them
    TTDBranchEvent _branchEvent;
    void NoteBranchEvent(const char* event, const TTDBranchInfo& branch, uint32_t removed = 0);
    /// Per branch id: where it was last visited (SwitchBranch's default)
    std::vector<std::optional<TTDTimePoint>> _lastVisited = {std::nullopt};
    /// The active lineage as the bookmark readers see it: a copy under its own lock, so a read while recording
    /// does not park the machine (GetBookmarks); empty = the trunk alone
    mutable std::mutex _lineageViewLock;
    std::vector<TTDLineageSpan> _lineageView;
    /// GetSessionInfo's branch list: rebuilt when the table's revision or the active branch moved
    mutable std::shared_ptr<const TTDBranchesView> _branchesView;
    mutable uint32_t _branchesViewTip = kTTDTrunk;
    /// The trunk alone (a new session, a load, an invalidation): the active line's state stays the trunk's
    void ResetBranches();
    /// A resume at @p from (cut @p cut) on the viewed line, which has recorded history after it: a new branch
    /// keeps that history, recording goes on the new one. False when the engine cannot fork (it truncates then)
    bool ForkBranchAt(const TTDTimePoint& from, const TTDTimePoint& cut);
    /// Read and write line @p id (the controller's and the engine's); the playback and journal hooks follow
    void ViewBranch(uint32_t id);
    /// The branch of the active lineage that owns frame @p frame (a fork's frame belongs to the branch)
    uint32_t OwnerOnLineage(uint64_t frame) const;
    /// The history before @p beforeT on the active lineage, newest part first (TTD branches §4.4): the line that owns
    /// that moment viewed, then each parent before its child's first checkpoint, down to the trunk.
    /// @p onLine(lineStartT, upToT) works on the viewed line from its start (0 for the trunk) up to upToT and returns
    /// false to stop. Afterwards the machine reads the line that owns where it stands. Without branches: one call
    void WalkLineageBack(uint64_t beforeT, const std::function<bool(uint64_t lineStartT, uint64_t upToT)>& onLine);
    /// FindLastAccess on the viewed line only (FindLastAccess walks the lineage with it); *outBlocked: a barrier
    /// stopped the walk
    std::optional<TTDSearchResult> FindLastAccessOnLine(const TTDSearchQuery& q, uint64_t beforeGlobalT,
                                                        TTDExternalEvent* outBlockingMarker,
                                                        TTDSearchWindow* outWindow, bool* outBlocked);
    /// The branch of the active lineage whose history holds @p time (the trunk without branches)
    uint32_t BranchHolding(const TTDTimePoint& time) const;
    /// The frame table holding @p frame on the active lineage (its owner's; the viewed line's without branches)
    const TTDFrameTable& LineageFrames(uint64_t frame) const;
    /// The frame table holding machine time @p t on the active lineage
    const TTDFrameTable& LineageFramesAtT(uint64_t t) const;
    /// Engine index of the active lineage's first checkpoint, and one past its last (the tip's)
    size_t LineageFirstIndex() const { return _engine->LineFirstCheckpoint(kTTDTrunk); }
    size_t LineageEndIndex() const { return _engine->LineFirstCheckpoint(_tip) + _lines[_tip]->timeline.size(); }
    /// View the lineage line holding engine checkpoint @p index (the deepest one starting at or before it)
    void ViewLineageIndex(size_t index);
    /// The controller checkpoint at engine index @p index of the lineage, its line viewed; null when not held
    const TTDCheckpoint* ViewLineageCheckpoint(size_t index);
    /// Engine index of the lineage checkpoint at or before @p t, its line viewed; -1 before the start
    int64_t ViewLineageIndexAtOrBefore(const TTDTimePoint& t);
    /// Back on the line that owns where the machine stands (a query that viewed other lines leaves through it)
    void ViewOwnerOfPosition();
    /// The checkpoint at the start of @p frame on its lineage line; null when none starts it (no view change)
    const TTDCheckpoint* LineageCheckpointAtFrame(uint64_t frame) const;
    /// The coverage index holding @p frame: its lineage line's (a branch's index starts at its fork)
    const TTDCoverageIndex& CoverageOfFrame(uint64_t frame) const;
    /// Whether any line of the active lineage sealed a frame of @p kind
    bool LineageHasCoverage(TTDCoverageKind kind) const;
    /// The frames of the active lineage covered for @p kind: each line's range up to where its child forks
    bool LineageCoveredRange(TTDCoverageKind kind, uint64_t& first, uint64_t& last) const;
    /// The active branch recorded nothing after its fork (§4.6): one checkpoint, no input or marker of its own, its
    /// end at the fork moment
    bool TipIsEmpty() const;
    /// An empty active branch goes at once, without a notice (§4.6): its parent is active again with its own end.
    /// Called by the user's acts that leave it (a seek elsewhere, a switch); true when one went
    bool DropEmptyTip(const TTDTimePoint& leavingTo);
    /// Release the lines of removed branches (engine and controller), their bookmarks with them
    void ReleaseBranchLines(const std::vector<uint32_t>& removed);
    /// View the lineage line holding @p frame (OwnerOnLineage)
    void ViewOwnerOfFrame(uint64_t frame);
    /// The input events of the active lineage in time order: each line's part before the next line starts
    std::vector<TTDInputEvent> LineageInputEvents() const;

    /// A query's way out: the machine reads the line that owns where it stands again
    struct LineageViewGuard
    {
        TimeTravelController& controller;
        ~LineageViewGuard() { controller.ViewOwnerOfPosition(); }
    };

    /// The engine as a query reads it with branches: every checkpoint index of the active lineage (one engine
    /// index space, a branch's starting at its fork checkpoint's), the line that holds an index viewed before it is
    /// read. Without branches the calls go straight to the engine. Same names as TimeTravelEngine's
    class LineageEngine
    {
    public:
        explicit LineageEngine(TimeTravelController& c) : _c(c), _e(*c._engine) {}
        size_t FirstCheckpoint() const { return _c.LineageFirstIndex(); }
        size_t CheckpointCount() const { return _c.LineageEndIndex(); }
        const TTDEngineCheckpoint* Checkpoint(size_t i) const { _c.ViewLineageIndex(i); return _e.Checkpoint(i); }
        int64_t CheckpointIndexOf(const TTDPosition& p) const
        {
            _c.ViewOwnerOfFrame(p.frame);
            return _e.CheckpointIndexOf(p);
        }
        uint32_t ChangeCount(size_t i, uint32_t region) const
        {
            _c.ViewLineageIndex(i);
            return _e.ChangeCount(i, region);
        }
        std::vector<std::pair<uint32_t, TTDPieceId>> ChangesAt(size_t i, uint32_t region) const
        {
            _c.ViewLineageIndex(i);
            return _e.ChangesAt(i, region);
        }
        TTDRestoreResult RestoreRegion(size_t i, uint32_t region, uint8_t* out,
                                       std::vector<uint8_t>* present = nullptr) const
        {
            _c.ViewLineageIndex(i);
            return _e.RestoreRegion(i, region, out, present);
        }
        uint32_t VersionAt(size_t i, uint32_t region, uint32_t piece) const
        {
            _c.ViewLineageIndex(i);
            return _e.VersionAt(i, region, piece);
        }
        const std::vector<TTDScreenRange>* ScreenLayoutOf(const TTDEngineCheckpoint& cp) const
        {
            return _e.ScreenLayoutOf(cp);
        }
        const std::vector<TTDRegionDesc>& Regions() const { return _e.Regions(); }
        const TTDPieceStore& PieceStore() const { return _e.PieceStore(); }

    private:
        TimeTravelController& _c;
        const TimeTravelEngine& _e;
    };
    /// Copy the active lineage for the bookmark readers (after every change of _tip or of the tree)
    void PublishLineage();
    /// Whether @p bookmark lies on the active lineage: its branch owns its moment there (_lineageView)
    bool OnActiveLineage(const TTDBookmark& bookmark) const;
    /// GetCheckpointCount with branches
    size_t LineageCheckpointCount() const;
    /// Does the active lineage hold recorded history after @p cut: a later part of it (a branch after the viewed
    /// line), a checkpoint, an input event or a marker. A resume there forks; at the end it continues (the few
    /// T-states between a restore and where the recording stopped hold none of them)
    bool HasHistoryAfter(const TTDTimePoint& cut) const;
    /// Running forward through the history of an ancestor: at the frame boundary where the next branch of the
    /// active lineage forks, the machine goes on with that branch's records. True when it moved
    bool FollowLineageAtBoundary(uint64_t frame);

    /// @brief Apply the history limit: the frame window and the byte budget
    /// drop the engine's oldest segments; _line->timeline and the journals follow.
    void EnforceHistoryLimit();
    /// @brief The engine's history policy from the limits (SetHistoryLimit)
    void ApplyHistoryPolicy();
    /// @brief _line->timeline drops what the engine dropped from the front, and the
    /// input, marker, bookmark and port journals are cut to the new start
    void SyncTimelineFront();
    /// @brief D8: a navigation while recording pauses the recording - stopped
    /// with everything up to now kept, marked paused at the present
    /// (_pausedEnd), browsable; execution reaching that point again, or a
    /// resume there, continues it. A stop ends it
    void PauseRecordingForBrowsing();

    /// The context and watched bytes of @p bookmark at its moment (AddBookmark); false when it cannot be reached
    bool CaptureBookmarkMoment(TTDBookmark& bookmark);
    /// @brief Continue recording from the machine's present (at or after
    /// @p from), dropping what was recorded after it: the second half of
    /// ResumeRecordingFrom, after its seek; called without one at a paused end
    bool ContinueRecordingAt(const TTDTimePoint& from);
    /// One branch's own history in the controller (docs/inprogress/2026-11-08-ttd-branches/architecture.md §5.1),
    /// beside the engine's line of the same id: what is read and written goes through _line. A session that never
    /// forks holds one line
    struct ControllerLine
    {
        /// The recorded timeline. Appended only on the emulator thread
        std::vector<TTDCheckpoint> timeline;
        /// Input journal: keyboard matrix mutations captured for replay (Item 3)
        TTDInputJournal inputJournal;
        /// Journal playback: next event to apply while the machine executes recorded history (ArmInputPlayback)
        size_t inputCursor = 0;
        /// External-event journal: replay barriers for nondeterminism sources that aren't input-journaled (Item 6)
        TTDExternalEventJournal externalEvents;
        std::unordered_map<size_t, std::vector<uint8_t>> toolEditPayloads;   ///< marker index -> edit bytes
        /// Per-frame coverage sets backing reverse-search frame skipping
        TTDCoverageIndex coverageIndex;
        /// The write journal (D40), allocated on the first StartRecording() when _enableWriteJournal is true
        std::unique_ptr<TTDWriteJournal> writeJournal;
        /// The write journal's segments (D40): closed spans, then the open one (to == kSegmentOpen)
        std::vector<TTDJournalSegment> journalSegments;
        size_t forkInputs = 0;    ///< a branch: its input events at the fork (the copied part), §4.6
        size_t forkMarkers = 0;   ///< a branch: its markers at the fork
    };
    static std::vector<std::unique_ptr<ControllerLine>> MakeTrunkLine()
    {
        std::vector<std::unique_ptr<ControllerLine>> lines;
        lines.push_back(std::make_unique<ControllerLine>());
        return lines;
    }
    /// A line's coverage index as a holder stream's bytes (empty: nothing sealed)
    std::vector<uint8_t> CoverageBytes(const ControllerLine& line) const;
    /// Every line by branch id (the trunk first) and the one read and written now
    std::vector<std::unique_ptr<ControllerLine>> _lines = MakeTrunkLine();
    ControllerLine* _line = _lines.front().get();

    bool _recordingPaused = false;   ///< D8: the recording is paused for browsing
    /// The boundary at the start of @p frame is a cut (a snapshot load recorded by an older build): its checkpoint holds the loaded state
    bool CutAtFrameStart(uint64_t frame) const;
    TTDTimePoint _pausedEnd{};       ///< where it paused
    /// @brief The engine checkpoint held at or before @p t, as an index into
    /// _line->timeline (_line->timeline[i] is the engine's FirstCheckpoint() + i), or -1
    /// when the history starts after it (Phase 5, C2)
    int64_t TimelineIndexAtOrBefore(const TTDTimePoint& t) const;

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
        TTDBankOverrides bankOverrides{};  ///< debugger-forced windows 1/2
        uint32_t        z80TInFrame = 0;   ///< z80.t (host-side, not in TTDCpuState)
        std::vector<uint8_t> ram;          ///< model RAM, _modelRamPages × 16 KB
        /// Every registered device's state, by registry entry (TTDPeripheralRegistry::SaveLive)
        std::vector<std::vector<uint8_t>> deviceStates;
        size_t inputCursor = 0;            ///< journal playback cursor (ServiceInput)
        bool   inputPlaybackArmed = false;
        size_t engineEventCursor = 0;      ///< the engine's event log: a replay in between delivers its events
        uint64_t cardEditFrame = UINT64_MAX;
        uint64_t inputStartFrame = UINT64_MAX;
        TTDMachineTime inputFrameStart = 0;
        bool inputFrameKnown = false;
        /// The replay engine's journals as the machine plays them (a machine on the recorded history after a seek
        /// runs from them; a throwaway replay starts them elsewhere) and the hooks the CPU and the media read through
        TTDPortJournal::Mode busReadMode = TTDPortJournal::Mode::Off;
        uint64_t busReadCursor = 0;
        TTDPortJournal::Mode busWriteMode = TTDPortJournal::Mode::Off;
        uint64_t busWriteCursor = 0;
        TTDPortJournal::Mode busVectorMode = TTDPortJournal::Mode::Off;
        uint64_t busVectorCursor = 0;
        TTDMediaJournal::Mode mediaReadMode = TTDMediaJournal::Mode::Off;
        uint64_t mediaReadCursor = 0;
        TTDPortJournal* portReadHook = nullptr;
        TTDPortJournal* portWriteHook = nullptr;
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
    /// timeline or dirty-tracker effects). Without @p withMemory the RAM, the framebuffer and plane B are left
    /// empty (a replay check compares memory through the engine's versions; such a snapshot is not restored)
    void SaveLiveState(LiveStateSnapshot& out, bool withMemory = true);

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
    mutable std::condition_variable _publishedCv;          ///< a publication happened
    mutable std::chrono::steady_clock::time_point _publishedAt{};
    /// The published snapshot, refreshed first when it is older than kPublishIntervalMs (ReadSessionInfo)
    TTDSessionInfo FreshPublishedSessionInfo() const;
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
    ///   - after a Change or a Configure, the outermost operation publishes the summary.
    /// An Idle session is not touched by a running machine, so it is not parked - except for a Configure: the
    /// settings it writes (the black box, a restart due) are read at every frame boundary, whatever the state.
    class SessionOperation
    {
    public:
        enum class Kind : uint8_t { Read, Change, Configure };
        SessionOperation(const TimeTravelController& manager, Kind kind);
        ~SessionOperation();
        SessionOperation(const SessionOperation&) = delete;
        SessionOperation& operator=(const SessionOperation&) = delete;

    private:
        const TimeTravelController& _manager;
        Kind _kind;
        bool _locked = false;
        /// The CPU held for the operation (a running machine executing an active session): execution arbiter
        execution::HoldToken _hold;
    };
    bool OnMachineThread() const;
    mutable std::recursive_mutex _controlMutex;
    mutable int _operationDepth = 0;  ///< nesting on the lock holder's thread (guarded by _controlMutex)
    std::chrono::steady_clock::time_point _lastPublish{};
    /// ROM signature of a live session, taken with its baseline (the ROM the
    /// recording relies on); GetSessionInfo no longer hashes the ROM per call
    uint64_t _liveRomSignature = 0;

    bool _debuggerHold = false;        ///< HoldBackgroundRecording: a debugger wants a history
    bool _backgroundSession = false;   ///< the session started as a background one (the black box, a debugger)

    /// The timeline's end (_line->timeline.back().time) as its last change left it: SessionEndPosition answers another
    /// thread from it while a recording runs. Reading the timeline itself needs the machine parked (it appends every
    /// frame), and a client polling the position (a companion's live readout, 50 times a second) parked the machine
    /// as often - every park a stall of the frame and a session publish on the machine's thread
    mutable std::mutex _liveEndMutex;
    TTDTimePoint _liveEnd{};
    /// After every change of _line->timeline
    void NoteTimelineEnd();
    /// SessionEndPosition's answer for a caller that may read the session (the machine's thread, an operation)
    TTDTimePoint TimelineEnd() const;
    /// The published cursor: PublishCursor stores the live position and bounds, from where reading the session is
    /// safe (the machine's thread at a frame boundary, GetSessionInfo)
    mutable TTDCursorCell _cursor;
    void PublishCursor() const;

    /// Model-specific peripheral serializers. The framework never names a
    /// machine: it registers whatever the active model provides (see
    /// RegisterModelPeripherals) and thereafter only calls TTDSerializable.
    TTDPerfCounters _perf;
    TTDCaptureWork _captureWork;   ///< filled while CaptureNow runs, published in _perf

    /// Shadow engine (see SetShadowEngine); not owned
    TimeTravelEngine* _shadowEngine = nullptr;
    /// Device memory the shadow engine records as regions 1.. (region 0 is RAM)
    std::vector<TTDDeviceRegion> _shadowDeviceRegions;
    bool _shadowArmed = false;
    /// Start or stop the devices marking their memory writes for the shadow engine
    void ArmShadowRegions(bool on);
    TTDExternalEvent _barrierScratch;   ///< EnumerateM1InRange's barrier, as a marker
    TTDV1EventCursor _shadowEvents;   ///< how far the shadow engine has the controller's journals
    std::vector<TTDPendingFact> _shadowFacts;   ///< the live machine's facts since the last boundary
    /// A fact at the current instant (normalized past the frame's end)
    void NoteFact(const TTDEvent& ev);
    /// The shadow engine's screenshot stream (frame-boundary stream 0, off until switched on)
    void RegisterScreenshotStream(TimeTravelEngine& engine);
    std::vector<uint8_t> _screenshotScratch;
    std::string _shadowRecordingRoot;
    std::unique_ptr<TTDRecordingFolder> _shadowFolder;
    std::unique_ptr<TTDRecordingWriter> _shadowWriter;
    /// Branches in the folder (storage.md §4): a file holds one line; only the active one records
    uint32_t _shadowWriterLine = 0;              ///< the engine line _shadowWriter writes
    std::set<uint32_t> _shadowLinesWithFiles;    ///< lines with a file in the folder
    TTDSessionSaveParams _shadowParams;          ///< the folder's files: identity, holder streams, branch records
    bool _shadowFilesStopped = false;            ///< writing failed: no new writer until the session's next folder
    bool _shadowDiskStop = false;                ///< the disk is full: the recording stops at the frame's end (S-7)
    TTDDiskSpace _shadowDiskSpace = TTDDiskSpace::Ok;
    std::function<uint64_t(const std::string&)> _freeSpaceProbe;
    uint32_t _shadowSinceSpaceCheck = 0;
    uint64_t _shadowClosedBytes = 0;    ///< the folder's closed files (the rate of the warning's 10 minutes)
    uint64_t _shadowFramesWritten = 0;  ///< frames captured while the folder was written
    /// The free space against the limits; false when writing must stop (the files are finished)
    bool CheckShadowDiskSpace();
    /// The writer of the active branch's files: a new branch's from its fork (its first file whole), one with
    /// files from its next segment; false when the line cannot be written (no folder, a branch whose fork has no file)
    bool StartLineFiles(TimeTravelEngine& engine);
    /// The open writer finishes on its line; that line's next recording starts a segment of its own
    void FinishLineFiles();
    /// The branch's record for the branch table (identity, metadata, end)
    std::vector<uint8_t> BranchRecordBytes(uint32_t id) const;
    /// The shadow session's files: finished (stop, a new session) or deleted (invalidated)
    void FinishShadowFiles();
    void DiscardShadowFiles();
    /// A black box restarting (after an acceleration, a machine change): its
    /// finished folder stays on disk for Load, under the crashed-recordings
    /// rule (kept while this process runs, then 7 days); an empty one goes
    void KeepShadowFiles();
    /// The controller's holder streams of a session file (facts, coverage,
    /// bookmarks) as they are now. @p declareAll: coverage too when empty (a
    /// recording's files declare the same streams in every segment)
    std::vector<TTDHolderStream> SessionHolderStreams(bool declareAll) const;

public:
    /// Frame-boundary stream 0 of the shadow engine: width u16, height u16,
    /// video mode u8, then the framebuffer (RGBA)
    static constexpr uint32_t kScreenshotStream = 0;

private:

    /// The media manager's read journal (Phase 3): sector reads go into the
    /// shadow engine while recording and come from the replay engine while
    /// a seek replays from it
    class MediaReadAdapter : public IMediaReadJournal
    {
    public:
        explicit MediaReadAdapter(TimeTravelController& owner) : _owner(owner) {}
        TimeTravelEngine* engine = nullptr;
        bool Playing() const override;
        bool Play(const std::string& slot, uint64_t lba, uint8_t* out, size_t size) override;
        void Record(const std::string& slot, uint64_t lba, const uint8_t* bytes, size_t size) override;

    private:
        TimeTravelController& _owner;
    };
    MediaReadAdapter _mediaReads{*this};
    /// Point the media manager at the journal the session now needs (recording, replaying, none)
    void SyncMediaReadJournal();
    /// Stop the replay engine's journals the machine plays on the recorded history (sectors, interrupt vectors, the
    /// CPU's IN / OUT hooks): it leaves the history (a resume, a reset, a new recording)
    void StopHistoryPlayback();
    std::map<size_t, std::vector<uint8_t>> _toolEditBefore;   ///< device states when a tool edit began, by registry entry
    bool _toolEditOpen = false;
    TTDTimePoint _toolEditAt{};   ///< where the edit began: its event's time (a trap's instruction boundary)
    uint32_t _toolEditTt = 0;     ///< the CPU clock (Z80::tt) when it began: a trap also spends time
    /// Apply a tool edit's bytes (a replay crossing it)
    void ApplyToolEdit(const std::vector<uint8_t>& payload);
    EditWriteObserver _editWriteObserver;   ///< see SetEditWriteObserver
    /// The replay's card edits of frame @p frame to the card targets (those of an earlier frame leave). The scan
    /// starts at the event cursor; @p fromFrameStart scans from the frame's first event instead (a restore mid-frame,
    /// where the cursor is already past some of the frame's edits)
    void DeliverCardEdits(uint64_t frame, bool fromFrameStart = false);
    /// The live machine's memory regions as the engine sees them: machine RAM, then each region source's
    std::vector<TTDRegionDesc> LiveRegions() const;
    uint64_t _shadowLastStart = 0;         ///< the last captured frame's start in machine time
    std::vector<ScreenMemorySpan> _shadowScreenSpans;   ///< the picture's memory at the boundary (reused)
    std::vector<TTDScreenRange> _shadowScreenRanges;
    uint64_t _shadowLastBase = 0;          ///< emulatorState.t_states at that capture
    uint64_t _shadowLastLength = 0;        ///< the length of the frame before it (0: none yet)
    uint64_t _shadowRomSignature = 0;      ///< the ROM set's, hashed once per shadow session
    uint64_t _shadowMediaStamp = 0;        ///< IMediaHistory::VersionStamp at the last capture
    bool _shadowMediaKnown = false;        ///< _shadowMediaStamp is valid for this session
    bool _shadowRescan = false;   ///< live memory may differ from the engine's delta base: hand it every piece
    /// Live memory holds what the engine's last restore wrote, apart from the writes the trackers marked since:
    /// set by a restore inside a KnownMemoryScope, cleared when the scope ends or anything else may write memory
    /// (the live state put back, a tool edit). A restore then decodes only the written and changed pieces
    bool _engineMemoryKnown = false;
    int _knownMemoryDepth = 0;   ///< open KnownMemoryScopes
    TTDRestoreStats _lastMemoryRestore;
    std::vector<uint8_t> _writtenRamPages;            ///< TakeWrittenSinceRestore: per RAM page
    std::vector<std::vector<uint8_t>> _writtenPieces; ///< TakeWrittenSinceRestore: per engine region, per piece
    bool _writtenHeld = false;                        ///< PeekWrittenSinceRestore: the next take merges
    std::vector<uint16_t> _writtenPages;              ///< TakeWrittenSinceRestore: the dirty tracker's pages
    std::vector<uint32_t> _writtenScratch;
    /// Hand this capture to the shadow engine
    bool FeedShadow(const TTDCheckpoint& out, bool baseline);
    /// Per registry entry: its index in the shadow engine's device table, -1 when it has none (no state). The
    /// table entry bound to the same live device (a session loaded from a file names its devices as the file does),
    /// else the one of the same key. Mapped at every capture: a table and the registry are a few dozen entries
    std::vector<int32_t> _shadowEntryDevice;
    void MapShadowEntries()
    {
        const std::vector<TTDRegistryEntry>& entries = _peripherals.Entries();
        _shadowEntryDevice.assign(entries.size(), -1);
        if (!_shadowEngine || !_shadowEngine->IsSessionOpen())
            return;
        const std::vector<TTDDeviceEntry>& table = _shadowEngine->Devices().Entries();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            for (size_t k = 0; k < table.size() && _shadowEntryDevice[i] < 0; ++k)
                if (table[k].device != nullptr && table[k].device == entries[i].device)
                    _shadowEntryDevice[i] = static_cast<int32_t>(k);
            if (_shadowEntryDevice[i] < 0)
                _shadowEntryDevice[i] = _shadowEngine->DeviceIndex(entries[i].key);
        }
    }
    /// The shadow engine's history no longer matches the timeline's: it starts over at the next capture
    void ResetShadow();
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

    /// SetHistoryLimit (0 = no limit). Atomic: a control thread sets them while
    /// the machine's thread enforces them after every capture
    std::atomic<uint64_t> _historyLimitFrames{0};
    std::atomic<uint64_t> _historyLimitBytes{0};
    uint64_t _evictedCheckpoints = 0;   ///< released by the limit in this session
    uint64_t _blobBytes = 0;            ///< device blob bytes of every checkpoint in _line->timeline (kept with it)
    static uint64_t BlobBytes(const TTDCheckpoint& cp);


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
    std::vector<TTDDeviceKey> _loadedNotRecorded;   ///< The loaded file's devices fitted but not recorded
    std::string _loadedRecordedBy;        ///< The loaded file's emulator_id

    bool _enableCoverageIndex = true;


    /// Reusable scratch buffer for CollectAndClear (avoids per-frame alloc).
    std::vector<uint16_t> _dirtyScratch;


    size_t _engineEventCursor = 0;          ///< the replay engine's event log (SetReplaySource)
    uint64_t _cardEditFrame = UINT64_MAX;    ///< the frame whose card edits the card targets hold (replay)
    uint64_t _replayRomSignature = 0;       ///< this machine's ROM set (SetReplaySource)
    TTDRestoreResult _lastEngineCheck;      ///< LastEngineCheck
    /// Settings and media of the replay engine's checkpoint @p index against
    /// this machine (into _lastEngineCheck)
    void CheckEngineCheckpoint(size_t index, bool forReplay);
    /// Every medium to its version at the replay engine's checkpoint @p index
    /// (the restore, before the devices load); what could not be set goes
    /// into _mediaIssues
    void SetMediaToCheckpoint(size_t index);
    std::vector<TTDRestoreIssue> _mediaIssues;  ///< SetMediaToCheckpoint's, for the replay's check
    TimeTravelEngine* _replayEngine = nullptr;
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
    /// Either queue holds something; written under _pendingInputMutex, read without it by the per-instruction
    /// ServiceInput, which takes the mutex only when it is set
    std::atomic<bool> _pendingQueued{false};
    /// The replay engine's start of the frame playback last looked up (one lookup a frame, not an instruction)
    uint64_t _inputStartFrame = UINT64_MAX;
    TTDMachineTime _inputFrameStart = 0;
    bool _inputFrameKnown = false;

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

    /// The engine's bus journals hold every IN / OUT of the session: recorded
    /// on every machine from the baseline on (the CPU's hooks write into them,
    /// port-journals-on-engine.md); false after a gap (DropPortJournal, the
    /// reason in _portJournalOffReason) or for a loaded session without them.
    /// _portJournalValid is the same flag as the session file and status name it
    bool _portJournalValid = false;
    bool _portJournalRecorded = false;
    std::string _portJournalOffReason;
    /// Give up the journal for this session (a gap in it): replay falls back
    /// to the live devices
    void DropPortJournal(const char* reason);
    /// Point EmulatorContext::ttdPortReads / ttdPortWrites at the recording engine's bus journals
    /// while recording, null otherwise (a replay points them at the replay engine's: RestoreCheckpointForReplay)
    void SyncPortJournalHook();
    /// The recording engine's bus journals back to recording at their ends (after a pause, a replay, a resume)
    void ResumeBusRecording();
    /// The recording engine's bus journals stop recording (a stop, a gap)
    void StopBusRecording();


    /// Advisory bookmarks (TD-4) — named annotations BESIDE the barrier
    /// journal above, never inside it. Same lifecycle: dropped on
    /// Invalidate/Start, truncated by Resume, persisted in the .ttd file.
    TTDBookmarkJournal _bookmarks;


    /// Whether to capture write journal entries. When false, journal is empty
    /// and reverse-watchpoint queries fall back to checkpoint replay.
    /// Set via SetEnableWriteJournal() before StartRecording().

    bool _enableWriteJournal = false;   ///< D40: the journal is recorded on demand
    std::atomic<bool> _checkSeeks{false};  ///< SetCheckSeeks
    uint64_t _seekChecks = 0;
    uint64_t _seekCheckDivergences = 0;
    std::string _lastSeekDivergence;
    /// check_seeks: the replay check of the frame a seek landed in, into the seek's result and the counters
    void CheckSeekLanding(TTDSeekResult& result);
    /// The live ring holds one frame's writes: it is drained into the engine at
    /// each capture and cleared, its memory committed in chunks as it fills.
    /// This is the most one frame may write before the oldest records are lost
    static constexpr size_t kDefaultWriteJournalBytes = 64u * 1024 * 1024;
    size_t _writeJournalBytes = kDefaultWriteJournalBytes;   ///< SetWriteJournalCapacity

    std::atomic<bool> _journalBuildActive{false};
    std::atomic<bool> _journalBuildCancel{false};
    std::atomic<uint64_t> _journalBuildDone{0};
    std::atomic<uint64_t> _journalBuildTotal{0};
    static constexpr uint64_t kSegmentOpen = UINT64_MAX;
    /// Writes reach the journal now: recording, journal on, capture features on
    bool JournalLive() const;
    /// Open a segment at the current position when writes start reaching the
    /// journal, close the open one when they stop. Called on every change of
    /// those conditions (SetState, SetEnableWriteJournal, UpdateFeatureCache)
    void SyncJournalSegment();
    /// Drop the segments' parts after @p cutT (a resume from an earlier point)
    void ClipJournalSegments(uint64_t cutT);
    /// The segments as they stand: the open one ends at the current position,
    /// and the ring's evicted records are no longer covered
    std::vector<TTDJournalSegment> JournalSegments() const;
    /// One segment from the session's first checkpoint to its last
    bool JournalCoversSession(const std::vector<TTDJournalSegment>& segments) const;
    /// See TTDSessionInfo::lastDropReason
    std::string _lastDropReason;
    /// See TTDSessionInfo::lastStopReason
    std::string _lastStopReason;
    std::string _lastStartError;   ///< LastStartError()
    std::string _unavailableReason;    // see SetUnavailableReason
    /// Position at StopRecording, to tell whether the machine ran before a live resume
    uint64_t _recordingStoppedAtT = 0;
    /// Where the last recording stopped, inside the frame after its last
    /// checkpoint: the history reaches there (StopRecording gives the engine
    /// that frame's journals). Valid until the session changes
    TTDTimePoint _stoppedEnd{};
    bool _stoppedEndValid = false;

    // -----------------------------------------------------------------------
    // Replay-mode state (Phase 2 Item 2; parent TDD §8.2)
    // -----------------------------------------------------------------------

    /// True while inside EnterReplayMode / ExitReplayMode, so nested calls are safe
    bool _inReplayMode = false;
    /// A probe-frames query runs: the replay steps with the instruction-start check, so its exec taps are told
    bool _replayTaps = false;

    /// The host audio hold of the replay (reason TtdReplay): taken by EnterReplayMode after the replay flag is
    /// set, released by ExitReplayMode before it clears it (and with the manager). Replay runs as fast as the
    /// host goes; the user's master mute is never touched
    SoundManager::HostOutputHold _replayHostHold;

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
    bool _accelerationLocked = false;       // this session's lock covers acceleration (not a background one)
    /// A new session now would be a background one: the black box is on, or a debugger holds a history
    bool BackgroundWanted() const { return _blackBox || _debuggerHold; }
    bool _blackBox = false;                 // SetBlackBox
    uint32_t _recordingNumber = 0;          // recordings this instance started (RecordingSessionLabel)
    /// DeZog-forced RAM windows 1/2 (no latch describes them): beside each checkpoint, re-applied after the banks
    void CaptureBankOverrides(TTDBankOverrides& out) const;
    void ApplyBankOverrides(const TTDBankOverrides& in);
    uint32_t _blackBoxMinutes = 5;
    bool _blackBoxSuspended = false;        // stopped for an acceleration; starts again after it
    std::atomic<bool> _startPending{false};  // StartRecording inside a card's run: the machine starts it at the next boundary
    bool _restartPending = false;           // the session ended (reset, snapshot load, machine change): a new one at the next frame boundary
    /// The one rule (D42): the recording session ends, @p keepHistory = the machine is still the one the history belongs to.
    /// A new session starts at the next frame boundary for a black box, or when the `ttdrestart` feature is on
    void EndSessionImpl(const char* reason, bool keepHistory);
    bool RestartFeatureOn() const;
    /// The black box's window in frames (its minutes at the model's frame length)
    uint64_t BlackBoxFrames() const;
    /// A change of the machine itself (ROM reload, model transfer, General Sound card, slots) ends the session:
    /// the recording stops, its history goes; a black box starts a new session on the changed machine
    void EndSessionForMachineChange(const char* reason);
};

} // namespace ttd
