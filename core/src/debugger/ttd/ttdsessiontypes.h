#pragma once

/// @file ttdsessiontypes.h
/// @brief The types a time-travel session reports and takes - status
/// (TTDSessionInfo), seek / reverse / coverage / journal-build results, clip
/// export options and frames, performance and heap counters. Shared by v1's
/// TimeTravelManager and the engine's controller (Phase 5), so neither depends
/// on the other's header; TTDControl and the surfaces use them by these names.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "debugger/ttd/timetravelhooks.h"
#include "ttdcheckpoint.h"
#include "ttdcoverageindex.h"
#include "ttdexternalevents.h"
#include "ttdfileinfo.h"
#include "ttdphyspage.h"
#include "ttdprobe.h"
#include "ttdwritejournal.h"
#include "engine/ttdrestoreresult.h"
#include "engine/ttdwriteindex.h"

namespace ttd
{
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
    uint64_t deviceStateBytes = 0;    ///< device-state bytes serialized (raw, before compression)
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

/// What BuildWriteJournal did
struct TTDJournalBuildResult
{
    bool ok = false;              ///< false: refused, see `error` (a cancelled build is ok, with `cancelled`)
    std::string error;
    bool cancelled = false;       ///< the progress callback asked to stop; the frames built so far are kept
    uint64_t framesBuilt = 0;     ///< replayed, their writes added to the journal
    uint64_t framesCovered = 0;   ///< already inside a journal segment, left as they were
    uint64_t framesRefused = 0;   ///< hold a v1 marker without its data: cannot be replayed
    uint64_t records = 0;         ///< writes added
};

/// Called after each frame of a build: (frames done, frames to build). Return
/// false to stop; what is built so far is kept
using TTDJournalBuildProgress = std::function<bool(uint64_t done, uint64_t total)>;

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

    /// The write journal is recorded now (while recording) or will be at the
    /// next start (D40: off by default, switchable at any moment)
    bool writeJournalEnabled = false;
    /// The spans the write journal covers (D40), oldest first, in machine time...
    std::vector<TTDJournalSegment> writeJournalSegments;
    /// ...and as positions (frame, T-state): `from` excluded, `to` included
    std::vector<std::pair<TTDTimePoint, TTDTimePoint>> writeJournalSpans;
    /// One segment over the whole session: every write search answers from the
    /// journal. Outside the segments a write search replays (same answer, slower)
    bool writeJournalComplete = false;

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

    /// Why the last recording stopped when something other than a stop request ended
    /// it: "feature-off:timetravel" / "feature-off:debugmode" (FR-17, the clean stop
    /// before the feature switches off), "capture-failed" (the engine did not take a
    /// frame; the history before it stays). Empty otherwise; a new recording clears it.
    std::string lastStopReason;

    /// Why time travel is not available for this instance at all (for example
    /// a member of a ZX-Poly machine); empty when it is available.
    std::string unavailableReason;
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

/// A build in progress, readable from any thread (surfaces poll it)
struct TTDJournalBuildState
{
    bool active = false;
    uint64_t done = 0;    ///< frames built so far
    uint64_t total = 0;   ///< frames to build
};

/// @brief Result struct returned by ReverseContinue.
struct TTDReverseContinueResult
{
    bool           matched   = false;
    uint16_t       pc        = 0xFFFF;   ///< Valid iff matched.
    TTDTimePoint   arrivedAt{};          ///< Where the emulator landed.
    TTDExternalEvent blockingMarker{};   ///< Set iff a barrier halted the scan.
    TTDSearchWindow  window{};           ///< Part of history the scan examined (TD-8).
};
}  // namespace ttd
