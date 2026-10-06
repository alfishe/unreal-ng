/// @file timetravelcontroller.cpp
/// @brief TimeTravelController — the engine's playback controller (Phase 5, item 2).
/// Started as a copy of v1's TimeTravelManager (timetravelmanager.cpp, which stays
/// unchanged as the byte-exact reference); recording and restore go to its own
/// TimeTravelEngine, and v1's own storage leaves this class step by step (C1-C4,
/// docs/inprogress/2026-09-25-ttd-v2-migration/phase-5-switchover-tdd.md).
///
/// Per parent TDD §6.3, §7.1. The hot path is OnFrameBoundary: dirty pages
/// are freshly Intern'd, clean pages AddRef the previous checkpoint's slot,
/// CPU/chipset are field-copied via the helpers in ttdcheckpoint.cpp.

#include "timetravelcontroller.h"
#include "ttddisplayparticipant.h"

#include "timetravelengine.h"


#include <algorithm>
#include <array>
#include <deque>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include "ttdcheckpoint.h"
#include "ttdinputapply.h"
#include "ttddirtytracker.h"
#include "ttddumpformat.h"     // .ttd binary format constants
#include "ttdcodecpagestore.h"
#include "ttdcompression.h"    // codec::Compress / Decompress / Crc32C

#include "machinestatehash.h"  // CaptureSnapshot / HashSnapshot (self-test)
#include "ide/ttdatachannel.h"  // IDE board (implementation-plan.md D4)
#include <random>
#include <ctime>
#include "emulator/config.h"
#include "ttdmachineperipherals.h"  // RegisterMachinePeripherals (shared with MachineStateTransfer)
#include "emulator/io/rtc/ds12887.h"
#include "debugger/ttd/ttdconfigcapture.h"
#include "emulator/media/mediamanager.h"
#include "emulator/io/ide/idecontroller.h"

// Pull in the actual struct definitions for the capture call sites.
#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"            // Z80, Z80State
#include "emulator/emulator.h"           // Emulator::RunTStates (seek engine)
#include "emulator/mainloop.h"           // MainLoop::IsRunThread (live input gateway)
#include "emulator/emulatorcontext.h"    // EmulatorContext
#include "emulator/notifications.h"   // EmulatorFramePayload
#include "emulator/io/fdc/wd1793.h"      // WD1793 (peripheral, P1.5)
#include "emulator/io/tape/tape.h"        // Tape (peripheral, P1.5)
#include "emulator/io/mouse/mouse.h"      // Mouse (Kempston Mouse peripheral + input journal replay)
#include "emulator/memory/memory.h"      // Memory
#include "emulator/ports/portdecoder.h"  // PortDecoder::TtdEnginesSealed (port journal gate)
#include "emulator/platform.h"           // EmulatorState, CONFIG, PAGE_SIZE, MAX_RAM_PAGES
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/iturbosounddevice.h"  // ITurboSoundDevice (TurboSound-slot peripheral, design §3.3 / §8.2)
#include "emulator/video/screen.h"       // Screen, SpectrumScreenEnum (SetActiveScreen / SetBorderColor on restore)
#include "emulator/sound/covox.h"                        // Covox (peripheral, P1.5)
#ifdef UNREALNG_HAVE_OPL4
#include "emulator/sound/chips/soundchip_moonsound.h"   // SoundChip_Moonsound (peripheral, Tier A)
#endif
#include "emulator/sound/soundmanager.h"                 // SoundManager
#include "stdafx.h"

// Static assertion: the .ttd v1 format assumes a little-endian host. Every
// multi-byte field in the header / cpu_state / chipset_state is written in
// host order. If we ever port to a big-endian platform we must either add
// byte-swapping or bump the schema version and document the new convention.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              ".ttd v1 schema assumes little-endian host; add endian conversion if porting");
#elif defined(_MSC_VER)
// MSVC on Windows is always little-endian (x86/x64/ARM)
#else
#error "Unknown compiler - cannot verify endianness"
#endif

namespace
{
} // anonymous namespace

namespace ttd {


// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TimeTravelController::TimeTravelController(EmulatorContext* context)
    : _context(context)
{
    if (_context)
    {
        _memory = _context->pMemory;
        _logger = _context->pModuleLogger;
        if (_memory)
        {
            _dirtyTracker = _memory->GetTTDDirtyTracker();
        }
    }
    // Phase 5, C1: the engine is this controller's store - every capture goes to it and
    // every restore comes from it. Bound to the live machine as a recording starts
    _engine = std::make_unique<TimeTravelEngine>();
    _shadowEngine = _engine.get();
    _replayEngine = _engine.get();
    ApplyHistoryPolicy();
}

TimeTravelController::~TimeTravelController()
{
    // A clean close of the instance: its unsaved recording goes with it
    DiscardShadowFiles();
    if (_context && _context->ttdCoverage == &_coverageIndex)
        _context->ttdCoverage = nullptr;
    if (_context && _context->ttdWriteSink == this)
        _context->ttdWriteSink = nullptr;
    if (_context && _context->ttdPortReads == &_portReads)
        _context->ttdPortReads = nullptr;
    if (_context && _context->ttdPortWrites == &_portWrites)
        _context->ttdPortWrites = nullptr;

}

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

void TimeTravelController::SetUnavailableReason(const std::string& reason)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    if (!reason.empty() && _state != TTDSessionState::Idle)
        InvalidateSession(reason.c_str());
    _unavailableReason = reason;
}

bool TimeTravelController::StartRecording()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    if (_state == TTDSessionState::Recording)
        return true;  // Idempotent

    if (!_unavailableReason.empty())
    {
        MLOGWARNING("TimeTravelController::StartRecording — refused: %s", _unavailableReason.c_str());
        return false;
    }

    // Leaving the replay/browse scope for live recording: free the decode cache.
    ClearFrameCache();
    _lastStopReason.clear();
    _recordingPaused = false;
    _stoppedEndValid = false;
    // The machine's ROM set: the engine's configuration and restore checks compare with it
    _replayRomSignature = ComputeRomSignature();

    // The black box keeps its window (D29): the last N minutes of frames
    if (_blackBox)
    {
        const uint64_t frames = BlackBoxFrames();
        _historyLimitFrames.store(frames, std::memory_order_release);
        _historyLimitBytes.store(0, std::memory_order_release);
        ApplyHistoryPolicy();
    }

    // Fresh session — clear any stale auto-pause signal from a previous
    // Detached window.
    _autoPauseRequested.store(false, std::memory_order_release);
    // ...and a stale invalidation request from the previous session
    _pendingInvalidation.store(nullptr, std::memory_order_release);

    if (!_context || !_memory || !_dirtyTracker)
    {
        MLOGWARNING("TimeTravelController::StartRecording — missing dependencies (context=%p memory=%p tracker=%p)",
                    (void*)_context, (void*)_memory, (void*)_dirtyTracker);
        return false;
    }

    // Pause the emulator while we toggle features + capture the baseline.
    // The kDebugMode flip swaps Z80::MemIf (read on every memory access by
    // the CPU thread), so we must not race with emulation. Pause blocks
    // until the Z80 thread has parked.
    Emulator* emu = _context->pEmulator;
    // On the machine's own thread (a black box around turbo tape's warp) it is not running a frame now
    const bool wasRunning = !OnMachineThread() && emu && emu->IsRunning() && !emu->IsPaused();
    if (wasRunning)
    {
        emu->Pause(false);
        emu->WaitForPauseConfirmation(1000);
    }

    // One time base for the session: the host's wall time now, at the
    // machine's emulated time now. Every real-time clock anchors at it (their
    // TTDRecordingStarted below); a resume keeps the anchors the chips restored
    _context->ttdSessionWallMicros = Ds12887::HostCivilMicrosNow();
    _context->ttdSessionEmulatedMicros =
        _context->pPortDecoder ? _context->pPortDecoder->EmulatedMicroseconds() : 0;

    // --- Feature-flag stewardship (TDD §6.2/§6.3) ---------------------------
    // Capture requires:
    //   - Features::kDebugMode ON  -> Core routes writes through
    //     MemoryWriteDebug, which is the only path that calls
    //     TTDDirtyTracker::MarkDirty.
    //   - Features::kTimeTravel ON -> Memory's cached _feature_ttd_enabled
    //     flag is true, so the dirty-tracker call is not skipped.
    // If either is OFF, flip it ON and remember that we did so StopRecording
    // can restore the prior state. setFeature cascades through
    // FeatureManager::onFeatureChanged -> SelectMemoryInterface +
    // Memory::UpdateFeatureCache, so the gating cache is coherent before
    // we capture the baseline.
    FeatureManager* fm = _context->pFeatureManager;
    _toggledDebugModeOn = false;
    _toggledTimeTravelOn = false;
    EngageCaptureFeatures();

    // A refusal past this point leaves nothing behind: the flags this call
    // switched on go back off and the emulator resumes if it was running
    auto refuse = [&]() {
        if (fm)
        {
            if (_toggledDebugModeOn)
                fm->setFeature(Features::kDebugMode, false);
            if (_toggledTimeTravelOn)
                fm->setFeature(Features::kTimeTravel, false);
        }
        _toggledDebugModeOn = false;
        _toggledTimeTravelOn = false;
        if (wasRunning && emu)
            emu->Resume(false);
        return false;
    };

    // Model-specific serializers belong to the session: rebuild them here so a
    // model switch between sessions cannot leave a stale machine registered.
    //
    // Refusing here is deliberate. A model whose declared state has no
    // serializer would record happily and restore wrong - the failure would
    // surface later as a divergence with no trail back to this point.
    {
        std::string registrationError;
        if (!RegisterModelPeripherals(&registrationError))
        {
            MLOGERROR("TimeTravelController::StartRecording - refusing to record: %s",
                      registrationError.c_str());
            return refuse();
        }
    }

    // Clear any prior history (StartRecording always begins a fresh session).
    ResetShadow();
    if (!_timeline.empty())
    {
        _timeline.clear();
        _blobBytes = 0;
        _dirtyTracker->ResetSession();
        _dirtyScratch.clear();
        _inputJournal.Clear();  // Phase 2 Item 3 — drop any prior input events
        DisarmInputPlayback();
        _externalEvents.Clear();  // Phase 2 Item 6 — drop any prior markers
        _toolEditPayloads.clear();
        _bookmarks.Clear();  // TD-4 — prior bookmarks point into wiped history
        if (_writeJournal)
            _writeJournal->Clear();  // Phase 4 — drop any prior write records
    }
    // A fresh session: coverage of an earlier one (live or loaded) describes a
    // different timeline, and a stale "never touched" would prune a hit
    _coverageIndex.Clear();

    // Lazy-allocate write journal on first recording if enabled.
    // Use async allocation to avoid blocking the emulator thread.
    if (_enableWriteJournal && !_writeJournal)
    {
        _writeJournal = std::make_unique<TTDWriteJournal>(_writeJournalBytes, true);
    }

    // Wait for journal allocation to complete before proceeding.
    // This synchronizes with the async allocation started above (or earlier).
    if (_writeJournal)
    {
        _writeJournal->WaitReady();
        if (!_writeJournal->IsEmpty())
            _writeJournal->Clear();  // it must hold this session's writes only
    }
    _journalSegments.clear();   // a fresh session: SetState(Recording) opens the first segment
    _journalLostUpTo = 0;

    _modelRamPages = ResolveModelRamPages();
    if (_modelRamPages == 0 || _modelRamPages > MAX_RAM_PAGES)
    {
        MLOGWARNING("TimeTravelController::StartRecording — implausible modelRamPages=%u, refusing to start",
                    static_cast<unsigned>(_modelRamPages));
        _modelRamPages = 0;
        return refuse();
    }

    ++_recordingNumber;   // past the last refusal: this is a new recording session

    // Engaged before the baseline (past the last refusal above), so the very
    // first checkpoint already holds the 1x machine; SetState below is then a no-op
    EngageRecordingLock();

    // Port-read journal: a fresh session records every IN from its baseline
    // on - on configurations whose outside world reaches the CPU through IN
    // alone (ttd-port-read-journal.md §2)
    _portReads.Clear();
    _portWrites.Clear();
    // Recorded on every machine: the engine's bus data (Phase 3). v1 replays
    // from them only where its gate allows
    _portReads.StartRecording();
    _portWrites.StartRecording();
    _portJournalRecorded = true;
    if (const char* reason = PortJournalUnsupportedReason())
    {
        _portJournalValid = false;
        _portJournalOffReason = reason;
    }
    else
    {
        _portJournalValid = true;
        _portJournalOffReason.clear();
    }

    // Capture the baseline checkpoint so the timeline always has at least
    // one entry. This is the only place we pay the full model-RAM copy cost
    // up front (v1 strategy — see the header doc for the v2 fast-path plan).
    TTDCheckpoint baseline;
    if (!CaptureNow(baseline))
    {
        // The engine holds the history: without its baseline there is none.
        // The stop releases what this call engaged
        MLOGWARNING("TimeTravelController::StartRecording — refused: the engine did not take the baseline");
        SetState(TTDSessionState::Recording);
        StopRecording();
        _lastStopReason = "capture-failed";
        if (wasRunning && emu)
            emu->Resume(false);
        return false;
    }
    _blobBytes += BlobBytes(baseline);
    _timeline.push_back(std::move(baseline));

    SetState(TTDSessionState::Recording);
    DisarmInputPlayback();  // live input again (journaled while recording)
    _context->ttdCoverageActive = _enableCoverageIndex;
    SyncPortJournalHook();

    // This is live capture now, not the file it may have replaced.
    _loadedFromFile   = false;
    _inputHistoryComplete = true;
    _sourcePath.clear();
    _capturedAtUnixMs = 0;
    _loadedRomSignature = 0;
    _loadedNotRecordedMask = 0;
    _loadedRecordedBy.clear();
    _liveRomSignature = ComputeRomSignature();  // the ROM this session relies on

    MLOGINFO("TimeTravelController::StartRecording — baseline captured: modelRamPages=%u, timeline=1, debugMemIf=%s",
             static_cast<unsigned>(_modelRamPages), (_toggledDebugModeOn ? "switched-on" : "already-on"));

    // Sector reads from media go into the shadow engine (Phase 3)
    SyncMediaReadJournal();
    // Published while the machine is still parked: once it resumes it
    // records, and only its own thread may read the session then
    PublishSessionInfo();

    // Resume the emulator if we paused it. The recording OnFrameBoundary
    // hook will now see dirty bits being set correctly.
    if (wasRunning && emu)
        emu->Resume(false);

    return true;
}

void TimeTravelController::StopRecording()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    _queuedSnapshotLoad = nullptr;   // a load waiting for the boundary runs without the recording
    if (_state != TTDSessionState::Recording)
    {
        // Compress whatever coverage is still accumulating, so size reporting
        // and any later serialization see the whole session rather than
        // all-but-the-last block. Not recording: the machine adds nothing
        _coverageIndex.FlushOpenBlocks();
        if (_recordingPaused)
        {
            // A paused recording ends here: what it recorded stays as history
            _recordingPaused = false;
            UpdateInputWorkFlag();
        }
        return;  // Idempotent
    }

    // Park the machine BEFORE touching the session: while it records, its
    // thread appends to the coverage index, the journals and the timeline, so
    // the flush, the state change and the journal stop below must not run
    // beside a frame (same pause discipline as StartRecording, TDD section
    // 7.2). From the machine's own thread the wait returns at once.
    Emulator* emu = _context ? _context->pEmulator : nullptr;
    const bool wasRunning = !OnMachineThread() && emu && emu->IsRunning() && !emu->IsPaused();
    if (wasRunning)
    {
        emu->Pause(false);
        emu->WaitForPauseConfirmation(1000);
    }

    // Compress whatever coverage is still accumulating (see above)
    _coverageIndex.FlushOpenBlocks();

    if (_context)
    {
        const TTDTimePoint stoppedAt = CurrentPosition();
        _recordingStoppedAtT = GlobalT(stoppedAt);
        _stoppedEnd = stoppedAt;
        _stoppedEndValid = !_timeline.empty() && _timeline.back().time < stoppedAt &&
                           stoppedAt.frame == _timeline.back().time.frame;
    }
    SetState(TTDSessionState::Idle);
    // The machine runs on unrecorded from here: I/O passes through
    _portReads.Stop();
    _portWrites.Stop();
    SyncPortJournalHook();
    // The engine gets the last frame's journals too (a replay from the last
    // checkpoint reads them); it otherwise gets them at the next boundary
    FlushToEngine();
    SyncMediaReadJournal();   // no more recording into the engine
    FinishShadowFiles();
    MLOGINFO("TimeTravelController::StopRecording — timeline retained with %zu checkpoints",
             _timeline.size());

    // Restore feature flags we toggled in StartRecording. Only flip back the
    // ones we actually turned ON — pre-existing user/debugger debug mode is
    // left intact. (The MemIf swap must not race with CPU execution either.)
    FeatureManager* fm = _context ? _context->pFeatureManager : nullptr;
    if (fm)
    {
        if (_toggledDebugModeOn)
        {
            fm->setFeature(Features::kDebugMode, false);
            _toggledDebugModeOn = false;
            MLOGINFO("TimeTravelController::StopRecording — restored feature '%s' to OFF (was auto-enabled by StartRecording)",
                     Features::kDebugMode);
        }
        // Note: kTimeTravel is left enabled. It only gates Memory's cached
        // _feature_ttd_enabled flag, which is harmless when not recording,
        // and leaving it on lets the next StartRecording skip the toggle.
        _toggledTimeTravelOn = false;
    }

    PublishSessionInfo();

    if (wasRunning && emu)
        emu->Resume(false);
}

bool TimeTravelController::BeginDebuggerLiveHistory()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    if (_state == TTDSessionState::Detached)
    {
        MLOGWARNING("TimeTravelController::BeginDebuggerLiveHistory — refused: session is Detached "
                    "(return to the present or ResumeRecordingFrom first)");
        return false;
    }

    if (_recordMode == TTDRecordMode::DebuggerLive && _state == TTDSessionState::Recording)
        return true;  // Already live-debugging.

    const bool wasRecording = _state == TTDSessionState::Recording;

    // Adopt an existing live recording; otherwise append to retained
    // history when it is continuable (ResumeRecordingLive refuses on an
    // unrecorded gap), else fall back to a fresh StartRecording baseline —
    // a gapped/empty timeline is unreachable from the present anyway.
    bool ok = wasRecording || (!_timeline.empty() && ResumeRecordingLive());
    if (!ok)
    {
        ok = StartRecording();
        if (!ok)
            return false;
    }

    _recordMode = TTDRecordMode::DebuggerLive;
    MLOGINFO("TimeTravelController::BeginDebuggerLiveHistory — live history active "
             "(adopted existing recording: %s, timeline: %zu checkpoints)",
             wasRecording ? "yes" : "no", _timeline.size());
    return true;
}

void TimeTravelController::EndDebuggerLiveHistory()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    if (_recordMode != TTDRecordMode::DebuggerLive)
        return;  // Idempotent

    _recordMode = TTDRecordMode::Session;
    // Keeps the timeline: StopRecording transitions Recording → Idle with
    // history retained, so the scrubber and .ttd flows can take over.
    StopRecording();
    MLOGINFO("TimeTravelController::EndDebuggerLiveHistory — timeline retained with %zu checkpoints",
             _timeline.size());
}

void TimeTravelController::InvalidateSession(const char* reason)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    _queuedSnapshotLoad = nullptr;
    _stoppedEndValid = false;
    ClearFrameCache();

    if (_timeline.empty() && _state == TTDSessionState::Idle)
        return;  // Nothing to invalidate

    MLOGINFO("TimeTravelController::InvalidateSession — reason='%s', dropping %zu checkpoints",
             reason ? reason : "(null)", _timeline.size());
    DiscardShadowFiles();
    ResetShadow();
    _lastDropReason = reason ? reason : "";
    _recordingPaused = false;

    _timeline.clear();
    _blobBytes = 0;
    _dirtyScratch.clear();
    ReleaseModelPeripherals();  // serializers are session-scoped, like the timeline
    _inputJournal.Clear();  // Phase 2 Item 3 — input history invalidates with the timeline
    DisarmInputPlayback();
    _externalEvents.Clear();  // Phase 2 Item 6 — markers invalidate with the timeline
    _toolEditPayloads.clear();
    _bookmarks.Clear();  // TD-4 — bookmarks invalidate with the timeline
    if (_writeJournal)
        _writeJournal->Clear();  // Phase 4 — write journal invalidates with the timeline
    _journalSegments.clear();
    _journalLostUpTo = 0;
    _modelRamPages = 0;
    _dirtyPageOverflowReported = false;
    _loadedFromFile = false;
    _inputHistoryComplete = true;
    _sourcePath.clear();
    _capturedAtUnixMs = 0;
    _sessionModelId = 0;
    _loadedRomSignature = 0;
    _loadedNotRecordedMask = 0;
    _loadedRecordedBy.clear();
    _coverageIndex.Clear();
    if (_context)
        _context->ttdCoverageActive = false;
    _portReads.Clear();
    _portWrites.Clear();
    _portJournalValid = false;
    _portJournalRecorded = false;
    _portJournalOffReason.clear();
    SyncPortJournalHook();
    SetState(TTDSessionState::Idle);

    // Reset Phase 5 codec state.
    _evictedCheckpoints = 0;


    // Reset the dirty tracker too — the session-scoped _everDirty set is part
    // of the captured history's validity contract.
    if (_dirtyTracker)
        _dirtyTracker->ResetSession();
}

void TimeTravelController::EngageCaptureFeatures()
{
    FeatureManager* fm = _context ? _context->pFeatureManager : nullptr;
    if (!fm)
    {
        MLOGWARNING("TimeTravelController — FeatureManager is null; cannot verify debug/ttd flags. Capture will be a "
                    "no-op if debug memory interface is inactive.");
        return;
    }

    // Both read up front: enabling 'timetravel' switches the master
    // 'debugmode' on by itself, which is still ours to switch back off
    const bool debugModeWasOn = fm->isEnabled(Features::kDebugMode);
    if (!fm->isEnabled(Features::kTimeTravel))
    {
        fm->setFeature(Features::kTimeTravel, true);
        _toggledTimeTravelOn = true;
        MLOGINFO("TimeTravelController — auto-enabled feature '%s' (required for TTD capture)", Features::kTimeTravel);
    }
    if (!debugModeWasOn)
    {
        if (!fm->isEnabled(Features::kDebugMode))
            fm->setFeature(Features::kDebugMode, true);
        _toggledDebugModeOn = true;
        MLOGINFO("TimeTravelController — auto-enabled feature '%s' (required to route writes through MemoryWriteDebug -> "
                 "MarkDirty)",
                 Features::kDebugMode);
    }
}

void TimeTravelController::SetState(TTDSessionState next)
{
    _state = next;
    SyncCoverageSink();
    if (next == TTDSessionState::Recording)
        EngageRecordingLock();
    else if (next == TTDSessionState::Idle)
        ReleaseRecordingLock();
    SyncJournalSegment();   // a recording starts or ends a journal segment (D40)
}

bool TimeTravelController::JournalLive() const
{
    if (_state != TTDSessionState::Recording || !_enableWriteJournal || !_writeJournal || !_context)
        return false;
    FeatureManager* fm = _context->pFeatureManager;
    return !fm || (fm->isEnabled(Features::kTimeTravel) && fm->isEnabled(Features::kDebugMode));
}

void TimeTravelController::SyncJournalSegment()
{
    const bool open = !_journalSegments.empty() && _journalSegments.back().to == kSegmentOpen;
    const bool live = JournalLive();
    if (live == open || !_context)
        return;
    const uint64_t now = GlobalT(CurrentPosition());
    if (live)
    {
        // Switched back on at the instant it was switched off: one segment
        if (!_journalSegments.empty() && _journalSegments.back().to == now)
            _journalSegments.back().to = kSegmentOpen;
        else
            _journalSegments.push_back({now, kSegmentOpen});
        return;
    }
    TTDJournalSegment& last = _journalSegments.back();
    last.to = now;
    if (last.to <= last.from)
        _journalSegments.pop_back();
}

void TimeTravelController::ClipJournalSegments(uint64_t cutT)
{
    std::vector<TTDJournalSegment> kept;
    for (TTDJournalSegment s : _journalSegments)
    {
        if (s.from >= cutT)
            continue;
        s.to = std::min(s.to, cutT);
        kept.push_back(s);
    }
    _journalSegments = std::move(kept);
}

std::vector<TTDJournalSegment> TimeTravelController::JournalSegments() const
{
    std::vector<TTDJournalSegment> out;
    // Records the ring overwrote are no longer covered: only times after the
    // oldest record it still holds (records at that same time may be gone)
    const uint64_t evictedUpTo = std::max(
        _journalLostUpTo, _writeJournal && _writeJournal->HasEvictedRecords() ? _writeJournal->OldestGlobalT() : 0);
    const uint64_t now = _context ? GlobalT(CurrentPosition()) : 0;
    for (TTDJournalSegment s : _journalSegments)
    {
        if (s.to == kSegmentOpen)
            s.to = now;
        s.from = std::max(s.from, evictedUpTo);
        if (s.to > s.from)
            out.push_back(s);
    }
    return out;
}

void TimeTravelController::EngageRecordingLock()
{
    if (_recordingLockEngaged || !_context)
        return;
    _recordingLockEngaged = true;

    // Devices with a host-time dependence (the RTC) switch to emulated time
    // here, before StartRecording captures its baseline - a black box too
    _accelerationLocked = !_blackBox;
    if (!_accelerationLocked)
    {
        _peripherals.NotifyRecording(true);
        return;
    }

    // Host speed control back to 1x, applied now rather than at the next frame
    // boundary so no recorded T-state runs dilated. Callers have the CPU parked
    Core* core = _context->pCore;
    _savedHostSpeedMultiplier = core ? core->GetHostSpeedMultiplier() : 1;
    if (core && _savedHostSpeedMultiplier != 1)
    {
        core->SetSpeedMultiplier(1);
        // Applies the composed multiplier (host << hardware turbo) mid-frame,
        // rescaling the raster position; not specific to the hardware strobe
        core->GetZ80()->ApplyHardwareTurboNow();
        MLOGINFO("TimeTravelController — recording lock: host speed %ux -> 1x (restored when the session returns to Idle)",
                 static_cast<unsigned>(_savedHostSpeedMultiplier));
    }

    // Turbo mode off, turbo mode / fast tape / turbo tape / fast disk refused
    if (_context->pFeatureManager)
        _context->pFeatureManager->onTtdRecordingStarted();

    // Devices with a host-time dependence (the RTC) switch to emulated time
    // here, before StartRecording captures its baseline
    _peripherals.NotifyRecording(true);
}

void TimeTravelController::ReleaseRecordingLock()
{
    if (!_recordingLockEngaged || !_context)
        return;
    _recordingLockEngaged = false;

    _peripherals.NotifyRecording(false);
    if (!_accelerationLocked)
        return;   // a black box held nothing else
    _accelerationLocked = false;

    // Lifts the FeatureManager gate first: the speed restore below is checked by it
    if (_context->pFeatureManager)
        _context->pFeatureManager->onTtdRecordingStopped();

    if (_context->pCore && _savedHostSpeedMultiplier != 1)
    {
        _context->pCore->SetSpeedMultiplier(_savedHostSpeedMultiplier);
        MLOGINFO("TimeTravelController — recording lock released: host speed back to %ux",
                 static_cast<unsigned>(_savedHostSpeedMultiplier));
    }
    _savedHostSpeedMultiplier = 1;
}

void TimeTravelController::StopForFeatureChange(const char* feature)
{
    if (!IsRecording())
        return;
    // StopRecording parks the machine first, so no write slips between the stop and the
    // flag; the history and the stop position stay valid and browsable
    StopRecording();
    _lastStopReason = std::string("feature-off:") + (feature ? feature : "");
    MLOGINFO("TimeTravelController - recording stopped: %s", _lastStopReason.c_str());
}

void TimeTravelController::UpdateFeatureCache()
{
    FeatureManager* fm = _context ? _context->pFeatureManager : nullptr;
    if (!fm)
        return;

    const bool ttdEnabled = fm->isEnabled(Features::kTimeTravel);

    // Writes stop reaching the journal when TTD or debug mode goes off during
    // a recording, so it no longer holds the whole session. Only while
    // Recording: nothing is journaled when Idle or Detached, and StopRecording
    // and step-over switch debug mode back in exactly those states.
    SyncJournalSegment();

    // Pre-allocate write journal when TTD is enabled (async, non-blocking).
    // This way allocation completes before StartRecording() is called.
    if (ttdEnabled && _enableWriteJournal && !_writeJournal)
    {
        MLOGINFO("TimeTravelController::UpdateFeatureCache — TTD enabled, pre-allocating write journal (async)");
        _writeJournal = std::make_unique<TTDWriteJournal>(_writeJournalBytes, true);
    }

    // When TimeTravel feature is disabled and we're not recording,
    // deallocate the write journal to free memory (~64MB). A history that
    // stays (FR-17's clean stop) loses only this accelerator: reverse queries
    // replay a frame instead, and status reports the journal absent
    if (!ttdEnabled && _state == TTDSessionState::Idle && _writeJournal)
    {
        MLOGINFO("TimeTravelController::UpdateFeatureCache — TTD disabled, deallocating write journal");
        _writeJournal.reset();
    }
}

TTDSessionInfo TimeTravelController::GetSessionInfo() const
{
    TTDSessionInfo info;
    info.state = _state;
    info.checkpointCount    = _timeline.size();
    // The engine's piece store (Phase 5, C4a): its arena, the compressed
    // pieces in it, and the distinct 4 KB versions it holds - the most direct
    // measure of how much unique state has been captured
    const TTDPieceStore& store = _engine->PieceStore();
    info.pageStoreBytes     = store.ArenaBytes();
    info.pageStoreUsedBytes = store.PayloadBytes();
    info.baselineFramesCaptured = store.LiveVersions();

    // Real heap footprint — the actual number to display for "session
    // size". Distinct from pageStore* above: this includes checkpoint
    // metadata, journal backing, and counts allocated (not just live)
    // page-store bytes because that's what the process is actually
    // consuming.
    info.sessionHeapBytes = EstimateSessionHeapBytes();

    // Provenance. A loaded session and a live one are otherwise
    // indistinguishable from the numbers, which is the first thing anyone
    // handed a session needs to know.
    info.loadedFromFile    = _loadedFromFile;
    info.sourcePath        = _sourcePath;
    info.capturedAtUnixMs  = _capturedAtUnixMs;
    info.modelId           = _loadedFromFile
                                 ? _sessionModelId
                                 : static_cast<uint8_t>(_context ? _context->config.mem_model : 0);
    info.modelRamPages     = _modelRamPages;

    // The recorded machine, as a file would state it (ttdfileinfo.h)
    if (!_timeline.empty())
    {
        info.machine.modelId = info.modelId;
        info.machine.ramPageBound = _modelRamPages;
        info.machine.romSignature = _loadedFromFile ? _loadedRomSignature : _liveRomSignature;
        for (const TTDDeviceEntry& device : _engine->Devices().Entries())
            if (const uint8_t id = static_cast<uint8_t>(device.descriptor.legacyId); id < 64)
                info.machine.peripheralMask |= uint64_t(1) << id;
        info.machine.notRecordedMask = NotRecordedMask();
        ttd::DescribeRecordedMachine(info.machine);
    }
    info.recordedBy = _loadedFromFile ? _loadedRecordedBy : std::string();

    // Sections. The write journal is normally the largest part of a session,
    // and the coverage index decides whether reverse queries run in
    // milliseconds or replay frames.
    // The engine's index, and the current frame's writes still in the ring
    info.writeJournalRecords = static_cast<size_t>(_engine->Writes().Size()) + (_writeJournal ? _writeJournal->Size() : 0);
    info.writeJournalBytes   = info.writeJournalRecords * sizeof(TTDWriteRecord);

    info.coverageIndexFrames = _coverageIndex.SealedFrameCount(TTDCoverageKind::Executed);
    info.coverageIndexBytes  = _coverageIndex.EncodedBytes(TTDCoverageKind::Executed) +
                               _coverageIndex.EncodedBytes(TTDCoverageKind::Written) +
                               _coverageIndex.EncodedBytes(TTDCoverageKind::Read);

    info.bookmarkCount = _bookmarks.Size();
    info.inputEventCount = _inputJournal.Size();
    info.externalEventCount = _externalEvents.Size();
    info.inputHistoryComplete = _inputHistoryComplete;
    info.portJournalActive = _portJournalValid;
    info.portJournalOffReason = _portJournalOffReason;
    if (_portJournalValid)
    {
        info.portReadCount = _portReads.Size();
        info.portWriteCount = _portWrites.Size();
        info.portJournalBytes = _portReads.SerializedBytes() + _portWrites.SerializedBytes();
    }
    info.portReplayValueMismatches = _portReads.ValueMismatches();
    info.portReplayDivergences = _portReads.Divergences() + _portWrites.Divergences();
    info.lastDropReason = _lastDropReason;
    info.lastStopReason = _lastStopReason;
    info.recordingPaused = _recordingPaused;
    info.unavailableReason = _unavailableReason;

    // Phase 5 codec telemetry — useful for the UI / WebAPI status surface
    // to show compression effectiveness at a glance.
    // Key frames: where a segment starts, every piece stored whole (D41)
    info.livePayloadBytes = store.PayloadBytes();
    info.compressionRatio = info.livePayloadBytes
                                ? static_cast<double>(store.LiveVersions()) * kTTDPieceSize / static_cast<double>(info.livePayloadBytes)
                                : 0.0;
    info.keyFrameCount    = _timeline.empty() ? 0 : _engine->Segments().size();
    info.deltaFrameCount  = _timeline.size() - info.keyFrameCount;

    if (!_timeline.empty())
    {
        const auto& first = _timeline.front();
        const auto& last  = _timeline.back();
        info.sessionStartFrame = first.time.frame;
        info.currentEndFrame   = last.time.frame;
    }
    info.historyLimitFrames = _historyLimitFrames;
    info.historyLimitBytes = _historyLimitBytes;
    info.evictedCheckpoints = _evictedCheckpoints;
    info.historyBytes = HistoryBytes();

    info.writeJournalEnabled = _enableWriteJournal && _writeJournal != nullptr;
    info.writeJournalSegments = JournalSegments();
    for (const TTDJournalSegment& s : info.writeJournalSegments)
        info.writeJournalSpans.emplace_back(TimePointAt(s.from), TimePointAt(s.to));
    info.writeJournalComplete = JournalCoversSession(info.writeJournalSegments);

    PublishSessionInfo(info);
    return info;
}

bool TimeTravelController::SetWriteJournalCapacity(size_t bytes)
{
    // Only without a session: the ring holds the session's writes
    if (_state != TTDSessionState::Idle || !_timeline.empty())
        return false;
    const size_t wanted = bytes ? bytes : kDefaultWriteJournalBytes;
    if (wanted != _writeJournalBytes)
    {
        _writeJournalBytes = wanted;
        _writeJournal.reset();   // allocated again, at this size, when a recording starts
    }
    return true;
}

bool TimeTravelController::SwitchWriteJournal(bool enable)
{
    Emulator* emu = _context ? _context->pEmulator : nullptr;
    const bool wasRunning = emu && emu->IsRunning() && !emu->IsPaused();
    if (wasRunning)
    {
        emu->Pause(false);
        emu->WaitForPauseConfirmation(1000);
    }
    const bool ok = SetEnableWriteJournal(enable);
    if (wasRunning)
        emu->Resume(false);
    return ok;
}

void TimeTravelController::SetSessionSourcePath(const std::string& path)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    _sourcePath = path;
}

bool TimeTravelController::OnMachineThread() const
{
    return _context && _context->pMainLoop && _context->pMainLoop->IsRunThread();
}

TimeTravelController::SessionOperation::SessionOperation(const TimeTravelController& manager, Kind kind)
    : _manager(manager), _kind(kind)
{
    if (_manager.OnMachineThread())
        return;  // the owner: nothing runs beside it
    _manager._controlMutex.lock();
    _locked = true;
    ++_manager._operationDepth;

    // An active session is read (Detached replay) or written (Recording) by a
    // running machine: park it for the operation. Already paused - by the
    // caller, an outer operation or a breakpoint - nothing to do
    Emulator* emu = _manager._context ? _manager._context->pEmulator : nullptr;
    if (emu && _manager._state.load() != TTDSessionState::Idle && emu->IsRunning() && !emu->IsPaused())
    {
        emu->Pause(false);
        emu->WaitForPauseConfirmation(1000);
        _parked = true;
    }
}

TimeTravelController::SessionOperation::~SessionOperation()
{
    if (_kind == Kind::Change)
    {
        // The outermost operation publishes, and only where reading the live
        // session is safe: on the machine's thread, or with no machine
        // executing a recording (an operation that resumed a recording
        // machine itself - StartRecording - published before it did)
        Emulator* emu = _manager._context ? _manager._context->pEmulator : nullptr;
        const bool recordingMachineRuns = emu && emu->IsRunning() && !emu->IsPaused() &&
                                          _manager._state.load() == TTDSessionState::Recording;
        if (!_locked || (_manager._operationDepth == 1 && !recordingMachineRuns))
            _manager.PublishSessionInfo();
    }
    if (_parked)
    {
        if (Emulator* emu = _manager._context ? _manager._context->pEmulator : nullptr)
            emu->Resume(false);
    }
    if (_locked)
    {
        --_manager._operationDepth;
        _manager._controlMutex.unlock();
    }
}

TTDSessionInfo TimeTravelController::ReadSessionInfo() const
{
    if (OnMachineThread())
        return GetSessionInfo();

    // Another control operation in progress: it publishes when it ends
    std::unique_lock<std::recursive_mutex> lock(_controlMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return GetPublishedSessionInfo();

    Emulator* emu = _context ? _context->pEmulator : nullptr;
    if (emu)
    {
        // Something drives the machine (its loop, or a control thread stepping it)
        if (emu->IsDirectStepping() || (emu->IsRunning() && !emu->IsEmulationParked()))
        {
            lock.unlock();   // the machine's thread may need the control lock to reach its boundary
            // Only a session the running machine changes is refreshed (it publishes at its boundaries then)
            return _state.load() == TTDSessionState::Idle ? GetPublishedSessionInfo() : FreshPublishedSessionInfo();
        }
        // Parked, but a Resume from any thread would let a recording run beside
        // the read: the snapshot the machine published as it parked is exact
        if (emu->IsRunning() && _state.load() == TTDSessionState::Recording)
            return GetPublishedSessionInfo();
    }
    return GetSessionInfo();
}

void TimeTravelController::OnMachineParking()
{
    if (_state.load() == TTDSessionState::Recording && !_inReplayMode)
        PublishSessionInfo();
}

TTDSessionInfo TimeTravelController::GetPublishedSessionInfo() const
{
    // An observer is looking: the machine's thread publishes again at its next
    // frame boundary once the interval has passed
    _publishRequested.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(_publishedMutex);
    return _published;
}

void TimeTravelController::PublishSessionInfo(const TTDSessionInfo& info) const
{
    {
        std::lock_guard<std::mutex> lock(_publishedMutex);
        _published = info;
        _publishedAt = std::chrono::steady_clock::now();
    }
    _publishedCv.notify_all();
}

TTDSessionInfo TimeTravelController::FreshPublishedSessionInfo() const
{
    std::unique_lock<std::mutex> lock(_publishedMutex);
    const auto stale = std::chrono::steady_clock::now() - std::chrono::milliseconds(kPublishIntervalMs);
    if (_publishedAt < stale)
    {
        // The machine publishes at its next frame boundary once asked
        const auto asked = _publishedAt;
        _publishRequested.store(true, std::memory_order_release);
        _publishedCv.wait_for(lock, std::chrono::milliseconds(kFreshPublishWaitMs),
                              [&] { return _publishedAt != asked; });
    }
    _publishRequested.store(true, std::memory_order_release);
    return _published;
}

void TimeTravelController::MaybePublishAtFrameBoundary()
{
    // Throwaway replays (display composition, frame-cache builds) cross frame
    // boundaries on a restored machine: nothing to report from there
    if (_inReplayMode || !_publishRequested.load(std::memory_order_acquire))
        return;
    const auto now = std::chrono::steady_clock::now();
    if (now - _lastPublish < std::chrono::milliseconds(kPublishIntervalMs))
        return;
    _lastPublish = now;
    _publishRequested.store(false, std::memory_order_release);
    PublishSessionInfo();
}

bool TimeTravelController::SetEnableWriteJournal(bool enable)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    if (enable == _enableWriteJournal)
        return true;
    // Any moment, also during a recording and inside a frame (D40): the switch
    // starts or ends a journal segment at the current instruction
    _enableWriteJournal = enable;
    if (enable && _state == TTDSessionState::Recording && !_writeJournal)
        _writeJournal = std::make_unique<TTDWriteJournal>(_writeJournalBytes, false);
    SyncJournalSegment();

    // Nothing to answer for and nothing to record into: give back the 64 MB
    // the feature pre-allocated. A retained session keeps its journal.
    if (!enable && _state == TTDSessionState::Idle && _timeline.empty() && _writeJournal)
    {
        _writeJournal->WaitReady();
        _writeJournal.reset();
    }
    return true;
}

uint64_t TimeTravelController::CheckpointStartT(const TTDCheckpoint& cp) const
{
    // The CPU stands past the frame boundary by the last instruction's overshoot
    const uint64_t units = _context && _context->config.frame ? std::max<uint64_t>(1, FrameSpan() / _context->config.frame) : 1;
    return GlobalT(cp.time) + uint64_t(GetChipsetCpuTInFrame(cp.chipset)) * units;
}

bool TimeTravelController::JournalCoversSession(const std::vector<TTDJournalSegment>& segments) const
{
    if (segments.size() != 1 || _timeline.empty())
        return false;
    return segments[0].from <= CheckpointStartT(_timeline.front()) && segments[0].to >= GlobalT(_timeline.back().time);
}

size_t TimeTravelController::EstimateSessionHeapBytes() const
{
    return GetHeapBreakdown().Total();
}

TTDHeapBreakdown TimeTravelController::GetHeapBreakdown() const
{
    TTDHeapBreakdown h;

    // The engine (Phase 5, C4a): its piece store, reference tables and
    // capture state; checkpoints with the frame table; device state is in its
    // pieces. Its copies of the journals are counted with v1's below
    const TTDEngineHeapBreakdown e = _engine->HeapBreakdown();
    h.pageStoreTable = e.pieceVersions + e.referenceTables + e.deltaBase + e.bookkeeping;
    h.ramPayload = e.piecePayload;
    h.ramPayloadSlack = e.arenaSlack;
    h.checkpoints = e.checkpoints + e.frameTable + _timeline.capacity() * sizeof(TTDCheckpoint);
    h.deviceBlobs = e.deviceBlobs + e.frameStreams;
    for (const TTDCheckpoint& cp : _timeline)
        for (const auto& entry : cp.peripheralBlobs)
            h.deviceBlobs += entry.second.capacity();

    // Input journal + external-event journal — same pattern: capacity is
    // what's allocated, size is what's logically used. Plus the session-scope
    // dirty-page scratch buffer (reused every frame, counted once).
    h.inputJournals = _inputJournal.Events().capacity() * sizeof(TTDInputEvent) +
                      _externalEvents.Events().capacity() * sizeof(TTDExternalEvent) +
                      _dirtyScratch.capacity() * sizeof(uint16_t) + e.eventLog;

    // Write journal (committed ring chunks, not the nominal 64 MB), coverage
    // index and the decoded-frame cache hold the session's data; without a
    // session what they keep reserved is not the session's (zero, as before)
    if (!_timeline.empty())
    {
        if (_writeJournal)
        {
            h.writeJournal = _writeJournal->HeapBytes();
            const size_t used = _writeJournal->Size() * sizeof(TTDWriteRecord);
            h.writeJournalSlack = h.writeJournal > used ? h.writeJournal - used : 0;
        }
        h.writeJournal += e.writeJournal;
        h.coverage = _coverageIndex.HeapBytes();
        h.coverageSlack = _coverageIndex.CompressedSlackBytes();
        h.portReads = _portReads.HeapBytes() + e.portReads + e.busVectors + e.mediaReads;
        h.portWrites = _portWrites.HeapBytes() + e.portWrites;
        h.portJournalSlack = _portReads.CompressedSlackBytes() + _portWrites.CompressedSlackBytes() + e.portJournalSlack;
        if (_frameCache)
            h.frameCache = _frameCache->Bytes();
    }

    return h;
}

// ---------------------------------------------------------------------------
// Capture (emulator thread)
// ---------------------------------------------------------------------------

std::string TimeTravelController::RecordingGuard(TTDGuardedAction action) const
{
    // Only a recording the user started is protected - also while it is paused
    // for browsing (D8): it goes on from where it paused. A debugger's live
    // history (DebuggerLive, DeZog) is a rolling background history: any
    // outside change drops it and the debugger restarts it on the next resume or step.
    if (!(IsRecording() || _recordingPaused) || IsDebuggerLive())
        return {};

    switch (action)
    {
        case TTDGuardedAction::LoadSnapshot:
            return {};   // part of the recording (D10, QueueSnapshotLoad)
        case TTDGuardedAction::LoadRom:
        case TTDGuardedAction::SwitchGsCard:
        case TTDGuardedAction::SwitchModel:
        case TTDGuardedAction::ChangeSlots:
            // A change of the machine itself ends the session (owner rule 2026-10-05):
            // EndSessionForMachineChange, then a black box starts a new one
            return {};
        case TTDGuardedAction::LoadTape:
            return "Cannot insert a tape while TTD is recording: a new medium would drop the recorded history. Insert "
                   "it before starting the recording, or stop the recording first.";
        case TTDGuardedAction::LoadDisk:
            return "Cannot insert a disk while TTD is recording: a new medium would drop the recorded history. Insert "
                   "it before starting the recording, or stop the recording first.";
        case TTDGuardedAction::CreateDisk:
            return "Cannot create a disk while TTD is recording: a new medium would drop the recorded history. Create "
                   "it before starting the recording, or stop the recording first.";
        case TTDGuardedAction::Invalidate:
            return "Cannot discard the TTD session while it is recording. Stop the recording first, then discard it.";
        case TTDGuardedAction::CdFrontPanel:
            return "Cannot play, pause, stop or change the volume of a CD drive from outside the guest while TTD is "
                   "recording: a replay would not repeat it. Let the guest's CD player do it, or stop the recording first.";
    }
    return "This action is not allowed while TTD is recording. Stop the recording first.";
}

void TimeTravelController::RequestInvalidation(const char* reason)
{
    if (_state != TTDSessionState::Recording)
        return;
    const char* expected = nullptr;
    if (_pendingInvalidation.compare_exchange_strong(expected, reason ? reason : "(unspecified)",
                                                     std::memory_order_acq_rel))
        MLOGINFO("TimeTravelController::RequestInvalidation — '%s' (applied at the frame boundary)",
                 reason ? reason : "(unspecified)");
}

void TimeTravelController::OnFrameBoundary()
{
    if (!_context)
        return;

    // A device TTD cannot follow was used during the frame that just ended:
    // its checkpoint would not be restorable, so the session ends here
    if (const char* reason = _pendingInvalidation.exchange(nullptr, std::memory_order_acq_rel))
    {
        if (_state == TTDSessionState::Recording)
        {
            InvalidateSession(reason);
            return;
        }
    }

    // A machine change ended the black box's session: the new one starts here,
    // on the changed machine
    if (_blackBoxRestart && _state == TTDSessionState::Idle)
    {
        _blackBoxRestart = false;
        if (_blackBox && StartRecording())
            MLOGINFO("TimeTravelController: the black box records again after a machine change (a new session)");
        return;
    }

    // ------------------------------------------------------------------
    // Recording: append a checkpoint for the just-completed frame.
    // ------------------------------------------------------------------
    if (_state == TTDSessionState::Recording)
    {
        // A DebuggerLive paused-browse build is invalidated the moment
        // execution advances past the frame it decoded — entries for the
        // cached frame would be incomplete. In Session mode this is a
        // harmless no-op: browse scopes never cross a live boundary (the
        // cache only exists while not Recording).
        ClearFrameCache();

        if (!_memory || !_dirtyTracker)
            return;  // Defensive — should not happen if StartRecording succeeded

        // Close the coverage sets for the frame that just ended.
        //
        // The frame counter has ALREADY advanced by the time this runs, so the
        // execution being sealed belongs to frame_counter - 1: a checkpoint
        // labelled N holds the state at the START of frame N, and the M1
        // records for frame N carry globalT in [N*frameT, (N+1)*frameT).
        // Sealing under frame_counter shifted every coverage set one frame into
        // the future, which made reverse search look in the wrong frame and
        // report no match for a PC that had plainly executed.
        const auto captureStart = std::chrono::steady_clock::now();
        const uint64_t endedFrame = _context->emulatorState.frame_counter;
        if (_enableCoverageIndex && endedFrame > 0)
            _coverageIndex.SealFrame(endedFrame - 1);

        // D10: a snapshot load queued for this boundary replaces the machine
        // here, and the checkpoint below takes the loaded state. Machine time
        // goes on: the loader's reset zeroed the frame counter and the T-state
        // count, which the frame table and every position follow
        if (_queuedSnapshotLoad)
        {
            const std::function<void()> load = std::move(_queuedSnapshotLoad);
            _queuedSnapshotLoad = nullptr;
            EmulatorState& st = _context->emulatorState;
            const uint64_t frame = st.frame_counter;
            const uint64_t base = st.t_states;
            load();
            st.frame_counter = frame;
            st.t_states = base;
            // The frame start ran before the load (MainLoop::CompleteFrame) and the
            // loader's reset cleared the device frame bases: take them from the
            // loaded state, as a load outside a recording does
            if (_context->pEmulator)
                _context->pEmulator->RestartFrame();
            // The cut sits at the boundary itself (the loaded CPU may stand a few T-states in)
            TTDPendingFact cut;
            cut.at = {frame, 0};
            cut.ev.kind = TTDEventKind::SnapshotLoad;
            _shadowFacts.push_back(cut);
            _shadowRescan = true;   // the loader wrote memory behind the dirty tracker
        }

        TTDCheckpoint cp;
        if (!CaptureNow(cp))
        {
            // The engine holds the history: a frame it did not take ends the
            // recording, everything before it stays browsable
            StopRecording();
            _lastStopReason = "capture-failed";
            MLOGWARNING("TimeTravelController - recording stopped: the engine did not take frame %llu",
                        static_cast<unsigned long long>(endedFrame));
            return;
        }
        _blobBytes += BlobBytes(cp);
        _timeline.push_back(std::move(cp));
        EnforceHistoryLimit();
        _perf.lastCaptureNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - captureStart).count());
        MaybePublishAtFrameBoundary();
        return;
    }

    // ------------------------------------------------------------------
    // Detached: the user seeked to a historical point and then resumed.
    // Auto-pause once execution reaches the end of the recorded session
    // so the emulator doesn't silently run past the timeline's known
    // state into unrecorded territory. The user can explicitly
    // ResumeRecordingFrom() to continue capturing new frames beyond the
    // original session end, or StartRecording() to begin a fresh session.
    //
    // This runs on the thread driving the frame (called from
    // MainLoop::CompleteFrame, the boundary of every run path). Emulator::Pause() is
    // safe to call here — it just sets the _isPaused flag, which the
    // MainLoop checks at the top of its next iteration.
    // ------------------------------------------------------------------
    // A throwaway replay (ComposeDisplay, frame-cache builds) crossing the
    // session end is not execution running into unrecorded territory; it
    // must leave no auto-pause behind.
    //
    // D10: execution on the recorded timeline (a replay, or the machine playing
    // the history forward) that reaches a snapshot load takes the loaded state
    // there, as the recording did
    if ((_state == TTDSessionState::Detached || _inReplayMode) && !_timeline.empty())
    {
        const uint64_t frame = _context->emulatorState.frame_counter;
        if (CutAtFrameStart(frame))
        {
            const int64_t i = TimelineIndexAtOrBefore(TTDTimePoint{frame, 0});
            if (i >= 0 && _timeline[static_cast<size_t>(i)].time.frame == frame)
                RestoreCheckpointForReplay(_timeline[static_cast<size_t>(i)]);
        }
    }
    if (_state == TTDSessionState::Detached && !_timeline.empty() && !_inReplayMode)
    {
        const uint64_t sessionEnd = _timeline.back().time.frame;
        const uint64_t currentFrame = _context->emulatorState.frame_counter;
        if (currentFrame > sessionEnd)
        {
            MLOGINFO("TimeTravelController: auto-pause at frame %llu "
                     "(reached session end %llu, state=Detached)",
                     static_cast<unsigned long long>(currentFrame),
                     static_cast<unsigned long long>(sessionEnd));
            // Set the flag first so synchronous-test callers can observe
            // the request even when Emulator::Pause() is a no-op (the
            // async main loop isn't running in test mode).
            _autoPauseRequested.store(true, std::memory_order_release);
            if (_context->pEmulator)
                _context->pEmulator->Pause();
        }
        MaybePublishAtFrameBoundary();
    }
}

std::string TimeTravelController::RecordingSessionLabel() const
{
    if (!IsRecording())
        return {};
    std::string label = "#" + std::to_string(_recordingNumber);
    if (!_timeline.empty())
        label += ", started at frame " + std::to_string(_timeline.front().time.frame);
    return label;
}

void TimeTravelController::CaptureBankOverrides(TTDBankOverrides& out) const
{
    out = TTDBankOverrides{};
    if (!_memory)
        return;
    for (uint8_t bank = 1; bank <= 2; ++bank)
        out.page[bank - 1] = _memory->GetDebuggerBankOverride(bank);  // MEMORY_UNMAPPABLE (0xFFFF) = none
}

void TimeTravelController::ApplyBankOverrides(const TTDBankOverrides& in)
{
    if (!_memory)
        return;
    for (uint8_t bank = 1; bank <= 2; ++bank)
    {
        const uint16_t page = in.page[bank - 1];
        if (page < MAX_RAM_PAGES)
            _memory->SetDebuggerRAMPageToBank(bank, page);
        else
            _memory->RevertDebuggerBankOverride(bank);  // the checkpoint had none
    }
}

uint64_t TimeTravelController::BlackBoxFrames() const
{
    const unsigned frameMicros = _context && _context->config.frame_duration_us ? _context->config.frame_duration_us : 20000;
    return uint64_t(_blackBoxMinutes) * 60 * 1000000 / frameMicros;
}

void TimeTravelController::SetBlackBoxMinutes(uint32_t minutes)
{
    _blackBoxMinutes = minutes ? minutes : 5;
    if (_blackBox && _state == TTDSessionState::Recording)
        SetHistoryLimit(BlackBoxFrames(), 0);
}

void TimeTravelController::SetBlackBox(bool on, uint32_t minutes)
{
    _blackBox = on;
    _blackBoxMinutes = minutes ? minutes : 5;
    _blackBoxSuspended = false;
    _blackBoxRestart = false;
}

bool TimeTravelController::AccelerationActive() const
{
    if (!_context)
        return false;
    // The fast loaders are not among them: their traps are recorded edits
    return _context->pCore && (_context->pCore->GetHostSpeedMultiplier() != 1 || _context->pCore->IsTurboMode());
}

void TimeTravelController::OnAccelerationChanging(bool accelerating)
{
    if (!_blackBox)
        return;
    if (accelerating)
    {
        // Before the acceleration takes effect: nothing accelerated is recorded
        if (_state == TTDSessionState::Recording)
        {
            StopRecording();
            _lastStopReason = "acceleration";
            _blackBoxSuspended = true;
            MLOGINFO("TimeTravelController: the black box stops while an acceleration runs (history kept)");
        }
        return;
    }
    if (_blackBoxSuspended && !AccelerationActive() && _state != TTDSessionState::Recording)
    {
        _blackBoxSuspended = false;
        KeepShadowFiles();   // what led up to the acceleration stays loadable
        if (StartRecording())
            MLOGINFO("TimeTravelController: the black box records again (a new session)");
    }
}

void TimeTravelController::OnConfigurationChange(TTDConfigChangeKind kind, const char* reason)
{
    // The black box's history was recorded at 1x and stays valid when the host
    // speed changes afterwards (the speed is pacing; OnAccelerationChanging
    // handles the recording)
    if (kind == TTDConfigChangeKind::SpeedMultiplier)
    {
        if (!_blackBox)
            InvalidateSession(reason);
        return;
    }
    EndSessionForMachineChange(reason);   // the ROM set, the General Sound card
}

void TimeTravelController::EndSessionForMachineChange(const char* reason)
{
    const bool blackBoxRecorded = _blackBox && (_state == TTDSessionState::Recording || _blackBoxSuspended);
    if (_state == TTDSessionState::Recording || _recordingPaused)
        StopRecording();   // cleanly: the machine parked, the features it switched on back off
    if (blackBoxRecorded)
        KeepShadowFiles();   // what led up to the change stays loadable
    InvalidateSession(reason);
    _lastStopReason = "machine-change";
    _blackBoxSuspended = false;
    _blackBoxRestart = blackBoxRecorded;
    if (_blackBoxRestart)
        MLOGINFO("TimeTravelController: '%s' ended the black box's session; a new one starts at the next frame", reason);
}

bool TimeTravelController::CutAtFrameStart(uint64_t frame) const
{
    return _engine && _engine->IsSessionOpen() && _engine->Events().HasCutAt(GlobalT(TTDTimePoint{frame, 0}));
}

void TimeTravelController::OnLoad(TTDLoadKind kind, const char* reason)
{
    // D10: a snapshot outside a recording replaces the live machine only; the
    // history stays, the machine leaves it as after a reset. A recording
    // paused for browsing ends where it paused (its history kept)
    if (kind == TTDLoadKind::Snapshot && _state != TTDSessionState::Recording)
    {
        if (_recordingPaused)
            StopRecording();
        OnMachineReset();
        return;
    }
    InvalidateSession(reason);
}

bool TimeTravelController::QueueSnapshotLoad(std::function<void()> load)
{
    // A recording paused at its end goes on with the load, as with an edit (D9)
    if (_recordingPaused && _state == TTDSessionState::Detached && CurrentPosition() == _pausedEnd)
        ContinueRecordingAt(_pausedEnd);
    if (!load)
    {
        _queuedSnapshotLoad = nullptr;   // the caller withdraws it
        return false;
    }
    if (_state != TTDSessionState::Recording)
        return false;
    _queuedSnapshotLoad = std::move(load);
    return true;
}

bool TimeTravelController::ConsumeAutoPauseRequest()
{
    return _autoPauseRequested.exchange(false, std::memory_order_acq_rel);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

bool TimeTravelController::CaptureNow(TTDCheckpoint& out)
{
    assert(_context && _memory && _dirtyTracker);

    _captureWork = TTDCaptureWork{};

    // --- Time coordinate: checkpoints sit at frame boundaries (TDD §4.1) ---
    const EmulatorState& st = _context->emulatorState;
    out.time.frame     = st.frame_counter;
    out.time.tInFrame  = 0;
    out.globalT        = st.frame_counter;

    // --- CPU + chipset (the chipset holds the frame-end overshoot) ---
    Z80* cpu = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (cpu)
    {
        out.cpu     = CaptureCpuState(*static_cast<Z80State*>(cpu));
        out.cpu.nmi_pending = cpu->IsNmiPending() ? 1 : 0;
    }
    out.chipset = CaptureChipsetState(st, cpu ? static_cast<uint32_t>(cpu->t) : 0u);
    CaptureBankOverrides(out.bankOverrides);

    // --- Device state, through the registry (TDD §6.4): raw, as the engine takes it. A
    // device whose state also carries the memory it offers as regions (General Sound)
    // gives its state without that memory; the registry does not serialize it whole ---
    _withoutRegions.clear();
    std::array<bool, 256> external{};
    for (ITTDRegionSource* source : _peripherals.RegionSources())
    {
        uint8_t id = 0;
        std::vector<uint8_t> state;
        if (source->TTDStateWithoutRegions(id, state))
        {
            external[id] = true;
            _withoutRegions.emplace_back(id, std::move(state));
        }
    }
    _peripherals.CaptureStates(_capturedDevices, external);
    _captureWork.deviceStateBytes += _peripherals.LastCaptureStateBytes();
    for (const auto& [id, state] : _withoutRegions)
        _captureWork.deviceStateBytes += state.size();

    // --- Port journal positions: a replay from here starts handing out records at them ---
    out.portReadCursor = _portJournalRecorded ? _portReads.Size() : 0;
    out.portWriteCursor = _portJournalRecorded ? _portWrites.Size() : 0;

    // --- Memory: the pieces written since the last capture go to the engine (all of them
    // on the first capture of a session, a rescan) ---
    const bool baseline = _timeline.empty();
    out.frameKind = baseline ? TTDFrameKind::KeyFrame : TTDFrameKind::DeltaFrame;
    out.keyFrameAnchor = baseline ? out.time.frame : _timeline.back().keyFrameAnchor;
    _dirtyScratch.clear();
    _dirtyTracker->CollectAndClear(_dirtyScratch);

    const bool taken = FeedShadow(out, baseline);
    _perf.lastCaptureWork = _captureWork;
    return taken;
}

uint16_t TimeTravelController::ResolveModelRamPages() const
{
    if (!_context)
        return 0;

    const CONFIG& cfg = _context->config;

    // The result is an EXCLUSIVE PAGE-INDEX BOUND, not a page count. Capture
    // walks [0, bound), so the bound must cover the highest page number the
    // configuration can address — which is not ramsize/16 whenever a model maps
    // its RAM at non-contiguous page numbers.
    //
    // The 48K machine is exactly that case: three pages of RAM, but Memory maps
    // them as pages 5 (screen), 2 and 0 (Memory::Reset). A ramsize/16 bound of 3
    // walks pages 0..2 and silently drops the screen, so a 48K recording
    // restores correct registers into a blank display.
    //
    // TODO(models): this per-model knowledge belongs in the model/config layer
    // next to the port decoders, not here. When TSConf/ZX-Evo/Profi/Scorpion
    // extended paging lands, replace this with a page set published by the
    // configuration itself — see the TDD section "Per-configuration RAM pages".
    if (cfg.mem_model == MM_SPECTRUM48)
        return 6;  // pages {0, 2, 5} in use → highest index 5

    // config.ramsize is in KB; each page is PAGE_SIZE = 16 KB.
    if (cfg.ramsize == 0 || cfg.ramsize > MAX_RAM_PAGES * (PAGE_SIZE / 1024))
    {
        MLOGWARNING("TimeTravelController::ResolveModelRamPages — implausible ramsize=%u KB, falling back to MAX_RAM_PAGES",
                    cfg.ramsize);
        return MAX_RAM_PAGES;
    }
    return static_cast<uint16_t>(cfg.ramsize / (PAGE_SIZE / 1024));
}

const TTDCheckpoint* TimeTravelController::GetCheckpoint(size_t idx) const
{
    if (idx >= _timeline.size())
        return nullptr;
    return &_timeline[idx];
}

// ---------------------------------------------------------------------------
// Restore path (Phase 2 Item 1; parent TDD §8.1 step 2)
// ---------------------------------------------------------------------------

bool TimeTravelController::RestoreCheckpointForTesting(size_t idx)
{
    if (idx >= _timeline.size())
    {
        MLOGWARNING("TimeTravelController::RestoreCheckpointForTesting — idx %zu out of range (timeline size=%zu)",
                    idx, _timeline.size());
        return false;
    }

    // Allow any non-empty state: Recording, Detached, and Idle-with-history
    // are all valid for direct checkpoint inspection. The timeline-empty
    // check above already handles the truly-empty case.
    // Note: this test-only API is intended to be permissive so tests can
    // inspect history without first transitioning to Detached.

    if (!_context || !_memory)
    {
        MLOGWARNING("TimeTravelController::RestoreCheckpointForTesting — missing dependencies");
        return false;
    }

    RestoreCheckpointForReplay(_timeline[idx]);
    return true;
}

uint64_t TimeTravelController::ComputeRomSignature() const
{
    if (!_memory || !_memory->ROMBase())
        return ttd::dump::kRomSignatureUnknown;

    // Hash the whole ROM region rather than just the pages the current paging
    // happens to expose: on ProfROM machines the quadrants outside the active
    // plane are exactly what a later seek will page in, so a session recorded
    // against a different image must not compare equal.
    const size_t romBytes = static_cast<size_t>(MAX_ROM_PAGES) * PAGE_SIZE;
    const uint64_t signature = ttd::HashBytes(_memory->ROMBase(), romBytes);

    // Never collide with the "unknown" sentinel.
    return signature == ttd::dump::kRomSignatureUnknown ? 1u : signature;
}

bool TimeTravelController::RegisterModelPeripherals(std::string* err)
{
    // The device set is enumerated in one place (ttdmachineperipherals.cpp),
    // shared with MachineStateTransfer. On failure the registry is left empty.
    return RegisterMachinePeripherals(_context, _peripherals, _ownedPeripherals, err);
}

void TimeTravelController::ReleaseModelPeripherals()
{
    // Clear wholesale rather than unregistering piecemeal: most registered
    // devices are owned by the emulator, so there is no local list of them to
    // walk, and this manager is the only thing that ever registers anything.
    _peripherals.Clear();
    _ownedPeripherals.clear();
}

void TimeTravelController::RestoreCheckpoint(const TTDCheckpoint& cp)
{
    assert(_context && _memory);
    using PerfClock = std::chrono::steady_clock;
    auto elapsedNs = [](PerfClock::time_point from, PerfClock::time_point to) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count());
    };
    const PerfClock::time_point restoreStart = PerfClock::now();

    // The engine is the store (Phase 5, C1c): its checkpoint of this frame holds the
    // CPU, the chipset, every memory region and every device
    const int64_t engineIndex = _engine->CheckpointIndexOf({0, cp.time.frame, 0});
    const TTDEngineCheckpoint* engineCp = engineIndex >= 0 ? _engine->Checkpoint(size_t(engineIndex)) : nullptr;
    if (!engineCp)
    {
        MLOGWARNING("TimeTravelController::RestoreCheckpoint — the engine has no checkpoint of frame %llu",
                    static_cast<unsigned long long>(cp.time.frame));
        return;
    }

    // --- CPU registers (TDD §8.1 step 2a). Host-side fields (MemIf pointers,
    // trace cursors, isDebugMode, prev_pc/m1_pc/last_branch/nextpc) are kept ---
    Z80* cpu = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (cpu)
    {
        RestoreCpuState(engineCp->cpu, static_cast<Z80State*>(cpu));
        cpu->SetNmiPending(engineCp->cpu.nmi_pending != 0);
    }

    // --- Chipset port latches + counters (a pure field copy into emulatorState) ---
    RestoreChipsetState(engineCp->chipset, &_context->emulatorState);

    // The CPU's in-frame position (the frame-end overshoot), then the frame geometry
    // (frame limit, INT window) from the restored multiplier
    if (cpu)
    {
        cpu->t = GetChipsetCpuTInFrame(engineCp->chipset);
        static_cast<Z80*>(cpu)->RecomputeFrameTiming();
    }

    // --- Every region (machine RAM, device memories), then the devices, then the
    // banks the restored latches select ---
    const PerfClock::time_point memoryStart = PerfClock::now();
    _engine->ForgetMemory();
    CheckEngineCheckpoint(size_t(engineIndex), false);
    const TTDRestoreResult memory = _engine->RestoreToMemory(size_t(engineIndex));
    const PerfClock::time_point devicesStart = PerfClock::now();
    const TTDRestoreResult devices =
        _engine->RestoreDevices(size_t(engineIndex), TTDRestoreContext{engineCp->position.frame, 0, true});
    if (!memory.Ok() || devices.status != TTDRestoreStatus::Exact)
        MLOGWARNING("TimeTravelController::RestoreCheckpoint — engine restore of frame %llu: %s%s%s",
                    static_cast<unsigned long long>(cp.time.frame), memory.message.c_str(),
                    memory.message.empty() ? "" : "; ", devices.message.c_str());
    _memory->UpdateZ80Banks();
    ApplyBankOverrides(cp.bankOverrides);
    _shadowRescan = true;   // live memory now differs from the engine's delta base
    const PerfClock::time_point devicesEnd = PerfClock::now();

    // --- Screen: the renderer's derived state (active screen bank, border) ---
    ResyncScreenState();

    _perf.lastRestoreCpuChipsetNs = elapsedNs(restoreStart, memoryStart);
    _perf.lastRestoreMemoryNs = elapsedNs(memoryStart, devicesStart);
    _perf.lastRestoreDevicesNs = elapsedNs(devicesStart, devicesEnd);
    _perf.lastRestoreScreenNs = elapsedNs(devicesEnd, PerfClock::now());
}

void TimeTravelController::ResyncScreenState()
{
    if (!_context || !_context->pScreen)
        return;

    // 1. Re-detect video mode from restored port values. InitRaster()
    //    handles all machine models: standard ZX (p7FFD), ATM (pFF77),
    //    Pentagon AlCo (pEFF7), etc. Without this, a seek to a frame with
    //    a different video mode would render with the wrong geometry.
    _context->pScreen->InitRaster();

    // 2. Sync the active screen bank from p7FFD bit 3 (bank 7 shadow vs
    //    bank 5 normal). Without this the renderer reads pixels from
    //    whichever bank was active when the previous frame ran — typically
    //    garbage after a restore that changed the paging latch.
    const uint8_t p7FFD = _context->emulatorState.p7FFD;
    const SpectrumScreenEnum screen = (p7FFD & 0b0000'1000)
                                        ? SCREEN_SHADOW   // bit 3 set → bank 7
                                        : SCREEN_NORMAL;  // bit 3 clear → bank 5
    _context->pScreen->SetActiveScreen(screen);

    // 3. Sync the border color from pFE bits 0-2.
    const uint8_t borderColor = _context->emulatorState.pFE & 0b0000'0111;
    _context->pScreen->SetBorderColor(borderColor);

    // 4. A checkpoint sits at its frame's start: the beam has drawn nothing of
    //    this frame yet, so the draw cursor restarts at 0. A stale cursor from
    //    the previous position makes the first DrawPeriod see from > to and
    //    skip the start of the frame.
    _context->pScreen->ResetPrevTstate();

    // 5. InitFrame resets the renderer's frame-local counters. No pixel
    //    decode here: a static memory decode is wrong for any frame that
    //    changes the border, attributes or screen bank mid-frame, and
    //    ComposeDisplay renders the real picture for user-facing positions.
    _context->pScreen->InitFrame();
}

// ---------------------------------------------------------------------------
// Silent replay mode (Phase 2 Item 2; parent TDD §8.2 + Appendix C)
// ---------------------------------------------------------------------------

void TimeTravelController::FlushToEngine()
{
    if (!_engine->IsSessionOpen())
        return;
    if (_portJournalRecorded)
    {
        TTDPortRecord r;
        for (; _shadowBusReads < _portReads.Size() && _portReads.Get(_shadowBusReads, r); ++_shadowBusReads)
            _engine->AppendBusRead(r);
        for (; _shadowBusWrites < _portWrites.Size() && _portWrites.Get(_shadowBusWrites, r); ++_shadowBusWrites)
            _engine->AppendBusWrite(r);
    }
    FeedV1Events(*_engine, _inputJournal, _externalEvents, _shadowEvents, UINT64_MAX, nullptr, &_toolEditPayloads,
                 &_shadowFacts);
    _shadowFacts.clear();
    _shadowEvents.facts = 0;
    DrainWritesToEngine();
}

void TimeTravelController::DrainWritesToEngine()
{
    if (!_writeJournal || !_engine->IsSessionOpen())
        return;
    TTDWriteIndex& writes = _engine->Writes();
    for (uint64_t seq = _writeJournal->SeqTail(); seq < _writeJournal->SeqHead(); ++seq)
        writes.Append(_writeJournal->RecordAt(seq));
    // A frame that wrote more than the ring holds lost its oldest records:
    // the journal covers only what came after them
    if (_writeJournal->HasEvictedRecords())
        _journalLostUpTo = std::max(_journalLostUpTo, _writeJournal->OldestGlobalT());
    _writeJournal->Clear();   // the ring holds one frame's writes at most
    writes.SetSegments(JournalSegments());
}

std::optional<TTDExternalEvent> TimeTravelController::FirstBarrierBetween(const TTDTimePoint& from,
                                                                          const TTDTimePoint& to) const
{
    const TTDEvent* ev = _engine->Events().FirstBarrierIn(GlobalT(from), GlobalT(to));
    if (!ev)
        return std::nullopt;
    // As a v1 marker: kind 0x0100 + TTDExternalEventKind, the reason as payload
    TTDExternalEvent out;
    out.time = TimePointAt(ev->machineTime);
    const uint16_t kind = static_cast<uint16_t>(ev->kind);
    out.kind = kind >= 0x0100 && kind < 0x0200 ? static_cast<TTDExternalEventKind>(kind - 0x0100)
                                               : TTDExternalEventKind::Other;
    if (ev->payload)
    {
        const std::vector<uint8_t>& reason = _engine->Payloads().Bytes(ev->payload);
        std::memcpy(out.reason, reason.data(), std::min(reason.size(), sizeof(out.reason) - 1));
    }
    return out;
}

void TimeTravelController::EnterReplayMode()
{
    if (_inReplayMode)
        return;  // Idempotent + nest-safe: no second hold
    // A replay while recording (a resume from the past) reads the current frame's input from the engine
    if (_state == TTDSessionState::Recording)
        FlushToEngine();

    if (!_context)
    {
        MLOGWARNING("TimeTravelController::EnterReplayMode — null _context, cannot engage replay mode");
        return;
    }

    _context->ttdReplayActive = true;
    _inReplayMode = true;
    // Nothing the replayed machine writes reaches a host file (FR-20)
    if (_context->pMediaManager)
        _context->pMediaManager->HoldHostWrites(true);
    SyncMediaReadJournal();

    // Replay runs as fast as the host goes: nothing reaches the host audio callback at all (TDD §8.2: device
    // ticks keep running, only the host boundary is held). Taken after the flag is set, so a resume reconcile
    // never sees the hold without its replay; the user's master mute is not touched
    if (_context->pSoundManager)
        _replayHostHold = SoundManager::HostOutputHold(_context->pSoundManager, SoundManager::HostHoldReason::TtdReplay);

    // The replay observers - the access probe, the frame-cache capture, the
    // dirty marks a mid-frame resume needs - live on the debug memory path.
    // The user's debug mode may have that path switched off (a recording
    // switches it back off on stop), so engage it for the replay only; the
    // feature settings stay untouched.
    if (Core* core = _context->pCore)
    {
        _debugModeBeforeReplay = core->GetZ80()->isDebugMode;
        core->GetZ80()->isDebugMode = true;
        core->SelectMemoryInterface();  // the debug path, contended where the machine is
    }
    if (_memory)
        _memory->UpdateFeatureCache();

    MLOGINFO("TimeTravelController::EnterReplayMode — replay mode engaged (host audio output held)");
}

void TimeTravelController::ExitReplayMode()
{
    if (!_inReplayMode)
        return;  // Idempotent

    if (!_context)
    {
        MLOGWARNING("TimeTravelController::ExitReplayMode — null _context, cannot disengage replay mode");
        return;
    }

    // The hold goes first, then the flag (see EnterReplayMode)
    _replayHostHold.Release();
    _context->ttdReplayActive = false;
    _inReplayMode = false;
    if (_context->pMediaManager)
        _context->pMediaManager->HoldHostWrites(false);
    if (_replayEngine && _replayEngine->MediaReads().GetMode() == TTDMediaJournal::Mode::Play)
        _replayEngine->MediaReads().Stop();
    if (_replayEngine && _replayEngine->BusVectors().GetMode() == TTDPortJournal::Mode::Play)
        _replayEngine->BusVectors().Stop();
    SyncMediaReadJournal();

    if (Core* core = _context->pCore)
    {
        core->GetZ80()->isDebugMode = _debugModeBeforeReplay;
        core->SelectMemoryInterface();
    }
    if (_memory)
        _memory->UpdateFeatureCache();

    MLOGINFO("TimeTravelController::ExitReplayMode — replay mode disengaged (host audio output released)");
}

bool TimeTravelController::IsReplayActive() const
{
    return _context && _context->ttdReplayActive;
}

// ---------------------------------------------------------------------------
// Input journal (Phase 2 Item 3; parent TDD §5 row #1)
// ---------------------------------------------------------------------------

void TimeTravelController::RecordInputEvent(uint8_t key, bool pressed)
{
    // Caller (DebugKeyboardManager::PressKey/ReleaseKey) already gates on
    // IsRecording() and !IsReplayActive(). We don't double-check here.
    //
    // Derive the current TTDTimePoint. frame_counter is the frame index;
    // the intra-frame position is z80.t (the per-frame t-state counter that
    // AdjustFrameCounters resets at each boundary). Note: emulatorState.
    // t_states is only updated at frame boundaries (MainLoop::OnFrameEnd
    // does `t_states += config.frame`), so its modulo is always 0.
    if (!_context)
        return;

    const EmulatorState& st = _context->emulatorState;
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;

    TTDInputEvent ev;
    ev.time.frame    = st.frame_counter;
    ev.time.tInFrame = z80 ? st.TtdTInFrame(z80->t) : 0;
    ev.kind          = TTDInputKind::Key;
    ev.key           = key;
    ev.pressed       = pressed;
    _inputJournal.Record(ev);
}

/// Current TTDTimePoint for an input mutation happening now (see RecordInputEvent)
static TTDTimePoint InputEventTimeNow(EmulatorContext* context)
{
    TTDTimePoint time;
    const EmulatorState& st = context->emulatorState;
    Z80* z80 = context->pCore ? context->pCore->GetZ80() : nullptr;
    time.frame    = st.frame_counter;
    time.tInFrame = z80 ? st.TtdTInFrame(z80->t) : 0;
    return time;
}

void TimeTravelController::RecordMouseMove(int dx, int dy)
{
    if (!_context)
        return;
    TTDInputEvent ev;
    ev.time = InputEventTimeNow(_context);
    ev.kind = TTDInputKind::MouseMove;
    ev.dx   = static_cast<int16_t>(dx);
    ev.dy   = static_cast<int16_t>(dy);
    _inputJournal.Record(ev);
}

void TimeTravelController::RecordMouseButtons(uint8_t activeLowMask)
{
    if (!_context)
        return;
    TTDInputEvent ev;
    ev.time       = InputEventTimeNow(_context);
    ev.kind       = TTDInputKind::MouseButtons;
    ev.buttonMask = activeLowMask;
    _inputJournal.Record(ev);
}

void TimeTravelController::RecordMouseWheel(int steps)
{
    if (!_context)
        return;
    TTDInputEvent ev;
    ev.time       = InputEventTimeNow(_context);
    ev.kind       = TTDInputKind::MouseWheel;
    ev.wheelSteps = static_cast<int8_t>(steps);
    _inputJournal.Record(ev);
}

void TimeTravelController::RecordKeyboardReset()
{
    if (!_context)
        return;
    TTDInputEvent ev;
    ev.time = InputEventTimeNow(_context);
    ev.kind = TTDInputKind::KeyboardReset;
    _inputJournal.Record(ev);
}

void TimeTravelController::RecordMouseCounters(uint8_t x, uint8_t y)
{
    if (!_context)
        return;
    TTDInputEvent ev;
    ev.time = InputEventTimeNow(_context);
    ev.kind = TTDInputKind::MouseCounters;
    ev.dx   = x;
    ev.dy   = y;
    _inputJournal.Record(ev);
}

// ---------------------------------------------------------------------------
// Input ownership (live input vs the recorded journal)
// ---------------------------------------------------------------------------

bool TimeTravelController::OwnsInput() const
{
    if (!_context)
        return false;

    // Seek / reverse-query replay: always journal-driven
    if (_context->ttdReplayActive)
        return true;

    // Seeked into the past and (possibly) running forward through it: the
    // recorded session is still being re-executed until its end
    if (_state != TTDSessionState::Detached || _timeline.empty())
        return false;
    return _context->emulatorState.frame_counter <= _timeline.back().time.frame;
}

void TimeTravelController::SetLiveInputInterceptor(std::function<bool(const TTDInputEvent&)> interceptor)
{
    std::lock_guard<std::mutex> lock(_liveInputInterceptorMutex);
    _liveInputInterceptor = std::move(interceptor);
}

bool TimeTravelController::SubmitLiveInput(const TTDInputEvent& ev)
{
    return SubmitLiveInputImpl(ev, nullptr, nullptr, 0);
}

bool TimeTravelController::SubmitLiveInput(const TTDInputEvent& ev, const TTDNetInput& net, const uint8_t* payload,
                                        uint32_t length)
{
    return SubmitLiveInputImpl(ev, &net, payload, length);
}

bool TimeTravelController::SubmitLiveInputImpl(const TTDInputEvent& ev, const TTDNetInput* net, const uint8_t* payload,
                                            uint32_t length)
{
    if (!_context || OwnsInput())
        return false;

    // A lockstep group takes plain input; network events are not shared
    // across a group and take the normal path
    if (!net)
    {
        std::lock_guard<std::mutex> lock(_liveInputInterceptorMutex);
        if (_liveInputInterceptor && _liveInputInterceptor(ev))
            return true;
    }

    // The machine lives on the emulator loop's thread: while the loop runs,
    // only that thread mutates input state - queue for its next instruction
    // boundary. Applied at once when the caller IS that thread (automation
    // sequences advanced at the frame boundary) or in synchronous mode (loop
    // not running: the caller is the only thread driving the machine).
    const bool loopRunning = _context->pEmulator && _context->pEmulator->IsRunning();
    const bool onLoopThread = _context->pMainLoop && _context->pMainLoop->IsRunThread();
    if (loopRunning && !onLoopThread)
    {
        // Paused and parked: nobody else drives the machine until Resume (which waits for this), so the input is
        // applied - and journaled - now, after whatever was queued before it. The API's "applied before the
        // response" then holds on a paused machine too (pause -> mouse/keyboard -> read state)
        if (_context->pEmulator->RunWhileParked([&]() {
                DrainPendingLiveInput();
                ApplyLiveInput(ev, net, payload, payload ? length : 0);
                UpdateInputWorkFlag();
            }))
            return true;

        {
            std::lock_guard<std::mutex> lock(_pendingInputMutex);
            PendingInput pending;
            pending.ev = ev;
            if (net)
            {
                pending.hasNet = true;
                pending.net = *net;
            }
            if (payload && length)
                pending.payload.assign(payload, payload + length);
            _pendingInput.push_back(std::move(pending));
        }
        _context->SetStepWork(EmulatorContext::kStepWorkTtdInput, true);
        return true;
    }

    ApplyLiveInput(ev, net, payload, payload ? length : 0);
    return true;
}

TimeTravelController::MachineTaskResult TimeTravelController::SubmitMachineTask(std::function<void()> task)
{
    if (!_context || !task || OwnsInput())
        return MachineTaskResult::Refused;

    // The same hand-off as SubmitLiveInput
    const bool loopRunning = _context->pEmulator && _context->pEmulator->IsRunning();
    const bool onLoopThread = _context->pMainLoop && _context->pMainLoop->IsRunThread();
    if (loopRunning && !onLoopThread)
    {
        {
            std::lock_guard<std::mutex> lock(_pendingInputMutex);
            _pendingTasks.push_back(std::move(task));
        }
        _context->SetStepWork(EmulatorContext::kStepWorkTtdInput, true);
        return MachineTaskResult::Queued;
    }

    task();
    return MachineTaskResult::RanNow;
}

void TimeTravelController::ApplyLiveInput(TTDInputEvent ev, const TTDNetInput* net, const uint8_t* payload,
                                       uint32_t length)
{
    // Journal BEFORE applying: the entry's time point is the moment of mutation
    TTDNetInput applied;
    if (net)
    {
        applied = *net;
        applied.payloadLength = payload ? length : 0;
    }
    if (_state == TTDSessionState::Recording)
    {
        ev.time = InputEventTimeNow(_context);
        if (net)
        {
            _inputJournal.Record(ev, applied, payload, applied.payloadLength);
            applied.journalIndex = static_cast<uint32_t>(_inputJournal.NetInputs().size());
            applied.payloadOffset = _inputJournal.NetInputs().back().payloadOffset;
        }
        else
        {
            _inputJournal.Record(ev);
        }
    }
    ApplyInputEvent(ev, InputDevicesOf(_context), net ? &applied : nullptr, payload);
}

void TimeTravelController::ServiceInput()
{
    if (!_context)
        return;

    // 0. Running forward through a paused recording: at the paused end, the
    // recording goes on (D8). Not inside a seek's own replay
    if (_recordingPaused && _state == TTDSessionState::Detached && !_inReplayMode &&
        !(CurrentPosition() < _pausedEnd))
    {
        ContinueRecordingAt(_pausedEnd);
        return;
    }

    // 1. Journal playback: every event due at or before the current machine time
    if (_inputPlaybackArmed && _replayEngine)
    {
        // Phase 3 A/B: the engine's event log (input kinds; network bytes from its payloads)
        const TTDTimePoint now = InputEventTimeNow(_context);
        TTDMachineTime start = 0;
        const TTDEventLog& log = _replayEngine->Events();
        if (_replayEngine->Frames().Start(now.frame, start))
            while (_engineEventCursor < log.Count() && log.At(_engineEventCursor).machineTime <= start + now.tInFrame)
            {
                const TTDEvent& ev = log.At(_engineEventCursor++);
                if (ev.kind == TTDEventKind::DebuggerEdit && TTDEventLog::RoleOf(ev) == TTDEventRole::Input)
                {
                    ApplyToolEdit(_replayEngine->Payloads().Bytes(ev.payload));
                    continue;
                }
                if (!IsInputKind(ev.kind))
                    continue;
                TTDInputEvent in;
                TTDEventLog::ToInput(ev, in);
                TTDNetInput net;
                const bool isNet = in.kind == TTDInputKind::NetEvent;
                if (isNet)
                {
                    TTDEventLog::UnpackNet(ev, net);
                    net.payloadLength = static_cast<uint32_t>(_replayEngine->Payloads().Bytes(ev.payload).size());
                }
                ApplyInputEvent(in, InputDevicesOf(_context), isNet ? &net : nullptr,
                                isNet && net.payloadLength ? _replayEngine->Payloads().Bytes(ev.payload).data() : nullptr);
            }
        if (_engineEventCursor >= log.Count())
            _inputPlaybackArmed = false;
    }
    else if (_inputPlaybackArmed)
    {
        const TTDTimePoint now = InputEventTimeNow(_context);
        const auto& events = _inputJournal.Events();
        while (_inputCursor < events.size() && !(now < events[_inputCursor].time))
        {
            const TTDInputEvent& ev = events[_inputCursor];
            const TTDNetInput* net = _inputJournal.NetOf(ev);
            ApplyInputEvent(ev, InputDevicesOf(_context), net, net ? _inputJournal.PayloadOf(*net) : nullptr);
            ++_inputCursor;
        }
        if (_inputCursor >= events.size())
            _inputPlaybackArmed = false;
    }

    // 2. Live input queued by other threads: applied (and journaled) here, or
    //    dropped when the journal took over input while it waited
    DrainPendingLiveInput();

    // 3. Machine tasks queued by other threads (SubmitMachineTask), dropped
    //    like live input when the journal took over while they waited
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(_pendingInputMutex);
        tasks.swap(_pendingTasks);
    }
    if (!tasks.empty() && !OwnsInput())
    {
        for (auto& task : tasks)
            task();
    }

    UpdateInputWorkFlag();
}

void TimeTravelController::DrainPendingLiveInput()
{
    std::vector<PendingInput> pending;
    {
        std::lock_guard<std::mutex> lock(_pendingInputMutex);
        pending.swap(_pendingInput);
    }
    if (!pending.empty() && !OwnsInput())
    {
        for (const PendingInput& p : pending)
            ApplyLiveInput(p.ev, p.hasNet ? &p.net : nullptr, p.payload.empty() ? nullptr : p.payload.data(),
                           static_cast<uint32_t>(p.payload.size()));
    }
}

void TimeTravelController::UpdateInputWorkFlag()
{
    if (!_context)
        return;

    bool pending;
    {
        std::lock_guard<std::mutex> lock(_pendingInputMutex);
        pending = !_pendingInput.empty() || !_pendingTasks.empty();
    }
    // A paused recording is watched for the moment execution reaches where it paused
    const bool watchPausedEnd = _recordingPaused && _state == TTDSessionState::Detached;
    _context->SetStepWork(EmulatorContext::kStepWorkTtdInput, _inputPlaybackArmed || pending || watchPausedEnd);
}

void TimeTravelController::ArmInputPlayback()
{
    if (!_context)
        return;

    // Events stamped with exactly the restored time were applied after the
    // checkpoint was captured, before the next instruction: the cursor starts
    // at them and the first step applies them (Z80::StepInstruction)
    _inputCursor = _inputJournal.FirstIndexAtOrAfter(InputEventTimeNow(_context));
    _inputPlaybackArmed = _inputCursor < _inputJournal.Size();
    if (_replayEngine)
    {
        const TTDTimePoint now = InputEventTimeNow(_context);
        TTDMachineTime start = 0;
        _replayEngine->Frames().Start(now.frame, start);
        _engineEventCursor = _replayEngine->Events().CursorAt(start + now.tInFrame);
        _inputPlaybackArmed = _engineEventCursor < _replayEngine->Events().Count();
    }
    UpdateInputWorkFlag();
}

void TimeTravelController::DisarmInputPlayback()
{
    _inputPlaybackArmed = false;
    _inputCursor = 0;
    UpdateInputWorkFlag();
}

void TimeTravelController::OnMachineReset()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    DisarmInputPlayback();
    if (_portReads.GetMode() == TTDPortJournal::Mode::Play || _portWrites.GetMode() == TTDPortJournal::Mode::Play)
    {
        // Off the recorded history: I/O is live again
        _portReads.Stop();
        _portWrites.Stop();
        SyncPortJournalHook();
    }
    if (_state == TTDSessionState::Detached)
    {
        // The machine no longer sits on the recorded timeline (which is
        // kept): back to Idle-with-history, live input allowed
        SetState(TTDSessionState::Idle);
    }
}

void TimeTravelController::RestoreCheckpointForReplay(const TTDCheckpoint& cp)
{
    RestoreCheckpoint(cp);
    ArmInputPlayback();

    // The CPU replays the recorded IN results from the engine's bus journals,
    // from its checkpoint's cursors; the live devices still answer, and a
    // differing answer is counted, not used
    const int64_t index = _replayEngine->CheckpointIndexOf({0, cp.time.frame, 0});
    _portReads.Stop();
    _portWrites.Stop();
    if (index >= 0 && _portJournalRecorded)
    {
        const TTDEngineCheckpoint* ecp = _replayEngine->Checkpoint(size_t(index));
        _replayEngine->PlayBus(ecp->busReadCursor, ecp->busWriteCursor, ecp->busVectorCursor);
        _replayEngine->MediaReads().StartPlayback(ecp->mediaReadCursor);
        _context->ttdPortReads = _replayEngine->BusReadsForPlayback();
        _context->ttdPortWrites = _replayEngine->BusWritesForPlayback();
    }
    else
        SyncPortJournalHook();
}

const char* TimeTravelController::PortJournalUnsupportedReason() const
{
    if (!_context)
        return "no emulator context";

    // The first version isolates machines whose outside world reaches the CPU
    // through IN alone. DMA writes memory without the CPU reading anything,
    // so these configurations still replay against the live devices
    // (ttd-port-read-journal.md §2)
    switch (_context->config.mem_model)
    {
        case MM_NEXT:
            return "ZX Next: its DMA moves data into RAM without IN (not isolated by the first version)";
        default:
            break;
    }
    if (_context->pSoundManager)
    {
        const GeneralSoundCard* gs = _context->pSoundManager->getGeneralSound();
        // Only where the card's ZX-bus carries the host's memory cycles: through the Sprinter's ISA ZX-bus adapter
        // (I/O cycles only) the ZX-DMA never installs, and every host access to the card is an ISA cycle of the
        // machine's own deterministic state, replayed from the card's blob
        if (gs && gs->implementation() == GSCardImplementation::NGS &&
            (!_context->pPortDecoder || _context->pPortDecoder->ZxBusMemoryCycles()))
            return "NeoGS: its ZX-DMA serves host memory reads without IN (not isolated by the first version)";
    }
    // A machine that owns its INT logic (IInterruptSource) may put the IM2
    // vector on the bus from a device - a read the journals do not record
    // (TTD v2 FR-21). The classic machines leave it to the floating bus
    // A machine whose vector and stepped engines follow recorded state only says so (PortDecoder::
    // TtdEnginesSealed: the Sprinter); for it the two checks below do not apply
    const bool enginesSealed = _context->pPortDecoder && _context->pPortDecoder->TtdEnginesSealed();
    if (const Z80* z80 = (_context->pCore && !enginesSealed) ? _context->pCore->GetZ80() : nullptr)
    {
        if (z80->GetInterruptSource())
            return "the machine's interrupt source supplies the IM2 vector, which the first version does not record";
        // A model engine stepped with the CPU (IMachineStepHook: TSConf's DMA
        // and TSU) changes what the program sees without an IN
        if (z80->GetMachineStepHook())
            return "a machine engine stepped with the CPU (DMA) changes memory without IN (not isolated by the "
                   "first version)";
    }
    return nullptr;
}

void TimeTravelController::DropPortJournal(const char* reason)
{
    if (!_portJournalValid && !_portJournalRecorded)
        return;
    _portJournalRecorded = false;
    MLOGWARNING("TimeTravelController — port-read journal dropped: %s; replay reads the live devices again", reason);
    _portReads.Clear();
    _portWrites.Clear();
    _portJournalValid = false;
    _portJournalOffReason = reason;
    for (TTDCheckpoint& cp : _timeline)
    {
        cp.portReadCursor = 0;
        cp.portWriteCursor = 0;
    }
    SyncPortJournalHook();
}

void TimeTravelController::SyncPortJournalHook()
{
    if (!_context)
        return;
    _context->ttdPortReads = _portReads.GetMode() == TTDPortJournal::Mode::Off ? nullptr : &_portReads;
    _context->ttdPortWrites = _portWrites.GetMode() == TTDPortJournal::Mode::Off ? nullptr : &_portWrites;
}

TTDPortSearchResult TimeTravelController::SearchPortEvents(const TTDPortQuery& q) const
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    if (!_portJournalValid)
    {
        TTDPortSearchResult result;
        result.error = "the session has no port journal" +
                       (_portJournalOffReason.empty() ? std::string() : " (" + _portJournalOffReason + ")");
        return result;
    }
    // A recording that is running appends to the journals from the emulation
    // thread; paused (or replaying) they only grow on that thread's next step
    Emulator* emu = _context ? _context->pEmulator : nullptr;
    if (_state == TTDSessionState::Recording && emu && emu->IsRunning() && !emu->IsPaused())
    {
        TTDPortSearchResult result;
        result.error = "the recording is running and still writing the journals: pause or stop it first";
        return result;
    }
    // Recording (paused): the live journals hold the current frame too;
    // otherwise the engine holds the session's bus journals (a loaded one too)
    if (_state == TTDSessionState::Recording)
        return ttd::SearchPortEvents(_portReads, _portWrites, q);
    return ttd::SearchPortEvents(_engine->BusReads(), _engine->BusWrites(), q);
}

// ---------------------------------------------------------------------------
// External-event markers (Phase 2 Item 6; parent TDD §5.1)
// ---------------------------------------------------------------------------

// A tool edit's bytes, as records: kind u8, id u16, index u32, length u32, bytes.
// Kinds: 1 a machine RAM page (index = page), 2 a device-memory piece (id =
// TTDRegionId, index = piece), 3 a device's whole state (id = v1 id), 4 the
// CPU (TTDCpuState), 5 the chipset latches (TTDChipsetState; its counters are
// the machine time and stay as they are, the paging is decoded from it), 6 the
// CPU time the edit spent (a fast loader's trap: u32, Z80::tt units), 7 the
// debugger-forced RAM windows 1/2 (TTDBankOverrides; applied after the banks)
namespace
{
enum : uint8_t
{
    kEditRamPage = 1,
    kEditRegionPiece = 2,
    kEditDeviceState = 3,
    kEditCpu = 4,
    kEditChipset = 5,
    kEditClock = 6,
    kEditBanks = 7,
};
void PutEditRecord(std::vector<uint8_t>& out, uint8_t kind, uint16_t id, uint32_t index, const uint8_t* bytes,
                   uint32_t length)
{
    const size_t at = out.size();
    out.resize(at + 11 + length);
    out[at] = kind;
    std::memcpy(out.data() + at + 1, &id, 2);
    std::memcpy(out.data() + at + 3, &index, 4);
    std::memcpy(out.data() + at + 7, &length, 4);
    std::memcpy(out.data() + at + 11, bytes, length);
}
}  // namespace

void TimeTravelController::BeginToolEdit()
{
    // A paused recording (D8): an edit at the paused point is the next thing
    // that happens in it, so the recording goes on and records the edit. One
    // before it would make the machine leave the recorded history while
    // running into the paused point continues it: the recording ends where
    // it paused instead (an edit in the past starts a branch with Step 2b)
    if (_recordingPaused && _state == TTDSessionState::Detached)
    {
        if (CurrentPosition() == _pausedEnd)
            ContinueRecordingAt(_pausedEnd);
        else
        {
            MLOGINFO("TimeTravelController: an edit before the paused end ends the paused recording");
            StopRecording();
        }
    }
    _toolEditBefore.clear();
    _toolEditOpen = _state == TTDSessionState::Recording;
    if (!_toolEditOpen)
        return;
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    _toolEditAt = {_context->emulatorState.frame_counter, TInFrameNow()};
    _toolEditTt = z80 ? z80->tt : 0;
    for (const auto& [id, device] : _peripherals.Devices())
        if (device && device->TTDStateSize() != 0)
            device->TTDSaveStateTo(_toolEditBefore[id]);
}

void TimeTravelController::EndToolEdit(const char* source)
{
    // No source: the edit did not happen (a fast loader's trap declined)
    if (!_toolEditOpen || _state != TTDSessionState::Recording || !source)
    {
        _toolEditOpen = false;
        _toolEditBefore.clear();
        return;
    }
    _toolEditOpen = false;

    // Everything written since the last checkpoint, whole: the RAM pages and
    // device-memory pieces marked dirty (the edit's among them) and every
    // device state the edit changed. A replay reaches the edit with the same
    // contents as when recording, so writing these is the state after it
    std::vector<uint8_t> payload;
    if (_dirtyTracker && _memory)
        for (uint16_t page = 0; page < _modelRamPages; ++page)
            if (_dirtyTracker->IsDirty(page))
                PutEditRecord(payload, kEditRamPage, 0, page, _memory->RAMPageAddress(page), 0x4000);
    std::vector<TTDDeviceRegion> regions;
    for (ITTDRegionSource* src : _peripherals.RegionSources())
        src->TTDRegions(regions);
    for (const TTDDeviceRegion& r : regions)
    {
        if (!r.desc.memory)
            continue;
        for (uint32_t p = 0; p < r.desc.pieces; ++p)
        {
            if (r.tracker && !r.compareEachCapture && !r.tracker->IsDirty(p))
                continue;
            const size_t offset = size_t(p) * kTTDPieceSize;
            const uint32_t length = static_cast<uint32_t>(std::min<size_t>(kTTDPieceSize, r.desc.bytes - offset));
            PutEditRecord(payload, kEditRegionPiece, static_cast<uint16_t>(r.desc.id), p, r.desc.memory + offset, length);
        }
    }
    std::vector<uint8_t> after;
    for (const auto& [id, device] : _peripherals.Devices())
    {
        if (!device || device->TTDStateSize() == 0)
            continue;
        device->TTDSaveStateTo(after);
        const auto it = _toolEditBefore.find(id);
        if (it == _toolEditBefore.end() || it->second != after)
            PutEditRecord(payload, kEditDeviceState, id, 0, after.data(), static_cast<uint32_t>(after.size()));
    }
    _toolEditBefore.clear();
    // The CPU and the chipset latches as the edit left them (a debugger sets registers and paging)
    if (Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr)
    {
        const TTDCpuState cpu = CaptureCpuState(*static_cast<const Z80State*>(z80));
        PutEditRecord(payload, kEditCpu, 0, 0, reinterpret_cast<const uint8_t*>(&cpu), sizeof(cpu));
        const TTDChipsetState chipset =
            CaptureChipsetState(_context->emulatorState, static_cast<uint32_t>(z80->t));
        PutEditRecord(payload, kEditChipset, 0, 0, reinterpret_cast<const uint8_t*>(&chipset), sizeof(chipset));
        TTDBankOverrides banks;
        CaptureBankOverrides(banks);
        PutEditRecord(payload, kEditBanks, 0, 0, reinterpret_cast<const uint8_t*>(&banks), sizeof(banks));
        // A trap runs its effect in no time steps: the replay advances the clock as it did, last
        if (const uint32_t spent = z80->tt - _toolEditTt)
            PutEditRecord(payload, kEditClock, 0, 0, reinterpret_cast<const uint8_t*>(&spent), sizeof(spent));
    }

    // The event sits where the edit began: a replay applies it before the
    // instruction that starts there, as the trap stood in for it
    const size_t before = _externalEvents.Size();
    RecordExternalEventAt(TTDExternalEventKind::DebuggerEdit, source, _toolEditAt);
    if (_externalEvents.Size() > before)
        _toolEditPayloads[_externalEvents.Size() - 1] = std::move(payload);
}

void TimeTravelController::ApplyToolEdit(const std::vector<uint8_t>& payload)
{
    std::vector<TTDRegionDesc> regions = LiveRegions();
    TTDBankOverrides banks;
    bool haveBanks = false;
    size_t at = 0;
    while (at + 11 <= payload.size())
    {
        const uint8_t kind = payload[at];
        uint16_t id = 0;
        uint32_t index = 0, length = 0;
        std::memcpy(&id, payload.data() + at + 1, 2);
        std::memcpy(&index, payload.data() + at + 3, 4);
        std::memcpy(&length, payload.data() + at + 7, 4);
        const uint8_t* bytes = payload.data() + at + 11;
        at += 11 + length;
        if (at > payload.size())
            break;
        if (kind == kEditRamPage && _memory && index < _modelRamPages && length == 0x4000)
            std::memcpy(_memory->RAMPageAddress(static_cast<uint16_t>(index)), bytes, length);
        else if (kind == kEditRegionPiece)
        {
            for (const TTDRegionDesc& r : regions)
                if (static_cast<uint16_t>(r.id) == id && r.memory && size_t(index) * kTTDPieceSize + length <= r.bytes)
                    std::memcpy(r.memory + size_t(index) * kTTDPieceSize, bytes, length);
        }
        else if (kind == kEditDeviceState)
        {
            const auto it = _peripherals.Devices().find(static_cast<uint8_t>(id));
            if (it != _peripherals.Devices().end() && it->second &&
                (it->second->TTDStateSize() == length || it->second->TTDVariableSize()))
                it->second->TTDLoadState(bytes);
        }
        else if (kind == kEditCpu && length == sizeof(TTDCpuState) && _context && _context->pCore)
        {
            TTDCpuState cpu;
            std::memcpy(&cpu, bytes, sizeof(cpu));
            RestoreCpuState(cpu, static_cast<Z80State*>(_context->pCore->GetZ80()));
        }
        else if (kind == kEditChipset && length == sizeof(TTDChipsetState) && _context)
        {
            TTDChipsetState chipset;
            std::memcpy(&chipset, bytes, sizeof(chipset));
            EmulatorState& st = _context->emulatorState;
            chipset.t_states = st.t_states;           // the machine time is not part of an edit
            chipset.frame_counter = st.frame_counter;
            RestoreChipsetState(chipset, &st);
        }
        else if (kind == kEditBanks && length == sizeof(TTDBankOverrides))
        {
            std::memcpy(&banks, bytes, sizeof(banks));
            haveBanks = true;
        }
        else if (kind == kEditClock && length == sizeof(uint32_t) && _context && _context->pCore)
        {
            uint32_t spent = 0;
            std::memcpy(&spent, bytes, sizeof(spent));
            _context->pCore->GetZ80()->tt += spent;
        }
    }
    if (_memory)
    {
        _memory->UpdateZ80Banks();
        if (haveBanks)
            ApplyBankOverrides(banks);
    }
}

void TimeTravelController::RecordExternalEvent(TTDExternalEventKind kind, const char* reason)
{
    if (!_context)
        return;
    RecordExternalEventAt(kind, reason, {_context->emulatorState.frame_counter, TInFrameNow()});
}

void TimeTravelController::RecordExternalEventAt(TTDExternalEventKind kind, const char* reason, const TTDTimePoint& at)
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    if (!_context)
        return;

    // Same defensive guard as RecordInputEvent: callers (Tape, BetaDisk,
    // debugger edit paths) are expected to check IsRecording() first, but a
    // stray call when not recording is a no-op rather than a journal
    // corruption.
    if (_state != TTDSessionState::Recording)
        return;

    TTDExternalEvent ev;
    ev.time = at;
    ev.kind = kind;

    // Truncate-and-copy the reason string into the inline buffer. strncpy
    // returns `dest` and zero-pads the remainder when src is shorter than
    // the count; the explicit NUL at the last byte guards against the
    // `src longer than count` case (no NUL terminator written).
    if (reason)
    {
        std::strncpy(ev.reason, reason, sizeof(ev.reason) - 1);
        ev.reason[sizeof(ev.reason) - 1] = '\0';
    }
    else
    {
        ev.reason[0] = '\0';
    }

    _externalEvents.Record(ev);

    MLOGINFO("TimeTravelController::RecordExternalEvent — recorded marker at "
             "(frame=%llu,tInFrame=%u) kind=%s reason='%.63s'",
             static_cast<unsigned long long>(ev.time.frame),
             static_cast<unsigned>(ev.time.tInFrame),
             TTDExternalEventKindToString(kind),
             ev.reason);
}

// ---------------------------------------------------------------------------
// Seek engine (Phase 2 Item 4; parent TTD §8.1)
// ---------------------------------------------------------------------------

TTDTimePoint TimeTravelController::CurrentPosition() const
{
    TTDTimePoint pos;
    if (!_context)
        return pos;

    const EmulatorState& st = _context->emulatorState;
    pos.frame    = st.frame_counter;
    // Intra-frame position comes from the Z80 accumulator, NOT
    // emulatorState.t_states. t_states is only updated at frame boundaries
    // (MainLoop::OnFrameEnd does `t_states += config.frame`), so its modulo
    // is always 0. z80.t is the per-frame counter that AdjustFrameCounters
    // resets at each boundary.
    pos.tInFrame = TInFrameNow();
    return pos;
}

uint32_t TimeTravelController::TInFrameNow() const
{
    if (!_context)
        return 0;
    const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    return z80 ? _context->emulatorState.TtdTInFrame(z80->t) : 0;
}

uint32_t TimeTravelController::FrameSpan() const
{
    if (!_context)
        return 69888;
    const uint8_t units = _context->emulatorState.ttd_clock_units;
    return _context->config.frame * (units ? units : 1);
}

uint64_t TimeTravelController::GlobalT(const TTDTimePoint& at) const
{
    // The engine's frame table (Phase 5, C3): a frame starts where the one
    // before it ended, whatever its length. Frames after the table continue
    // with the current span, frames before it count back from its first
    const TTDFrameTable& frames = _engine->Frames();
    TTDMachineTime start = 0;
    if (!_engine->IsSessionOpen() || frames.Empty())
        return at.frame * FrameSpan() + at.tInFrame;
    if (frames.Start(at.frame, start))
        return start + at.tInFrame;
    const uint64_t span = FrameSpan();
    if (at.frame > frames.LastFrame())
    {
        frames.Start(frames.LastFrame(), start);
        return start + (at.frame - frames.LastFrame()) * span + at.tInFrame;
    }
    frames.Start(frames.FirstFrame(), start);
    const uint64_t back = (frames.FirstFrame() - at.frame) * span;
    return (start > back ? start - back : 0) + at.tInFrame;
}

TTDTimePoint TimeTravelController::TimePointAt(uint64_t globalT) const
{
    const uint64_t span = FrameSpan();
    const TTDFrameTable& frames = _engine->Frames();
    TTDMachineTime first = 0;
    if (!_engine->IsSessionOpen() || frames.Empty() || !frames.Start(frames.FirstFrame(), first))
        return TTDTimePoint{globalT / span, static_cast<uint32_t>(globalT % span)};
    if (globalT < first)
    {
        // Before the held history: frames of the current span counted back from it
        const uint64_t back = (first - globalT + span - 1) / span;
        if (back > frames.FirstFrame() || back * span > first)
            return TTDTimePoint{globalT / span, static_cast<uint32_t>(globalT % span)};
        return TTDTimePoint{frames.FirstFrame() - back, static_cast<uint32_t>(globalT - (first - back * span))};
    }
    TTDPosition p;
    _engine->PositionOf(globalT, p);
    // Past the last recorded frame's span: the frames after it
    if (p.frame == frames.LastFrame() && p.tInFrame >= span)
        return TTDTimePoint{p.frame + p.tInFrame / span, static_cast<uint32_t>(p.tInFrame % span)};
    return TTDTimePoint{p.frame, static_cast<uint32_t>(p.tInFrame)};
}

void TimeTravelController::RunToTInFrame(uint32_t targetTInFrame)
{
    if (!_context || !_context->pEmulator)
        return;
    const uint64_t frame = _context->emulatorState.frame_counter;
    for (uint32_t now = TInFrameNow(); _context->emulatorState.frame_counter == frame && now < targetTInFrame;)
    {
        const uint32_t perT = _context->emulatorState.TtdUnitsPerTState();
        _context->pEmulator->RunTStates((targetTInFrame - now + perT - 1) / perT, /*skipBreakpoints=*/true);
        const uint32_t next = TInFrameNow();
        if (_context->emulatorState.frame_counter == frame && next <= now)
            break;  // no progress - never spin
        now = next;
    }
}

TTDTimePoint TimeTravelController::FrameEndPosition(uint64_t frame) const
{
    const TTDTimePoint next{frame + 1, 0};
    if (_timeline.empty())
        return next;
    const TTDTimePoint end = _state == TTDSessionState::Recording ? CurrentPosition() : SessionEndPosition();
    return end < next && end.frame >= frame ? end : next;
}

uint32_t TimeTravelController::FrameLength(uint64_t frame) const
{
    if (_timeline.empty() || frame < _timeline.front().time.frame)
        return FrameSpan();
    return static_cast<uint32_t>(GlobalT({frame + 1, 0}) - GlobalT({frame, 0}));
}

TTDTimePoint TimeTravelController::SessionEndPosition() const
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    if (_timeline.empty())
        return TTDTimePoint{};
    // A paused recording reaches to where it paused, inside its last frame;
    // a stopped one to where it stopped
    if (_recordingPaused)
        return _pausedEnd;
    if (_state != TTDSessionState::Recording && _stoppedEndValid && _timeline.back().time < _stoppedEnd &&
        _stoppedEnd.frame == _timeline.back().time.frame)
        return _stoppedEnd;
    return _timeline.back().time;
}

void TimeTravelController::PauseRecordingForBrowsing()
{
    if (_state != TTDSessionState::Recording)
        return;
    const TTDTimePoint here = CurrentPosition();
    StopRecording();   // everything up to here is in the engine; the machine stays parked
    _recordingPaused = true;
    _pausedEnd = here;
    // The machine is in the history now, at its end: browsing moves it from
    // here, and running from here goes on recording (ServiceInput)
    SetState(TTDSessionState::Detached);
    UpdateInputWorkFlag();
    MLOGINFO("TimeTravelController - recording paused at (frame=%llu, tInFrame=%u) for browsing",
             static_cast<unsigned long long>(here.frame), static_cast<unsigned>(here.tInFrame));
}

bool TimeTravelController::SeekTo(const TTDTimePoint& target, TTDSeekResult* outResult)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // Each new Detached window starts with a clean auto-pause signal.
    // The flag is set by OnFrameBoundary when execution runs past
    // SessionEndPosition(); clearing here means callers can poll
    // ConsumeAutoPauseRequest() after resuming from this seek and get a
    // meaningful result.
    _autoPauseRequested.store(false, std::memory_order_release);

    // ------------------------------------------------------------------
    // Public SeekTo guards against Recording state — scrubbing during
    // recording would trash live emulator state (RestoreCheckpoint
    // overwrites it) and corrupt the timeline's sorted invariant (the
    // next OnFrameBoundary would capture at the restored frame, potentially
    // before existing checkpoints). Callers MUST StopRecording first.
    //
    // ResumeRecordingFrom legitimately needs to seek during Recording —
    // it uses SeekToInternal directly because it owns the timeline
    // truncation that keeps the invariant intact.
    // ------------------------------------------------------------------
    // Browsing while recording pauses the recording (D8): resumed where it paused, it goes on
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();

    TTDSeekResult localResult;
    TTDSeekResult& result = outResult ? *outResult : localResult;
    const bool ok = SeekToInternal(target, &result);

    // A marker halt still moved the machine, so it is shown too. Every
    // position shows what the beam drew up to it, as a live machine has it
    // there: at a frame's start that is the previous frame's final picture.
    // "Frame N" asks for frame N's end (D13), which shows frame N's picture
    // with the machine state that goes with it
    if (ok || result.haltReason == TTDSeekHaltReason::ExternalEvent)
        PresentPosition(false);

    return ok;
}

// ---------------------------------------------------------------------------
// Agent bookmarks (TD-4)
// ---------------------------------------------------------------------------

bool TimeTravelController::AddBookmark(const TTDTimePoint& time, const std::string& label,
                                    std::string* err)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // A bookmark into empty history dangles immediately — there is no
    // checkpoint to return to. Refuse at creation instead of at seek time.
    if (_timeline.empty())
    {
        if (err)
            *err = "no recorded history to bookmark (start recording first)";
        MLOGWARNING("TimeTravelController::AddBookmark — rejected: timeline is empty");
        return false;
    }

    // Same principle for a position past the session end: the bookmark can
    // never be reached, so it must never be created.
    // While recording the present is in reach (a seek there pauses the recording at it)
    const TTDTimePoint end = _state == TTDSessionState::Recording ? CurrentPosition() : SessionEndPosition();
    if (end < time)
    {
        if (err)
            *err = "bookmark position (frame=" + std::to_string(time.frame) +
                   ", tInFrame=" + std::to_string(time.tInFrame) +
                   ") is beyond the session end (frame=" + std::to_string(end.frame) + ")";
        MLOGWARNING("TimeTravelController::AddBookmark — rejected: frame %llu beyond session end %llu",
                    static_cast<unsigned long long>(time.frame),
                    static_cast<unsigned long long>(end.frame));
        return false;
    }

    TTDBookmark bookmark;
    bookmark.time  = time;
    bookmark.label = label;
    return _bookmarks.Add(bookmark, err);
}

std::vector<TTDBookmark> TimeTravelController::GetBookmarks() const
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    return _bookmarks.Snapshot();
}

bool TimeTravelController::FindBookmark(const std::string& label, TTDBookmark& out) const
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    return _bookmarks.Find(label, out);
}

bool TimeTravelController::RemoveBookmark(const std::string& label)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    return _bookmarks.Remove(label);
}

bool TimeTravelController::SeekToBookmark(const std::string& label, TTDSeekResult* outResult,
                                       std::string* err)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDBookmark bookmark;
    if (!_bookmarks.Find(label, bookmark))
    {
        if (outResult)
        {
            outResult->reached        = false;
            outResult->arrivedAt      = TTDTimePoint{};
            outResult->haltReason     = TTDSeekHaltReason::OutOfRange;
            outResult->blockingMarker = TTDExternalEvent{};
        }
        if (err)
            *err = "unknown bookmark '" + label + "'";
        MLOGWARNING("TimeTravelController::SeekToBookmark — unknown label '%s'", label.c_str());
        return false;
    }

    // Nothing bookmark-specific from here on — a bookmark seek IS a seek.
    // halt_reason semantics are exactly the plain SeekTo's, so a real barrier
    // between the restore checkpoint and the target still surfaces as
    // "external_event" and the bookmark itself can never be one.
    return SeekTo(bookmark.time, outResult);
}

void TimeTravelController::PublishSeekedFrame()
{
    // Deliberately here and not in RestoreCheckpoint. Restores also happen deep
    // inside FindLastAccess and ReverseContinue, which walk hundreds of
    // checkpoints looking for a match; publishing each one would add a full
    // framebuffer copy per restore and flicker the display through frames the
    // user never asked to see. This is the user-initiated path — an explicit
    // scrub, a frame step — and the only one whose result should reach the
    // screen. Normal emulation does not come through here at all.
    if (!_context)
        return;

    // 1. Flush the video delay line and publish the restored frame.
    //
    // The UI reads through Screen::CopyPresentedFramebuffer, which serves the
    // present QUEUE rather than the live framebuffer, so a repaint alone leaves
    // the pre-seek image on screen with the correct one sitting in memory
    // behind it. On a paused machine no later frame arrives to push it through.
    // The queue refills by itself once playback resumes.
    if (_context->pScreen)
        _context->pScreen->FlushAndPresentFramebuffer();

    // 2. Tell observers a new final frame exists. NC_VIDEO_FRAME_REFRESH is
    // otherwise posted only by MainLoop::OnFrameEnd, which does not run while
    // paused.
    try
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        const std::string emulatorId = _context->pEmulator ? _context->pEmulator->GetId() : "";
        messageCenter.Post(NC_VIDEO_FRAME_REFRESH,
                           new EmulatorFramePayload(emulatorId,
                                                    _context->emulatorState.frame_counter));
    }
    catch (const std::exception& e)
    {
        // A refresh that fails to be announced is cosmetic; it must not take
        // the seek down with it.
        MLOGERROR("TimeTravelController::PublishSeekedFrame — MessageCenter post failed: %s",
                  e.what());
    }
}



bool TimeTravelController::SeekToInternal(const TTDTimePoint& target, TTDSeekResult* outResult)
{
    _perf.lastReplayNs = 0;
    _perf.lastPresentNs = 0;

    // ------------------------------------------------------------------
    // Default the out-result to a failure state. Every return path below
    // either leaves this default (false / OutOfRange) or overwrites it
    // before returning true.
    // ------------------------------------------------------------------
    if (outResult)
    {
        outResult->reached     = false;
        outResult->arrivedAt   = TTDTimePoint{};
        outResult->haltReason  = TTDSeekHaltReason::OutOfRange;
        outResult->blockingMarker = TTDExternalEvent{};
    }

    // ------------------------------------------------------------------
    // Validate preconditions.
    // ------------------------------------------------------------------
    if (!_context)
    {
        MLOGWARNING("TimeTravelController::SeekToInternal — null _context");
        return false;
    }

    // Idle-with-history is allowed (typical after StopRecording); Detached
    // is the other valid state. Recording is also allowed for internal
    // callers (ResumeRecordingFrom) — they manage the invariant themselves.
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::SeekToInternal — timeline is empty "
                    "(state=%s)",
                    TTDSessionStateToString(_state.load()));
        return false;
    }

    const TTDTimePoint sessionEnd = _timeline.back().time;
    // Reject only if the target frame is beyond the session. Intra-frame
    // replay at the session-end frame IS allowed: checkpoints sit at frame
    // boundaries (tInFrame == 0), so {lastFrame, T>0} is a valid target
    // that ReplayWithinFrame handles by running T t-states forward from
    // the lastFrame checkpoint. The old `sessionEnd < target` comparison
    // wrongly rejected this case because {lastFrame, 0} < {lastFrame, T}
    // for any T > 0.
    if (target.frame > sessionEnd.frame)
    {
        MLOGWARNING("TimeTravelController::SeekTo — target frame %llu "
                    "is beyond session end frame %llu",
                    static_cast<unsigned long long>(target.frame),
                    static_cast<unsigned long long>(sessionEnd.frame));
        // outResult already defaults to OutOfRange / reached=false.
        return false;
    }

    // ------------------------------------------------------------------
    // Step 1: binary search for the latest checkpoint with cp.time <= target.
    //
    // Timeline is sorted ascending by `time`. We want the rightmost cp whose
    // time is <= target. std::upper_bound finds the first cp > target; the
    // one we want is the iterator before it. Reverse-iterator trick gives us
    // the rightmost cp <= target directly when combined with a less-than
    // comparator on (cp.time < target) — but for clarity we use forward
    // iteration and walk back from upper_bound.
    // ------------------------------------------------------------------
    const int64_t atOrBefore = TimelineIndexAtOrBefore(target);
    if (atOrBefore < 0)
    {
        // Before the earliest kept position (a history limit released the
        // older frames, or the target precedes the recording): the machine
        // stays where it is, and the answer names where history starts (D12)
        if (outResult)
        {
            outResult->arrivedAt = CurrentPosition();
            outResult->earliest = _timeline.front().time;
            outResult->beforeEarliest = true;
        }
        MLOGWARNING("TimeTravelController::SeekTo — target frame %llu is before the earliest kept frame %llu",
                    static_cast<unsigned long long>(target.frame),
                    static_cast<unsigned long long>(_timeline.front().time.frame));
        return false;
    }

    const size_t cpIdx = static_cast<size_t>(atOrBefore);
    const TTDCheckpoint& cp = _timeline[cpIdx];

    MLOGINFO("TimeTravelController::SeekTo — target=(frame=%llu,tInFrame=%u) "
             "restoring from checkpoint idx=%zu (frame=%llu)",
             static_cast<unsigned long long>(target.frame),
             static_cast<unsigned>(target.tInFrame),
             cpIdx,
             static_cast<unsigned long long>(cp.time.frame));

    // ------------------------------------------------------------------
    // Step 2: RestoreCheckpoint(cp). Leaves emulatorState.t_states /
    // frame_counter set to the checkpoint's frame boundary and z80.t at the
    // CPU's in-frame position there - the last instruction's overshoot past
    // the boundary (TTDChipsetState::cpu_t_in_frame), not 0.
    // ------------------------------------------------------------------
    RestoreCheckpointForReplay(cp);

    // The checkpoint restore leaves z80.t at this frame's overshoot (the
    // last instruction's spill past the boundary — a handful of T-states,
    // not 0). StepForwardFrame/StepBackFrame carry that same overshoot
    // forward as their target's tInFrame (CurrentPosition() reports z80.t),
    // so "target.tInFrame > 0" is true for every ordinary frame step and
    // says nothing about whether there's an actual interval left to
    // replay — compare against the checkpoint's own restored position
    // instead.
    const Z80* restoredZ80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    const uint32_t restoredTInFrame = restoredZ80 ? static_cast<uint32_t>(restoredZ80->t) : 0;
    // The seek ends on the checkpoint itself unless it replays past it
    _seekLandedOnCheckpoint = !(target.tInFrame > restoredTInFrame);

    // ------------------------------------------------------------------
    // Step 3: intra-frame silent replay if target.tInFrame is past where
    // the restore already left the CPU.
    //
    // Phase 2 Item 6 (parent TDD §5.1): check for external-event markers in
    // the replay interval (cp.time, target]. If any marker falls there, the
    // seek must stop at the earliest such marker — replay cannot reproduce
    // the marker's nondeterministic effect, so crossing it silently would
    // produce a misleading "this is the state at target" claim.
    //
    // Frame-aligned targets never trigger this check: with nothing left to
    // replay, the chosen checkpoint already reflects any markers at or
    // before that frame boundary.
    // ------------------------------------------------------------------
    // The engine's data: a sealed replay has no barrier but a v1 record without its data
    if (target.tInFrame > restoredTInFrame)
    {
        // A replay: every setting and medium counts (Phase 3, Step 4); after
        // the replay, which restores the checkpoint again
        const int64_t engineIndex = _replayEngine->CheckpointIndexOf({0, cp.time.frame, 0});
        auto checkForReplay = [&]() {
            if (engineIndex >= 0)
                CheckEngineCheckpoint(size_t(engineIndex), true);
        };
        TTDMachineTime start = 0;
        _replayEngine->Frames().Start(cp.time.frame, start);
        if (const TTDEvent* barrier = _replayEngine->Events().FirstBarrierIn(start, start + target.tInFrame))
        {
            const uint32_t at = static_cast<uint32_t>(barrier->machineTime - start);
            if (at > 0)
                ReplayWithinFrame(cp.time.frame, at);
            checkForReplay();
            SetState(TTDSessionState::Detached);
            if (outResult)
            {
                outResult->reached = false;
                outResult->arrivedAt = TTDTimePoint{cp.time.frame, at};
                outResult->haltReason = TTDSeekHaltReason::ExternalEvent;
            }
            return false;
        }
        ReplayWithinFrame(cp.time.frame, target.tInFrame);
        checkForReplay();
    }

    // ------------------------------------------------------------------
    // Step 4: transition to Detached (TDD §4.2).
    // ------------------------------------------------------------------
    SetState(TTDSessionState::Detached);

    MLOGINFO("TimeTravelController::SeekTo — arrived at (frame=%llu,tInFrame=%u), state=Detached",
             static_cast<unsigned long long>(target.frame),
             static_cast<unsigned>(target.tInFrame));

    if (outResult)
    {
        outResult->reached    = true;
        outResult->arrivedAt  = target;
        outResult->haltReason = TTDSeekHaltReason::Target;
    }
    return true;
}

void TimeTravelController::ReplayWithinFrame(uint64_t targetFrame, uint32_t targetTInFrame)
{
    const auto replayStart = std::chrono::steady_clock::now();
    struct ReplayTimer
    {
        std::chrono::steady_clock::time_point start;
        uint64_t& sink;
        ~ReplayTimer()
        {
            sink = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        }
    } replayTimer{replayStart, _perf.lastReplayNs};

    // Emulator must be available. The PageStore/Capture path doesn't need
    // it, but RunTStates does.
    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelController::ReplayWithinFrame — null _context or pEmulator, "
                    "skipping replay (frame=%llu, targetTInFrame=%u)",
                    static_cast<unsigned long long>(targetFrame),
                    static_cast<unsigned>(targetTInFrame));
        return;
    }

    // Defensive: clamp targetTInFrame to the frame (in TTD time units: the
    // frame at the model's top clock, whatever turbo runs now - B4).
    const uint32_t frameSpan = FrameSpan();
    if (targetTInFrame > frameSpan)
    {
        MLOGWARNING("TimeTravelController::ReplayWithinFrame — targetTInFrame=%u > "
                    "frame span=%u, clamping",
                    static_cast<unsigned>(targetTInFrame),
                    static_cast<unsigned>(frameSpan));
        targetTInFrame = frameSpan;
    }

    // ------------------------------------------------------------------
    // Engage silent-replay mode for the duration of the loop. EnterReplayMode
    // is idempotent and saves the host audio mute state so we can restore
    // it on exit (TDD §8.2).
    // ------------------------------------------------------------------
    ReplayModeScope replay(*this);

    // ------------------------------------------------------------------
    // Run to the target. Recorded input is applied by the stepping engine
    // itself (ServiceInput after every instruction, armed by the restore
    // that positioned the machine), at exactly the instruction boundaries it
    // was recorded at - the same path a Detached forward run uses.
    // ------------------------------------------------------------------
    // The CPU resumes at the checkpoint's overshoot, not at 0
    RunToTInFrame(targetTInFrame);

    replay.Exit();
}

void TimeTravelController::RunToFrameEnd()
{
    Z80* z80 = _context->pCore->GetZ80();
    // A checkpoint restores the CPU at the frame's overshoot past the
    // boundary, so the remainder is one frame's worth of T-states (scaled by
    // the restored CPU frequency multiplier). Crossing the limit runs the
    // frame-end processing, which completes the frame's picture.
    const uint32_t frameTStates = _context->emulatorState.BaseToCpuT(_context->config.frame);
    const uint32_t startT = static_cast<uint32_t>(z80->t);
    if (startT < frameTStates)
        _context->pEmulator->RunTStates(frameTStates - startT, /*skipBreakpoints=*/true);
}

void TimeTravelController::ComposeDisplay(bool frameTarget)
{
    if (!_context || !_context->pEmulator || !_context->pCore || !_context->pScreen || _timeline.empty())
        return;

    Z80* z80 = _context->pCore->GetZ80();
    if (!z80)
        return;

    const uint64_t frame = _context->emulatorState.frame_counter;
    const uint32_t tInFrame = TInFrameNow();

    // Last checkpoint at or before `f`; nullptr when `f` precedes the session.
    auto checkpointAtOrBefore = [this](uint64_t f) -> const TTDCheckpoint* {
        const int64_t i = TimelineIndexAtOrBefore(TTDTimePoint{f, 0});
        return i < 0 ? nullptr : &_timeline[static_cast<size_t>(i)];
    };

    // Deliberately a LOCAL snapshot, not the shared _liveSnapshot member:
    // RunTStates pumps MessageCenter (NC_EXECUTION_CPU_STEP) and a synchronous
    // subscriber (debugger views) can reenter GetFrameCache, which saves and
    // restores through _liveSnapshot.
    LiveStateSnapshot local;
    SaveLiveState(local);
    ReplayModeScope replay(*this);

    // Replay whole frames from `cp` until frame `f` is the current frame.
    auto runUntilFrame = [this](uint64_t f) {
        while (_context->emulatorState.frame_counter < f)
        {
            const uint64_t before = _context->emulatorState.frame_counter;
            RunToFrameEnd();
            if (_context->emulatorState.frame_counter == before)
                break;  // no progress - never spin
        }
    };

    // Static decode of the restored checkpoint as the base under the beam.
    // The beam repaints everything from the checkpoint's position onward; the
    // base only shows where the recording holds no beam history - the part of
    // the session's first frame before recording started (its baseline is
    // captured mid-frame). Painting it makes the picture independent of
    // whatever the framebuffer held before.
    auto paintStaticBase = [this]() {
        _context->pScreen->RenderOnlyMainScreen();
        _context->pScreen->FillBorderWithColor(_context->emulatorState.pFE & 0b0000'0111);
        // The static decode carries no beam history: plane B says "not drawn"
        size_t planeBCount = 0;
        if (uint16_t* planeB = _context->pScreen->GetPlaneB(&planeBCount))
            std::fill(planeB, planeB + planeBCount, uint16_t{0});
    };

    ITTDDisplayParticipant* participant = _context->pTtdDisplayParticipant;
    if (frameTarget)
    {
        // The frame's final picture: its own T-states, start to end. A device
        // picture (VDAC2) runs on its own frame clock: its frame that finished
        // last may have started before this frame, so replay from earlier
        const uint64_t leadIn = participant ? participant->TTDLeadInFrames() : 0;
        const uint64_t from = frame > leadIn ? frame - leadIn : 0;
        if (const TTDCheckpoint* cp = checkpointAtOrBefore(from))
        {
            RestoreCheckpointForReplay(*cp);
            paintStaticBase();
            runUntilFrame(frame);
            RunToFrameEnd();
        }
    }
    else
    {
        // Base: frame f-1's final picture, as a live machine has it when f
        // starts. The first frame of the session has no predecessor; its base
        // is the static decode of its checkpoint.
        const TTDCheckpoint* prev = frame > 0 ? checkpointAtOrBefore(frame - 1) : nullptr;
        const TTDCheckpoint* cur = checkpointAtOrBefore(frame);
        if (prev)
        {
            RestoreCheckpointForReplay(*prev);
            paintStaticBase();
            runUntilFrame(frame);
        }
        else if (cur)
        {
            RestoreCheckpointForReplay(*cur);
            paintStaticBase();
        }

        // Then what the beam draws in frame f up to the position.
        if (_context->emulatorState.frame_counter == frame)
            RunToTInFrame(tInFrame);
        _context->pScreen->UpdateScreen();
    }

    // A device picture (VDAC2) gets ready for the target kind before the copy
    if (participant)
        participant->TTDPrepareComposedPicture(frameTarget);

    std::vector<uint8_t> composed;
    uint32_t* fb = nullptr;
    size_t fbSize = 0;
    _context->pScreen->GetFramebufferData(&fb, &fbSize);
    if (fb && fbSize)
        composed.assign(reinterpret_cast<const uint8_t*>(fb), reinterpret_cast<const uint8_t*>(fb) + fbSize);

    // Plane B was rendered by the same replay (ZX DLSS), so it is composed too
    std::vector<uint16_t> composedPlaneB;
    size_t planeBCount = 0;
    if (const uint16_t* planeB = _context->pScreen->GetPlaneB(&planeBCount))
        composedPlaneB.assign(planeB, planeB + planeBCount);

    replay.Exit();
    RestoreLiveState(local);

    // Machine state is exactly the caller's again; only the pixels change.
    _context->pScreen->GetFramebufferData(&fb, &fbSize);
    if (fb && fbSize == composed.size())
        std::memcpy(fb, composed.data(), fbSize);
    if (uint16_t* planeB = _context->pScreen->GetPlaneB(&planeBCount))
    {
        if (planeBCount == composedPlaneB.size())
            std::memcpy(planeB, composedPlaneB.data(), planeBCount * sizeof(uint16_t));
    }
}

void TimeTravelController::PresentPosition(bool frameTarget)
{
    const auto start = std::chrono::steady_clock::now();
    // Composing the picture restores neighboring checkpoints; the position's
    // own settings and media check stays the seek's
    const TTDRestoreResult seekCheck = _lastEngineCheck;
    ComposeDisplay(frameTarget);
    _lastEngineCheck = seekCheck;
    PublishSeekedFrame();
    _perf.lastPresentNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
}

bool TimeTravelController::StepBackFrame()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // Browsing while recording pauses the recording (D8): resumed where it paused, it goes on
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();

    // Idle-with-history is allowed; only the timeline-empty case fails.
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::StepBackFrame — no recorded history");
        return false;
    }

    const TTDTimePoint current = CurrentPosition();
    if (current.frame == 0)
    {
        MLOGINFO("TimeTravelController::StepBackFrame — already at frame 0, cannot step back");
        return false;
    }

    // Frame steps are positioning by frame number: land on the frame boundary
    // and show that frame's final picture. Carrying current.tInFrame (the
    // previous checkpoint's instruction overshoot) turned a frame step into
    // an intra-frame seek whose overshoot grew with every step.
    return SeekTo(TTDTimePoint{current.frame - 1, 0});
}

bool TimeTravelController::StepForwardFrame()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // Browsing while recording pauses the recording (D8): resumed where it paused, it goes on
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();

    // Idle-with-history is allowed; only the timeline-empty case fails.
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::StepForwardFrame — no recorded history");
        return false;
    }

    const TTDTimePoint current   = CurrentPosition();
    const TTDTimePoint sessionEnd = SessionEndPosition();

    if (current.frame >= sessionEnd.frame)
    {
        MLOGINFO("TimeTravelController::StepForwardFrame — already at or past the "
                 "last captured frame (%llu), cannot step forward",
                 static_cast<unsigned long long>(current.frame));
        return false;
    }

    // See StepBackFrame: a frame step lands on the frame boundary.
    return SeekTo(TTDTimePoint{current.frame + 1, 0});
}

// ---------------------------------------------------------------------------
// Resume-from-past (Phase 2 Item 5; parent TDD §8.3)
// ---------------------------------------------------------------------------

bool TimeTravelController::ResumeRecordingFrom(const TTDTimePoint& from)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // Returning to live recording ends the browse scope: free the decode cache.
    ClearFrameCache();

    // ------------------------------------------------------------------
    // Validate preconditions. Same shape as SeekTo — the truncation rule
    // is meaningless without a recorded timeline to truncate.
    // ------------------------------------------------------------------
    if (!_context)
    {
        MLOGWARNING("TimeTravelController::ResumeRecordingFrom — null _context");
        return false;
    }

    if (_state == TTDSessionState::Idle)
    {
        MLOGWARNING("TimeTravelController::ResumeRecordingFrom — session is Idle "
                    "(no history to resume from)");
        return false;
    }

    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::ResumeRecordingFrom — timeline is empty");
        return false;
    }

    // A paused recording reaches to where it paused, inside the last frame
    const TTDTimePoint sessionEnd = _recordingPaused ? _pausedEnd : _timeline.back().time;
    if (sessionEnd < from)
    {
        MLOGWARNING("TimeTravelController::ResumeRecordingFrom — target "
                    "(frame=%llu, tInFrame=%u) is beyond session end "
                    "(frame=%llu, tInFrame=%u)",
                    static_cast<unsigned long long>(from.frame),
                    static_cast<unsigned>(from.tInFrame),
                    static_cast<unsigned long long>(sessionEnd.frame),
                    static_cast<unsigned>(sessionEnd.tInFrame));
        return false;
    }

    // ------------------------------------------------------------------
    // Step 1: ensure the emulator is positioned at `from`. SeekToInternal
    // handles binary search, RestoreCheckpoint, intra-frame silent replay,
    // and the Detached transition. If the caller already SeekTo'd to `from`
    // this is a re-restore (deterministic — same machine state results).
    //
    // We use SeekToInternal (NOT public SeekTo) because we legitimately
    // need to seek during Recording — the truncation in Step 2 keeps the
    // timeline's sorted invariant intact.
    // ------------------------------------------------------------------
    if (!SeekToInternal(from, nullptr))
    {
        // SeekToInternal already logged the specific failure.
        return false;
    }
    return ContinueRecordingAt(from);

}

bool TimeTravelController::ContinueRecordingAt(const TTDTimePoint& from)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    const size_t preTimelineSize  = _timeline.size();
    const size_t preJournalSize   = _inputJournal.Size();
    const size_t preMarkerCount   = _externalEvents.Size();

    // ------------------------------------------------------------------
    // Step 2: truncate timeline + page refs after `from`. Page refs held
    // by dropped checkpoints are released back to the page store; the
    // slots become eligible for reuse by future Intern calls (TDD §6.3).
    // ------------------------------------------------------------------
    // The resume point is where the machine actually stands: a frame-aligned
    // `from` restores the checkpoint's CPU at its overshoot past the frame
    // boundary (TTDChipsetState::cpu_t_in_frame), so events recorded there
    // after the seek sit at (from.frame, overshoot), not at tInFrame 0.
    const TTDTimePoint here = CurrentPosition();
    const TTDTimePoint cut = (from < here) ? here : from;

    // ------------------------------------------------------------------
    // Step 3: truncate input journal after the resume point. Events exactly
    // at it are kept (they happened at the resume point, not after it).
    // ------------------------------------------------------------------
    _inputJournal.DropAfter(cut);
    _externalEvents.DropAfter(cut);  // Phase 2 Item 6 — markers past the resume point are dead future
    for (auto it = _toolEditPayloads.begin(); it != _toolEditPayloads.end();)
        it = it->first >= _externalEvents.Size() ? _toolEditPayloads.erase(it) : std::next(it);
    _bookmarks.DropAfter(cut);  // TD-4 — bookmarks past the resume point are dead future

    // Coverage of the discarded future must go too, or reverse search prunes
    // frames of the new history by the old one. Frames before the resume
    // frame stay indexed; the resume frame itself is re-collected only from
    // the resume point on, so unless the machine stands exactly at that
    // frame's checkpoint it becomes a hole (queries replay it)
    const bool atCheckpoint = _seekLandedOnCheckpoint && !_timeline.empty() &&
                              _timeline.back().time.frame == cut.frame;
    _coverageIndex.DropFramesFrom(cut.frame, !atCheckpoint);

    // Port records past the resume point are dead future. By time: the
    // replay read the engine's bus journals, so these recorders' cursors did
    // not move (records at the cut stay, as in the engine)
    if (_portJournalRecorded)
    {
        const TTDTimePoint after{cut.frame, cut.tInFrame + 1};
        _portReads.TruncateTo(_portReads.LowerBound(after));
        _portWrites.TruncateTo(_portWrites.LowerBound(after));
    }

    // Phase 4 — write journal: convert the resume point to a globalT and
    // drop records strictly past it. Records exactly at it are kept.
    if (_writeJournal)
    {
        _writeJournal->DropAfter(GlobalT(cut));
    }
    ClipJournalSegments(GlobalT(cut));

    // The checkpoints after `from` and the engine's records after `cut` go
    // last: the engine's cursors into v1's journals follow their new ends
    TruncateTimelineAfter(from, cut);

    // ------------------------------------------------------------------
    // Step 4: return to Recording. Next OnFrameBoundary will append a fresh
    // checkpoint at frame `from.frame + 1` (the live emulator's frame
    // counter is set by SeekTo). A stop may have switched the capture flags
    // back off; the machine is parked at `from` after the seek.
    // ------------------------------------------------------------------
    EngageCaptureFeatures();
    SetState(TTDSessionState::Recording);   // the journal, if on, opens a segment at the resume point
    _recordingPaused = false;               // a paused recording goes on from here
    DisarmInputPlayback();  // live input again (journaled while recording)
    // A loaded session had collection switched off; the new history is live
    _context->ttdCoverageActive = _enableCoverageIndex;
    if (_portJournalRecorded)
    {
        _portReads.StartRecording();
        _portWrites.StartRecording();
    }
    SyncPortJournalHook();

    MLOGINFO("TimeTravelController::ResumeRecordingFrom — resumed at "
             "(frame=%llu, tInFrame=%u); timeline %zu→%zu checkpoints, "
             "journal %zu→%zu events, markers %zu→%zu, state=Recording",
             static_cast<unsigned long long>(from.frame),
             static_cast<unsigned>(from.tInFrame),
             preTimelineSize, _timeline.size(),
             preJournalSize, _inputJournal.Size(),
             preMarkerCount, _externalEvents.Size());

    return true;
}

bool TimeTravelController::ResumeRecordingLive()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // Leaving the browse scope: free the decode cache.
    ClearFrameCache();

    if (!_context)
    {
        MLOGWARNING("TimeTravelController::ResumeRecordingLive — null _context");
        return false;
    }

    if (_state == TTDSessionState::Recording)
        return true;  // Idempotent

    if (_state == TTDSessionState::Detached && _recordingPaused)
        return ResumeRecordingFrom(_pausedEnd);   // the paused recording goes on where it paused
    if (_state == TTDSessionState::Detached)
    {
        MLOGWARNING("TimeTravelController::ResumeRecordingLive — refused: session is Detached "
                    "(use ResumeRecordingFrom to continue from a historical point)");
        return false;
    }

    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::ResumeRecordingLive — timeline is empty "
                    "(StartRecording begins a fresh session)");
        return false;
    }

    // No-unrecorded-gap guard: the present must still be inside the frame
    // the recording ended in. If the emulator ran while recording was
    // stopped, those frames have no checkpoints and no journaled writes —
    // replaying across the gap from the older checkpoint would silently
    // produce wrong state, so the caller must wipe and StartRecording
    // instead (exactly the pre-DebuggerLive behavior).
    const TTDTimePoint present = CurrentPosition();
    const TTDTimePoint recordedEnd = _timeline.back().time;
    if (present.frame != recordedEnd.frame)
    {
        MLOGWARNING("TimeTravelController::ResumeRecordingLive — refused: present frame %llu is %s "
                    "the recorded end frame %llu (unrecorded gap; StartRecording instead)",
                    static_cast<unsigned long long>(present.frame),
                    present.frame < recordedEnd.frame ? "before" : "past",
                    static_cast<unsigned long long>(recordedEnd.frame));
        return false;
    }

    // Feature stewardship as in StartRecording: re-enable capture flags if
    // another surface released them while recording was stopped. Flipping
    // kDebugMode swaps the memory interface, so pause if running
    // (defensive; the debugger browse/leave flow calls this while paused).
    Emulator* emu = _context->pEmulator;
    const bool wasRunning = emu && emu->IsRunning() && !emu->IsPaused();
    if (wasRunning)
    {
        emu->Pause(false);
        emu->WaitForPauseConfirmation(1000);
    }

    EngageCaptureFeatures();

    // Instructions executed since the stop wrote nothing to the journal: the
    // segment closed at the stop, a new one opens here (SetState below)

    // The port-read journal has no room for a gap: a replay across the reads
    // made while stopped would hand out every later record one read early
    if (GlobalT(present) != _recordingStoppedAtT)
        DropPortJournal("the machine ran unrecorded between the stop and the resume");

    SetState(TTDSessionState::Recording);
    DisarmInputPlayback();  // live input again (journaled while recording)
    _context->ttdCoverageActive = _enableCoverageIndex;
    if (_portJournalRecorded)
    {
        _portReads.StartRecording();
        _portWrites.StartRecording();
    }
    SyncPortJournalHook();

    MLOGINFO("TimeTravelController::ResumeRecordingLive — resumed at (frame=%llu, tInFrame=%u); "
             "timeline keeps %zu checkpoints, appending after (frame=%llu, tInFrame=%u)",
             static_cast<unsigned long long>(present.frame),
             static_cast<unsigned>(present.tInFrame),
             _timeline.size(),
             static_cast<unsigned long long>(recordedEnd.frame),
             static_cast<unsigned>(recordedEnd.tInFrame));

    // Published while the machine is still parked (it records once resumed)
    PublishSessionInfo();

    if (wasRunning && emu)
        emu->Resume(false);

    return true;
}

void TimeTravelController::SetHistoryLimit(uint64_t maxFrames, uint64_t maxBytes)
{
    // A recording machine is parked by the operation (the Qt history combo
    // calls this from the UI thread while it records): the eviction never
    // runs beside its capture
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    _historyLimitFrames.store(maxFrames, std::memory_order_release);
    _historyLimitBytes.store(maxBytes, std::memory_order_release);
    ApplyHistoryPolicy();
    if (_state == TTDSessionState::Recording)
        EnforceHistoryLimit();
}

uint64_t TimeTravelController::BlobBytes(const TTDCheckpoint& cp)
{
    uint64_t bytes = 0;
    for (const auto& blob : cp.peripheralBlobs)
        bytes += blob.second.size();
    return bytes;
}

uint64_t TimeTravelController::HistoryBytes() const
{
    return _engine->PieceStore().ArenaBytes() + _blobBytes;
}

void TimeTravelController::ApplyHistoryPolicy()
{
    // The engine keeps every segment; the limits drop them here
    // (EnforceHistoryLimit). With a frame limit a segment is an eighth of the
    // window (the history holds up to that much more), otherwise a baseline
    // every minute
    constexpr uint64_t kSegmentFrames = 60 * 50;
    const uint64_t frames = _historyLimitFrames.load(std::memory_order_acquire);
    TTDHistoryPolicy policy;
    policy.mode = TTDHistoryMode::Growable;
    policy.segmentFrames = static_cast<uint32_t>(frames ? std::clamp<uint64_t>(frames / 8, 1, kSegmentFrames) : kSegmentFrames);
    _engine->SetHistoryPolicy(policy);
}

void TimeTravelController::EnforceHistoryLimit()
{
    // Whole segments, oldest first: while the next one alone covers the frame
    // window (what the engine's ring does), and while the store is over budget
    const uint64_t frames = _historyLimitFrames.load(std::memory_order_acquire);
    const uint64_t bytes = _historyLimitBytes.load(std::memory_order_acquire);
    const auto& segments = _engine->Segments();
    while (frames != 0 && segments.size() >= 2 && _engine->Frames().LastFrame() - segments[1].firstFrame >= frames &&
           _engine->DropOldestHeldSegment())
    {
    }
    while (bytes != 0 && HistoryBytes() > bytes && _engine->DropOldestHeldSegment())
    {
    }
    SyncTimelineFront();
    // A black box keeps its window on disk too: the files of the dropped segments go
    if (_blackBox && _shadowFolder)
        _shadowFolder->DropOldestSegments(_engine->Segments().size());
}

void TimeTravelController::SyncTimelineFront()
{
    const size_t held = _engine->CheckpointCount() - _engine->FirstCheckpoint();
    if (_timeline.size() <= held)
        return;
    const size_t count = _timeline.size() - held;
    for (size_t i = 0; i < count; ++i)
    {
        _blobBytes -= BlobBytes(_timeline[i]);
    }
    _timeline.erase(_timeline.begin(), _timeline.begin() + static_cast<std::ptrdiff_t>(count));
    _evictedCheckpoints += count;

    // The journals start where the history now starts
    const TTDCheckpoint& front = _timeline.front();
    _engine->Writes().DropBefore(CheckpointStartT(front));
    _inputJournal.DropBefore(front.time);
    _externalEvents.DropBefore(front.time);
    _bookmarks.DropBefore(front.time);
    if (_portJournalRecorded)
    {
        _portReads.DropBefore(front.portReadCursor);
        _portWrites.DropBefore(front.portWriteCursor);
    }
    if (_inputPlaybackArmed)
        _inputCursor = _inputJournal.FirstIndexAtOrAfter(InputEventTimeNow(_context));
    ClearFrameCache();
}

int64_t TimeTravelController::TimelineIndexAtOrBefore(const TTDTimePoint& t) const
{
    // Checkpoints sit at frame boundaries (tInFrame 0): the one at or before
    // t is the last whose frame is at or before t's
    const int64_t index = _engine->CheckpointAtOrBefore(t.frame);
    if (index < 0)
        return -1;
    const size_t i = static_cast<size_t>(index) - _engine->FirstCheckpoint();
    return i < _timeline.size() ? static_cast<int64_t>(i) : -1;
}

void TimeTravelController::SetShadowEngine(TimeTravelEngine* engine)
{
    // Detaching keeps what the engine recorded (a benchmark reads it after the run)
    if (!engine)
        ArmShadowRegions(false);
    _shadowEngine = engine;
    _shadowRescan = true;
}

void TimeTravelController::RegisterScreenshotStream(TimeTravelEngine& engine)
{
    // Frame-boundary stream 0 (D19, phase-4 TDD §5.4): the final picture of
    // the frame that just ended, the one a seek to this frame shows. Off
    // until switched on: a frame with it off costs one mask check
    if (engine.Streams().IsRegistered(kScreenshotStream))
        return;
    engine.Streams().Register(kScreenshotStream, "screenshot", [this, &engine](const TTDPosition& at) {
        Screen* screen = _context ? _context->pScreen : nullptr;
        if (!screen)
            return;
        const FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
        if (!fb.memoryBuffer || fb.memoryBufferSize == 0)
            return;
        // Width, height and video mode first: a mode change needs no special case
        std::vector<uint8_t>& copy = _screenshotScratch;
        copy.resize(5 + fb.memoryBufferSize);
        std::memcpy(copy.data(), &fb.width, 2);
        std::memcpy(copy.data() + 2, &fb.height, 2);
        copy[4] = static_cast<uint8_t>(fb.videoMode);
        std::memcpy(copy.data() + 5, fb.memoryBuffer, fb.memoryBufferSize);
        engine.AddFrameStreamCopy(kScreenshotStream, at.frame, copy.data(), copy.size());
    });
}

void TimeTravelController::NoteFact(const TTDEvent& ev)
{
    if (_state != TTDSessionState::Recording || _inReplayMode || !_shadowEngine)
        return;
    TTDPendingFact fact;
    fact.at = {_context->emulatorState.frame_counter, TInFrameNow()};
    // The step crossed the frame's end, which the frame loop handles after it
    if (const uint32_t span = FrameSpan(); span && fact.at.tInFrame >= span)
    {
        fact.at.frame += fact.at.tInFrame / span;
        fact.at.tInFrame %= span;
    }
    fact.ev = ev;
    _shadowFacts.push_back(fact);
}

void TimeTravelController::NoteRzxFrameEnd(uint64_t rzxFrame, bool interrupt)
{
    TTDEvent ev;
    ev.kind = TTDEventKind::InterruptFrame;
    std::memcpy(ev.args, &rzxFrame, sizeof(rzxFrame));
    ev.args[kInterruptFrameInterruptArg] = interrupt ? 1 : 0;
    NoteFact(ev);
}

void TimeTravelController::NoteReplaySource(TTDReplaySource source)
{
    TTDEvent ev;
    ev.kind = TTDEventKind::ReplaySourceChange;
    ev.args[0] = static_cast<uint8_t>(source);
    NoteFact(ev);
}

std::string TimeTravelController::ShadowRecordingFolder() const
{
    return _shadowFolder ? _shadowFolder->Path() : std::string();
}

void TimeTravelController::FinishShadowFiles()
{
    // The facts, the coverage and the bookmarks as they are at the end: a
    // finished recording's folder loads like a saved session
    if (_shadowWriter && _shadowEngine)
        _shadowWriter->SetHolderStreams(SessionHolderStreams(/*declareAll=*/true));
    if (_shadowWriter && _shadowEngine && !_shadowWriter->Finish(*_shadowEngine))
        MLOGWARNING("TimeTravelController: the shadow session's files: %s", _shadowWriter->Error().c_str());
    _shadowWriter.reset();   // the folder stays the session's until a new one starts or it is invalidated
}

void TimeTravelController::DiscardShadowFiles()
{
    _shadowWriter.reset();   // its thread stops; the files go with the folder
    if (_shadowFolder)
        _shadowFolder->Discard();
    _shadowFolder.reset();
}

void TimeTravelController::KeepShadowFiles()
{
    FinishShadowFiles();
    if (_shadowFolder && _shadowFolder->Segments().empty())
        _shadowFolder->Discard();
    else if (_shadowFolder)
        MLOGINFO("TimeTravelController: the black box's previous recording stays in %s", _shadowFolder->Path().c_str());
    _shadowFolder.reset();
}

void TimeTravelController::ResetShadow()
{
    // The session in memory goes, and its files with it: a recording's folder
    // lives as long as its session (a saved session is a file of its own; a
    // crash leaves the folder, which the startup cleanup removes after 7 days)
    DiscardShadowFiles();
    ArmShadowRegions(false);
    _shadowDeviceRegions.clear();
    if (_shadowEngine)
        _shadowEngine->EndSession();
}

void TimeTravelController::ArmShadowRegions(bool on)
{
    if (_shadowArmed == on)
        return;
    for (ITTDRegionSource* source : _peripherals.RegionSources())
        source->TTDArmRegions(on);
    _shadowArmed = on;
}

bool TimeTravelController::MediaReadAdapter::Playing() const
{
    return engine && engine->MediaReads().GetMode() == TTDMediaJournal::Mode::Play;
}

bool TimeTravelController::MediaReadAdapter::Play(const std::string& slot, uint64_t lba, uint8_t* out, size_t size)
{
    return engine && engine->MediaReads().PlayNext(slot, lba, out, size);
}

void TimeTravelController::MediaReadAdapter::Record(const std::string& slot, uint64_t lba, const uint8_t* bytes,
                                                 size_t size)
{
    if (!engine || engine->MediaReads().GetMode() != TTDMediaJournal::Mode::Record || !_owner._context)
        return;
    const EmulatorState& st = _owner._context->emulatorState;
    const Z80* z80 = _owner._context->pCore ? _owner._context->pCore->GetZ80() : nullptr;
    engine->MediaReads().Append(st.frame_counter, z80 ? st.TtdTInFrame(z80->t) : 0, slot, lba, bytes, size);
}

void TimeTravelController::SyncMediaReadJournal()
{
    if (!_context || !_context->pMediaManager)
        return;
    if (_inReplayMode && _replayEngine && _replayEngine->MediaReads().GetMode() == TTDMediaJournal::Mode::Play)
        _mediaReads.engine = _replayEngine;
    else if (_state == TTDSessionState::Recording && _shadowEngine && _shadowEngine->IsSessionOpen())
        _mediaReads.engine = _shadowEngine;
    else
        _mediaReads.engine = nullptr;
    _context->pMediaManager->SetReadJournal(_mediaReads.engine ? &_mediaReads : nullptr);
    // The interrupt-vector journal follows the same engine (recorded or played)
    TTDPortJournal* vectors = _mediaReads.engine ? &_mediaReads.engine->BusVectors() : nullptr;
    _context->ttdVectors = vectors && vectors->GetMode() != TTDPortJournal::Mode::Off ? vectors : nullptr;
}

std::vector<TTDRegionDesc> TimeTravelController::LiveRegions() const
{
    std::vector<TTDRegionDesc> regions;
    TTDRegionDesc ram;
    ram.id = TTDRegionId::MachineRam;
    ram.name = "ram";
    ram.pieces = static_cast<uint32_t>(_modelRamPages) * 4;
    ram.bytes = ram.pieces * kTTDPieceSize;
    ram.memory = _memory ? _memory->RAMPageAddress(0) : nullptr;
    regions.push_back(ram);
    std::vector<TTDDeviceRegion> device;
    for (ITTDRegionSource* source : _peripherals.RegionSources())
        source->TTDRegions(device);
    for (const TTDDeviceRegion& r : device)
        regions.push_back(r.desc);
    return regions;
}

void TimeTravelController::CheckEngineCheckpoint(size_t index, bool forReplay)
{
    _lastEngineCheck = _replayEngine->CheckConfiguration(
        index, CaptureConfigFingerprint(*_context, _replayRomSignature), forReplay);
    if (!forReplay)
        return;
    // A medium that changed since this checkpoint: set back to its recorded
    // version, or, without versions, reported (the CPU still reads the
    // recorded sectors; the controller's state where the replay stops may differ)
    IMediaHistory* media = _context->pMediaManager;
    if (!media)
        return;
    std::vector<MediaVersionInfo> live;
    media->CurrentVersions(live);
    const std::vector<TTDMediaSlot>& slots = _replayEngine->MediaSlots();
    for (size_t s = 0; s < slots.size(); ++s)
    {
        TTDMediaVersion recorded;
        if (!_replayEngine->MediaVersionAt(index, s, recorded))
            continue;
        const auto it = std::find_if(live.begin(), live.end(),
                                     [&](const MediaVersionInfo& v) { return v.slot == slots[s].slot; });
        const TTDMediaVersion now = it == live.end() ? TTDMediaVersion{} : TTDMediaVersion{it->contentId, it->version};
        if (now == recorded)
            continue;
        if (now.contentId == recorded.contentId && media->SetHead(slots[s].slot, recorded.version))
            continue;
        TTDRestoreIssue issue;
        issue.kind = TTDRestoreIssueKind::MediaVersionDiffers;
        issue.severity = TTDRestoreStatus::NotBitExact;
        issue.detail = slots[s].slot +
                       (now.contentId != recorded.contentId ? ": another medium than the recording's"
                                                            : ": written since this point, no versions to go back to") +
                       " (the replay reads the recorded sectors; the controller's state where it stops may differ)";
        _lastEngineCheck.Add(std::move(issue));
    }
}

void TimeTravelController::SetReplaySource(TimeTravelEngine* engine)
{
    _replayEngine = engine;
    _lastEngineCheck = {};
    if (!engine)
        return;
    _replayRomSignature = ComputeRomSignature();
    // A session fed from a file holds no live pointers: bind it to this machine
    std::string unbound;
    if (engine->BindLive(LiveRegions(), _peripherals.DeviceEntries(), &unbound) != 0)
        MLOGWARNING("TimeTravelController::SetReplaySource — not bound to this machine: %s", unbound.c_str());
}

bool TimeTravelController::FeedShadow(const TTDCheckpoint& out, bool baseline)
{
    TimeTravelEngine& engine = *_shadowEngine;
    std::string error;
    const uint32_t pieces = static_cast<uint32_t>(_modelRamPages) * 4;
    if (baseline || !engine.IsSessionOpen() || engine.Regions().empty() || engine.Regions()[0].pieces != pieces)
    {
        TTDRegionDesc ram;
        ram.id = TTDRegionId::MachineRam;
        ram.name = "ram";
        ram.pieces = pieces;
        ram.bytes = pieces * kTTDPieceSize;
        ram.dirtyGranularity = kTTDPieceSize * 4;
        ram.memory = _memory->RAMPageAddress(0);   // machine RAM is one contiguous block

        // Device memory as further regions (NeoGS RAM and flash, ...)
        ArmShadowRegions(false);
        _shadowDeviceRegions.clear();
        for (ITTDRegionSource* source : _peripherals.RegionSources())
            source->TTDRegions(_shadowDeviceRegions);
        std::vector<TTDRegionDesc> regions = {ram};
        for (const TTDDeviceRegion& r : _shadowDeviceRegions)
            regions.push_back(r.desc);

        // The device table, from the same registry v1 records from (checked
        // when recording started: CheckDeviceTable)
        if (!engine.BeginSession(regions, _peripherals.DeviceEntries(), error))
        {
            MLOGWARNING("TimeTravelController: shadow engine refused the session: %s", error.c_str());
            _shadowDeviceRegions.clear();
            return false;
        }
        ArmShadowRegions(true);
        _shadowRescan = true;
        // Events and bus data the engine gets from here on: what v1 journals after this point
        _shadowEvents.input = _inputJournal.Size();
        _shadowEvents.external = _externalEvents.Size();
        _shadowEvents.facts = 0;
        _shadowFacts.clear();
        RegisterScreenshotStream(engine);
        if (_context->rzxPlayer)
        {
            // The session starts inside an RZX playback
            TTDPendingFact source;
            source.at = {out.time.frame, 0};
            source.ev.kind = TTDEventKind::ReplaySourceChange;
            source.ev.args[0] = static_cast<uint8_t>(TTDReplaySource::RzxPlayback);
            _shadowFacts.push_back(source);
        }
        _shadowBusReads = _portReads.Size();
        _shadowBusWrites = _portWrites.Size();
        _shadowLastLength = 0;
        _shadowRomSignature = ComputeRomSignature();   // the ROM set does not change within a session (D39)
        _shadowMediaKnown = false;
        SyncMediaReadJournal();   // sector reads go into the engine from here
    }

    // Media versions before the capture: a version that changed goes with
    // this frame's checkpoint (Phase 3, Step 4). Read only when the media
    // layer's stamp moved (an insert, an eject, a frame that wrote)
    if (IMediaHistory* media = _context->pMediaManager)
    {
        const uint64_t stamp = media->VersionStamp();
        if (!_shadowMediaKnown || stamp != _shadowMediaStamp)
        {
            std::vector<MediaVersionInfo> versions;
            media->CurrentVersions(versions);
            for (const MediaVersionInfo& v : versions)
                engine.NoteMediaVersion(v.slot, v.format, v.hasVersions, {v.contentId, v.version});
            _shadowMediaStamp = stamp;
            _shadowMediaKnown = true;
        }
    }

    TTDFrameInput in;
    in.position.frame = out.time.frame;
    // Machine time from the frame table (Phase 3, Step 3): a frame starts where
    // the last captured one started plus its measured length - the base
    // T-states the machine ran since (emulatorState.t_states grows by each
    // closed frame) in TTD units. frame x the current length (v1's GlobalT)
    // goes backwards when a frame's length changes (Sprinter 320 / 312 lines)
    const bool freshShadow = engine.CheckpointCount() == 0;
    const uint64_t baseNow = _context->emulatorState.t_states;
    const uint8_t clockUnits = _context->emulatorState.ttd_clock_units ? _context->emulatorState.ttd_clock_units : 1;
    const TTDMachineTime closedLength = freshShadow ? 0 : (baseNow - _shadowLastBase) * clockUnits;
    in.start = freshShadow ? GlobalT(out.time) : _shadowLastStart + closedLength;
    in.cpu = out.cpu;
    in.chipset = out.chipset;
    // Device states, raw, as the capture serialized them; a device whose
    // state also carries its region memory (General Sound) gives the engine
    // its state without that memory (CaptureNow took both)
    in.deviceStateBytes = _captureWork.deviceStateBytes;
    std::array<const std::vector<uint8_t>*, 256> without{};
    for (const auto& [id, state] : _withoutRegions)
        without[id] = &state;
    for (const uint8_t id : _capturedDevices)
    {
        const std::vector<uint8_t>& raw = without[id] ? *without[id] : _peripherals.LastCaptureState(id);
        if (!raw.empty())
            in.deviceStates.push_back({id, raw.data(), raw.size()});
    }
    auto addPage = [&](uint16_t page) {
        const uint8_t* bytes = _memory->RAMPageAddress(page);
        if (!bytes)
            return;
        for (uint32_t sub = 0; sub < 4; ++sub)
            in.changed.push_back({0, uint32_t(page) * 4 + sub, bytes + sub * kTTDPieceSize});
    };
    // Device regions: the pieces their devices marked (all of them on a rescan).
    // A partial last piece is handed over padded to 4 KB
    std::deque<std::vector<uint8_t>> padded;   // stable addresses while it grows
    std::vector<uint32_t> written;
    auto addDevicePieces = [&](uint32_t region, const TTDDeviceRegion& r, bool all) {
        written.clear();
        r.tracker->CollectAndClear(written);
        if (all)
        {
            written.clear();
            for (uint32_t p = 0; p < r.tracker->Pieces(); ++p)
                written.push_back(p);
        }
        for (const uint32_t p : written)
        {
            const size_t offset = size_t(p) * kTTDPieceSize;
            const uint8_t* bytes = r.tracker->Memory() + offset;
            if (offset + kTTDPieceSize > r.tracker->Bytes())
            {
                padded.emplace_back(kTTDPieceSize, 0);
                std::memcpy(padded.back().data(), bytes, r.tracker->Bytes() - offset);
                bytes = padded.back().data();
            }
            in.changed.push_back({region, p, bytes});
        }
    };

    for (ITTDRegionSource* source : _peripherals.RegionSources())
        source->TTDBeforeCapture();

    if (_shadowRescan)
    {
        in.changed.reserve(pieces);
        for (uint16_t p = 0; p < _modelRamPages; ++p)
            addPage(p);
    }
    else
        for (const uint16_t p : _dirtyScratch)
            addPage(p);
    for (size_t i = 0; i < _shadowDeviceRegions.size(); ++i)
        addDevicePieces(static_cast<uint32_t>(i + 1), _shadowDeviceRegions[i],
                        _shadowRescan || _shadowDeviceRegions[i].compareEachCapture);
    _shadowRescan = false;

    // Bus data up to this boundary (the checkpoint keeps where it stands)
    if (_portJournalRecorded)
    {
        TTDPortRecord r;
        for (; _shadowBusReads < _portReads.Size() && _portReads.Get(_shadowBusReads, r); ++_shadowBusReads)
            engine.AppendBusRead(r);
        for (; _shadowBusWrites < _portWrites.Size() && _portWrites.Get(_shadowBusWrites, r); ++_shadowBusWrites)
            engine.AppendBusWrite(r);
    }

    if (!engine.CaptureFrame(in, error))
    {
        MLOGWARNING("TimeTravelController: shadow engine capture failed: %s", error.c_str());
        return false;
    }
    // The write journal's new records and its spans (D40)
    DrainWritesToEngine();
    // What v1 journaled up to this boundary: input, network, markers (Phase 3, Step 1)
    FeedV1Events(engine, _inputJournal, _externalEvents, _shadowEvents, out.time.frame, nullptr, &_toolEditPayloads,
                 &_shadowFacts);
    _shadowFacts.erase(_shadowFacts.begin(), _shadowFacts.begin() + static_cast<std::ptrdiff_t>(_shadowEvents.facts));
    _shadowEvents.facts = 0;
    // A frame of another length than the one before it: a fact at this
    // boundary, the closed frame's length in TTD units (args, u32)
    if (!freshShadow && _shadowLastLength != 0 && closedLength != _shadowLastLength)
    {
        TTDEvent change;
        change.kind = TTDEventKind::FrameLengthChange;
        const uint32_t length = static_cast<uint32_t>(closedLength);
        std::memcpy(change.args, &length, sizeof(length));
        engine.AppendEvent(out.time.frame, 0, change);
    }
    if (!freshShadow)
        _shadowLastLength = closedLength;
    // The settings this frame runs with: the session's first, or a
    // ConfigChange cut at this boundary when they changed (Phase 3, Step 4)
    engine.SetConfiguration(out.time.frame, CaptureConfigFingerprint(*_context, _shadowRomSignature));
    _shadowLastStart = in.start;
    _shadowLastBase = baseNow;

    // Written as it records, a file per segment (Phase 4)
    if (!_shadowRecordingRoot.empty())
    {
        if (!_shadowWriter && !_shadowFolder)
        {
            const TMemModel* model = Config::FindModelByEnum(_context->config.mem_model);
            std::string why;
            _shadowFolder = TTDRecordingFolder::Create(_shadowRecordingRoot, model ? model->ShortName : "session",
                                                       std::time(nullptr), why);
            if (_shadowFolder)
            {
                TTDSessionSaveParams params;
                params.createdMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                                 std::chrono::system_clock::now().time_since_epoch())
                                                                 .count());
                std::random_device random;
                for (uint8_t& b : params.uuid)
                    b = static_cast<uint8_t>(random());
                params.holderStreams = SessionHolderStreams(/*declareAll=*/true);
                TTDRecordingFolder* folder = _shadowFolder.get();
                _shadowWriter = std::make_unique<TTDRecordingWriter>(
                    [folder](uint32_t n) { return folder->SegmentPath(n); }, params);
                if (!_shadowWriter->Begin(engine, why))
                    _shadowWriter.reset();
            }
            if (!_shadowWriter)
                MLOGWARNING("TimeTravelController: the shadow session is not written: %s", why.c_str());
        }
        else if (_shadowWriter && !_shadowWriter->Collect(engine))
            MLOGWARNING("TimeTravelController: writing the shadow session stopped: %s", _shadowWriter->Error().c_str());
    }
    return true;
}

void TimeTravelController::TruncateTimelineAfter(const TTDTimePoint& from, const TTDTimePoint& cut)
{
    // The last checkpoint at or before `from` stays (the one the seek restored)
    const int64_t keep = TimelineIndexAtOrBefore(from);
    if (keep < 0)
        return;
    const size_t dropCount = _timeline.size() - static_cast<size_t>(keep) - 1;
    const size_t keepEngine = _engine->FirstCheckpoint() + static_cast<size_t>(keep);
    const TTDCheckpoint& kept = _timeline[static_cast<size_t>(keep)];

    // The files written as it recorded hold the old future: they go, and the
    // continued session writes a folder of its own from its next capture
    DiscardShadowFiles();

    std::string error;
    const TTDPosition engineCut{0, cut.frame, cut.frame == kept.time.frame ? cut.tInFrame : 0};
    if (!_engine->TruncateAfter(keepEngine, engineCut, error))
    {
        // A session the engine cannot continue (one loaded from a file): it
        // ends, the recording starts a new one at the next capture
        MLOGWARNING("TimeTravelController::TruncateTimelineAfter — the engine cannot continue: %s", error.c_str());
        ResetShadow();
    }
    else
    {
        // The engine's cursors into v1's journals, at their new ends
        _shadowEvents.input = std::min(_shadowEvents.input, _inputJournal.Size());
        _shadowEvents.external = std::min(_shadowEvents.external, _externalEvents.Size());
        for (auto it = _shadowFacts.begin(); it != _shadowFacts.end();)
        {
            if (cut < it->at)
            {
                _engine->Payloads().Release(it->ev.payload);
                it = _shadowFacts.erase(it);
            }
            else
                ++it;
        }
        _shadowEvents.facts = 0;
        if (_portJournalRecorded)
        {
            _shadowBusReads = std::min<uint64_t>(_shadowBusReads, _portReads.Size());
            _shadowBusWrites = std::min<uint64_t>(_shadowBusWrites, _portWrites.Size());
        }
        // The writes after the resume point go with the future they belonged to
        _engine->Writes().DropAfter(GlobalT(cut));
        _engine->Writes().SetSegments(JournalSegments());
        // The next capture continues from the kept checkpoint's time
        const TTDEngineCheckpoint* cp = _engine->Checkpoint(keepEngine);
        const TTDEngineCheckpoint* before = keepEngine > _engine->FirstCheckpoint() ? _engine->Checkpoint(keepEngine - 1) : nullptr;
        _shadowLastStart = cp->start;
        _shadowLastBase = cp->chipset.t_states;
        _shadowLastLength = before ? cp->start - before->start : 0;
        _shadowMediaKnown = false;   // media versions are noted again
        // A session that was loaded, not recorded here, has no device regions
        // armed and no ROM signature for its settings: as a new session gets them
        if (!_shadowArmed)
        {
            _shadowDeviceRegions.clear();
            for (ITTDRegionSource* source : _peripherals.RegionSources())
                source->TTDRegions(_shadowDeviceRegions);
            ArmShadowRegions(true);
            _shadowRescan = true;
            _shadowRomSignature = ComputeRomSignature();
        }
    }

    if (dropCount == 0)
        return;
    for (size_t i = static_cast<size_t>(keep) + 1; i < _timeline.size(); ++i)
    {
        _blobBytes -= BlobBytes(_timeline[i]);
    }
    _timeline.resize(static_cast<size_t>(keep) + 1);

    MLOGINFO("TimeTravelController::TruncateTimelineAfter — dropped %zu checkpoints "
             "after (frame=%llu, tInFrame=%u); timeline now has %zu entries",
             dropCount,
             static_cast<unsigned long long>(from.frame),
             static_cast<unsigned>(from.tInFrame),
             _timeline.size());
}

// ---------------------------------------------------------------------------
// Session files: ttdcontrollersessionfile.cpp (the engine's format, Phase 5 C4b)
// ---------------------------------------------------------------------------

bool TimeTravelController::TurboSoundSessionKindMatches(
    const std::unordered_map<uint8_t, std::vector<uint8_t>>& sessionBlobs,
    const TTDSerializable& liveSlotDevice)
{
    const uint8_t legacyId = static_cast<uint8_t>(PeripheralId::TurboSound);
    const uint8_t fmId = static_cast<uint8_t>(PeripheralId::TSFM);
    const uint8_t liveId = static_cast<uint8_t>(liveSlotDevice.TTDPeripheralId());

    const bool sessionHasLegacy = sessionBlobs.find(legacyId) != sessionBlobs.end();
    const bool sessionHasFm = sessionBlobs.find(fmId) != sessionBlobs.end();

    // No slot blob in the session: the recording machine had no slot device -
    // nothing to mismatch against (RestoreAll's missingBlobs path covers it).
    if (!sessionHasLegacy && !sessionHasFm)
        return true;

    // Defensive: one device occupies the slot, so a session carrying both ids
    // cannot come from a healthy writer - refuse rather than guess which
    // blob to trust.
    if (sessionHasLegacy && sessionHasFm)
        return false;

    return sessionHasLegacy ? liveId == legacyId : liveId == fmId;
}

TimeTravelController::SelfTestResult TimeTravelController::CaptureRestoreSelfTest()
{
    SelfTestResult result;

    if (!_context || !_memory)
    {
        result.notes = "missing dependencies (context or memory)";
        return result;
    }

    Z80* cpu = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (!cpu)
    {
        result.notes = "missing CPU";
        return result;
    }
    // The capture goes into the engine, the history's store: with a session
    // there it would land in the middle of it
    if (!_timeline.empty())
    {
        result.notes = "a session holds history: the self-test runs without one";
        return result;
    }

    // Hash the live architectural state before capture. The RAM digest is
    // included so any missed page restoration shows up here.
    const uint32_t ramBytes = static_cast<uint32_t>(_context->config.ramsize) * 1024u;
    const uint64_t preRamDigest = ttd::HashBytes(_memory->RAMBase(), ramBytes);
    const auto preSnap = ttd::CaptureSnapshot(*static_cast<Z80State*>(cpu),
                                              _context->emulatorState,
                                              preRamDigest,
                                              &_peripherals);
    result.pre_hash = ttd::HashSnapshot(preSnap);

    // Capture a fresh checkpoint at the current live state, then immediately
    // restore it. This isolates capture/restore correctness from timeline
    // evolution — if a single-frame round-trip diverges, RestoreCheckpoint
    // or CaptureNow is broken.
    TTDCheckpoint cp;
    if (!CaptureNow(cp))
    {
        result.notes = "the engine did not take the capture";
        ResetShadow();
        return result;
    }
    RestoreCheckpoint(cp);
    ResetShadow();              // ...nor the engine session it opened

    // Hash the post-restore state. Identical to pre_hash iff capture and
    // restore are mutually inverse on every architectural field.
    const uint64_t postRamDigest = ttd::HashBytes(_memory->RAMBase(), ramBytes);
    const auto postSnap = ttd::CaptureSnapshot(*static_cast<Z80State*>(cpu),
                                               _context->emulatorState,
                                               postRamDigest,
                                               &_peripherals);
    result.post_hash = ttd::HashSnapshot(postSnap);

    result.pre_post_match = (result.pre_hash == result.post_hash);
    if (result.pre_post_match)
    {
        result.notes = "OK — single-frame capture/restore round-trip is byte-identical";
    }
    else
    {
        result.notes = "MISMATCH — pre=" + ttd::HashToString(result.pre_hash) +
                       " post=" + ttd::HashToString(result.post_hash) +
                       "; RestoreCheckpoint lost or corrupted some architectural field";
    }

    return result;
}

// ---------------------------------------------------------------------------
// Phase 4: Reverse-search engine (parent TDD §9 + §10.4)
// ---------------------------------------------------------------------------

void TimeTravelController::SetEnableCoverageIndex(bool enable)
{
    _enableCoverageIndex = enable;

    if (!enable)
        _coverageIndex.Clear();

    // Only mirror into the hot-path flag while a session is live; StartRecording
    // sets it otherwise.
    if (_context && _state == TTDSessionState::Recording)
        _context->ttdCoverageActive = enable;
    SyncCoverageSink();
}

void TimeTravelController::SyncCoverageSink()
{
    if (!_context)
        return;
    if (_state == TTDSessionState::Recording && _enableCoverageIndex)
        _context->ttdCoverage = &_coverageIndex;
    else if (_context->ttdCoverage == &_coverageIndex)
        _context->ttdCoverage = nullptr;
}

void TimeTravelController::RecordMemoryWrite(uint16_t addr, uint8_t oldVal, uint8_t newVal,
                                          uint16_t m1pc, PhysPage physPage)
{
    // Per-frame decode-cache capture (during a build replay only): attach the
    // write to the instruction currently being captured. Cheap — a couple of
    // stores, and only while the cache is being filled.
    if (_frameCaptureActive && _capturingCache && !_capturingCache->entries.empty())
    {
        _capturingCache->accesses.push_back({addr, newVal, TTDAccessKind::MemWrite});
        _capturingCache->entries.back().accessCount++;
    }

    // Only journal during active recording. During replay (seek / probe),
    // writes are reproducing recorded state — re-journaling would corrupt
    // the ring with duplicate records (TDD §9.3).
    if (_state != TTDSessionState::Recording)
        return;
    if (!_context)
        return;

    // Coverage is recorded independently of the write journal: the journal
    // answers "what was written", the coverage set answers "which frames are
    // worth replaying at all", and a user may want the second without paying
    // for the first.
    if (_enableCoverageIndex)
        _coverageIndex.Record(TTDCoverageKind::Written, MakeCoverageKey(physPage, addr));

    if (!_enableWriteJournal)
        return;

    TTDWriteRecord rec{};
    rec.globalT  = GlobalT({_context->emulatorState.frame_counter, TInFrameNow()});
    rec.addr     = addr;
    rec.isIo     = 0;
    rec.m1pc     = m1pc;
    rec.value    = newVal;
    // Journaled writes always have a RAM page (Memory gates on kPhysPageNone),
    // and RAM pages are 0..255, so the record's byte holds it exactly.
    rec.physPage = static_cast<uint8_t>(physPage);
    (void)oldVal;  // Not stored in the compact 12-byte record (TDD §9.3)

    if (_writeJournal)
        _writeJournal->Append(rec);
}

void TimeTravelController::RecordIoWrite(uint16_t port, uint8_t value, uint16_t m1pc)
{
    // Per-frame decode-cache capture (build replay only).
    if (_frameCaptureActive && _capturingCache && !_capturingCache->entries.empty())
    {
        _capturingCache->accesses.push_back({port, value, TTDAccessKind::PortWrite});
        _capturingCache->entries.back().accessCount++;
    }
    // Not in the write journal (D40): the port journal, recorded in every
    // session, has every OUT with its time and PC
    (void)m1pc;
}

bool TimeTravelController::RegenerateFrameWrites(uint64_t frame, std::vector<TTDSearchResult>& out)
{
    out.clear();
    if (!_context || _state == TTDSessionState::Recording)
        return false;
    const int64_t index = TimelineIndexAtOrBefore(TTDTimePoint{frame, 0});
    if (index < 0 || _timeline[static_cast<size_t>(index)].time.frame != frame ||
        static_cast<size_t>(index) + 1 == _timeline.size())
        return false;
    const auto it = _timeline.begin() + static_cast<std::ptrdiff_t>(index);
    const uint32_t frameT = FrameSpan();
    if (FirstBarrierBetween(it->time, TTDTimePoint{frame, frameT}))
        return false;

    RestoreCheckpointForReplay(*it);
    TTDSearchQuery all;
    all.access = TTDAccessType::Write;
    all.addrFrom = 0;
    all.addrTo = 0xFFFF;
    _context->ttdProbe.Reset();
    _context->ttdProbe.Arm(all);
    EnterReplayMode();
    ReplayWithinFrame(frame, frameT);
    ExitReplayMode();
    out = _context->ttdProbe.ExtractHits();
    _context->ttdProbe.Disarm();
    return true;
}

TTDJournalBuildResult TimeTravelController::BuildWriteJournal(uint64_t fromT, uint64_t toT,
                                                          const TTDJournalBuildProgress& progress)
{
    TTDJournalBuildResult r;
    if (!_context)
    {
        r.error = "no machine";
        return r;
    }
    if (_state == TTDSessionState::Recording)
    {
        r.error = "stop the recording first: the journal is built by replaying recorded history";
        return r;
    }
    if (_timeline.size() < 2)
    {
        r.error = "the session has no complete frame to replay";
        return r;
    }
    if (toT <= fromT)
    {
        r.error = "the span is empty";
        return r;
    }
    const TTDTimePoint home = CurrentPosition();

    // The spans covered now (the ring's evicted part already left out): the
    // journal is rebuilt below and must not claim them back
    _journalSegments = JournalSegments();

    // Progress and cancel for other threads (GetJournalBuildState, CancelJournalBuild)
    _journalBuildCancel.store(false);
    _journalBuildDone.store(0);
    _journalBuildActive.store(true);
    struct ActiveFlag
    {
        std::atomic<bool>& flag;
        ~ActiveFlag() { flag.store(false); }
    } activeFlag{_journalBuildActive};

    // The frames to build: those overlapping (fromT, toT] that a segment does
    // not cover yet
    std::vector<size_t> frames;
    for (size_t i = 0; i + 1 < _timeline.size(); ++i)
    {
        const uint64_t a = CheckpointStartT(_timeline[i]);
        const uint64_t b = CheckpointStartT(_timeline[i + 1]);
        if (b <= fromT || a >= toT)
            continue;
        bool inside = false;
        for (const TTDJournalSegment& s : _journalSegments)
            inside = inside || (s.from <= a && b <= s.to);
        if (inside)
            ++r.framesCovered;
        else
            frames.push_back(i);
    }

    struct BuiltFrame
    {
        TTDJournalSegment span;
        std::vector<TTDWriteRecord> records;
    };
    std::vector<BuiltFrame> built;
    std::vector<TTDSearchResult> hits;
    _journalBuildTotal.store(frames.size());
    for (size_t k = 0; k < frames.size(); ++k)
    {
        _journalBuildDone.store(k);
        if ((progress && !progress(k, frames.size())) || _journalBuildCancel.load())
        {
            r.cancelled = true;
            break;
        }
        const size_t i = frames[k];
        if (!RegenerateFrameWrites(_timeline[i].time.frame, hits))
        {
            ++r.framesRefused;
            continue;
        }
        BuiltFrame f{{CheckpointStartT(_timeline[i]), CheckpointStartT(_timeline[i + 1])}, {}};
        f.records.reserve(hits.size());
        for (const TTDSearchResult& h : hits)
        {
            const uint64_t t = GlobalT(h.time);
            if (t <= f.span.from || t > f.span.to)
                continue;
            TTDWriteRecord rec{};
            rec.globalT = t;
            rec.addr = h.addr;
            rec.isIo = 0;
            rec.m1pc = h.pc;
            rec.value = h.value;
            rec.physPage = static_cast<uint8_t>(h.physPage);   // as the live journal stores it
            f.records.push_back(rec);
        }
        r.records += f.records.size();
        built.push_back(std::move(f));
        ++r.framesBuilt;
    }
    if (!r.cancelled)
    {
        _journalBuildDone.store(frames.size());
        if (progress)
            progress(frames.size(), frames.size());
    }

    if (!built.empty())
    {
        // The index again, in time order: its records outside the built
        // frames, and the built frames' records (they replace any partial
        // ones a frame already had)
        TTDWriteIndex& index = _engine->Writes();
        std::vector<TTDWriteRecord> existing;
        existing.reserve(static_cast<size_t>(index.Size()));
        index.ForEach([&existing](const TTDWriteRecord& rec) { existing.push_back(rec); });
        std::vector<TTDWriteRecord> merged;
        merged.reserve(existing.size() + r.records);
        size_t k = 0;
        for (const BuiltFrame& f : built)
        {
            for (; k < existing.size() && existing[k].globalT <= f.span.from; ++k)
                merged.push_back(existing[k]);
            while (k < existing.size() && existing[k].globalT <= f.span.to)
                ++k;
            merged.insert(merged.end(), f.records.begin(), f.records.end());
        }
        merged.insert(merged.end(), existing.begin() + static_cast<std::ptrdiff_t>(k), existing.end());
        index.Rebuild(merged);

        // The built frames join the segments; touching or overlapping spans merge
        for (const BuiltFrame& f : built)
            _journalSegments.push_back(f.span);
        std::sort(_journalSegments.begin(), _journalSegments.end(),
                  [](const TTDJournalSegment& a, const TTDJournalSegment& b) { return a.from < b.from; });
        std::vector<TTDJournalSegment> joined;
        for (const TTDJournalSegment& s : _journalSegments)
        {
            if (!joined.empty() && s.from <= joined.back().to)
                joined.back().to = std::max(joined.back().to, s.to);
            else
                joined.push_back(s);
        }
        _journalSegments = std::move(joined);
        index.SetSegments(JournalSegments());
    }

    // Back where the machine stood
    if (r.framesBuilt || r.framesRefused)
    {
        const TTDTimePoint end = SessionEndPosition();
        SeekTo(end < home ? end : home);
    }
    r.ok = true;
    return r;
}

TTDJournalBuildResult TimeTravelController::BuildWriteJournalFrames(uint64_t fromFrame, uint64_t toFrame,
                                                                const TTDJournalBuildProgress& progress)
{
    // A frame runs from where its checkpoint's CPU stands to where the next
    // one's does: the span is (start of fromFrame, start of toFrame + 1]
    auto startOf = [this](uint64_t frame) -> uint64_t {
        // The first checkpoint at or after `frame`
        const int64_t before = TimelineIndexAtOrBefore(TTDTimePoint{frame, 0});
        size_t i = before < 0 ? 0 : static_cast<size_t>(before);
        if (i < _timeline.size() && _timeline[i].time.frame < frame)
            ++i;
        return i >= _timeline.size() ? UINT64_MAX : CheckpointStartT(_timeline[i]);
    };
    const uint64_t fromT = startOf(fromFrame);
    const uint64_t toT = toFrame == UINT64_MAX ? UINT64_MAX : startOf(toFrame + 1);
    if (fromT == UINT64_MAX)
    {
        TTDJournalBuildResult r;
        r.error = "frame " + std::to_string(fromFrame) + " is past the session's last frame";
        return r;
    }
    return BuildWriteJournal(fromT, toT, progress);
}

std::optional<TTDSearchResult>
TimeTravelController::FindLastAccess(const TTDSearchQuery& q,
                                  TTDExternalEvent* outBlockingMarker,
                                  TTDSearchWindow* outWindow)
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    // ------------------------------------------------------------------
    // Guards — same shape as SeekTo.
    // ------------------------------------------------------------------
    if (outBlockingMarker)
        *outBlockingMarker = TTDExternalEvent{};

    // Browsing while recording pauses the recording (D8): resumed where it paused, it goes on
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::FindLastAccess — no recorded history");
        return std::nullopt;
    }
    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelController::FindLastAccess — null context or emulator");
        return std::nullopt;
    }

    const uint32_t frameT = FrameSpan();  // TTD time units per frame (B4)

    // ------------------------------------------------------------------
    // Step 1: resolve beforeGlobalT → absolute t-state coordinate.
    // Default beforeGlobalT == UINT64_MAX means "current position".
    // ------------------------------------------------------------------
    uint64_t beforeGlobalT = q.beforeGlobalT;
    if (beforeGlobalT == UINT64_MAX)
    {
        const TTDTimePoint now = CurrentPosition();
        beforeGlobalT = GlobalT(now);
    }

    // TD-8: every answer below also says which part of history it covered -
    // the search walks back from `to` and ends at the match, at a barrier it
    // cannot replay across, or at the session start.
    const TTDTimePoint sessionStart = _timeline.front().time;
    auto reportWindow = [outWindow](const TTDTimePoint& from, const TTDTimePoint& to)
    {
        if (outWindow)
        {
            outWindow->searched = true;
            outWindow->from = from;
            outWindow->to = to;
        }
    };

    // ------------------------------------------------------------------
    // Step 2: port writes come from the port journal (D40): every session
    // records each OUT with its time and PC, so no replay is needed
    // ------------------------------------------------------------------
    if (q.access == TTDAccessType::Io && _portJournalRecorded)
    {
        const TTDTimePoint before = TimePointAt(beforeGlobalT);
        const TTDPortJournal& writes = _engine->BusWrites();
        TTDPortJournal::ReadCache cache;
        TTDPortRecord rec;
        for (uint64_t k = writes.LowerBound({before.frame, before.tInFrame + 1}, cache); k > 0; --k)
        {
            if (k - 1 < writes.FirstIndex() || !writes.Get(k - 1, rec, cache))
                break;
            if (rec.Time() < sessionStart)
                break;
            if (rec.port < q.addrFrom || rec.port > q.addrTo) continue;
            if (q.hasValueFilter && rec.value != q.value) continue;
            if (q.hasPcFilter && (rec.pc < q.pcFrom || rec.pc > q.pcTo)) continue;
            TTDSearchResult result;
            result.time = rec.Time();
            result.pc = rec.pc;
            result.value = rec.value;
            result.physPage = kPhysPageNone;
            result.access = TTDAccessType::Io;
            result.addr = rec.port;
            reportWindow(result.time, std::min(before, SessionEndPosition()));
            return result;
        }
        reportWindow(sessionStart, std::min(before, SessionEndPosition()));
        return std::nullopt;
    }

    // ------------------------------------------------------------------
    // Step 3: walk the checkpoint intervals backward from the target.
    //   - A run of frames inside one write journal segment (D40) answers
    //     from the journal at once.
    //   - Any other frame: check external-event markers (stop if blocked),
    //     skip it when the coverage index proves the access absent, else
    //     restore its checkpoint, replay it with the probe armed and take
    //     the last hit.
    // ------------------------------------------------------------------

    // Convert beforeGlobalT to a TTDTimePoint for checkpoint lookup.
    TTDTimePoint targetTime;
    targetTime = TimePointAt(beforeGlobalT);

    // Clamp target frame to session bounds.
    const uint64_t sessionEndFrame = _timeline.back().time.frame;
    if (targetTime.frame > sessionEndFrame)
    {
        targetTime.frame    = sessionEndFrame;
        targetTime.tInFrame = 0;
    }

    // Binary-search for the checkpoint at-or-before the target frame.
    // Same upper_bound trick as SeekToInternal.
    const int64_t atOrBefore = TimelineIndexAtOrBefore(targetTime);
    if (atOrBefore < 0)
    {
        // Target precedes the first checkpoint — nothing to scan.
        reportWindow(targetTime, targetTime);
        return std::nullopt;
    }
    const size_t targetCpIdx = static_cast<size_t>(atOrBefore);

    TTDSearchResult answer;
    bool found = false;
    bool blocked = false;

    // Write journal segments (D40). A frame runs from where its checkpoint's
    // CPU stood (the frame before ended past its boundary) to where the next
    // one's stands: (startT(i), startT(i + 1)]
    const TTDWriteIndex& writes = _engine->Writes();
    const std::vector<TTDJournalSegment> segments =
        q.access == TTDAccessType::Write ? writes.Segments() : std::vector<TTDJournalSegment>{};
    auto startT = [&](size_t index) { return CheckpointStartT(_timeline[index]); };
    auto segmentOf = [&](uint64_t after, uint64_t upTo) -> int {
        for (size_t k = 0; k < segments.size(); ++k)
            if (segments[k].from <= after && upTo <= segments[k].to)
                return static_cast<int>(k);
        return -1;
    };
    auto journalPred = [&](const TTDWriteRecord& rec) -> bool {
        if (rec.isIo) return false;
        if (rec.addr < q.addrFrom || rec.addr > q.addrTo) return false;
        if (q.hasValueFilter && rec.value != q.value) return false;
        if (q.hasPcFilter && (rec.m1pc < q.pcFrom || rec.m1pc > q.pcTo)) return false;
        if (q.hasPhysPageFilter && rec.physPage != q.physPage) return false;   // bank-aware (TDD §9.4)
        return true;
    };

    // Walk backward from the target checkpoint to the beginning.
    for (size_t i = targetCpIdx + 1; i-- > 0; )
    {
        const TTDCheckpoint& cp = _timeline[i];

        // Inside a journal segment: the whole run of covered frames at once
        if (!segments.empty())
        {
            const uint64_t upTo = i == targetCpIdx ? beforeGlobalT : startT(i + 1);
            const int seg = segmentOf(startT(i), upTo);
            if (seg >= 0)
            {
                size_t first = i;
                while (first > 0 && segmentOf(startT(first - 1), startT(first)) == seg)
                    --first;
                if (auto rec = writes.FindLastInRange(startT(first), upTo, journalPred))
                {
                    answer.time = TimePointAt(rec->globalT);
                    answer.pc = rec->m1pc;
                    answer.value = rec->value;
                    answer.physPage = PhysPage{rec->physPage};
                    answer.access = TTDAccessType::Write;
                    answer.addr = rec->addr;
                    reportWindow(answer.time, std::min(TimePointAt(beforeGlobalT), SessionEndPosition()));
                    return answer;   // from the journal: the machine stays where it is
                }
                i = first;   // the loop steps to the frame before the run
                continue;
            }
        }

        // Interval end: for the target checkpoint, it's the target position;
        // for earlier checkpoints, it's the full frame.
        uint32_t replayEndT;
        if (i == targetCpIdx)
            replayEndT = targetTime.tInFrame;
        else
            replayEndT = frameT;

        // Skip zero-length interval (target exactly at frame boundary).
        if (replayEndT == 0 && i == targetCpIdx)
            continue;

        // Check external-event markers in this interval.
        TTDTimePoint intervalEnd;
        intervalEnd.frame    = cp.time.frame;
        intervalEnd.tInFrame = replayEndT;

        if (const std::optional<TTDExternalEvent> barrier = FirstBarrierBetween(cp.time, intervalEnd))
        {
            MLOGINFO("TimeTravelController::FindLastAccess — marker barrier at "
                     "(frame=%llu, tInFrame=%u) blocks interval %zu",
                     static_cast<unsigned long long>(barrier->time.frame),
                     static_cast<unsigned>(barrier->time.tInFrame),
                     i);
            if (outBlockingMarker)
                *outBlockingMarker = *barrier;
            reportWindow(barrier->time, targetTime);
            blocked = true;
            break;  // Can't replay past a marker — stop searching.
        }

        // Coverage-index prune. Restoring a checkpoint and replaying a frame is
        // the expensive part of this loop (~1.3 ms), and most frames cannot
        // possibly contain the access being searched for — measured densities
        // are 7.3% for execution and 1.2% for writes. Asking the index first
        // turns "replay everything backwards" into "replay only candidates".
        //
        // The check is conservative in both directions that matter: it prunes
        // only frames the index actually watched, and only when the query's
        // address range maps onto a single non-wrapping offset interval. Any
        // uncertainty answers "maybe" and the frame gets replayed as before.
        if (CanPruneByCoverage(q))
        {
            const uint16_t offsetLow  = static_cast<uint16_t>(q.addrFrom & 0x3FFF);
            const uint16_t offsetHigh = static_cast<uint16_t>(q.addrTo & 0x3FFF);
            const TTDCoverageKind coverageKind = q.access == TTDAccessType::Execute ? TTDCoverageKind::Executed
                                                 : q.access == TTDAccessType::Write ? TTDCoverageKind::Written
                                                                                    : TTDCoverageKind::Read;

            if (!_coverageIndex.FrameMayContain(coverageKind, cp.time.frame,
                                                offsetLow, offsetHigh,
                                                q.hasPhysPageFilter, q.physPage))
            {
                continue;  // Proven absent — no restore, no replay.
            }
        }

        // Restore checkpoint silently (z80.t = its overshoot, see RestoreCheckpoint)
        RestoreCheckpointForReplay(cp);

        // Arm probe, replay, extract hits.
        _context->ttdProbe.Reset();
        _context->ttdProbe.Arm(q);

        ReplayModeScope replay(*this);
        ReplayWithinFrame(cp.time.frame, replayEndT);
        replay.Exit();

        auto hits = _context->ttdProbe.ExtractHits();
        _context->ttdProbe.Disarm();
        // The replay finishes the instruction that crosses the target: its
        // accesses after the target are not "before" it
        while (!hits.empty() && GlobalT(hits.back().time) > beforeGlobalT)
            hits.pop_back();

        if (!hits.empty())
        {
            answer = hits.back();  // Last hit in the interval = closest to target.
            found = true;
            break;  // First interval (walking backward) with a hit is the answer.
        }
    }

    if (!found)
    {
        if (!blocked)
            reportWindow(sessionStart, targetTime);  // examined everything back to the start
        return std::nullopt;
    }

    reportWindow(answer.time, targetTime);

    // Position the emulator at the answer.
    SeekTo(answer.time);

    MLOGINFO("TimeTravelController::FindLastAccess — match at (frame=%llu, tInFrame=%u) "
             "pc=0x%04X value=0x%02X access=%s",
             static_cast<unsigned long long>(answer.time.frame),
             static_cast<unsigned>(answer.time.tInFrame),
             answer.pc, answer.value,
             TTDAccessTypeToString(answer.access));

    return answer;
}

bool TimeTravelController::StepBackInstruction()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // Browsing while recording pauses the recording (D8): resumed where it paused, it goes on
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::StepBackInstruction — no recorded history");
        return false;
    }

    // Find the most recent Execute (M1) access strictly before the current
    // position. That is the previous instruction boundary.
    const TTDTimePoint now = CurrentPosition();
    const uint64_t nowGlobalT = GlobalT(now);

    // Refuse at position (0, 0) — no prior instruction exists.
    if (now.frame == 0 && now.tInFrame == 0)
    {
        MLOGINFO("TimeTravelController::StepBackInstruction — already at session start");
        return false;
    }

    TTDSearchQuery q;
    q.access        = TTDAccessType::Execute;
    q.addrFrom      = 0;
    q.addrTo        = 0xFFFF;
    q.beforeGlobalT = nowGlobalT > 0 ? nowGlobalT - 1 : 0;

    auto result = FindLastAccess(q);
    if (!result)
    {
        MLOGINFO("TimeTravelController::StepBackInstruction — no prior instruction found");
        return false;
    }

    // FindLastAccess already SeekTo'd the answer.
    return true;
}

bool TimeTravelController::StepForwardInstruction()
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // Browsing while recording pauses the recording (D8): resumed where it paused, it goes on
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::StepForwardInstruction — no recorded history");
        return false;
    }

    const TTDTimePoint now        = CurrentPosition();
    const TTDTimePoint sessionEnd = SessionEndPosition();

    // Refuse if at or past the session end.
    if (now.frame > sessionEnd.frame ||
        (now.frame == sessionEnd.frame && now.tInFrame >= sessionEnd.tInFrame))
    {
        MLOGINFO("TimeTravelController::StepForwardInstruction — at or past session end");
        return false;
    }

    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelController::StepForwardInstruction — null context or emulator");
        return false;
    }

    // Run exactly one instruction via silent replay. RunTStates(1, true)
    // enters the Z80Step loop once — Z80Step executes one complete
    // instruction regardless of its t-state length.
    ReplayModeScope replay(*this);
    _context->pEmulator->RunTStates(1, true);
    replay.Exit();

    PresentPosition(false);
    return true;
}

// ===========================================================================
// Phase 4 reverse execution: M1 enumeration + ReverseStep / ReverseContinue
// ===========================================================================
//
// The single-opcode StepBackInstruction above calls FindLastAccess(Execute)
// which restores a checkpoint + replays forward — once per call. For N
// backward opcodes, that's N independent restore+replay passes.
//
// The reverse-execution primitives do ONE restore+replay pass over the
// whole interval of interest, recording every M1 cycle in a vector, then
// index/scan the result. Strategy thresholds are pinned by
// `core/benchmarks/debugger/ttd/ttd_reverse_benchmark.cpp`.
// ===========================================================================

TimeTravelController::EnumerateResult
TimeTravelController::EnumerateM1InRange(uint64_t startGlobalT,
                                      uint64_t endGlobalT,
                                      std::vector<TTDM1Record>& outM1s,
                                      TTDExternalEvent* outBlockingMarkerStorage)
{
    EnumerateResult result;
    if (outBlockingMarkerStorage)
        *outBlockingMarkerStorage = TTDExternalEvent{};

    outM1s.clear();

    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelController::EnumerateM1InRange — null _context or pEmulator");
        return result;
    }
    if (_timeline.empty())
    {
        MLOGINFO("TimeTravelController::EnumerateM1InRange — empty timeline");
        return result;
    }
    if (startGlobalT >= endGlobalT)
    {
        MLOGINFO("TimeTravelController::EnumerateM1InRange — empty interval "
                 "(start=%llu, end=%llu)",
                 static_cast<unsigned long long>(startGlobalT),
                 static_cast<unsigned long long>(endGlobalT));
        return result;
    }

    const uint32_t frameT = FrameSpan();  // TTD time units per frame (B4)
    const uint64_t sessionEndGlobalT =
        GlobalT(_timeline.back().time);
    if (endGlobalT > sessionEndGlobalT)
        endGlobalT = sessionEndGlobalT;
    if (startGlobalT >= endGlobalT)
        return result;

    // Decompose endpoints into (frame, tInFrame).
    TTDTimePoint startTime;
    startTime = TimePointAt(startGlobalT);

    TTDTimePoint endTime;
    endTime = TimePointAt(endGlobalT);

    // Find the checkpoint at-or-before endTime. This is the latest interval
    // we'll scan. (Same upper_bound pattern as SeekToInternal.)
    const int64_t endAtOrBefore = TimelineIndexAtOrBefore(endTime);
    if (endAtOrBefore < 0)
    {
        // endTime precedes the first checkpoint — nothing to scan.
        return result;
    }
    const size_t endCpIdx = static_cast<size_t>(endAtOrBefore);

    // Walk backward from endCpIdx, collecting M1 records in each interval.
    // Hits get prepended to keep `outM1s` sorted ascending by globalT.
    // We stop when we either (a) hit the startGlobalT boundary or (b) hit
    // a marker barrier.
    bool hitBarrier = false;
    for (size_t i = endCpIdx + 1; i-- > 0; )
    {
        const TTDCheckpoint& cp = _timeline[i];

        // Interval end for this checkpoint. For the endCpIdx we use
        // endTime.tInFrame; for earlier checkpoints the whole frame.
        uint32_t replayEndT = (i == endCpIdx) ? endTime.tInFrame : frameT;

        // Skip zero-length interval (e.g. endTime exactly at frame boundary).
        if (replayEndT == 0 && i == endCpIdx)
            continue;

        // Marker barrier check (same logic as FindLastAccess).
        TTDTimePoint intervalEnd;
        intervalEnd.frame    = cp.time.frame;
        intervalEnd.tInFrame = replayEndT;
        if (const std::optional<TTDExternalEvent> found = FirstBarrierBetween(cp.time, intervalEnd))
        {
            _barrierScratch = *found;
            const TTDExternalEvent* barrier = &_barrierScratch;
            MLOGINFO("TimeTravelController::EnumerateM1InRange — marker barrier at "
                     "(frame=%llu, tInFrame=%u) blocks interval %zu",
                     static_cast<unsigned long long>(barrier->time.frame),
                     static_cast<unsigned>(barrier->time.tInFrame), i);
            result.barrier = barrier;
            if (outBlockingMarkerStorage)
                *outBlockingMarkerStorage = *barrier;
            hitBarrier = true;
            break;
        }

        // Record the earliest globalT actually scanned (for the caller's
        // "we covered this much" logic).
        const uint64_t intervalStartGlobalT =
            GlobalT(TTDTimePoint{cp.time.frame, 0});
        result.earliestScannedGlobalT =
            (i == 0) ? intervalStartGlobalT : result.earliestScannedGlobalT;
        if (i == endCpIdx)
            result.earliestScannedGlobalT = intervalStartGlobalT;

        // Restore the checkpoint (z80.t = its overshoot, see RestoreCheckpoint)
        RestoreCheckpointForReplay(cp);

        // Arm probe with Execute + full address range, no value/PC filter.
        TTDSearchQuery q;
        q.access        = TTDAccessType::Execute;
        q.addrFrom      = 0;
        q.addrTo        = 0xFFFF;
        q.beforeGlobalT = UINT64_MAX;
        _context->ttdProbe.Reset();
        _context->ttdProbe.Arm(q);

        ReplayModeScope replay(*this);
        ReplayWithinFrame(cp.time.frame, replayEndT);
        replay.Exit();

        auto hits = _context->ttdProbe.ExtractHits();
        _context->ttdProbe.Disarm();

        // Convert hits (TTDSearchResult) → TTDM1Record and prepend to keep
        // ascending time order.
        std::vector<TTDM1Record> intervalM1s;
        intervalM1s.reserve(hits.size());
        for (const TTDSearchResult& h : hits)
        {
            TTDM1Record m1;
            m1.globalT  = GlobalT(h.time);
            m1.pc       = h.pc;
            m1.physPage = h.physPage;
            intervalM1s.push_back(m1);
        }

        if (!intervalM1s.empty())
        {
            // Prepend. (vector::insert at begin is O(N) but the cumulative
            // size across all intervals stays bounded by total instructions
            // in the session — typically a few thousand for the magnitudes
            // we're called with. A deque would be marginally faster but the
            // outM1s storage is also consumed as a vector by callers, so the
            // conversion would cost the same.)
            outM1s.insert(outM1s.begin(),
                          std::make_move_iterator(intervalM1s.begin()),
                          std::make_move_iterator(intervalM1s.end()));
        }

        // If this checkpoint's frame is at or before startTime.frame, we've
        // reached the lower bound — stop scanning further back.
        if (cp.time.frame <= startTime.frame)
            break;
    }

    // Trim leading M1s whose globalT < startGlobalT. (The earliest interval
    // may have captured instructions before startGlobalT; drop them.)
    while (!outM1s.empty() && outM1s.front().globalT < startGlobalT)
        outM1s.erase(outM1s.begin());

    MLOGINFO("TimeTravelController::EnumerateM1InRange — enumerated %zu M1 records "
             "in [%llu, %llu)%s",
             outM1s.size(),
             static_cast<unsigned long long>(startGlobalT),
             static_cast<unsigned long long>(endGlobalT),
             hitBarrier ? " (stopped at barrier)" : "");
    (void)hitBarrier;
    return result;
}

bool TimeTravelController::ReverseStepInstructions(uint32_t n)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    if (n == 0)
    {
        MLOGWARNING("TimeTravelController::ReverseStepInstructions — n=0 is a no-op");
        return true;
    }

    // State guards — same shape as StepBackInstruction.
    // Browsing while recording pauses the recording (D8): resumed where it paused, it goes on
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::ReverseStepInstructions — no recorded history");
        return false;
    }
    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelController::ReverseStepInstructions — null _context or pEmulator");
        return false;
    }

    // Strategy A: small n delegates to repeated StepBackInstruction.
    if (n <= kReverseSeqStepMaxN)
    {
        for (uint32_t i = 0; i < n; ++i)
        {
            if (!StepBackInstruction())
            {
                MLOGINFO("TimeTravelController::ReverseStepInstructions — ran out of "
                         "history after %u/%u steps", i, n);
                return false;
            }
        }
        return true;
    }

    // Strategy B: M1 enumeration + index Nth-from-end.
    const TTDTimePoint now = CurrentPosition();
    const uint32_t frameT  = FrameSpan();  // TTD time units per frame (B4)
    const uint64_t nowGlobalT =
        GlobalT(now);

    if (now.frame == 0 && now.tInFrame == 0)
    {
        MLOGINFO("TimeTravelController::ReverseStepInstructions — already at session start");
        return false;
    }

    // Estimate a safe lower bound for the enumeration window. The longest
    // Z80 instruction is 23 cycles, so N instructions fit in N*23 t-states.
    // We add one frame of slack to absorb estimation error at frame
    // boundaries (the leading-trim in EnumerateM1InRange drops over-scan).
    const uint64_t estimatedTStates = static_cast<uint64_t>(n) * 23;
    const uint64_t slack            = frameT;
    uint64_t startGlobalT = (estimatedTStates >= nowGlobalT)
                            ? 0
                            : (nowGlobalT - estimatedTStates);
    if (startGlobalT >= slack)
        startGlobalT -= slack;
    else
        startGlobalT = 0;

    // Enumerate M1s in [startGlobalT, nowGlobalT). The end is exclusive in
    // our intent (we want M1s strictly before the current position), so we
    // subtract 1 from nowGlobalT to exclude the M1 at the current position
    // itself (matches StepBackInstruction's beforeGlobalT = nowGlobalT - 1).
    const uint64_t endGlobalT = nowGlobalT > 0 ? nowGlobalT - 1 : 0;
    if (endGlobalT == 0)
        return false;  // At session start — caller should've been caught above.

    std::vector<TTDM1Record> m1s;
    TTDExternalEvent barrierStorage;
    EnumerateM1InRange(startGlobalT, endGlobalT, m1s, &barrierStorage);

    if (m1s.size() < n)
    {
        MLOGINFO("TimeTravelController::ReverseStepInstructions — only %zu M1s in "
                 "range, need %u",
                 m1s.size(), n);
        return false;
    }

    // Nth-from-end (0-indexed: size - n).
    const size_t targetIdx = m1s.size() - n;
    const TTDM1Record& target = m1s[targetIdx];

    TTDTimePoint targetTime;
    targetTime = TimePointAt(target.globalT);

    if (!SeekTo(targetTime))
    {
        MLOGWARNING("TimeTravelController::ReverseStepInstructions — SeekTo(target) failed");
        return false;
    }

    MLOGINFO("TimeTravelController::ReverseStepInstructions — stepped back %u opcodes "
             "to (frame=%llu, tInFrame=%u) pc=0x%04X",
             n,
             static_cast<unsigned long long>(targetTime.frame),
             static_cast<unsigned>(targetTime.tInFrame),
             target.pc);
    return true;
}

bool TimeTravelController::ReverseStepTStates(uint64_t n)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // State guards.
    // Browsing while recording pauses the recording (D8): resumed where it paused, it goes on
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::ReverseStepTStates — no recorded history");
        return false;
    }
    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelController::ReverseStepTStates — null _context or pEmulator");
        return false;
    }

    const TTDTimePoint now = CurrentPosition();
    const uint32_t frameT  = FrameSpan();  // TTD time units per frame (B4)
    const uint64_t nowGlobalT =
        GlobalT(now);

    if (n >= nowGlobalT)
    {
        MLOGINFO("TimeTravelController::ReverseStepTStates — n=%llu >= nowGlobalT=%llu "
                 "(would step past session start)",
                 static_cast<unsigned long long>(n),
                 static_cast<unsigned long long>(nowGlobalT));
        return false;
    }

    const uint64_t targetGlobalT = nowGlobalT - n;

    // Enumerate M1s in a window around target. Start one frame below target
    // to ensure the M1 ≤ target is captured (instructions span up to 23
    // t-states, so the M1 ≤ target.globalT could be up to 23 t-states earlier).
    uint64_t startGlobalT = (targetGlobalT > frameT) ? (targetGlobalT - frameT) : 0;

    std::vector<TTDM1Record> m1s;
    TTDExternalEvent barrierStorage;
    EnumerateM1InRange(startGlobalT, nowGlobalT, m1s, &barrierStorage);

    // Find the last M1 whose globalT <= targetGlobalT.
    auto it = std::find_if(m1s.rbegin(), m1s.rend(),
        [&](const TTDM1Record& m) { return m.globalT <= targetGlobalT; });
    if (it == m1s.rend())
    {
        MLOGINFO("TimeTravelController::ReverseStepTStates — no M1 ≤ targetGlobalT=%llu "
                 "in window",
                 static_cast<unsigned long long>(targetGlobalT));
        return false;
    }

    const TTDM1Record& target = *it;
    TTDTimePoint targetTime;
    targetTime = TimePointAt(target.globalT);

    if (!SeekTo(targetTime))
    {
        MLOGWARNING("TimeTravelController::ReverseStepTStates — SeekTo(target) failed");
        return false;
    }

    MLOGINFO("TimeTravelController::ReverseStepTStates — stepped back %llu t-states "
             "to (frame=%llu, tInFrame=%u) pc=0x%04X",
             static_cast<unsigned long long>(n),
             static_cast<unsigned long long>(targetTime.frame),
             static_cast<unsigned>(targetTime.tInFrame),
             target.pc);
    return true;
}

TimeTravelController::TTDReverseContinueResult
TimeTravelController::ReverseContinue(const std::vector<uint16_t>& breakpoints)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDReverseContinueResult result;

    // Browsing while recording pauses the recording (D8): resumed where it paused, it goes on
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelController::ReverseContinue — no recorded history");
        return result;
    }
    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelController::ReverseContinue — null _context or pEmulator");
        return result;
    }
    if (breakpoints.empty())
    {
        MLOGINFO("TimeTravelController::ReverseContinue — empty breakpoint set, no-op");
        return result;
    }

    const TTDTimePoint now = CurrentPosition();
    const uint64_t nowGlobalT =
        GlobalT(now);

    // TD-8: the scan walks back from `now` and ends at the match, at a barrier
    // it cannot replay across, or at the session start.
    const TTDTimePoint sessionStart = _timeline.front().time;
    auto reportWindow = [&result, &now](const TTDTimePoint& from)
    {
        result.window.searched = true;
        result.window.from = from;
        result.window.to = now;
    };

    // ------------------------------------------------------------------
    // Fast path: let the coverage index nominate candidate frames.
    //
    // The fallback below enumerates every M1 cycle from session start, which
    // means silently replaying the entire recording — measured at 68-168 ms and
    // growing without bound as the session gets longer. The index already knows
    // which frames executed which addresses, so instead of replaying everything
    // we replay one candidate frame at a time, newest first, and stop at the
    // first real hit.
    //
    // The index is keyed by (physical page, offset within page), while a
    // breakpoint is a bare Z80 address. Asking "any page, this offset"
    // over-approximates — four Z80 addresses share an offset — so a candidate
    // frame may turn out not to contain the PC at all. That costs one wasted
    // frame enumeration and is retried on the next candidate; it can never
    // skip a real hit, which is the only direction that would be wrong.
    // ------------------------------------------------------------------
    uint64_t coverFirst = 0, coverLast = 0;
    const bool coverageUsable =
        _enableCoverageIndex &&
        _coverageIndex.CoveredRange(TTDCoverageKind::Executed, coverFirst, coverLast);
    // Coverage may reach back before the history limit's start
    if (!_timeline.empty())
        coverFirst = std::max(coverFirst, _timeline.front().time.frame);

    if (coverageUsable)
    {
        const uint64_t scanFrom = std::min<uint64_t>(now.frame, coverLast);

        // Note on degradation. With a large breakpoint set the index stops
        // pruning — it is keyed by offset-within-page, so each breakpoint
        // aliases four Z80 addresses and nearly every frame becomes a
        // candidate. That is not a cliff: enumerating N frames one at a time
        // costs about the same replay work as enumerating them in one pass,
        // differing only by N checkpoint restores. Measured with 100
        // breakpoints: 65 ms per-frame against 76 ms bulk. An explicit
        // bail-out to the bulk path was tried and made it worse (97 ms),
        // because the fruitless per-frame work is then paid twice.

        for (uint64_t frame = scanFrom + 1; frame-- > coverFirst;)
        {
            bool candidate = false;
            for (uint16_t bp : breakpoints)
            {
                const uint16_t offset = static_cast<uint16_t>(bp & 0x3FFF);
                if (_coverageIndex.FrameMayContain(TTDCoverageKind::Executed, frame,
                                                   offset, offset,
                                                   /*hasPage=*/false, 0))
                {
                    candidate = true;
                    break;
                }
            }
            if (!candidate)
            {
                if (frame == 0) break;
                continue;
            }

            // Enumerate only this frame, clipped to the current position.
            const uint64_t frameStart = GlobalT(TTDTimePoint{frame, 0});
            const uint64_t frameEnd =
                std::min<uint64_t>(nowGlobalT, GlobalT(TTDTimePoint{frame + 1, 0}));
            if (frameStart >= frameEnd)
            {
                if (frame == 0) break;
                continue;
            }

            std::vector<TTDM1Record> frameM1s;
            TTDExternalEvent frameBarrier;
            EnumerateM1InRange(frameStart, frameEnd, frameM1s, &frameBarrier);

            auto hit = std::find_if(frameM1s.rbegin(), frameM1s.rend(),
                [&](const TTDM1Record& m) {
                    return std::find(breakpoints.begin(), breakpoints.end(), m.pc)
                           != breakpoints.end();
                });

            if (hit != frameM1s.rend())
            {
                result.matched = true;
                result.pc      = hit->pc;
                result.arrivedAt = TimePointAt(hit->globalT);
                reportWindow(result.arrivedAt);
                if (frameBarrier.reason[0] != '\0')
                    result.blockingMarker = frameBarrier;

                if (!SeekTo(result.arrivedAt))
                {
                    MLOGWARNING("TimeTravelController::ReverseContinue — SeekTo(arrivedAt) failed");
                    result.matched = false;
                }
                return result;
            }

            if (frameBarrier.reason[0] != '\0')
            {
                // The index says a breakpoint PC ran in this frame, but a marker
                // keeps the frame from being replayed. A run there would be later
                // than anything an older frame holds, so the scan must stop here
                // - like the unindexed scan - instead of answering with an older
                // hit. (Frames the index rules out are skipped even across
                // markers: the index was captured live, not by replay.)
                result.blockingMarker = frameBarrier;
                reportWindow(frameBarrier.time);
                MLOGINFO("TimeTravelController::ReverseContinue — barrier at (frame=%llu, tInFrame=%u) "
                         "blocks candidate frame %llu",
                         static_cast<unsigned long long>(frameBarrier.time.frame),
                         static_cast<unsigned>(frameBarrier.time.tInFrame),
                         static_cast<unsigned long long>(frame));
                return result;
            }

            if (frame == 0)
                break;
        }

        {

        // The indexed range does not necessarily start where the session does.
        // Coverage is sealed at frame boundaries, so the first recorded frame
        // has no entry: the index covers [coverFirst, coverLast] while the
        // timeline starts at coverFirst - 1 or earlier. Frames below coverFirst
        // are UNKNOWN, not empty, and skipping them silently loses hits that
        // happened during the session's opening frames.
        const uint64_t prefixEnd = std::min<uint64_t>(nowGlobalT, GlobalT(TTDTimePoint{coverFirst, 0}));
        if (prefixEnd > 0)
        {
            std::vector<TTDM1Record> prefixM1s;
            TTDExternalEvent prefixBarrier;
            EnumerateM1InRange(0, prefixEnd, prefixM1s, &prefixBarrier);

            auto hit = std::find_if(prefixM1s.rbegin(), prefixM1s.rend(),
                [&](const TTDM1Record& m) {
                    return std::find(breakpoints.begin(), breakpoints.end(), m.pc)
                           != breakpoints.end();
                });

            if (hit != prefixM1s.rend())
            {
                result.matched = true;
                result.pc      = hit->pc;
                result.arrivedAt = TimePointAt(hit->globalT);
                reportWindow(result.arrivedAt);
                if (prefixBarrier.reason[0] != '\0')
                    result.blockingMarker = prefixBarrier;

                if (!SeekTo(result.arrivedAt))
                {
                    MLOGWARNING("TimeTravelController::ReverseContinue — SeekTo(arrivedAt) failed");
                    result.matched = false;
                }
                return result;
            }

            if (prefixBarrier.reason[0] != '\0')
            {
                result.blockingMarker = prefixBarrier;
                reportWindow(prefixBarrier.time);
                return result;
            }
        }

            reportWindow(sessionStart);
            MLOGINFO("TimeTravelController::ReverseContinue — no PC match in the indexed range "
                     "nor in the unindexed prefix");
            return result;
        }
    }

    // Enumerate M1s from session start up to (but not including) now.
    std::vector<TTDM1Record> m1s;
    TTDExternalEvent barrierStorage;
    EnumerateM1InRange(0, nowGlobalT, m1s, &barrierStorage);

    // If we hit a barrier, surface it to the caller.
    if (barrierStorage.reason[0] != '\0')
    {
        result.blockingMarker = barrierStorage;
        MLOGINFO("TimeTravelController::ReverseContinue — barrier at (frame=%llu, tInFrame=%u) "
                 "halted the scan",
                 static_cast<unsigned long long>(barrierStorage.time.frame),
                 static_cast<unsigned>(barrierStorage.time.tInFrame));
    }

    // Scan backward for the first PC match.
    auto it = std::find_if(m1s.rbegin(), m1s.rend(),
        [&](const TTDM1Record& m) {
            return std::find(breakpoints.begin(), breakpoints.end(), m.pc)
                   != breakpoints.end();
        });
    if (it == m1s.rend())
    {
        reportWindow(barrierStorage.reason[0] != '\0' ? barrierStorage.time : sessionStart);
        MLOGINFO("TimeTravelController::ReverseContinue — no PC match in %zu M1s",
                 m1s.size());
        return result;
    }

    result.matched    = true;
    result.pc         = it->pc;
    result.arrivedAt = TimePointAt(it->globalT);
    reportWindow(result.arrivedAt);

    if (!SeekTo(result.arrivedAt))
    {
        MLOGWARNING("TimeTravelController::ReverseContinue — SeekTo(arrivedAt) failed");
        result.matched = false;
        return result;
    }

    MLOGINFO("TimeTravelController::ReverseContinue — hit PC=0x%04X at "
             "(frame=%llu, tInFrame=%u)",
             result.pc,
             static_cast<unsigned long long>(result.arrivedAt.frame),
             static_cast<unsigned>(result.arrivedAt.tInFrame));
    return result;
}

// ===========================================================================
// Per-frame decode cache (reverse-browsing accelerator)
// ===========================================================================

void TimeTravelController::ClearFrameCache()
{
    _frameCache.reset();          // frees the entries vector
    _capturingCache = nullptr;
    _frameCaptureActive = false;
}

void TimeTravelController::CaptureM1(uint16_t pc)
{
    if (!_capturingCache || !_context)
        return;

    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (!z80 || !_memory)
        return;

    TTDFrameCacheEntry e;
    e.tInFrame = _context->emulatorState.TtdTInFrame(z80->t);
    e.pc  = pc;
    e.sp  = z80->sp;
    e.af  = z80->af;
    e.bc  = z80->bc;
    e.de  = z80->de;
    e.hl  = z80->hl;
    e.ix  = z80->ix;
    e.iy  = z80->iy;
    e.af2 = z80->alt.af;
    e.bc2 = z80->alt.bc;
    e.de2 = z80->alt.de;
    e.hl2 = z80->alt.hl;
    e.i   = z80->i;
    e.r   = z80->r_low;
    e.im  = z80->im;

    for (int i = 0; i < 4; ++i)
        e.opcodes[i] = _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(pc + i));
    uint16_t lo = _memory->DirectReadFromZ80Memory(z80->sp);
    uint16_t hi = _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(z80->sp + 1));
    e.spContent = static_cast<uint16_t>(lo | (hi << 8));

    // 128K-style 4-slot view (ROM page + RAM banks 1..3). The DZRP layer maps
    // these to DeZog's model; a 48K machine still reports 4 here harmlessly.
    e.slotCount = 4;
    e.bankSlots[0] = static_cast<uint8_t>(_memory->GetROMPage());
    e.bankSlots[1] = static_cast<uint8_t>(_memory->GetRAMPageForBank1());
    e.bankSlots[2] = static_cast<uint8_t>(_memory->GetRAMPageForBank2());
    e.bankSlots[3] = static_cast<uint8_t>(_memory->GetRAMPageForBank3());

    // Accesses of this instruction pack contiguously into the shared arena,
    // starting at the current arena end. The write hooks below append and bump
    // accessCount until the next M1 begins a new entry.
    e.accessOffset = static_cast<uint32_t>(_capturingCache->accesses.size());
    e.accessCount = 0;

    _capturingCache->entries.push_back(e);
}

void TimeTravelController::BuildFrameCache(uint64_t frame, TTDFrameCache& out)
{
    out.frame = frame;
    out.entries.clear();
    out.accesses.clear();

    Emulator* emu = _context ? _context->pEmulator : nullptr;
    Z80* z80 = (_context && _context->pCore) ? _context->pCore->GetZ80() : nullptr;
    if (!emu || !z80)
        return;

    const uint32_t frameT = _context->config.frame;

    // Upper bound on instructions in a frame. A frame is config.frame t-states
    // at 1x, and scales with the CPU frequency multiplier (turbo runs more
    // instructions per frame). The shortest Z80 instruction is
    // kMinInstructionTStates long, so at most (frameT * multiplier) / that many
    // instructions — hence records — fit in the largest frame. Reserve once so
    // the fill never reallocates mid-capture; on a reused cache block these are
    // no-ops (capacity retained). The arena is one pre-allocated segment packed
    // sequentially each build — no fragmentation to manage.
    const size_t frameTStates = _context->emulatorState.BaseToCpuT(frameT);
    // Ceiling division (an instruction can straddle the frame boundary) + margin.
    const size_t maxInstrPerFrame =
        (frameTStates + kMinInstructionTStates - 1) / kMinInstructionTStates + kFrameReserveMargin;
    out.entries.reserve(maxInstrPerFrame);
    out.accesses.reserve(maxInstrPerFrame);  // ~≤1 write/instruction typical; grows if exceeded

    // Restore the start of the target frame (Detached).
    if (!SeekToInternal(TTDTimePoint{frame, 0}, nullptr))
        return;

    // Install the M1 capture hook for the duration of the replay only.
    auto prevHook = z80->m1TraceHook;
    _capturingCache = &out;
    _frameCaptureActive = true;
    z80->m1TraceHook = [this](uint16_t pc) { CaptureM1(pc); };

    // Replay the rest of the frame forward. A frame is frameT t-states at 1x
    // and scales with the CPU frequency multiplier (turbo runs more
    // instructions per frame — e.g. 16x at 56 MHz), so run to the scaled
    // frame end, otherwise a turbo frame would be captured only
    // 1/multiplier of the way. The frame starts where the restore left the
    // CPU - the previous frame's overshoot past the boundary, not 0 - so
    // only the remainder is run (running a full frame spilled into the next)
    const uint32_t startT = static_cast<uint32_t>(z80->t);
    if (startT < frameTStates)
        emu->RunTStates(static_cast<unsigned>(frameTStates - startT), /*skipBreakpoints=*/true);

    z80->m1TraceHook = prevHook;
    _frameCaptureActive = false;
    _capturingCache = nullptr;
}

void TimeTravelController::SaveLiveState(LiveStateSnapshot& out)
{
    Z80* z80 = (_context && _context->pCore) ? _context->pCore->GetZ80() : nullptr;
    if (z80)
    {
        out.cpu = CaptureCpuState(*static_cast<Z80State*>(z80));
        out.cpu.nmi_pending = z80->IsNmiPending() ? 1 : 0;
    }
    out.chipset = CaptureChipsetState(_context->emulatorState, z80 ? static_cast<uint32_t>(z80->t) : 0u);
    CaptureBankOverrides(out.bankOverrides);
    if (_context->pScreen)
    {
    }
    // z80.t (the per-frame t-state counter) is not part of TTDCpuState; a
    // live snapshot can sit anywhere inside a frame, so it is kept here
    // (checkpoints carry it in TTDChipsetState::cpu_t_in_frame).
    out.z80TInFrame = z80 ? z80->t : 0;

    // Peripherals — the same registry, and therefore the same blobs, the
    // checkpoints carry.
    _peripherals.CaptureAll(out.peripheralBlobs);
    out.inputCursor = _inputCursor;
    out.inputPlaybackArmed = _inputPlaybackArmed;
    out.portReadMode = _portReads.GetMode();
    out.portReadCursor = _portReads.Cursor();
    out.portWriteMode = _portWrites.GetMode();
    out.portWriteCursor = _portWrites.Cursor();

    out.hasKeyboard = _context->pKeyboard != nullptr;
    if (out.hasKeyboard)
        out.keyboard = _context->pKeyboard->CaptureInputState();

    out.framebuffer.clear();
    if (_context->pScreen)
    {
        out.screenPrevTstate = _context->pScreen->GetPrevTstate();
        uint32_t* fb = nullptr;
        size_t fbSize = 0;
        _context->pScreen->GetFramebufferData(&fb, &fbSize);
        if (fb && fbSize)
            out.framebuffer.assign(reinterpret_cast<const uint8_t*>(fb),
                                   reinterpret_cast<const uint8_t*>(fb) + fbSize);

        size_t planeBCount = 0;
        const uint16_t* planeB = _context->pScreen->GetPlaneB(&planeBCount);
        if (planeB)
            out.planeB.assign(planeB, planeB + planeBCount);
        else
            out.planeB.clear();
    }

    // Full RAM copy (model pages only — e.g. 128 KB on a 128K model). The
    // build replay overwrites live RAM with historic content as it runs, so
    // only a verbatim copy restores the caller's memory exactly.
    if (_memory)
    {
        const size_t pageBytes = 4 * kTTDPieceSize;  // 16 KB
        out.ram.resize(static_cast<size_t>(_modelRamPages) * pageBytes);
        for (uint16_t p = 0; p < _modelRamPages; ++p)
        {
            const uint8_t* pageData = _memory->RAMPageAddress(p);
            if (pageData)
                std::memcpy(&out.ram[static_cast<size_t>(p) * pageBytes], pageData, pageBytes);
        }
    }
    else
    {
        out.ram.clear();
    }
}

void TimeTravelController::RestoreLiveState(const LiveStateSnapshot& snap)
{
    Z80* z80 = (_context && _context->pCore) ? _context->pCore->GetZ80() : nullptr;

    // Mirror RestoreCheckpoint's ordering (TDD §8.1): CPU, chipset,
    // peripherals, bank rebuild, RAM, screen resync. Peripherals MUST
    // restore before the bank rebuild — model serializers (Scorpion's
    // #1FFD/ProfROM, ATM paging, ...) restore the latches the paging chain
    // reads, so rebuilding banks first pages from stale values and never
    // re-derives (same reasoning as RestoreCheckpoint's step 2a2).
    if (z80)
    {
        RestoreCpuState(snap.cpu, static_cast<Z80State*>(z80));
        z80->SetNmiPending(snap.cpu.nmi_pending != 0);
    }
    RestoreChipsetState(snap.chipset, &_context->emulatorState);
    if (z80)
    {
        z80->t = snap.z80TInFrame;
        z80->RecomputeFrameTiming();  // geometry follows the restored multiplier
    }
    _peripherals.RestoreAll(snap.peripheralBlobs);
    if (_memory)
    {
        _memory->UpdateZ80Banks();
        ApplyBankOverrides(snap.bankOverrides);
    }

    if (!snap.ram.empty() && _memory)
    {
        const size_t pageBytes = 4 * kTTDPieceSize;  // 16 KB
        const uint16_t pages = static_cast<uint16_t>(std::min<size_t>(
            _modelRamPages, snap.ram.size() / pageBytes));
        for (uint16_t p = 0; p < pages; ++p)
        {
            uint8_t* pageData = _memory->RAMPageAddress(p);
            if (pageData)
                std::memcpy(pageData, &snap.ram[static_cast<size_t>(p) * pageBytes], pageBytes);
        }
    }

    _inputCursor = snap.inputCursor;
    _inputPlaybackArmed = snap.inputPlaybackArmed;
    UpdateInputWorkFlag();
    _portReads.RestorePosition(snap.portReadMode, snap.portReadCursor);
    _portWrites.RestorePosition(snap.portWriteMode, snap.portWriteCursor);
    SyncPortJournalHook();

    if (snap.hasKeyboard && _context->pKeyboard)
        _context->pKeyboard->RestoreInputState(snap.keyboard);

    ResyncScreenState();

    // The snapshot may sit mid-frame: hand back the exact draw cursor, not the
    // frame-start one ResyncScreenState sets.
    if (_context->pScreen)
        _context->pScreen->SetPrevTstate(snap.screenPrevTstate);

    if (!snap.framebuffer.empty() && _context->pScreen)
    {
        uint32_t* fb = nullptr;
        size_t fbSize = 0;
        _context->pScreen->GetFramebufferData(&fb, &fbSize);
        if (fb && fbSize == snap.framebuffer.size())
            std::memcpy(fb, snap.framebuffer.data(), fbSize);
    }

    if (!snap.planeB.empty() && _context->pScreen)
    {
        size_t planeBCount = 0;
        uint16_t* planeB = _context->pScreen->GetPlaneB(&planeBCount);
        if (planeB && planeBCount == snap.planeB.size())
            std::memcpy(planeB, snap.planeB.data(), planeBCount * sizeof(uint16_t));
    }
}

const TTDFrameCache* TimeTravelController::GetFrameCache(uint64_t frame)
{
    // Never build a cache during live recording — the accelerator is a
    // replay-scope facility only. Exception: DebuggerLive while the
    // emulator is paused (reverse-debugging.md §6) — the emulator thread is
    // parked, the build replays under EnterReplayMode (journaling muted) and
    // round-trips live state exactly, so debugger history browsing must not
    // stop the recording to look at it.
    if (_state == TTDSessionState::Recording &&
        !(_recordMode == TTDRecordMode::DebuggerLive && _context && _context->pEmulator &&
          _context->pEmulator->IsPaused()))
    {
        return nullptr;
    }
    if (_timeline.empty())
        return nullptr;
    if (frame > _timeline.back().time.frame)
        return nullptr;

    if (_frameCache && _frameCache->frame == frame)
        return _frameCache.get();

    if (!_context || !_context->pEmulator)
        return nullptr;

    // Reuse the existing cache block across frame crossings: BuildFrameCache
    // clear()s the vectors and reuses their capacity, so the pre-allocated
    // records + arena segments are allocated at most once for the browse scope.
    if (!_frameCache)
        _frameCache = std::make_unique<TTDFrameCache>();

    // Remember where the caller was, then make the build replay transparent
    // via an EXACT snapshot restore. The previous mechanism — SeekToInternal
    // back to the caller's position — replays from the nearest checkpoint,
    // and replay CANNOT cross external-event markers (their effects are not
    // reproducible): any recorded debugger edit inside the present frame
    // (soft breakpoints are WRITE_MEM edits) parked the emulator at the
    // marker instead of the present. A verbatim snapshot/restore is
    // marker-proof and exact.
    //
    // The build replays through SeekToInternal, which transitions the
    // session to Detached (TDD §4.2). A cache build is a transparent
    // facility, NOT a state transition: restore the caller's session state
    // so a DebuggerLive browse (Recording while paused, §6) keeps recording,
    // and a Session-mode browse stays Detached exactly as before. Without
    // this restore every browse stranded the manager in Detached while the
    // capture was still live — BeginDebuggerLiveHistory then refused and
    // history silently died at the first browse.
    const TTDSessionState stateBeforeBuild = _state;

    SaveLiveState(_liveSnapshot);

    ReplayModeScope replay(*this);
    BuildFrameCache(frame, *_frameCache);
    RestoreLiveState(_liveSnapshot);
    replay.Exit();

    SetState(stateBeforeBuild);

    if (_frameCache->entries.empty())
    {
        // Nothing recorded in that frame. Keep the block (capacity retained) but
        // mark it unmatched so a later call rebuilds rather than returning empty.
        _frameCache->frame = UINT64_MAX;
        return nullptr;
    }

    return _frameCache.get();
}

TTDCoverageProbeResult TimeTravelController::QueryCoverageProbe(
    uint64_t frame,
    TTDCoverageKind kind,
    uint16_t addrFrom,
    uint16_t addrTo,
    std::optional<PhysPage> physPage) const
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    TTDCoverageProbeResult result;
    result.frame = frame;
    result.kind = kind;
    result.addrFrom = addrFrom;
    result.addrTo = addrTo;
    result.physPage = physPage;
    result.touched = false;

    // Per-frame availability. FrameMayContain is a conservative pruning primitive (it answers
    // "true" whenever it cannot prove absence), so an exact probe must first confirm the frame
    // is inside [firstCovered, lastCovered]. Frames outside the covered range report
    // indexAvailable=false instead of a false-positive "touched".
    result.indexAvailable = _coverageIndex.CoversFrame(kind, frame);
    if (!result.indexAvailable)
    {
        return result;
    }

    uint16_t offsetLow = addrFrom & 0x3FFF;
    uint16_t offsetHigh = addrTo & 0x3FFF;
    if (addrTo < addrFrom || (addrTo - addrFrom) >= 0x3FFF || offsetLow > offsetHigh)
    {
        offsetLow = 0;
        offsetHigh = 0x3FFF;
    }

    const bool hasPage = physPage.has_value();
    const PhysPage page = hasPage ? *physPage : PhysPage{0};

    result.touched = _coverageIndex.FrameMayContain(kind, frame, offsetLow, offsetHigh, hasPage, page);
    return result;
}

TTDCoverageScanResult TimeTravelController::QueryCoverageScan(
    uint64_t fromFrame,
    uint64_t toFrame,
    TTDCoverageKind kind,
    uint16_t addrFrom,
    uint16_t addrTo,
    std::optional<PhysPage> physPage,
    size_t limit) const
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    TTDCoverageScanResult result;
    result.kind = kind;
    result.addrFrom = addrFrom;
    result.addrTo = addrTo;
    result.physPage = physPage;

    result.indexAvailable = (_coverageIndex.SealedFrameCount(kind) > 0);
    if (!result.indexAvailable)
    {
        return result;
    }

    limit = std::clamp(limit, size_t{1}, size_t{1000});

    uint64_t firstCovered = 0, lastCovered = 0;
    if (!_coverageIndex.CoveredRange(kind, firstCovered, lastCovered))
    {
        return result;
    }
    result.coveredFrom = firstCovered;
    result.coveredTo = lastCovered;

    uint64_t startFrame = std::max(fromFrame, firstCovered);
    uint64_t endFrame = std::min(toFrame, lastCovered);
    if (startFrame > endFrame)
    {
        return result;
    }

    uint16_t offsetLow = addrFrom & 0x3FFF;
    uint16_t offsetHigh = addrTo & 0x3FFF;
    if (addrTo < addrFrom || (addrTo - addrFrom) >= 0x3FFF || offsetLow > offsetHigh)
    {
        offsetLow = 0;
        offsetHigh = 0x3FFF;
    }

    const bool hasPage = physPage.has_value();
    const PhysPage page = hasPage ? *physPage : PhysPage{0};

    for (uint64_t f = startFrame; f <= endFrame; ++f)
    {
        result.scannedFrames++;
        if (_coverageIndex.FrameMayContain(kind, f, offsetLow, offsetHigh, hasPage, page))
        {
            result.matchingFrames++;
            if (result.frames.size() < limit)
            {
                if (result.frames.empty())
                {
                    result.firstMatch = f;
                }
                result.lastMatch = f;
                result.frames.push_back(f);
            }
            else
            {
                result.truncated = true;
            }
        }
    }

    return result;
}

TTDCoverageSummaryResult TimeTravelController::QueryCoverageSummary(
    uint64_t fromFrame,
    uint64_t toFrame,
    std::optional<TTDCoverageKind> kind,
    uint64_t bucketSize,
    size_t limit) const
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    TTDCoverageSummaryResult result;
    result.fromFrame = fromFrame;
    result.toFrame = toFrame;

    result.indexAvailable = (_coverageIndex.SealedFrameCount(TTDCoverageKind::Executed) > 0 ||
                             _coverageIndex.SealedFrameCount(TTDCoverageKind::Written) > 0 ||
                             _coverageIndex.SealedFrameCount(TTDCoverageKind::Read) > 0);
    if (!result.indexAvailable)
    {
        return result;
    }

    // Echo the covered window (union across all covered kinds) so callers can see
    // how the requested range relates to what the index actually covers.
    for (int k = 0; k < 3; ++k)
    {
        const TTDCoverageKind coveredKind = static_cast<TTDCoverageKind>(k);
        uint64_t firstCovered = 0, lastCovered = 0;
        if (_coverageIndex.CoveredRange(coveredKind, firstCovered, lastCovered))
        {
            result.coveredFrom = (result.coveredFrom == 0) ? firstCovered : std::min(result.coveredFrom, firstCovered);
            result.coveredTo = std::max(result.coveredTo, lastCovered);
        }
    }

    limit = std::clamp(limit, size_t{1}, size_t{500});
    if (toFrame < fromFrame)
    {
        toFrame = fromFrame;
    }

    const uint64_t totalFrames = toFrame - fromFrame + 1;
    if (bucketSize == 0)
    {
        bucketSize = (totalFrames + limit - 1) / limit;
        if (bucketSize == 0) bucketSize = 1;
    }

    result.bucketSize = bucketSize;
    const size_t bucketCount = static_cast<size_t>((totalFrames + bucketSize - 1) / bucketSize);
    result.bucketCount = std::min(bucketCount, limit);

    std::vector<TTDCoverageKey> keysScratch;
    std::unordered_set<TTDCoverageKey> distinctExec;
    std::unordered_set<TTDCoverageKey> distinctWrite;
    std::unordered_set<TTDCoverageKey> distinctRead;

    for (size_t b = 0; b < result.bucketCount; ++b)
    {
        TTDCoverageSummaryBucket bucket;
        bucket.frameStart = fromFrame + b * bucketSize;
        bucket.frameEnd = std::min(bucket.frameStart + bucketSize - 1, toFrame);

        // A key frame: a checkpoint that stores every piece whole, where a
        // segment of the engine's history starts (D41)
        bucket.hasKeyframe = false;
        for (const TTDSegmentInfo& segment : _engine->Segments())
            if (segment.firstFrame >= bucket.frameStart && segment.firstFrame <= bucket.frameEnd)
            {
                bucket.hasKeyframe = true;
                break;
            }

        if (!kind || *kind == TTDCoverageKind::Executed)
        {
            distinctExec.clear();
            for (uint64_t f = bucket.frameStart; f <= bucket.frameEnd; ++f)
            {
                if (_coverageIndex.GetFrameKeys(TTDCoverageKind::Executed, f, keysScratch))
                {
                    distinctExec.insert(keysScratch.begin(), keysScratch.end());
                }
            }
            bucket.executedDistinct = static_cast<uint32_t>(distinctExec.size());
        }

        if (!kind || *kind == TTDCoverageKind::Written)
        {
            distinctWrite.clear();
            for (uint64_t f = bucket.frameStart; f <= bucket.frameEnd; ++f)
            {
                if (_coverageIndex.GetFrameKeys(TTDCoverageKind::Written, f, keysScratch))
                {
                    distinctWrite.insert(keysScratch.begin(), keysScratch.end());
                }
            }
            bucket.writtenDistinct = static_cast<uint32_t>(distinctWrite.size());
        }

        if (!kind || *kind == TTDCoverageKind::Read)
        {
            distinctRead.clear();
            for (uint64_t f = bucket.frameStart; f <= bucket.frameEnd; ++f)
            {
                if (_coverageIndex.GetFrameKeys(TTDCoverageKind::Read, f, keysScratch))
                {
                    distinctRead.insert(keysScratch.begin(), keysScratch.end());
                }
            }
            bucket.readDistinct = static_cast<uint32_t>(distinctRead.size());
        }

        result.buckets.push_back(bucket);
    }

    return result;
}

} // namespace ttd
