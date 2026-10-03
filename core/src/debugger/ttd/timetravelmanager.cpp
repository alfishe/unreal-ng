/// @file timetravelmanager.cpp
/// @brief TimeTravelManager — capture orchestrator implementation.
///
/// Per parent TDD §6.3, §7.1. The hot path is OnFrameBoundary: dirty pages
/// are freshly Intern'd, clean pages AddRef the previous checkpoint's slot,
/// CPU/chipset are field-copied via the helpers in ttdcheckpoint.cpp.

#include "timetravelmanager.h"
#include "ttddisplayparticipant.h"


#include <algorithm>
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
#include "ttdmachineperipherals.h"  // RegisterMachinePeripherals (shared with MachineStateTransfer)
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

namespace
{
/// Session time point of an absolute t-state (frame length frameT)
TTDTimePoint TimePointOf(uint64_t globalT, uint32_t frameT)
{
    return TTDTimePoint{globalT / frameT, static_cast<uint32_t>(globalT % frameT)};
}
}  // namespace

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------

const char* TTDSessionStateToString(TTDSessionState state)
{
    switch (state)
    {
        case TTDSessionState::Idle:      return "idle";
        case TTDSessionState::Recording: return "recording";
        case TTDSessionState::Detached:  return "detached";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TimeTravelManager::TimeTravelManager(EmulatorContext* context)
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
}

TimeTravelManager::~TimeTravelManager()
{
    if (_context && _context->ttdPortReads == &_portReads)
        _context->ttdPortReads = nullptr;
    if (_context && _context->ttdPortWrites == &_portWrites)
        _context->ttdPortWrites = nullptr;

    // Release all page-store refs held by the timeline before the page store
    // itself goes away (it's a member, destroyed right after this dtor body).
    for (auto& cp : _timeline)
    {
        ReleaseCheckpointRefs(cp);
    }
}

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

void TimeTravelManager::SetUnavailableReason(const std::string& reason)
{
    if (!reason.empty() && _state != TTDSessionState::Idle)
        InvalidateSession(reason.c_str());
    _unavailableReason = reason;
    PublishSessionInfo();
}

bool TimeTravelManager::StartRecording()
{
    if (_state == TTDSessionState::Recording)
        return true;  // Idempotent

    if (!_unavailableReason.empty())
    {
        MLOGWARNING("TimeTravelManager::StartRecording — refused: %s", _unavailableReason.c_str());
        return false;
    }

    // Leaving the replay/browse scope for live recording: free the decode cache.
    ClearFrameCache();

    // Fresh session — clear any stale auto-pause signal from a previous
    // Detached window.
    _autoPauseRequested.store(false, std::memory_order_release);
    // ...and a stale invalidation request from the previous session
    _pendingInvalidation.store(nullptr, std::memory_order_release);

    if (!_context || !_memory || !_dirtyTracker)
    {
        MLOGWARNING("TimeTravelManager::StartRecording — missing dependencies (context=%p memory=%p tracker=%p)",
                    (void*)_context, (void*)_memory, (void*)_dirtyTracker);
        return false;
    }

    // Pause the emulator while we toggle features + capture the baseline.
    // The kDebugMode flip swaps Z80::MemIf (read on every memory access by
    // the CPU thread), so we must not race with emulation. Pause blocks
    // until the Z80 thread has parked.
    Emulator* emu = _context->pEmulator;
    const bool wasRunning = emu && emu->IsRunning() && !emu->IsPaused();
    if (wasRunning)
    {
        emu->Pause(false);
        emu->WaitForPauseConfirmation(1000);
    }

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
            MLOGERROR("TimeTravelManager::StartRecording - refusing to record: %s",
                      registrationError.c_str());
            return refuse();
        }
    }

    // Clear any prior history (StartRecording always begins a fresh session).
    if (!_timeline.empty())
    {
        for (auto& cp : _timeline)
            ReleaseCheckpointRefs(cp);
        _timeline.clear();
        _blobBytes = 0;
        _pageStore.Reset();
        _dirtyTracker->ResetSession();
        _dirtyScratch.clear();
        _inputJournal.Clear();  // Phase 2 Item 3 — drop any prior input events
        DisarmInputPlayback();
        _externalEvents.Clear();  // Phase 2 Item 6 — drop any prior markers
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
        _writeJournal = std::make_unique<TTDWriteJournal>(64u * 1024 * 1024, true);
    }

    // Wait for journal allocation to complete before proceeding.
    // This synchronizes with the async allocation started above (or earlier).
    if (_writeJournal)
    {
        _writeJournal->WaitReady();
        if (!_writeJournal->IsEmpty())
            _writeJournal->Clear();  // it must hold this session's writes only
    }
    ClearJournalGap();
    _journalGapless = _enableWriteJournal && _writeJournal != nullptr;
    if (!_journalGapless)
    {
        _journalGapless = true;  // so the gap below is recorded
        MarkJournalGap("recorded without the write journal");
    }

    _modelRamPages = ResolveModelRamPages();
    if (_modelRamPages == 0 || _modelRamPages > MAX_RAM_PAGES)
    {
        MLOGWARNING("TimeTravelManager::StartRecording — implausible modelRamPages=%u, refusing to start",
                    static_cast<unsigned>(_modelRamPages));
        _modelRamPages = 0;
        return refuse();
    }

    // Engaged before the baseline (past the last refusal above), so the very
    // first checkpoint already holds the 1x machine; SetState below is then a no-op
    EngageRecordingLock();

    // Port-read journal: a fresh session records every IN from its baseline
    // on - on configurations whose outside world reaches the CPU through IN
    // alone (ttd-port-read-journal.md §2)
    _portReads.Clear();
    _portWrites.Clear();
    if (const char* reason = PortJournalUnsupportedReason())
    {
        _portJournalValid = false;
        _portJournalOffReason = reason;
    }
    else
    {
        _portJournalValid = true;
        _portJournalOffReason.clear();
        _portReads.StartRecording();
        _portWrites.StartRecording();
    }

    // Capture the baseline checkpoint so the timeline always has at least
    // one entry. This is the only place we pay the full model-RAM copy cost
    // up front (v1 strategy — see the header doc for the v2 fast-path plan).
    TTDCheckpoint baseline;
    CaptureNow(baseline);
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
    _loadedRecordedBy.clear();
    _liveRomSignature = ComputeRomSignature();  // the ROM this session relies on

    MLOGINFO("TimeTravelManager::StartRecording — baseline captured: modelRamPages=%u, timeline=1, pageStoreBytes=%zu, debugMemIf=%s",
             static_cast<unsigned>(_modelRamPages), _pageStore.GetCapacityBytes(),
             (_toggledDebugModeOn ? "switched-on" : "already-on"));

    // Published while the machine is still parked: once it resumes it
    // records, and only its own thread may read the session then
    PublishSessionInfo();

    // Resume the emulator if we paused it. The recording OnFrameBoundary
    // hook will now see dirty bits being set correctly.
    if (wasRunning && emu)
        emu->Resume(false);

    return true;
}

void TimeTravelManager::StopRecording()
{
    if (_state != TTDSessionState::Recording)
    {
        // Compress whatever coverage is still accumulating, so size reporting
        // and any later serialization see the whole session rather than
        // all-but-the-last block. Not recording: the machine adds nothing
        _coverageIndex.FlushOpenBlocks();
        return;  // Idempotent
    }

    // Park the machine BEFORE touching the session: while it records, its
    // thread appends to the coverage index, the journals and the timeline, so
    // the flush, the state change and the journal stop below must not run
    // beside a frame (same pause discipline as StartRecording, TDD section
    // 7.2). From the machine's own thread the wait returns at once.
    Emulator* emu = _context ? _context->pEmulator : nullptr;
    const bool wasRunning = emu && emu->IsRunning() && !emu->IsPaused();
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
    }
    SetState(TTDSessionState::Idle);
    // The machine runs on unrecorded from here: I/O passes through
    _portReads.Stop();
    _portWrites.Stop();
    SyncPortJournalHook();
    MLOGINFO("TimeTravelManager::StopRecording — timeline retained with %zu checkpoints",
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
            MLOGINFO("TimeTravelManager::StopRecording — restored feature '%s' to OFF (was auto-enabled by StartRecording)",
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

bool TimeTravelManager::BeginDebuggerLiveHistory()
{
    if (_state == TTDSessionState::Detached)
    {
        MLOGWARNING("TimeTravelManager::BeginDebuggerLiveHistory — refused: session is Detached "
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
    MLOGINFO("TimeTravelManager::BeginDebuggerLiveHistory — live history active "
             "(adopted existing recording: %s, timeline: %zu checkpoints)",
             wasRecording ? "yes" : "no", _timeline.size());
    return true;
}

void TimeTravelManager::EndDebuggerLiveHistory()
{
    if (_recordMode != TTDRecordMode::DebuggerLive)
        return;  // Idempotent

    _recordMode = TTDRecordMode::Session;
    // Keeps the timeline: StopRecording transitions Recording → Idle with
    // history retained, so the scrubber and .ttd flows can take over.
    StopRecording();
    MLOGINFO("TimeTravelManager::EndDebuggerLiveHistory — timeline retained with %zu checkpoints",
             _timeline.size());
}

void TimeTravelManager::InvalidateSession(const char* reason)
{
    const PublishOnExit publish{*this};
    ClearFrameCache();

    if (_timeline.empty() && _state == TTDSessionState::Idle)
        return;  // Nothing to invalidate

    MLOGINFO("TimeTravelManager::InvalidateSession — reason='%s', dropping %zu checkpoints",
             reason ? reason : "(null)", _timeline.size());
    _lastDropReason = reason ? reason : "";

    for (auto& cp : _timeline)
        ReleaseCheckpointRefs(cp);
    _timeline.clear();
    _blobBytes = 0;
    _pageStore.Reset();
    _dirtyScratch.clear();
    ReleaseModelPeripherals();  // serializers are session-scoped, like the timeline
    _inputJournal.Clear();  // Phase 2 Item 3 — input history invalidates with the timeline
    DisarmInputPlayback();
    _externalEvents.Clear();  // Phase 2 Item 6 — markers invalidate with the timeline
    _bookmarks.Clear();  // TD-4 — bookmarks invalidate with the timeline
    if (_writeJournal)
        _writeJournal->Clear();  // Phase 4 — write journal invalidates with the timeline
    _journalGapless = false;
    ClearJournalGap();
    _modelRamPages = 0;
    _dirtyPageOverflowReported = false;
    _loadedFromFile = false;
    _inputHistoryComplete = true;
    _sourcePath.clear();
    _capturedAtUnixMs = 0;
    _sessionModelId = 0;
    _loadedRomSignature = 0;
    _loadedRecordedBy.clear();
    _coverageIndex.Clear();
    if (_context)
        _context->ttdCoverageActive = false;
    _portReads.Clear();
    _portWrites.Clear();
    _portJournalValid = false;
    _portJournalOffReason.clear();
    SyncPortJournalHook();
    SetState(TTDSessionState::Idle);

    // Reset Phase 5 codec state.
    _lastKeyFrameIdx = 0;
    _evictedCheckpoints = 0;
    _forceNextKeyFrame = true;

    // Clear previous-page cache (only allocated during active recording)
    _prevPageCache.clear();
    _prevPageCache.shrink_to_fit();
    _prevPageCacheValid = false;

    // Reset the dirty tracker too — the session-scoped _everDirty set is part
    // of the captured history's validity contract.
    if (_dirtyTracker)
        _dirtyTracker->ResetSession();
}

void TimeTravelManager::EngageCaptureFeatures()
{
    FeatureManager* fm = _context ? _context->pFeatureManager : nullptr;
    if (!fm)
    {
        MLOGWARNING("TimeTravelManager — FeatureManager is null; cannot verify debug/ttd flags. Capture will be a "
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
        MLOGINFO("TimeTravelManager — auto-enabled feature '%s' (required for TTD capture)", Features::kTimeTravel);
    }
    if (!debugModeWasOn)
    {
        if (!fm->isEnabled(Features::kDebugMode))
            fm->setFeature(Features::kDebugMode, true);
        _toggledDebugModeOn = true;
        MLOGINFO("TimeTravelManager — auto-enabled feature '%s' (required to route writes through MemoryWriteDebug -> "
                 "MarkDirty)",
                 Features::kDebugMode);
    }
}

void TimeTravelManager::SetState(TTDSessionState next)
{
    _state = next;
    if (next == TTDSessionState::Recording)
        EngageRecordingLock();
    else if (next == TTDSessionState::Idle)
        ReleaseRecordingLock();
}

void TimeTravelManager::EngageRecordingLock()
{
    if (_recordingLockEngaged || !_context)
        return;
    _recordingLockEngaged = true;

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
        MLOGINFO("TimeTravelManager — recording lock: host speed %ux -> 1x (restored when the session returns to Idle)",
                 static_cast<unsigned>(_savedHostSpeedMultiplier));
    }

    // Turbo mode off, turbo mode / fast tape / turbo tape / fast disk refused
    if (_context->pFeatureManager)
        _context->pFeatureManager->onTtdRecordingStarted();

    // Devices with a host-time dependence (the RTC) switch to emulated time
    // here, before StartRecording captures its baseline
    _peripherals.NotifyRecording(true);
}

void TimeTravelManager::ReleaseRecordingLock()
{
    if (!_recordingLockEngaged || !_context)
        return;
    _recordingLockEngaged = false;

    _peripherals.NotifyRecording(false);

    // Lifts the FeatureManager gate first: the speed restore below is checked by it
    if (_context->pFeatureManager)
        _context->pFeatureManager->onTtdRecordingStopped();

    if (_context->pCore && _savedHostSpeedMultiplier != 1)
    {
        _context->pCore->SetSpeedMultiplier(_savedHostSpeedMultiplier);
        MLOGINFO("TimeTravelManager — recording lock released: host speed back to %ux",
                 static_cast<unsigned>(_savedHostSpeedMultiplier));
    }
    _savedHostSpeedMultiplier = 1;
}

void TimeTravelManager::UpdateFeatureCache()
{
    FeatureManager* fm = _context ? _context->pFeatureManager : nullptr;
    if (!fm)
        return;

    const bool ttdEnabled = fm->isEnabled(Features::kTimeTravel);

    // Writes stop reaching the journal when TTD or debug mode goes off during
    // a recording, so it no longer holds the whole session. Only while
    // Recording: nothing is journaled when Idle or Detached, and StopRecording
    // and step-over switch debug mode back in exactly those states.
    if (_state == TTDSessionState::Recording && (!ttdEnabled || !fm->isEnabled(Features::kDebugMode)))
        MarkJournalGap(!ttdEnabled ? "time travel switched off during the recording"
                                   : "debug mode switched off during the recording");

    // Pre-allocate write journal when TTD is enabled (async, non-blocking).
    // This way allocation completes before StartRecording() is called.
    if (ttdEnabled && _enableWriteJournal && !_writeJournal)
    {
        MLOGINFO("TimeTravelManager::UpdateFeatureCache — TTD enabled, pre-allocating write journal (async)");
        _writeJournal = std::make_unique<TTDWriteJournal>(64u * 1024 * 1024, true);
    }

    // When TimeTravel feature is disabled and we're not recording,
    // deallocate the write journal to free memory (~64MB)
    if (!ttdEnabled && _state == TTDSessionState::Idle && _writeJournal)
    {
        MLOGINFO("TimeTravelManager::UpdateFeatureCache — TTD disabled, deallocating write journal");
        _writeJournal.reset();
    }
}

TTDSessionInfo TimeTravelManager::GetSessionInfo() const
{
    TTDSessionInfo info;
    info.state = _state;
    info.checkpointCount    = _timeline.size();
    info.pageStoreBytes     = _pageStore.GetCapacityBytes();
    info.pageStoreUsedBytes = _pageStore.GetUsedBytes();
    // Distinct RAM snapshots currently live in the page store. Each slot
    // is a 4 KB sub-page captured at some frame; slots are shared across
    // checkpoints via refcount when a sub-page hasn't changed. This is
    // the most direct measure of "how much unique state has been captured"
    // and tracks the working-set size of the session.
    info.baselineFramesCaptured = _pageStore.GetUsedSlots();

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
        for (const auto& blob : _timeline.front().peripheralBlobs)
            if (blob.first < 64)
                info.machine.peripheralMask |= uint64_t(1) << blob.first;
        ttd::DescribeRecordedMachine(info.machine);
    }
    info.recordedBy = _loadedFromFile ? _loadedRecordedBy : std::string();

    // Sections. The write journal is normally the largest part of a session,
    // and the coverage index decides whether reverse queries run in
    // milliseconds or replay frames.
    if (_writeJournal)
    {
        info.writeJournalRecords = _writeJournal->Size();
        info.writeJournalBytes   = _writeJournal->Size() * sizeof(TTDWriteRecord);
    }

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
    info.unavailableReason = _unavailableReason;

    // Phase 5 codec telemetry — useful for the UI / WebAPI status surface
    // to show compression effectiveness at a glance.
    info.compressionRatio = _pageStore.GetCompressionRatio();
    info.livePayloadBytes = _pageStore.GetLivePayloadBytes();
    info.keyFrameCount    = 0;
    info.deltaFrameCount  = 0;
    for (const auto& cp : _timeline)
    {
        if (cp.frameKind == TTDFrameKind::KeyFrame)
            ++info.keyFrameCount;
        else
            ++info.deltaFrameCount;
    }

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
    info.writeJournalComplete = _journalGapless && _writeJournal != nullptr;
    info.writeJournalWrapped = _writeJournal != nullptr && _writeJournal->HasEvictedRecords();
    info.journalGapReason = _journalGapReason;
    info.journalGapHasPosition = _journalGapHasPosition;
    info.journalGapAt = _journalGapAt;

    PublishSessionInfo(info);
    return info;
}

TTDSessionInfo TimeTravelManager::GetPublishedSessionInfo() const
{
    // An observer is looking: the machine's thread publishes again at its next
    // frame boundary once the interval has passed
    _publishRequested.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(_publishedMutex);
    return _published;
}

void TimeTravelManager::PublishSessionInfo(const TTDSessionInfo& info) const
{
    std::lock_guard<std::mutex> lock(_publishedMutex);
    _published = info;
}

void TimeTravelManager::MaybePublishAtFrameBoundary()
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

bool TimeTravelManager::SetEnableWriteJournal(bool enable)
{
    if (enable == _enableWriteJournal)
        return true;
    if (!RecordingGuard(TTDGuardedAction::ChangeWriteJournal).empty())
        return false;
    if (!_timeline.empty())
        MarkJournalGap(enable ? "write journal switched on during the session"
                              : "write journal switched off during the session");
    _enableWriteJournal = enable;

    // Nothing to answer for and nothing to record into: give back the 64 MB
    // the feature pre-allocated. A retained session keeps its journal.
    if (!enable && _state == TTDSessionState::Idle && _timeline.empty() && _writeJournal)
    {
        _writeJournal->WaitReady();
        _writeJournal.reset();
    }
    return true;
}

void TimeTravelManager::MarkJournalGap(const char* reason, bool hasPosition)
{
    if (!_journalGapless)
        return;  // already incomplete: keep the first cause
    _journalGapless = false;
    _journalGapReason = reason ? reason : "";
    _journalGapHasPosition = hasPosition && !_timeline.empty();
    _journalGapAt = _journalGapHasPosition ? CurrentPosition() : TTDTimePoint{};
    if (_journalGapHasPosition)
        MLOGWARNING("TimeTravelManager — the write journal no longer covers the session (%s, at frame %llu "
                    "t=%u): write/port find-last replays instead of answering from it",
                    _journalGapReason.c_str(), static_cast<unsigned long long>(_journalGapAt.frame),
                    static_cast<unsigned>(_journalGapAt.tInFrame));
    else
        MLOGWARNING("TimeTravelManager — the write journal does not cover the session (%s): "
                    "write/port find-last replays instead of answering from it",
                    _journalGapReason.c_str());
}

void TimeTravelManager::ClearJournalGap()
{
    _journalGapReason.clear();
    _journalGapHasPosition = false;
    _journalGapAt = TTDTimePoint{};
}

size_t TimeTravelManager::EstimateSessionHeapBytes() const
{
    return GetHeapBreakdown().Total();
}

TTDHeapBreakdown TimeTravelManager::GetHeapBreakdown() const
{
    TTDHeapBreakdown h;

    // Page store: slot table plus every compressed payload allocation (the
    // payloads are separate heap blocks; the slot table alone left out the
    // pages themselves - B7). Allocated, not live: free-list slots keep
    // their capacity until reused.
    const size_t payloadCapacity = _pageStore.PayloadCapacityBytes();
    h.pageStoreTable = _pageStore.HeapBytes() - payloadCapacity;
    h.ramPayload = _pageStore.GetLivePayloadBytes();
    h.ramPayloadSlack = payloadCapacity - h.ramPayload;

    // Per-checkpoint: the struct itself + every vector's allocated backing
    // (capacity, not size — capacity is what's actually on the heap).
    //sizeof(TTDCheckpoint) covers time, globalT, cpu, chipset, journal
    // offsets, and the std::vector headers (pointer/size/capacity triple).
    // The vector capacity × element-size additions below account for the
    // heap allocations those vector headers point at.
    for (const TTDCheckpoint& cp : _timeline)
    {
        h.checkpoints += sizeof(TTDCheckpoint);
        for (const auto& entry : cp.peripheralBlobs)
            h.deviceBlobs += entry.second.capacity() * sizeof(uint8_t);
        h.pageRefs += cp.ramPages.capacity() * sizeof(TTDPageRef);
    }

    // Input journal + external-event journal — same pattern: capacity is
    // what's allocated, size is what's logically used. Plus the session-scope
    // dirty-page scratch buffer (reused every frame, counted once).
    h.inputJournals = _inputJournal.Events().capacity() * sizeof(TTDInputEvent) +
                      _externalEvents.Events().capacity() * sizeof(TTDExternalEvent) +
                      _dirtyScratch.capacity() * sizeof(uint16_t);

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
        h.coverage = _coverageIndex.HeapBytes();
        h.coverageSlack = _coverageIndex.CompressedSlackBytes();
        h.portReads = _portReads.HeapBytes();
        h.portWrites = _portWrites.HeapBytes();
        h.portJournalSlack = _portReads.CompressedSlackBytes() + _portWrites.CompressedSlackBytes();
        if (_frameCache)
            h.frameCache = _frameCache->Bytes();
    }

    return h;
}

// ---------------------------------------------------------------------------
// Capture (emulator thread)
// ---------------------------------------------------------------------------

std::string TimeTravelManager::RecordingGuard(TTDGuardedAction action) const
{
    // Only a recording the user started is protected. A debugger's live history
    // (DebuggerLive, DeZog) is a rolling background history: any outside change
    // drops it and the debugger restarts it on the next resume or step.
    if (!IsRecording() || IsDebuggerLive())
        return {};

    switch (action)
    {
        case TTDGuardedAction::LoadSnapshot:
            return "Cannot load a snapshot while TTD is recording: it replaces the whole machine state and would drop "
                   "the recorded history. Stop the recording first.";
        case TTDGuardedAction::LoadTape:
            return "Cannot insert a tape while TTD is recording: a new medium would drop the recorded history. Insert "
                   "it before starting the recording, or stop the recording first.";
        case TTDGuardedAction::LoadDisk:
            return "Cannot insert a disk while TTD is recording: a new medium would drop the recorded history. Insert "
                   "it before starting the recording, or stop the recording first.";
        case TTDGuardedAction::CreateDisk:
            return "Cannot create a disk while TTD is recording: a new medium would drop the recorded history. Create "
                   "it before starting the recording, or stop the recording first.";
        case TTDGuardedAction::LoadRom:
            return "Cannot load a ROM while TTD is recording: the recorded history relies on the current ROM and would "
                   "be dropped. Stop the recording first.";
        case TTDGuardedAction::Invalidate:
            return "Cannot discard the TTD session while it is recording. Stop the recording first, then discard it.";
        case TTDGuardedAction::DisableTimeTravel:
            return "Cannot switch the timetravel feature off while TTD is recording: capture would stop mid-session "
                   "and the recorded history would be corrupt. Stop the recording first.";
        case TTDGuardedAction::DisableDebugMode:
            return "Cannot switch debug mode off while TTD is recording: memory writes would stop reaching the "
                   "recorded history, which would then be corrupt. Stop the recording first.";
        case TTDGuardedAction::ChangeWriteJournal:
            return "Cannot change the write journal mode while TTD is recording: a recording keeps the mode it "
                   "started with. Choose it when starting, or stop the recording first.";
        case TTDGuardedAction::SwitchGsCard:
            return "Cannot switch the General Sound card type while TTD is recording: the recorded history holds "
                   "the current card's state, which the other card type cannot take back. Stop the recording first.";
        case TTDGuardedAction::CdFrontPanel:
            return "Cannot play, pause, stop or change the volume of a CD drive from outside the guest while TTD is "
                   "recording: a replay would not repeat it. Let the guest's CD player do it, or stop the recording first.";
    }
    return "This action is not allowed while TTD is recording. Stop the recording first.";
}

void TimeTravelManager::RequestInvalidation(const char* reason)
{
    if (_state != TTDSessionState::Recording)
        return;
    const char* expected = nullptr;
    if (_pendingInvalidation.compare_exchange_strong(expected, reason ? reason : "(unspecified)",
                                                     std::memory_order_acq_rel))
        MLOGINFO("TimeTravelManager::RequestInvalidation — '%s' (applied at the frame boundary)",
                 reason ? reason : "(unspecified)");
}

void TimeTravelManager::OnFrameBoundary()
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

        TTDCheckpoint cp;
        CaptureNow(cp);
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
    if (_state == TTDSessionState::Detached && !_timeline.empty() && !_inReplayMode)
    {
        const uint64_t sessionEnd = _timeline.back().time.frame;
        const uint64_t currentFrame = _context->emulatorState.frame_counter;
        if (currentFrame > sessionEnd)
        {
            MLOGINFO("TimeTravelManager: auto-pause at frame %llu "
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

bool TimeTravelManager::ConsumeAutoPauseRequest()
{
    return _autoPauseRequested.exchange(false, std::memory_order_acq_rel);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void TimeTravelManager::CaptureNow(TTDCheckpoint& out)
{
    assert(_context && _memory && _dirtyTracker);

    _captureWork = TTDCaptureWork{};
    _pageStore.ResetWork();

    // --- Time coordinate ---
    const EmulatorState& st = _context->emulatorState;
    out.time.frame     = st.frame_counter;
    out.time.tInFrame  = 0;  // Checkpoints always sit at frame boundaries (TDD §4.1)
    out.globalT        = st.frame_counter;  // At frame boundary globalT == frame

    // --- CPU + chipset ---
    // Z80 inherits from Z80State (see z80.h:322), so a Z80* IS-A Z80State.
    Z80* cpu = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (cpu)
    {
        out.cpu     = CaptureCpuState(*static_cast<Z80State*>(cpu));
    }
    // The CPU resumes where the frame's last instruction left it, a few
    // T-states in (the overshoot) - restored with the checkpoint
    out.chipset = CaptureChipsetState(st, cpu ? static_cast<uint32_t>(cpu->t) : 0u);

    // --- Model-specific chipset state (TDD §6.4) ---
    // Whatever the active model registered serializes itself here. The
    // framework stays model-agnostic: it never names a machine, it just walks
    // the registry.
    //
    _peripherals.CaptureAll(out.peripheralBlobs);
    for (const auto& blob : out.peripheralBlobs)
        _captureWork.deviceBlobBytes += blob.second.size();

    // --- Port-read journal position: the reads before it happened before
    // this point, so a replay from here starts handing out records at it ---
    out.portReadCursor = _portJournalValid ? _portReads.Size() : 0;
    out.portWriteCursor = _portJournalValid ? _portWrites.Size() : 0;

    // --- RAM pages ---
    // First capture of a session: Intern every model-RAM page as the baseline
    // (this is an I-frame by definition). Subsequent captures follow the
    // I/P pattern: every kKeyFrameInterval-th frame is a key frame, others
    // are delta frames that only re-intern dirty pages.
    const bool isKeyFrame = _timeline.empty()
                            || _forceNextKeyFrame
                            || (out.time.frame - _lastKeyFrameIdx >= kKeyFrameInterval);
    out.frameKind = isKeyFrame ? TTDFrameKind::KeyFrame : TTDFrameKind::DeltaFrame;

    if (_timeline.empty())
    {
        CaptureBaselineRamPages(out.ramPages);
        out.keyFrameAnchor = out.time.frame;
        _lastKeyFrameIdx = out.time.frame;
        _forceNextKeyFrame = false;
    }
    else
    {
        const TTDCheckpoint& prev = _timeline.back();

        if (isKeyFrame)
        {
            out.keyFrameAnchor = out.time.frame;
            _lastKeyFrameIdx = out.time.frame;
            _forceNextKeyFrame = false;
        }
        else
        {
            out.keyFrameAnchor = _lastKeyFrameIdx;
        }

        // Collect dirty pages from the tracker. The buffer is reused across
        // frames to avoid per-frame allocation (CollectAndClear appends).
        _dirtyScratch.clear();
        _dirtyTracker->CollectAndClear(_dirtyScratch);

        UpdateRamPages(_dirtyScratch, prev.ramPages, out.ramPages, isKeyFrame);
    }

    // Update previous-page cache for next frame's XOR delta computation.
    // This caches current RAM content so we can compute XOR deltas without
    // decompressing the slots we just created.
    UpdatePrevPageCache();

    const TTDCodecPageStore::Work& storeWork = _pageStore.GetWork();
    _captureWork.bytesScanned = storeWork.bytesScanned;
    _captureWork.compressCalls = storeWork.compressCalls;
    _captureWork.compressInputBytes = storeWork.compressInputBytes;
    _captureWork.slotsDecoded = storeWork.slotsDecoded;
    _perf.lastCaptureWork = _captureWork;
}

void TimeTravelManager::CaptureBaselineRamPages(std::vector<TTDPageRef>& outRamPages)
{
    // Baseline = I-frame: every model RAM page is captured as 4 × Full
    // sub-pages. This is the only place where we pay the full uncompressed
    // cost up front (modulo zstd-1 compression, which typically achieves
    // 2-3x ratio on real emulator state).
    outRamPages.resize(_modelRamPages);
    for (uint16_t p = 0; p < _modelRamPages; ++p)
    {
        _captureWork.pagesVisited++;
        // Memory owns the RAM backing; RAMPageAddress returns a host pointer
        // to the 16 KB page. We split it into 4 × 4 KB sub-pages and intern
        // each one independently. Most baseline pages compress well with
        // zstd-1; the codec store handles the Full encoding automatically.
        const uint8_t* pageData = _memory->RAMPageAddress(p);
        if (pageData == nullptr)
        {
            // Should never happen — Memory always allocates the full model
            // RAM backing. Defensive: mark as never-touched so restore skips.
            outRamPages[p].SetNeverTouched();
            continue;
        }

        // Intern each of the 4 × 4 KB sub-pages as Full snapshots.
        for (uint32_t s = 0; s < 4; ++s)
        {
            const uint8_t* sub = pageData + (s * TTDCodecPageStore::kPageSize);
            outRamPages[p].pageSlots[s] = _pageStore.InternFull(sub);
        }
    }
}

/// @brief Is a 16 KB RAM page entirely zero?
/// Cheap pre-check that keeps a key frame from paying the intern + hash cost
/// for RAM the guest never populated.
bool TimeTravelManager::IsPageAllZero(const uint8_t* page)
{
    if (page == nullptr)
        return true;

    const size_t words = (4 * TTDCodecPageStore::kPageSize) / sizeof(uint64_t);
    const uint64_t* p64 = reinterpret_cast<const uint64_t*>(page);
    for (size_t i = 0; i < words; ++i)
    {
        if (p64[i] != 0)
            return false;
    }
    return true;
}

void TimeTravelManager::UpdateRamPages(const std::vector<uint16_t>& dirtyPages,
                                const std::vector<TTDPageRef>& prevRamPages,
                                std::vector<TTDPageRef>& outRamPages,
                                bool isKeyFrame)
{
    // ------------------------------------------------------------------
    // Refcount invariant: every slot index that appears in any
    // _timeline checkpoint's ramPages[*].pageSlots[s] MUST have a matching
    // live refcount in the page store, INCLUDING the delta-chain refs
    // that XorPrev slots hold against their prevSlot (the latter are
    // managed internally by InternXor / Release — see
    // ttdcodecpagestore.cpp).
    //
    // outRamPages.assign(prevRamPages) COPIES slot indices but does NOT
    // bump refcounts. The logic below must therefore AddRef every slot
    // that outRamPages will keep referencing. There is no "phantom ref"
    // to release later — outRamPages's references must each be paid for
    // with an explicit AddRef or replaced with a freshly-interned slot
    // (whose Intern returns refcount=1).
    //
    // Concretely, for each sub-page we do exactly ONE of:
    //   (a) Clean: AddRef the existing slot  → outRamPages shares prev.
    //   (b) Dirty P-frame: InternXor          → outRamPages gets new slot.
    //   (c) Dirty I-frame: InternFull         → outRamPages gets new slot.
    // In none of these branches do we Release prevSlot — prevRamPages
    // (the previous checkpoint, still in _timeline) keeps its own ref,
    // and the codec store tracks any delta-chain ref internally.
    // ------------------------------------------------------------------
    outRamPages.assign(prevRamPages.begin(), prevRamPages.end());

    // dirtyPages is in ascending order (CollectAndClear guarantee), so a
    // two-pointer walk avoids a hash-set lookup per page.
    size_t dirtyCursor = 0;
    for (uint16_t p = 0; p < _modelRamPages; ++p)
    {
        _captureWork.pagesVisited++;
        const bool dirty = (dirtyCursor < dirtyPages.size() && dirtyPages[dirtyCursor] == p);
        if (dirty)
            ++dirtyCursor;

        // A key frame must be a SELF-CONTAINED snapshot, so it re-interns every
        // page that holds data - not just the ones dirtied since the previous
        // checkpoint.
        //
        // Sharing prev's slots for clean pages is correct only while the chain
        // still reaches the session's first checkpoint, the one place that ever
        // captured RAM in full. Anything that re-anchors a timeline (truncate +
        // resume-from-here, a restored session, a dropped baseline) leaves key
        // frames inheriting refs for pages that were never captured, and their
        // content is gone for good - the symptom being a seek that restores
        // correct registers into empty memory.
        //
        // Untouched pages stay cheap: an all-zero page is recognised by the
        // store and interned as a shared Zero slot, so the cost of a key frame
        // is proportional to the RAM that actually holds something.
        bool capture = dirty;
        if (isKeyFrame && !dirty)
        {
            const uint8_t* pageData = _memory->RAMPageAddress(p);
            if (pageData != nullptr && !IsPageAllZero(pageData))
                capture = true;
        }

        if (!capture)
        {
            // Clean page (I-frame OR P-frame): share prev's slots via AddRef.
            // The I-frame path deliberately does NOT re-intern clean pages —
            // doing so would (a) waste storage duplicating unchanged content
            // and (b) drop prevRamPages's ref via a spurious Release, which
            // was the root cause of the backward-seek screen-corruption bug.
            // Sharing is correctness-equivalent because restore reads the
            // same bytes regardless of who else references the slot.
            const TTDPageRef& prevRef = prevRamPages[p];
            if (!prevRef.IsNeverTouched())
            {
                for (uint32_t s = 0; s < 4; ++s)
                {
                    if (prevRef.pageSlots[s] != TTDPageRef::kNeverTouched)
                    {
                        _pageStore.AddRef(prevRef.pageSlots[s]);
                    }
                }
                // outRamPages[p].pageSlots[s] already copied from prevRef above.
            }
            // else: was NEVER_TOUCHED, still NEVER_TOUCHED — nothing to do.
            continue;
        }

        // Page carrying data: re-intern each sub-page. I-frame uses InternFull so
        // the new slot is an independent anchor (no delta-chain dependency);
        // P-frame uses InternXor which falls back to Full automatically when
        // XOR doesn't compress well. Both paths leave prevRamPages's slots
        // untouched — the previous checkpoint must remain restorable.
        const uint8_t* pageData = _memory->RAMPageAddress(p);
        if (pageData == nullptr)
        {
            outRamPages[p].SetNeverTouched();
            continue;
        }

        for (uint32_t s = 0; s < 4; ++s)
        {
            const uint8_t* sub = pageData + (s * TTDCodecPageStore::kPageSize);
            const uint32_t prevSlot = prevRamPages[p].pageSlots[s];

            if (isKeyFrame || prevSlot == TTDPageRef::kNeverTouched)
            {
                // I-frame dirty page, or first-ever capture of this sub-page:
                // emit an independent Full snapshot so future P-frames can
                // build fresh XOR chains off a known-good anchor.
                outRamPages[p].pageSlots[s] = _pageStore.InternFull(sub);
            }
            else
            {
                // P-frame dirty page: XOR against prev.
                // OPTIMIZATION: Use cached previous page to avoid decompression
                const size_t cacheOffset = (p * 4 + s) * TTDCodecPageStore::kPageSize;
                if (_prevPageCacheValid && cacheOffset + TTDCodecPageStore::kPageSize <= _prevPageCache.size())
                {
                    // Use cached path - no decompression needed
                    outRamPages[p].pageSlots[s] = _pageStore.InternXorCached(
                        prevSlot, sub, &_prevPageCache[cacheOffset]);
                }
                else
                {
                    // Fallback to decompression path (first frame or cache miss)
                    outRamPages[p].pageSlots[s] = _pageStore.InternXor(prevSlot, sub);
                }
            }
        }
    }

    // Safety net for configurations whose page bound is wrong. The walk above
    // consumes dirtyPages in ascending order, so anything left over sits at a
    // page index at or beyond _modelRamPages and was NOT captured. Silently
    // dropping it is how the 48K screen went missing; make it loud instead.
    // Logged once per session to keep a mis-sized model from flooding the log.
    if (dirtyCursor < dirtyPages.size() && !_dirtyPageOverflowReported)
    {
        _dirtyPageOverflowReported = true;
        MLOGWARNING("TimeTravelManager::UpdateRamPages — %zu dirty page(s) at or beyond the page bound %u were not "
                    "captured (first is page %u). The model's RAM page set is wider than ResolveModelRamPages() "
                    "reports; recordings for this configuration are incomplete.",
                    dirtyPages.size() - dirtyCursor,
                    static_cast<unsigned>(_modelRamPages),
                    static_cast<unsigned>(dirtyPages[dirtyCursor]));
    }
}

void TimeTravelManager::ReleaseCheckpointRefs(TTDCheckpoint& cp)
{
    for (auto& ref : cp.ramPages)
    {
        if (!ref.IsNeverTouched())
        {
            for (uint32_t s = 0; s < 4; ++s)
            {
                if (ref.pageSlots[s] != TTDPageRef::kNeverTouched)
                {
                    _pageStore.Release(ref.pageSlots[s]);
                    ref.pageSlots[s] = TTDPageRef::kNeverTouched;
                }
            }
        }
    }
}

void TimeTravelManager::UpdatePrevPageCache()
{
    if (!_memory || _modelRamPages == 0)
    {
        _prevPageCacheValid = false;
        return;
    }

    // Allocate cache on first use: _modelRamPages * 4 sub-pages * 4KB each
    const size_t cacheSize = static_cast<size_t>(_modelRamPages) * 4 * TTDCodecPageStore::kPageSize;
    if (_prevPageCache.size() != cacheSize)
    {
        _prevPageCache.resize(cacheSize);
    }

    // Copy current RAM state into cache
    for (uint16_t p = 0; p < _modelRamPages; ++p)
    {
        const uint8_t* pageData = _memory->RAMPageAddress(p);
        if (pageData)
        {
            const size_t cacheOffset = static_cast<size_t>(p) * 4 * TTDCodecPageStore::kPageSize;
            std::memcpy(&_prevPageCache[cacheOffset], pageData, PAGE_SIZE);
            _captureWork.deltaBaseBytes += PAGE_SIZE;
        }
    }

    _prevPageCacheValid = true;
}

uint16_t TimeTravelManager::ResolveModelRamPages() const
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
        MLOGWARNING("TimeTravelManager::ResolveModelRamPages — implausible ramsize=%u KB, falling back to MAX_RAM_PAGES",
                    cfg.ramsize);
        return MAX_RAM_PAGES;
    }
    return static_cast<uint16_t>(cfg.ramsize / (PAGE_SIZE / 1024));
}

const TTDCheckpoint* TimeTravelManager::GetCheckpoint(size_t idx) const
{
    if (idx >= _timeline.size())
        return nullptr;
    return &_timeline[idx];
}

// ---------------------------------------------------------------------------
// Restore path (Phase 2 Item 1; parent TDD §8.1 step 2)
// ---------------------------------------------------------------------------

bool TimeTravelManager::RestoreCheckpointForTesting(size_t idx)
{
    if (idx >= _timeline.size())
    {
        MLOGWARNING("TimeTravelManager::RestoreCheckpointForTesting — idx %zu out of range (timeline size=%zu)",
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
        MLOGWARNING("TimeTravelManager::RestoreCheckpointForTesting — missing dependencies");
        return false;
    }

    RestoreCheckpointForReplay(_timeline[idx]);
    return true;
}

uint64_t TimeTravelManager::ComputeRomSignature() const
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

bool TimeTravelManager::RegisterModelPeripherals(std::string* err)
{
    // The device set is enumerated in one place (ttdmachineperipherals.cpp),
    // shared with MachineStateTransfer. On failure the registry is left empty.
    return RegisterMachinePeripherals(_context, _peripherals, _ownedPeripherals, err);
}

void TimeTravelManager::ReleaseModelPeripherals()
{
    // Clear wholesale rather than unregistering piecemeal: most registered
    // devices are owned by the emulator, so there is no local list of them to
    // walk, and this manager is the only thing that ever registers anything.
    _peripherals.Clear();
    _ownedPeripherals.clear();
}

void TimeTravelManager::RestoreCheckpoint(const TTDCheckpoint& cp)
{
    assert(_context && _memory);

    MLOGINFO("TimeTravelManager::RestoreCheckpoint — frame=%llu, globalT=%llu, ramPages=%zu",
             static_cast<unsigned long long>(cp.time.frame),
             static_cast<unsigned long long>(cp.globalT),
             cp.ramPages.size());

    // --- Step 1: CPU registers (TDD §8.1 step 2a) ---
    // Z80 inherits from Z80State (see z80.h), so Z80* IS-A Z80State*.
    // Host-side fields (MemIf pointers, trace cursors, isDebugMode,
    // prev_pc/m1_pc/last_branch/nextpc) are preserved by RestoreCpuState —
    // they remain valid because we're not tearing down the emulator.
    using PerfClock = std::chrono::steady_clock;
    auto elapsedNs = [](PerfClock::time_point from, PerfClock::time_point to) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count());
    };
    const PerfClock::time_point restoreStart = PerfClock::now();

    Z80* cpu = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (cpu)
    {
        RestoreCpuState(cp.cpu, static_cast<Z80State*>(cpu));
    }

    // --- Step 2: Chipset port latches + counters (TDD §8.1 step 2b) ---
    // RestoreChipsetState is a pure field copy into emulatorState. It does
    // NOT re-run the port decoder — that's the next sub-step.
    RestoreChipsetState(cp.chipset, &_context->emulatorState);

    // The CPU's in-frame position (the frame-end overshoot) - before the
    // peripherals load, so devices rebuild their timelines around the
    // position the machine really resumes at
    if (cpu)
    {
        cpu->t = GetChipsetCpuTInFrame(cp.chipset);

        // Frame geometry (frame limit, INT window) derives from the restored
        // multiplier; every run path reads it from the CPU
        static_cast<Z80*>(cpu)->RecomputeFrameTiming();
    }

    // --- Step 2a2: Model-specific chipset state (TDD §6.4) ---
    // MUST run before UpdateZ80Banks: model serializers restore latches that
    // feed the paging chain (on Scorpion the ProfROM plane and the #1FFD
    // service/RAM0 bits), so rebuilding banks first would page from stale
    // values and then never re-derive.
    // A device set that differs from the checkpoint's (FR-4) leaves devices
    // in the live machine's state; say so instead of restoring silently
    const PerfClock::time_point devicesStart = PerfClock::now();
    const TTDRestoreReport devices = _peripherals.RestoreAll(cp.peripheralBlobs);
    const PerfClock::time_point devicesEnd = PerfClock::now();
    if (!devices.Complete())
        MLOGWARNING("TimeTravelManager::RestoreCheckpoint — frame %llu: device set differs from the checkpoint "
                    "(%zu restored, %zu without state, %zu size mismatches, %zu unclaimed)",
                    static_cast<unsigned long long>(cp.time.frame), devices.restored, devices.missingBlobs,
                    devices.sizeMismatches, devices.unclaimedBlobs);

    // --- Step 2b: Rebuild memory banking from restored port latches ---
    // Memory::UpdateZ80Banks reads the latches we just wrote and rebuilds
    // the four-bank mapping (ROM/RAM page in each 16 KB slot). Pentagon 128K
    // uses only p7FFD; extended models would extend this (Phase 2 carry).
    _memory->UpdateZ80Banks();

    // --- Step 3: RAM page content (TDD §8.1 step 2c) ---
    // Memcpy every referenced page from the COW page store into the live
    // Memory backing store. The optimization to skip pages whose content
    // already matches is deferred (TDD §8.1 "often a handful of pages";
    // restore is rare, not a per-frame hot path).
    RestoreRamPages(cp.ramPages);
    const PerfClock::time_point memoryEnd = PerfClock::now();

    // --- Step 5: Screen (TDD §8.1 step 2e) ---
    // The screen renderer caches derived state (active screen bank from
    // p7FFD, border color from pFE, framebuffer pixels) that the field
    // copies above do NOT update — the restore bypassed the port decoder.
    // ResyncScreenState re-derives it (the snapshot loader, loader_z80.cpp,
    // uses the same pattern for the same reason). Pixels are not touched:
    // what a position shows is decided by ComposeDisplay alone.
    ResyncScreenState();

    _perf.lastRestoreCpuChipsetNs = elapsedNs(restoreStart, devicesStart);
    _perf.lastRestoreDevicesNs = elapsedNs(devicesStart, devicesEnd);
    _perf.lastRestoreMemoryNs = elapsedNs(devicesEnd, memoryEnd);
    _perf.lastRestoreScreenNs = elapsedNs(memoryEnd, PerfClock::now());

    // t_states and frame_counter were already restored by RestoreChipsetState.
}

void TimeTravelManager::ResyncScreenState()
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

void TimeTravelManager::RestoreRamPages(const std::vector<TTDPageRef>& ramPages)
{
    const uint16_t pages = std::min<uint16_t>(_modelRamPages,
                                              static_cast<uint16_t>(ramPages.size()));
    for (uint16_t p = 0; p < pages; ++p)
    {
        const TTDPageRef& ref = ramPages[p];
        if (ref.IsNeverTouched())
            continue;  // Live RAM content is correct for this page.

        uint8_t* pageData = _memory->RAMPageAddress(p);
        if (!pageData)
        {
            MLOGWARNING("TimeTravelManager::RestoreRamPages — null RAMPageAddress for page %u",
                        static_cast<unsigned>(p));
            continue;
        }

        // Restore each of the 4 × 4 KB sub-pages individually. GetPage
        // returns false on CRC mismatch — log and continue with zero-fill
        // for the affected sub-page so the rest of the page is still restored.
        // The caller (RestoreCheckpoint) can detect the corruption via
        // subsequent verification (e.g., CaptureRestoreSelfTest hash compare).
        for (uint32_t s = 0; s < 4; ++s)
        {
            const uint32_t slot = ref.pageSlots[s];
            if (slot == TTDPageRef::kNeverTouched)
            {
                // Sub-page was never touched in session — leave live bytes alone.
                continue;
            }

            uint8_t* subDst = pageData + (s * TTDCodecPageStore::kPageSize);
            if (!_pageStore.GetPage(slot, subDst))
            {
                MLOGWARNING("TimeTravelManager::RestoreRamPages — CRC mismatch on page=%u sub=%u slot=%u; zero-filling",
                            static_cast<unsigned>(p), static_cast<unsigned>(s),
                            static_cast<unsigned>(slot));
                std::memset(subDst, 0, TTDCodecPageStore::kPageSize);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Silent replay mode (Phase 2 Item 2; parent TDD §8.2 + Appendix C)
// ---------------------------------------------------------------------------

void TimeTravelManager::EnterReplayMode()
{
    if (_inReplayMode)
        return;  // Idempotent + nest-safe: do NOT overwrite saved mute state

    if (!_context)
    {
        MLOGWARNING("TimeTravelManager::EnterReplayMode — null _context, cannot engage replay mode");
        return;
    }

    // Capture current SoundManager mute state so ExitReplayMode can restore
    // it exactly. The TDD is explicit that host-buffer submission is muted
    // but device ticks (handleStep / handleFrameStart) keep running — using
    // the existing mute() facility is precisely this contract, since mute
    // only zeroes the output buffer at the host boundary in handleFrameEnd.
    if (_context->pSoundManager)
    {
        _soundMuteBeforeReplay = _context->pSoundManager->isMuted();
        _context->pSoundManager->mute();
    }
    else
    {
        _soundMuteBeforeReplay = false;
    }

    _context->ttdReplayActive = true;
    _inReplayMode = true;

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

    MLOGINFO("TimeTravelManager::EnterReplayMode — replay mode engaged (sound mute saved=%d)",
             static_cast<int>(_soundMuteBeforeReplay));
}

void TimeTravelManager::ExitReplayMode()
{
    if (!_inReplayMode)
        return;  // Idempotent

    if (!_context)
    {
        MLOGWARNING("TimeTravelManager::ExitReplayMode — null _context, cannot disengage replay mode");
        return;
    }

    _context->ttdReplayActive = false;
    _inReplayMode = false;

    if (Core* core = _context->pCore)
    {
        core->GetZ80()->isDebugMode = _debugModeBeforeReplay;
        core->SelectMemoryInterface();
    }
    if (_memory)
        _memory->UpdateFeatureCache();

    // Restore the saved mute state. If the user had muted audio before the
    // seek, they want it muted after; if not, the existing unmute() path is
    // the right call.
    if (_context->pSoundManager)
    {
        if (_soundMuteBeforeReplay)
            _context->pSoundManager->mute();
        else
            _context->pSoundManager->unmute();
    }

    MLOGINFO("TimeTravelManager::ExitReplayMode — replay mode disengaged (sound mute restored)");
}

bool TimeTravelManager::IsReplayActive() const
{
    return _context && _context->ttdReplayActive;
}

// ---------------------------------------------------------------------------
// Input journal (Phase 2 Item 3; parent TDD §5 row #1)
// ---------------------------------------------------------------------------

void TimeTravelManager::RecordInputEvent(uint8_t key, bool pressed)
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

void TimeTravelManager::RecordMouseMove(int dx, int dy)
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

void TimeTravelManager::RecordMouseButtons(uint8_t activeLowMask)
{
    if (!_context)
        return;
    TTDInputEvent ev;
    ev.time       = InputEventTimeNow(_context);
    ev.kind       = TTDInputKind::MouseButtons;
    ev.buttonMask = activeLowMask;
    _inputJournal.Record(ev);
}

void TimeTravelManager::RecordMouseWheel(int steps)
{
    if (!_context)
        return;
    TTDInputEvent ev;
    ev.time       = InputEventTimeNow(_context);
    ev.kind       = TTDInputKind::MouseWheel;
    ev.wheelSteps = static_cast<int8_t>(steps);
    _inputJournal.Record(ev);
}

void TimeTravelManager::RecordKeyboardReset()
{
    if (!_context)
        return;
    TTDInputEvent ev;
    ev.time = InputEventTimeNow(_context);
    ev.kind = TTDInputKind::KeyboardReset;
    _inputJournal.Record(ev);
}

void TimeTravelManager::RecordMouseCounters(uint8_t x, uint8_t y)
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

bool TimeTravelManager::OwnsInput() const
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

void TimeTravelManager::SetLiveInputInterceptor(std::function<bool(const TTDInputEvent&)> interceptor)
{
    std::lock_guard<std::mutex> lock(_liveInputInterceptorMutex);
    _liveInputInterceptor = std::move(interceptor);
}

bool TimeTravelManager::SubmitLiveInput(const TTDInputEvent& ev)
{
    return SubmitLiveInputImpl(ev, nullptr, nullptr, 0);
}

bool TimeTravelManager::SubmitLiveInput(const TTDInputEvent& ev, const TTDNetInput& net, const uint8_t* payload,
                                        uint32_t length)
{
    return SubmitLiveInputImpl(ev, &net, payload, length);
}

bool TimeTravelManager::SubmitLiveInputImpl(const TTDInputEvent& ev, const TTDNetInput* net, const uint8_t* payload,
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

TimeTravelManager::MachineTaskResult TimeTravelManager::SubmitMachineTask(std::function<void()> task)
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

void TimeTravelManager::ApplyLiveInput(TTDInputEvent ev, const TTDNetInput* net, const uint8_t* payload,
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

void TimeTravelManager::ServiceInput()
{
    if (!_context)
        return;

    // 1. Journal playback: every event due at or before the current machine time
    if (_inputPlaybackArmed)
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

void TimeTravelManager::UpdateInputWorkFlag()
{
    if (!_context)
        return;

    bool pending;
    {
        std::lock_guard<std::mutex> lock(_pendingInputMutex);
        pending = !_pendingInput.empty() || !_pendingTasks.empty();
    }
    _context->SetStepWork(EmulatorContext::kStepWorkTtdInput, _inputPlaybackArmed || pending);
}

void TimeTravelManager::ArmInputPlayback()
{
    if (!_context)
        return;

    // Events stamped with exactly the restored time were applied after the
    // checkpoint was captured, before the next instruction: the cursor starts
    // at them and the first step applies them (Z80::StepInstruction)
    _inputCursor = _inputJournal.FirstIndexAtOrAfter(InputEventTimeNow(_context));
    _inputPlaybackArmed = _inputCursor < _inputJournal.Size();
    UpdateInputWorkFlag();
}

void TimeTravelManager::DisarmInputPlayback()
{
    _inputPlaybackArmed = false;
    _inputCursor = 0;
    UpdateInputWorkFlag();
}

void TimeTravelManager::OnMachineReset()
{
    const PublishOnExit publish{*this};
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

void TimeTravelManager::RestoreCheckpointForReplay(const TTDCheckpoint& cp)
{
    RestoreCheckpoint(cp);
    ArmInputPlayback();

    // The CPU replays the recorded IN results from here; the live devices
    // still answer, and a differing answer is counted, not used
    if (_portJournalValid)
    {
        _portReads.StartPlayback(cp.portReadCursor);
        _portWrites.StartPlayback(cp.portWriteCursor);
    }
    else
    {
        _portReads.Stop();
        _portWrites.Stop();
    }
    SyncPortJournalHook();
}

const char* TimeTravelManager::PortJournalUnsupportedReason() const
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
        if (gs && gs->implementation() == GSCardImplementation::NGS)
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

void TimeTravelManager::DropPortJournal(const char* reason)
{
    if (!_portJournalValid)
        return;
    MLOGWARNING("TimeTravelManager — port-read journal dropped: %s; replay reads the live devices again", reason);
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

void TimeTravelManager::SyncPortJournalHook()
{
    if (!_context)
        return;
    _context->ttdPortReads = _portReads.GetMode() == TTDPortJournal::Mode::Off ? nullptr : &_portReads;
    _context->ttdPortWrites = _portWrites.GetMode() == TTDPortJournal::Mode::Off ? nullptr : &_portWrites;
}

TTDPortSearchResult TimeTravelManager::SearchPortEvents(const TTDPortQuery& q) const
{
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
    return ttd::SearchPortEvents(_portReads, _portWrites, q);
}

// ---------------------------------------------------------------------------
// External-event markers (Phase 2 Item 6; parent TDD §5.1)
// ---------------------------------------------------------------------------

void TimeTravelManager::RecordExternalEvent(TTDExternalEventKind kind, const char* reason)
{
    if (!_context)
        return;

    // Same defensive guard as RecordInputEvent: callers (Tape, BetaDisk,
    // debugger edit paths) are expected to check IsRecording() first, but a
    // stray call when not recording is a no-op rather than a journal
    // corruption.
    if (_state != TTDSessionState::Recording)
        return;

    const EmulatorState& st = _context->emulatorState;
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;

    TTDExternalEvent ev;
    ev.time.frame    = st.frame_counter;
    ev.time.tInFrame = z80 ? st.TtdTInFrame(z80->t) : 0;
    ev.kind          = kind;

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

    MLOGINFO("TimeTravelManager::RecordExternalEvent — recorded marker at "
             "(frame=%llu,tInFrame=%u) kind=%s reason='%.63s'",
             static_cast<unsigned long long>(ev.time.frame),
             static_cast<unsigned>(ev.time.tInFrame),
             TTDExternalEventKindToString(kind),
             ev.reason);
}

// ---------------------------------------------------------------------------
// Seek engine (Phase 2 Item 4; parent TTD §8.1)
// ---------------------------------------------------------------------------

TTDTimePoint TimeTravelManager::CurrentPosition() const
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

uint32_t TimeTravelManager::TInFrameNow() const
{
    if (!_context)
        return 0;
    const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    return z80 ? _context->emulatorState.TtdTInFrame(z80->t) : 0;
}

uint32_t TimeTravelManager::FrameSpan() const
{
    if (!_context)
        return 69888;
    const uint8_t units = _context->emulatorState.ttd_clock_units;
    return _context->config.frame * (units ? units : 1);
}

void TimeTravelManager::RunToTInFrame(uint32_t targetTInFrame)
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

TTDTimePoint TimeTravelManager::SessionEndPosition() const
{
    if (_timeline.empty())
        return TTDTimePoint{};
    return _timeline.back().time;
}

bool TimeTravelManager::SeekTo(const TTDTimePoint& target, TTDSeekResult* outResult)
{
    const PublishOnExit publish{*this};
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
    if (_state == TTDSessionState::Recording)
    {
        if (outResult)
        {
            outResult->reached        = false;
            outResult->arrivedAt      = TTDTimePoint{};
            outResult->haltReason     = TTDSeekHaltReason::OutOfRange;
            outResult->blockingMarker = TTDExternalEvent{};
        }
        MLOGWARNING("TimeTravelManager::SeekTo — rejected: session is Recording "
                    "(call StopRecording first to preserve history)");
        return false;
    }

    TTDSeekResult localResult;
    TTDSeekResult& result = outResult ? *outResult : localResult;
    const bool ok = SeekToInternal(target, &result);

    // A marker halt still moved the machine, so it is shown too. Positioning
    // by frame number (tInFrame 0) shows the frame's final picture; any other
    // point shows what the beam drew up to it (display rule, design §3).
    if (ok || result.haltReason == TTDSeekHaltReason::ExternalEvent)
        PresentPosition(ok && target.tInFrame == 0);

    return ok;
}

// ---------------------------------------------------------------------------
// Agent bookmarks (TD-4)
// ---------------------------------------------------------------------------

bool TimeTravelManager::AddBookmark(const TTDTimePoint& time, const std::string& label,
                                    std::string* err)
{
    // A bookmark into empty history dangles immediately — there is no
    // checkpoint to return to. Refuse at creation instead of at seek time.
    if (_timeline.empty())
    {
        if (err)
            *err = "no recorded history to bookmark (start recording first)";
        MLOGWARNING("TimeTravelManager::AddBookmark — rejected: timeline is empty");
        return false;
    }

    // Same principle for a position past the session end: the bookmark can
    // never be reached, so it must never be created.
    const TTDTimePoint end = SessionEndPosition();
    if (end < time)
    {
        if (err)
            *err = "bookmark position (frame=" + std::to_string(time.frame) +
                   ", tInFrame=" + std::to_string(time.tInFrame) +
                   ") is beyond the session end (frame=" + std::to_string(end.frame) + ")";
        MLOGWARNING("TimeTravelManager::AddBookmark — rejected: frame %llu beyond session end %llu",
                    static_cast<unsigned long long>(time.frame),
                    static_cast<unsigned long long>(end.frame));
        return false;
    }

    TTDBookmark bookmark;
    bookmark.time  = time;
    bookmark.label = label;
    return _bookmarks.Add(bookmark, err);
}

std::vector<TTDBookmark> TimeTravelManager::GetBookmarks() const
{
    return _bookmarks.Snapshot();
}

bool TimeTravelManager::FindBookmark(const std::string& label, TTDBookmark& out) const
{
    return _bookmarks.Find(label, out);
}

bool TimeTravelManager::RemoveBookmark(const std::string& label)
{
    return _bookmarks.Remove(label);
}

bool TimeTravelManager::SeekToBookmark(const std::string& label, TTDSeekResult* outResult,
                                       std::string* err)
{
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
        MLOGWARNING("TimeTravelManager::SeekToBookmark — unknown label '%s'", label.c_str());
        return false;
    }

    // Nothing bookmark-specific from here on — a bookmark seek IS a seek.
    // halt_reason semantics are exactly the plain SeekTo's, so a real barrier
    // between the restore checkpoint and the target still surfaces as
    // "external_event" and the bookmark itself can never be one.
    return SeekTo(bookmark.time, outResult);
}

void TimeTravelManager::PublishSeekedFrame()
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
        MLOGERROR("TimeTravelManager::PublishSeekedFrame — MessageCenter post failed: %s",
                  e.what());
    }
}



bool TimeTravelManager::SeekToInternal(const TTDTimePoint& target, TTDSeekResult* outResult)
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
        MLOGWARNING("TimeTravelManager::SeekToInternal — null _context");
        return false;
    }

    // Idle-with-history is allowed (typical after StopRecording); Detached
    // is the other valid state. Recording is also allowed for internal
    // callers (ResumeRecordingFrom) — they manage the invariant themselves.
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::SeekToInternal — timeline is empty "
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
        MLOGWARNING("TimeTravelManager::SeekTo — target frame %llu "
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
    auto upperIt = std::upper_bound(_timeline.begin(), _timeline.end(), target,
        [](const TTDTimePoint& t, const TTDCheckpoint& cp) {
            return t < cp.time;
        });

    if (upperIt == _timeline.begin())
    {
        // Every checkpoint is strictly greater than target — target is
        // before the first captured frame. This shouldn't be reachable
        // (we'd have failed the sessionEnd check above if target was
        // out of bounds, and target < first checkpoint means target < (0,0)
        // which is impossible for an unsigned coordinate). Defensive.
        MLOGWARNING("TimeTravelManager::SeekTo — target precedes the first checkpoint");
        return false;
    }

    const size_t cpIdx = static_cast<size_t>((upperIt - _timeline.begin()) - 1);
    const TTDCheckpoint& cp = _timeline[cpIdx];

    MLOGINFO("TimeTravelManager::SeekTo — target=(frame=%llu,tInFrame=%u) "
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
    if (target.tInFrame > restoredTInFrame)
    {
        if (const TTDExternalEvent* barrier = _externalEvents.FirstMarkerInInterval(cp.time, target))
        {
            MLOGINFO("TimeTravelManager::SeekTo — marker barrier at (frame=%llu,tInFrame=%u) "
                     "kind=%s reason='%.63s'; stopping replay at marker",
                     static_cast<unsigned long long>(barrier->time.frame),
                     static_cast<unsigned>(barrier->time.tInFrame),
                     TTDExternalEventKindToString(barrier->kind),
                     barrier->reason);

            // Replay only as far as the marker — its effect is reproducible
            // up to but not including the marker itself.
            if (barrier->time.tInFrame > 0)
                ReplayWithinFrame(cp.time.frame, barrier->time.tInFrame);

            SetState(TTDSessionState::Detached);

            if (outResult)
            {
                outResult->reached        = false;
                outResult->arrivedAt      = barrier->time;
                outResult->haltReason     = TTDSeekHaltReason::ExternalEvent;
                outResult->blockingMarker = *barrier;
            }
            return false;
        }

        ReplayWithinFrame(cp.time.frame, target.tInFrame);
    }

    // ------------------------------------------------------------------
    // Step 4: transition to Detached (TDD §4.2).
    // ------------------------------------------------------------------
    SetState(TTDSessionState::Detached);

    MLOGINFO("TimeTravelManager::SeekTo — arrived at (frame=%llu,tInFrame=%u), state=Detached",
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

void TimeTravelManager::ReplayWithinFrame(uint64_t targetFrame, uint32_t targetTInFrame)
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
        MLOGWARNING("TimeTravelManager::ReplayWithinFrame — null _context or pEmulator, "
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
        MLOGWARNING("TimeTravelManager::ReplayWithinFrame — targetTInFrame=%u > "
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
    EnterReplayMode();

    // ------------------------------------------------------------------
    // Run to the target. Recorded input is applied by the stepping engine
    // itself (ServiceInput after every instruction, armed by the restore
    // that positioned the machine), at exactly the instruction boundaries it
    // was recorded at - the same path a Detached forward run uses.
    // ------------------------------------------------------------------
    // The CPU resumes at the checkpoint's overshoot, not at 0
    RunToTInFrame(targetTInFrame);

    ExitReplayMode();
}

void TimeTravelManager::RunToFrameEnd()
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

void TimeTravelManager::ComposeDisplay(bool frameTarget)
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
        auto it = std::upper_bound(_timeline.begin(), _timeline.end(), f,
                                   [](uint64_t value, const TTDCheckpoint& cp) { return value < cp.time.frame; });
        return it == _timeline.begin() ? nullptr : &*(it - 1);
    };

    // Deliberately a LOCAL snapshot, not the shared _liveSnapshot member:
    // RunTStates pumps MessageCenter (NC_EXECUTION_CPU_STEP) and a synchronous
    // subscriber (debugger views) can reenter GetFrameCache, which saves and
    // restores through _liveSnapshot.
    LiveStateSnapshot local;
    SaveLiveState(local);
    EnterReplayMode();

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

    ExitReplayMode();
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

void TimeTravelManager::PresentPosition(bool frameTarget)
{
    const auto start = std::chrono::steady_clock::now();
    ComposeDisplay(frameTarget);
    PublishSeekedFrame();
    _perf.lastPresentNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
}

bool TimeTravelManager::StepBackFrame()
{
    if (_state == TTDSessionState::Recording)
    {
        MLOGWARNING("TimeTravelManager::StepBackFrame — rejected: session is Recording "
                    "(call StopRecording first)");
        return false;
    }

    // Idle-with-history is allowed; only the timeline-empty case fails.
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::StepBackFrame — no recorded history");
        return false;
    }

    const TTDTimePoint current = CurrentPosition();
    if (current.frame == 0)
    {
        MLOGINFO("TimeTravelManager::StepBackFrame — already at frame 0, cannot step back");
        return false;
    }

    // Frame steps are positioning by frame number: land on the frame boundary
    // and show that frame's final picture. Carrying current.tInFrame (the
    // previous checkpoint's instruction overshoot) turned a frame step into
    // an intra-frame seek whose overshoot grew with every step.
    return SeekTo(TTDTimePoint{current.frame - 1, 0});
}

bool TimeTravelManager::StepForwardFrame()
{
    if (_state == TTDSessionState::Recording)
    {
        MLOGWARNING("TimeTravelManager::StepForwardFrame — rejected: session is Recording "
                    "(call StopRecording first)");
        return false;
    }

    // Idle-with-history is allowed; only the timeline-empty case fails.
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::StepForwardFrame — no recorded history");
        return false;
    }

    const TTDTimePoint current   = CurrentPosition();
    const TTDTimePoint sessionEnd = SessionEndPosition();

    if (current.frame >= sessionEnd.frame)
    {
        MLOGINFO("TimeTravelManager::StepForwardFrame — already at or past the "
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

bool TimeTravelManager::ResumeRecordingFrom(const TTDTimePoint& from)
{
    const PublishOnExit publish{*this};  // the caller resumes the machine afterwards
    // Returning to live recording ends the browse scope: free the decode cache.
    ClearFrameCache();

    // ------------------------------------------------------------------
    // Validate preconditions. Same shape as SeekTo — the truncation rule
    // is meaningless without a recorded timeline to truncate.
    // ------------------------------------------------------------------
    if (!_context)
    {
        MLOGWARNING("TimeTravelManager::ResumeRecordingFrom — null _context");
        return false;
    }

    if (_state == TTDSessionState::Idle)
    {
        MLOGWARNING("TimeTravelManager::ResumeRecordingFrom — session is Idle "
                    "(no history to resume from)");
        return false;
    }

    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::ResumeRecordingFrom — timeline is empty");
        return false;
    }

    const TTDTimePoint sessionEnd = _timeline.back().time;
    if (sessionEnd < from)
    {
        MLOGWARNING("TimeTravelManager::ResumeRecordingFrom — target "
                    "(frame=%llu, tInFrame=%u) is beyond session end "
                    "(frame=%llu, tInFrame=%u)",
                    static_cast<unsigned long long>(from.frame),
                    static_cast<unsigned>(from.tInFrame),
                    static_cast<unsigned long long>(sessionEnd.frame),
                    static_cast<unsigned>(sessionEnd.tInFrame));
        return false;
    }

    const size_t preTimelineSize  = _timeline.size();
    const size_t preJournalSize   = _inputJournal.Size();
    const size_t preMarkerCount   = _externalEvents.Size();

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

    // ------------------------------------------------------------------
    // Step 2: truncate timeline + page refs after `from`. Page refs held
    // by dropped checkpoints are released back to the page store; the
    // slots become eligible for reuse by future Intern calls (TDD §6.3).
    // ------------------------------------------------------------------
    TruncateTimelineAfter(from);

    // The delta base cache mirrors the RAM of the old session end, not of the
    // checkpoint the timeline now ends at: the next delta must be computed
    // against that checkpoint's decoded slots instead (the capture refills the cache)
    _prevPageCacheValid = false;

    // ------------------------------------------------------------------
    // Step 3: truncate input journal after the resume point. Events exactly
    // at it are kept (they happened at the resume point, not after it). The
    // resume point is where the machine actually stands: a frame-aligned
    // `from` restores the checkpoint's CPU at its overshoot past the frame
    // boundary (TTDChipsetState::cpu_t_in_frame), so events recorded there
    // after the seek sit at (from.frame, overshoot), not at tInFrame 0.
    // ------------------------------------------------------------------
    const TTDTimePoint here = CurrentPosition();
    const TTDTimePoint cut = (from < here) ? here : from;
    _inputJournal.DropAfter(cut);
    _externalEvents.DropAfter(cut);  // Phase 2 Item 6 — markers past the resume point are dead future
    _bookmarks.DropAfter(cut);  // TD-4 — bookmarks past the resume point are dead future

    // Coverage of the discarded future must go too, or reverse search prunes
    // frames of the new history by the old one. Frames before the resume
    // frame stay indexed; the resume frame itself is re-collected only from
    // the resume point on, so unless the machine stands exactly at that
    // frame's checkpoint it becomes a hole (queries replay it)
    const bool atCheckpoint = _seekLandedOnCheckpoint && !_timeline.empty() &&
                              _timeline.back().time.frame == cut.frame;
    _coverageIndex.DropFramesFrom(cut.frame, !atCheckpoint);

    // Port reads past the resume point are dead future: the seek left the
    // journal positioned at the first read after it
    if (_portJournalValid)
    {
        _portReads.TruncateTo(_portReads.Cursor());
        _portWrites.TruncateTo(_portWrites.Cursor());
    }

    // Phase 4 — write journal: convert the resume point to a globalT and
    // drop records strictly past it. Records exactly at it are kept.
    if (_writeJournal)
    {
        _writeJournal->DropAfter(GlobalT(cut));
    }

    // ------------------------------------------------------------------
    // Step 4: return to Recording. Next OnFrameBoundary will append a fresh
    // checkpoint at frame `from.frame + 1` (the live emulator's frame
    // counter is set by SeekTo). A stop may have switched the capture flags
    // back off; the machine is parked at `from` after the seek.
    // ------------------------------------------------------------------
    EngageCaptureFeatures();
    if (!_enableWriteJournal)
        MarkJournalGap("recording resumed with the write journal off");  // the writes from here on are not journaled
    SetState(TTDSessionState::Recording);
    DisarmInputPlayback();  // live input again (journaled while recording)
    // A loaded session had collection switched off; the new history is live
    _context->ttdCoverageActive = _enableCoverageIndex;
    if (_portJournalValid)
    {
        _portReads.StartRecording();
        _portWrites.StartRecording();
    }
    SyncPortJournalHook();

    MLOGINFO("TimeTravelManager::ResumeRecordingFrom — resumed at "
             "(frame=%llu, tInFrame=%u); timeline %zu→%zu checkpoints, "
             "journal %zu→%zu events, markers %zu→%zu, state=Recording",
             static_cast<unsigned long long>(from.frame),
             static_cast<unsigned>(from.tInFrame),
             preTimelineSize, _timeline.size(),
             preJournalSize, _inputJournal.Size(),
             preMarkerCount, _externalEvents.Size());

    return true;
}

bool TimeTravelManager::ResumeRecordingLive()
{
    // Leaving the browse scope: free the decode cache.
    ClearFrameCache();

    if (!_context)
    {
        MLOGWARNING("TimeTravelManager::ResumeRecordingLive — null _context");
        return false;
    }

    if (_state == TTDSessionState::Recording)
        return true;  // Idempotent

    if (_state == TTDSessionState::Detached)
    {
        MLOGWARNING("TimeTravelManager::ResumeRecordingLive — refused: session is Detached "
                    "(use ResumeRecordingFrom to continue from a historical point)");
        return false;
    }

    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::ResumeRecordingLive — timeline is empty "
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
        MLOGWARNING("TimeTravelManager::ResumeRecordingLive — refused: present frame %llu is %s "
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

    // Instructions executed since the stop (within this frame) wrote nothing
    // to the journal: from here on it cannot vouch for the whole session
    if (!_enableWriteJournal)
        MarkJournalGap("recording resumed with the write journal off");
    else if (GlobalT(present) != _recordingStoppedAtT)
        MarkJournalGap("the machine ran unrecorded between the stop and the resume");

    // The port-read journal has no room for a gap: a replay across the reads
    // made while stopped would hand out every later record one read early
    if (GlobalT(present) != _recordingStoppedAtT)
        DropPortJournal("the machine ran unrecorded between the stop and the resume");

    SetState(TTDSessionState::Recording);
    DisarmInputPlayback();  // live input again (journaled while recording)
    _context->ttdCoverageActive = _enableCoverageIndex;
    if (_portJournalValid)
    {
        _portReads.StartRecording();
        _portWrites.StartRecording();
    }
    SyncPortJournalHook();

    MLOGINFO("TimeTravelManager::ResumeRecordingLive — resumed at (frame=%llu, tInFrame=%u); "
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

void TimeTravelManager::SetHistoryLimit(uint64_t maxFrames, uint64_t maxBytes)
{
    _historyLimitFrames.store(maxFrames, std::memory_order_release);
    _historyLimitBytes.store(maxBytes, std::memory_order_release);
    if (_state != TTDSessionState::Recording)
    {
        PublishSessionInfo();
        return;
    }

    // While the loop runs, the recording belongs to the machine's thread:
    // evicting from here would free checkpoints beside its capture (the Qt
    // history combo calls this while recording). That thread enforces the new
    // limit at its next frame boundary, as it does after every capture
    const bool loopRunning = _context && _context->pEmulator && _context->pEmulator->IsRunning();
    const bool onLoopThread = _context && _context->pMainLoop && _context->pMainLoop->IsRunThread();
    if (loopRunning && !onLoopThread)
    {
        _publishRequested.store(true, std::memory_order_release);
        return;
    }
    EnforceHistoryLimit();
    PublishSessionInfo();
}

uint64_t TimeTravelManager::BlobBytes(const TTDCheckpoint& cp)
{
    uint64_t bytes = 0;
    for (const auto& blob : cp.peripheralBlobs)
        bytes += blob.second.size();
    return bytes;
}

uint64_t TimeTravelManager::HistoryBytes() const
{
    return _pageStore.GetUsedBytes() + _blobBytes;
}

void TimeTravelManager::EnforceHistoryLimit()
{
    if (_historyLimitFrames == 0 && _historyLimitBytes == 0)
        return;
    if (_historyLimitFrames != 0 && _timeline.size() > _historyLimitFrames)
        EvictOldest(_timeline.size() - static_cast<size_t>(_historyLimitFrames));
    if (_historyLimitBytes != 0)
    {
        // Released slots free their bytes only once no later checkpoint shares
        // them, so measure again after each step; steps of 1/64 of the history
        // keep the vector erases few
        while (_timeline.size() > 2 && HistoryBytes() > _historyLimitBytes)
            EvictOldest(std::max<size_t>(1, _timeline.size() / 64));
    }
}

void TimeTravelManager::EvictOldest(size_t count)
{
    // Two checkpoints always stay: the start of the history and the present
    if (_timeline.size() <= 2 || count == 0)
        return;
    count = std::min(count, _timeline.size() - 2);

    // Each checkpoint decodes on its own: its delta pages hold references on
    // their base pages, so releasing the oldest ones never breaks a later one
    for (size_t i = 0; i < count; ++i)
    {
        ReleaseCheckpointRefs(_timeline[i]);
        _blobBytes -= BlobBytes(_timeline[i]);
    }
    _timeline.erase(_timeline.begin(), _timeline.begin() + static_cast<std::ptrdiff_t>(count));
    _evictedCheckpoints += count;

    // The journals start where the history now starts
    const TTDCheckpoint& front = _timeline.front();
    _inputJournal.DropBefore(front.time);
    _externalEvents.DropBefore(front.time);
    _bookmarks.DropBefore(front.time);
    if (_portJournalValid)
    {
        _portReads.DropBefore(front.portReadCursor);
        _portWrites.DropBefore(front.portWriteCursor);
    }
    if (_inputPlaybackArmed)
        _inputCursor = _inputJournal.FirstIndexAtOrAfter(InputEventTimeNow(_context));
    ClearFrameCache();
}

void TimeTravelManager::TruncateTimelineAfter(const TTDTimePoint& from)
{
    // ------------------------------------------------------------------
    // Find the first checkpoint strictly greater than `from`. Same
    // upper_bound comparator shape as SeekTo so the two methods agree
    // on "strictly after" (i.e. cp.time > from, NOT cp.time >= from).
    // ------------------------------------------------------------------
    auto upperIt = std::upper_bound(_timeline.begin(), _timeline.end(), from,
        [](const TTDTimePoint& t, const TTDCheckpoint& cp) {
            return t < cp.time;
        });

    if (upperIt == _timeline.end())
    {
        // Nothing to drop — every checkpoint is <= `from`. Common case
        // when `from` is exactly at the last captured frame boundary.
        return;
    }

    const size_t dropCount = static_cast<size_t>(_timeline.end() - upperIt);

    // Release page refs for each dropped checkpoint before erasing. The
    // refs are how the page store knows which slots are still in use by
    // some checkpoint; failing to release would leak slots.
    for (auto it = upperIt; it != _timeline.end(); ++it)
    {
        ReleaseCheckpointRefs(*it);
        _blobBytes -= BlobBytes(*it);
    }

    _timeline.erase(upperIt, _timeline.end());

    MLOGINFO("TimeTravelManager::TruncateTimelineAfter — dropped %zu checkpoints "
             "after (frame=%llu, tInFrame=%u); timeline now has %zu entries",
             dropCount,
             static_cast<unsigned long long>(from.frame),
             static_cast<unsigned>(from.tInFrame),
             _timeline.size());
}

// ---------------------------------------------------------------------------
// Session serialization (.ttd format)
// ---------------------------------------------------------------------------
//
// The .ttd binary format is the portable contract between every TTD consumer:
//   - core tests (round-trip verification)
//   - the CLI (`automation-cli ttd dump`)
//   - WebAPI wrapper (optional — a thin handler around SerializeSession)
//   - the Python analyzer in tools/verification/ttd-analyzer/
//   - any third-party tool that generates a parser from ttd.ksy
//
// Layout: core/src/debugger/ttd/ttd.ksy is the canonical schema and
// ttddumpformat.h holds the constants. A header, the codec page store (each
// slot: encoding, refcount, prev slot, CRC32C, zstd payload), the checkpoints
// (time, frame kind, key-frame anchor, CPU and chipset state, one slot ref per
// 4 KB sub-page, peripheral blobs), then the optional write journal, coverage
// index and bookmark sections flagged in the header. Kept out of this comment
// on purpose: a second copy of the layout went stale once already.
//
// We serialize only the live page-store slots (refcount > 0). The original
// slot indices are remapped to a compact [0..N) range via a map; checkpoints'
// ram_page_refs are translated through this map on write and read. This keeps
// the dump file proportional to the working set, not to high-water-mark
// capacity (free slots left behind by thinning are not emitted).

namespace {

/// @brief Write a POD struct verbatim to the stream (host order = LE on
/// little-endian hosts, which is asserted at the top of this file).
template <typename Pod>
bool WritePod(std::ostream& out, const Pod& value, std::string& err)
{
    static_assert(std::is_trivially_copyable<Pod>::value,
                  "WritePod requires trivially-copyable type");
    out.write(reinterpret_cast<const char*>(&value), sizeof(Pod));
    if (!out)
    {
        err = "stream write failed";
        return false;
    }
    return true;
}

/// @brief Read a POD struct verbatim from the stream.
template <typename Pod>
bool ReadPod(std::istream& in, Pod& value, std::string& err)
{
    static_assert(std::is_trivially_copyable<Pod>::value,
                  "ReadPod requires trivially-copyable type");
    in.read(reinterpret_cast<char*>(&value), sizeof(Pod));
    if (!in)
    {
        err = "stream read failed";
        return false;
    }
    return true;
}

/// @brief Write a length-prefixed byte vector (size as u32, then raw bytes).
/// Refuses a blob over the reader's cap: a file that saves must load.
bool WriteBlob(std::ostream& out, const std::vector<uint8_t>& blob, std::string& err)
{
    if (blob.size() > ttd::dump::kMaxPeripheralBlobBytes)
    {
        err = "device blob of " + std::to_string(blob.size()) + " bytes exceeds the " +
              std::to_string(ttd::dump::kMaxPeripheralBlobBytes) + "-byte limit";
        return false;
    }
    const uint32_t sz = static_cast<uint32_t>(blob.size());
    if (!WritePod(out, sz, err))
        return false;
    if (sz != 0)
    {
        out.write(reinterpret_cast<const char*>(blob.data()), sz);
        if (!out)
        {
            err = "stream write failed (blob body)";
            return false;
        }
    }
    return true;
}

/// @brief Read a length-prefixed byte vector written by WriteBlob.
bool ReadBlob(std::istream& in, std::vector<uint8_t>& blob, std::string& err)
{
    uint32_t sz = 0;
    if (!ReadPod(in, sz, err))
        return false;
    // Sanity cap shared with the writer (ttddumpformat.h): a bigger claim is
    // corruption, never a blob this build wrote
    if (sz > ttd::dump::kMaxPeripheralBlobBytes)
    {
        err = "implausible peripheral blob size " + std::to_string(sz);
        return false;
    }
    blob.resize(sz);
    if (sz != 0)
    {
        in.read(reinterpret_cast<char*>(blob.data()), sz);
        if (!in)
        {
            err = "stream read failed (blob body)";
            return false;
        }
    }
    return true;
}

/// @brief Input-journal section (header bit 6): u32 count, then per event the
/// fields of TTDInputEvent one by one (ttddumpformat.h, kFlagsHasInputJournal).
/// Field-by-field, never the struct: no padding byte reaches the file, and the
/// layout does not depend on the compiler.
bool WriteInputJournalSection(std::ostream& out, const std::vector<TTDInputEvent>& events, std::string& err)
{
    const uint32_t count = static_cast<uint32_t>(events.size());
    if (!WritePod(out, count, err)) return false;
    for (const TTDInputEvent& ev : events)
    {
        const uint8_t kind = static_cast<uint8_t>(ev.kind);
        const uint8_t pressed = ev.pressed ? 1 : 0;
        if (!WritePod(out, ev.time.frame, err) || !WritePod(out, ev.time.tInFrame, err) ||
            !WritePod(out, kind, err) || !WritePod(out, ev.key, err) || !WritePod(out, pressed, err) ||
            !WritePod(out, ev.dx, err) || !WritePod(out, ev.dy, err) || !WritePod(out, ev.buttonMask, err) ||
            !WritePod(out, ev.wheelSteps, err) || !WritePod(out, ev.value, err))
        {
            err = "stream write failed (input journal section)";
            return false;
        }
    }
    return true;
}

/// @brief Read the input-journal section. Refuses what a healthy writer cannot
/// produce: a count over the cap, an unknown kind, a pressed byte other than
/// 0 / 1, events out of time order.
bool ReadInputJournalSection(std::istream& in, std::vector<TTDInputEvent>& events, std::string& err)
{
    uint32_t count = 0;
    if (!ReadPod(in, count, err))
    {
        err = "stream read failed (input journal count)";
        return false;
    }
    if (count > ttd::dump::kMaxInputEvents)
    {
        err = "implausible input event count " + std::to_string(count);
        return false;
    }
    // No reserve from the claimed count: a corrupt count must not allocate
    for (uint32_t i = 0; i < count; ++i)
    {
        TTDInputEvent ev;
        uint8_t kind = 0;
        uint8_t pressed = 0;
        if (!ReadPod(in, ev.time.frame, err) || !ReadPod(in, ev.time.tInFrame, err) ||
            !ReadPod(in, kind, err) || !ReadPod(in, ev.key, err) || !ReadPod(in, pressed, err) ||
            !ReadPod(in, ev.dx, err) || !ReadPod(in, ev.dy, err) || !ReadPod(in, ev.buttonMask, err) ||
            !ReadPod(in, ev.wheelSteps, err) || !ReadPod(in, ev.value, err))
        {
            err = "stream read failed (input event " + std::to_string(i) + ")";
            return false;
        }
        if (kind > static_cast<uint8_t>(kLastTTDInputKind))
        {
            err = "input event " + std::to_string(i) + ": unknown kind " + std::to_string(kind);
            return false;
        }
        if (pressed > 1)
        {
            err = "input event " + std::to_string(i) + ": invalid pressed byte " + std::to_string(pressed);
            return false;
        }
        ev.kind = static_cast<TTDInputKind>(kind);
        ev.pressed = pressed != 0;
        if (!events.empty() && ev.time < events.back().time)
        {
            err = "input event " + std::to_string(i) + " is earlier than the one before it";
            return false;
        }
        events.push_back(ev);
    }
    return true;
}

/// @brief Network-input section (header bit 10, ttddumpformat.h kFlagsHasNetInputs)
bool WriteNetInputSection(std::ostream& out, const std::vector<TTDInputEvent>& events,
                          const std::vector<TTDNetInput>& net, const std::vector<uint8_t>& payload, std::string& err)
{
    uint32_t count = 0;
    for (const TTDInputEvent& ev : events)
        count += (ev.netIndex != 0 && ev.netIndex <= net.size()) ? 1 : 0;
    if (!WritePod(out, count, err))
        return false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(events.size()); ++i)
    {
        const TTDInputEvent& ev = events[i];
        if (ev.netIndex == 0 || ev.netIndex > net.size())
            continue;
        const TTDNetInput& n = net[ev.netIndex - 1];
        if (!WritePod(out, i, err) || !WritePod(out, n.socket, err) || !WritePod(out, n.event, err) ||
            !WritePod(out, n.status, err) || !WritePod(out, n.addr, err) || !WritePod(out, n.port, err) ||
            !WritePod(out, n.payloadOffset, err) || !WritePod(out, n.payloadLength, err))
        {
            err = "stream write failed (network input section)";
            return false;
        }
    }
    const uint32_t size = static_cast<uint32_t>(payload.size());
    if (!WritePod(out, size, err))
        return false;
    if (size)
    {
        out.write(reinterpret_cast<const char*>(payload.data()), size);
        if (!out)
        {
            err = "stream write failed (network payload)";
            return false;
        }
    }
    return true;
}

/// @brief Read the network-input section: the records of the NetEvents the
/// input journal section already staged, and the payload store. Refuses
/// records that point at a non-NetEvent, out of order, or outside the store.
bool ReadNetInputSection(std::istream& in, std::vector<TTDInputEvent>& events, std::vector<TTDNetInput>& net,
                         std::vector<uint8_t>& payload, std::string& err)
{
    uint32_t count = 0;
    if (!ReadPod(in, count, err))
    {
        err = "stream read failed (network input count)";
        return false;
    }
    if (count > events.size())
    {
        err = "implausible network input count " + std::to_string(count);
        return false;
    }
    int64_t lastIndex = -1;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t index = 0;
        TTDNetInput n;
        if (!ReadPod(in, index, err) || !ReadPod(in, n.socket, err) || !ReadPod(in, n.event, err) ||
            !ReadPod(in, n.status, err) || !ReadPod(in, n.addr, err) || !ReadPod(in, n.port, err) ||
            !ReadPod(in, n.payloadOffset, err) || !ReadPod(in, n.payloadLength, err))
        {
            err = "stream read failed (network input " + std::to_string(i) + ")";
            return false;
        }
        if (index >= events.size() || static_cast<int64_t>(index) <= lastIndex ||
            events[index].kind != TTDInputKind::NetEvent)
        {
            err = "network input " + std::to_string(i) + ": bad event index " + std::to_string(index);
            return false;
        }
        lastIndex = index;
        net.push_back(n);
        events[index].netIndex = static_cast<uint32_t>(net.size());
    }
    uint32_t size = 0;
    if (!ReadPod(in, size, err))
    {
        err = "stream read failed (network payload size)";
        return false;
    }
    if (size > ttd::dump::kMaxNetPayloadBytes)
    {
        err = "implausible network payload size " + std::to_string(size);
        return false;
    }
    payload.resize(size);
    if (size)
    {
        in.read(reinterpret_cast<char*>(payload.data()), size);
        if (!in)
        {
            err = "stream read failed (network payload)";
            return false;
        }
    }
    for (const TTDNetInput& n : net)
    {
        if (n.payloadLength && static_cast<uint64_t>(n.payloadOffset) + n.payloadLength > size)
        {
            err = "network input payload outside the store";
            return false;
        }
    }
    for (const TTDInputEvent& ev : events)
    {
        if (ev.kind == TTDInputKind::NetEvent && ev.netIndex == 0)
        {
            err = "a network event without its network record";
            return false;
        }
    }
    return true;
}

/// @brief External-event section (header bit 7): u32 count, then per marker
/// u64 frame, u32 tInFrame, u8 kind, u8 reason_len, reason bytes.
bool WriteExternalEventSection(std::ostream& out, const std::vector<TTDExternalEvent>& events, std::string& err)
{
    const uint32_t count = static_cast<uint32_t>(events.size());
    if (!WritePod(out, count, err)) return false;
    for (const TTDExternalEvent& ev : events)
    {
        const uint8_t kind = static_cast<uint8_t>(ev.kind);
        const size_t len = strnlen(ev.reason, sizeof(ev.reason));
        const uint8_t reasonLen =
            static_cast<uint8_t>(std::min<size_t>(len, ttd::dump::kMaxExternalEventReason));
        if (!WritePod(out, ev.time.frame, err) || !WritePod(out, ev.time.tInFrame, err) ||
            !WritePod(out, kind, err) || !WritePod(out, reasonLen, err))
        {
            err = "stream write failed (external event section)";
            return false;
        }
        if (reasonLen != 0)
        {
            out.write(ev.reason, reasonLen);
            if (!out)
            {
                err = "stream write failed (external event reason)";
                return false;
            }
        }
    }
    return true;
}

/// @brief Read the external-event section. Unknown kinds are kept (a marker
/// is a barrier whatever its kind; a newer writer may add kinds).
bool ReadExternalEventSection(std::istream& in, std::vector<TTDExternalEvent>& events, std::string& err)
{
    uint32_t count = 0;
    if (!ReadPod(in, count, err))
    {
        err = "stream read failed (external event count)";
        return false;
    }
    if (count > ttd::dump::kMaxExternalEvents)
    {
        err = "implausible external event count " + std::to_string(count);
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        TTDExternalEvent ev;
        uint8_t kind = 0;
        uint8_t reasonLen = 0;
        if (!ReadPod(in, ev.time.frame, err) || !ReadPod(in, ev.time.tInFrame, err) ||
            !ReadPod(in, kind, err) || !ReadPod(in, reasonLen, err))
        {
            err = "stream read failed (external event " + std::to_string(i) + ")";
            return false;
        }
        if (reasonLen > ttd::dump::kMaxExternalEventReason)
        {
            err = "external event " + std::to_string(i) + ": implausible reason length " + std::to_string(reasonLen);
            return false;
        }
        if (reasonLen != 0)
        {
            in.read(ev.reason, reasonLen);
            if (!in)
            {
                err = "stream read failed (external event " + std::to_string(i) + " reason)";
                return false;
            }
        }
        ev.reason[reasonLen] = '\0';
        ev.kind = static_cast<TTDExternalEventKind>(kind);
        if (!events.empty() && ev.time < events.back().time)
        {
            err = "external event " + std::to_string(i) + " is earlier than the one before it";
            return false;
        }
        events.push_back(ev);
    }
    return true;
}

} // anonymous namespace

bool TimeTravelManager::SerializeSession(std::ostream& out, std::string& err) const
{
    // --- Resolve header metadata ---
    // cpu_state_size / chipset_state_size are written so a future C++ reader
    // can detect struct-layout drift between the writer and reader builds.
    // Kaitai-generated readers ignore these fields (the .ksy is the layout
    // contract for them).
    const uint16_t cpuStateSize     = static_cast<uint16_t>(sizeof(TTDCpuState));
    const uint16_t chipsetStateSize = static_cast<uint16_t>(sizeof(TTDChipsetState));

    // model_ram_pages is u2. It was u1 until a 4 MB machine's 256 pages were
    // found to truncate to 0, leaving the file describing a checkpoint table of
    // zero RAM refs.
    const uint16_t modelRamPagesOut = _modelRamPages;

    // Symbolic emulator identifier (best-effort; empty when no Emulator is
    // attached — e.g. a deserialized-then-reserialized session).
    std::string emulatorId;
    if (_context && _context->pEmulator)
    {
        emulatorId = _context->pEmulator->GetSymbolicId();
    }
    if (emulatorId.size() > 255)
        emulatorId.resize(255);  // emulator_id_len is u8

    // Model metadata (best-effort).
    uint8_t modelId = 0;
    if (_context)
        modelId = static_cast<uint8_t>(_context->config.mem_model);

    // Wall-clock capture time. Informational; not used by the format.
    const auto now = std::chrono::system_clock::now();
    const uint64_t capturedAtMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());

    // --- Build the live-slot remap ---
    // Walk the page store; assign compact indices 0, 1, 2, ... to every slot
    // whose refcount > 0. The map translates from the in-memory storeIndex
    // used by checkpoints to the on-disk slot index. NEVER_TOUCHED refs pass
    // through unchanged (they're never looked up in the map on read).
    //
    // The on-disk order puts every XorPrev slot after the slot it XORs
    // against: the reader rebuilds the store in file order and requires
    // prev_slot < index. In-memory index order does not guarantee that - a
    // resume from the past frees slots and the store reuses them newest
    // first, so a later delta can sit at a lower index than its base. Each
    // live slot is emitted after its chain of not-yet-emitted predecessors.
    std::unordered_map<uint32_t, uint32_t> slotRemap;
    std::vector<uint32_t> slotOrder;
    slotRemap.reserve(_pageStore.GetCapacity());
    {
        std::vector<uint32_t> chain;
        for (uint32_t idx = 0; idx < _pageStore.GetCapacity(); ++idx)
        {
            if (_pageStore.GetRefCount(idx) == 0 || slotRemap.count(idx) != 0)
                continue;
            // Walk down to the first predecessor already placed (or a chain root)
            chain.clear();
            uint32_t cur = idx;
            while (true)
            {
                chain.push_back(cur);
                if (_pageStore.GetEncoding(cur) != TTDCodecPageStore::Encoding::XorPrev)
                    break;
                const uint32_t prev = _pageStore.GetPrevSlot(cur);
                if (slotRemap.count(prev) != 0)
                    break;
                if (prev >= _pageStore.GetCapacity() || _pageStore.GetRefCount(prev) == 0 ||
                    chain.size() > _pageStore.GetCapacity())
                {
                    err = "slot " + std::to_string(cur) + " has prev_slot " + std::to_string(prev) +
                          " which is not a live slot (corrupt store?)";
                    return false;
                }
                cur = prev;
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                slotRemap.emplace(*it, static_cast<uint32_t>(slotOrder.size()));
                slotOrder.push_back(*it);
            }
        }
    }
    const uint32_t liveSlotCount = static_cast<uint32_t>(slotRemap.size());

    // --- Write header ---
    out.write(ttd::dump::kMagic, 4);
    if (!out) { err = "stream write failed (magic)"; return false; }

    const uint16_t schemaVersion = ttd::dump::kSchemaVersion;
    if (!WritePod(out, schemaVersion, err)) return false;

    // Only set journal flag if we actually have journal entries to write
    const bool hasJournalData = _enableWriteJournal && _writeJournal && !_writeJournal->IsEmpty();
    uint16_t flags = ttd::dump::kFlagsLittleEndian | ttd::dump::kFlagsTopClockTime;
    if (hasJournalData)
        flags |= ttd::dump::kFlagsHasWriteJournal;
    if (hasJournalData && _journalGapless && !_writeJournal->HasEvictedRecords())
        flags |= ttd::dump::kFlagsWriteJournalComplete;

    // The coverage index is written whenever there is one. It is derived data,
    // so a reader that skips it still gets a correct session — just one whose
    // reverse queries fall back to replay.
    const bool hasCoverage =
        _enableCoverageIndex &&
        _coverageIndex.SealedFrameCount(TTDCoverageKind::Executed) > 0;
    if (hasCoverage)
        flags |= ttd::dump::kFlagsHasCoverageIndex;

    // Bookmarks are advisory annotations — written whenever any exist.
    const bool hasBookmarks = !_bookmarks.IsEmpty();
    if (hasBookmarks)
        flags |= ttd::dump::kFlagsHasBookmarks;
    // Replay inputs: always written, empty or not (see kFlagsHasInputJournal)
    flags |= ttd::dump::kFlagsHasInputJournal | ttd::dump::kFlagsHasExternalEvents;
    // Port-read journal: only a session that holds every IN of its history
    if (_portJournalValid)
        flags |= ttd::dump::kFlagsHasPortJournals;
    // The recorded device set, in the header: readers provision a matching
    // machine before the load (ttdfileinfo.h). The baseline checkpoint's blob
    // ids speak for the whole session, as the loader's slot checks assume.
    uint64_t peripheralMask = 0;
    if (!_timeline.empty())
    {
        for (const auto& blob : _timeline.front().peripheralBlobs)
            if (blob.first < 64)
                peripheralMask |= uint64_t(1) << blob.first;
    }
    flags |= ttd::dump::kFlagsHasPeripheralMask;
    // Network inputs only when the session has them: sessions without a
    // network adapter stay byte-identical to the older layout
    if (!_inputJournal.NetInputs().empty())
        flags |= ttd::dump::kFlagsHasNetInputs;
    if (!WritePod(out, flags, err)) return false;

    if (!WritePod(out, modelId, err)) return false;
    if (!WritePod(out, modelRamPagesOut, err)) return false;
    if (!WritePod(out, cpuStateSize, err)) return false;
    if (!WritePod(out, chipsetStateSize, err)) return false;

    // ROM fingerprint — see ComputeRomSignature. Sits with the other
    // compatibility fields so a reader validates everything config-related
    // before it starts materializing checkpoints.
    const uint64_t romSignature = ComputeRomSignature();
    if (!WritePod(out, romSignature, err)) return false;

    if (!WritePod(out, capturedAtMs, err)) return false;

    const uint8_t emulatorIdLen = static_cast<uint8_t>(emulatorId.size());
    if (!WritePod(out, emulatorIdLen, err)) return false;
    if (emulatorIdLen != 0)
    {
        out.write(emulatorId.data(), emulatorIdLen);
        if (!out) { err = "stream write failed (emulator_id)"; return false; }
    }

    const uint8_t sessionState = static_cast<uint8_t>(_state.load());
    if (!WritePod(out, sessionState, err)) return false;

    const uint64_t sessionStart = _timeline.empty() ? 0 : _timeline.front().time.frame;
    const uint64_t sessionEnd   = _timeline.empty() ? 0 : _timeline.back().time.frame;
    if (!WritePod(out, sessionStart, err)) return false;
    if (!WritePod(out, sessionEnd, err)) return false;

    if (!WritePod(out, liveSlotCount, err)) return false;

    const uint32_t checkpointCount = static_cast<uint32_t>(_timeline.size());
    if (!WritePod(out, checkpointCount, err)) return false;

    static_assert(ttd::dump::kSubPageSize == TTDCodecPageStore::kPageSize,
                  "sub-page size mismatch between format and codec page store");
    // Formerly 8 reserved bytes: the peripheral mask (kFlagsHasPeripheralMask)
    if (!WritePod(out, peripheralMask, err)) return false;

    // --- Write page store (only live slots, in remapped order) ---
    //
    // v2 layout per slot:
    //   u8  encoding       (0=Full, 1=XorPrev, 2=Zero)
    //   u32 refcount       (informational; reader uses timeline-derived refcount)
    //   u32 prev_slot      (compact index; 0xFFFFFFFF when encoding != XorPrev)
    //   u32 crc32c         (CRC32C of the reconstructed 4 KB, from the page store)
    //   u32 payload_size   (bytes of zstd-compressed payload)
    //   u8[payload_size]   payload
    //
    // The stored payload and CRC are written as they are (GetPayload /
    // GetCrc32C): no decompress/recompress on serialize.
    for (const uint32_t idx : slotOrder)
    {
        const auto encoding = _pageStore.GetEncoding(idx);
        const uint8_t encByte = static_cast<uint8_t>(encoding);
        if (!WritePod(out, encByte, err)) return false;

        const uint32_t refcount = _pageStore.GetRefCount(idx);
        if (!WritePod(out, refcount, err)) return false;

        // prev_slot in compact-remapped form (or sentinel).
        uint32_t prevSlotOut = ttd::dump::kNeverTouchedSlot;
        if (encoding == TTDCodecPageStore::Encoding::XorPrev)
        {
            const uint32_t prevSlotIn = _pageStore.GetPrevSlot(idx);
            auto it = slotRemap.find(prevSlotIn);
            if (it == slotRemap.end())
            {
                err = "slot " + std::to_string(idx) + " has prev_slot " +
                      std::to_string(prevSlotIn) + " not in live remap (corrupt store?)";
                return false;
            }
            prevSlotOut = it->second;
        }
        if (!WritePod(out, prevSlotOut, err)) return false;

        // CRC field: write the stored CRC32C directly.
        const uint32_t crc32c = _pageStore.GetCrc32C(idx);
        if (!WritePod(out, crc32c, err)) return false;

        // Payload: write the stored compressed payload directly (no decompress/recompress).
        // This preserves XOR-delta encoding for ~20x smaller files.
        if (encoding == TTDCodecPageStore::Encoding::Zero)
        {
            const uint32_t payloadSize = 0;
            if (!WritePod(out, payloadSize, err)) return false;
        }
        else
        {
            std::vector<uint8_t> payload;
            if (!_pageStore.GetPayload(idx, payload))
            {
                err = "failed to get payload for slot " + std::to_string(idx);
                return false;
            }
            const uint32_t payloadSize = static_cast<uint32_t>(payload.size());
            if (!WritePod(out, payloadSize, err)) return false;
            out.write(reinterpret_cast<const char*>(payload.data()), payloadSize);
            if (!out)
            {
                err = "stream write failed (slot " + std::to_string(idx) + " payload)";
                return false;
            }
        }
    }

    // --- Write checkpoints ---
    for (const TTDCheckpoint& cp : _timeline)
    {
        if (!WritePod(out, cp.time.frame, err)) return false;
        if (!WritePod(out, cp.globalT, err)) return false;

        // v2 additions: frame kind + keyframe anchor.
        const uint8_t frameKindByte = static_cast<uint8_t>(cp.frameKind);
        if (!WritePod(out, frameKindByte, err)) return false;
        if (!WritePod(out, cp.keyFrameAnchor, err)) return false;

        // CPU + chipset: POD structs, written verbatim. Padding bytes were
        // zeroed by CaptureCpuState/CaptureChipsetState (memset before field
        // copies), so the output is deterministic across runs.
        if (!WritePod(out, cp.cpu, err)) return false;
        if (!WritePod(out, cp.chipset, err)) return false;

        // RAM page refs: 4 sub-page slots per emulator RAM page, remapped to
        // compact on-disk indices.
        // Defensive: a checkpoint's ramPages.size() should equal _modelRamPages,
        // but historical checkpoints from earlier sessions might have a
        // different count if the model was reconfigured mid-session (which
        // P1.6 invalidation should have prevented — but be defensive here).
        const uint32_t pagesToWrite =
            std::min<uint32_t>(static_cast<uint32_t>(cp.ramPages.size()),
                                static_cast<uint32_t>(_modelRamPages));
        for (uint32_t p = 0; p < static_cast<uint32_t>(_modelRamPages); ++p)
        {
            for (uint32_t s = 0; s < ttd::dump::kSubPagesPerEmuPage; ++s)
            {
                uint32_t refOut = ttd::dump::kNeverTouchedSlot;
                if (p < pagesToWrite)
                {
                    const TTDPageRef& ref = cp.ramPages[p];
                    if (ref.pageSlots[s] != TTDPageRef::kNeverTouched)
                    {
                        auto it = slotRemap.find(ref.pageSlots[s]);
                        if (it == slotRemap.end())
                        {
                            err = "checkpoint " + std::to_string(cp.time.frame) +
                                  " references page " + std::to_string(p) + " sub " +
                                  std::to_string(s) + " slot " +
                                  std::to_string(ref.pageSlots[s]) +
                                  " which is not in the live remap (corrupt timeline?)";
                            return false;
                        }
                        refOut = it->second;
                    }
                }
                if (!WritePod(out, refOut, err)) return false;
            }
        }

        // Peripheral blobs from the registry — every device, core and
        // model-specific alike. Emitted sorted by id: the source container is
        // an unordered_map, so writing it in iteration order would make the
        // byte image depend on hash seeding and break reproducible output.
        {
            std::vector<uint8_t> ids;
            ids.reserve(cp.peripheralBlobs.size());
            for (const auto& entry : cp.peripheralBlobs)
                ids.push_back(entry.first);
            std::sort(ids.begin(), ids.end());

            if (!WritePod(out, static_cast<uint16_t>(ids.size()), err)) return false;
            for (uint8_t id : ids)
            {
                if (!WritePod(out, id, err)) return false;
                if (!WriteBlob(out, cp.peripheralBlobs.at(id), err)) return false;
            }
        }
    }

    // --- Write journal section (TDD §9.3) ---
    // Flag bit 1 in the header tells the reader this section exists.
    // Only write if journal capture was enabled and has data.
    if (hasJournalData && _writeJournal)
    {
        if (!_writeJournal->Serialize(out))
        {
            err = "stream write failed (write journal section)";
            return false;
        }
    }

    // --- Reverse-search coverage index ---
    // Flag bit 2. Written last, after the journal, so a reader that knows only
    // the earlier layout stops at the end of the journal rather than misparsing
    // this as journal data.
    if (hasCoverage)
    {
        if (!_coverageIndex.Serialize(out))
        {
            err = "stream write failed (coverage index section)";
            return false;
        }
    }

    // --- Advisory bookmarks (TD-4) ---
    // Flag bit 3, written last. Layout: u32 count, then per bookmark
    // u64 frame, u32 tInFrame, u8 label_len, label bytes. Field-by-field
    // rather than whole-struct POD so no padding bytes ever enter the file;
    // the journal's Add already guarantees non-empty, unique, length-capped
    // labels, so the label_len prefix can never exceed
    // kMaxBookmarkLabelLength.
    if (hasBookmarks)
    {
        const std::vector<TTDBookmark> bookmarks = _bookmarks.Snapshot();
        const uint32_t bookmarkCount = static_cast<uint32_t>(bookmarks.size());
        if (!WritePod(out, bookmarkCount, err)) return false;
        for (const TTDBookmark& b : bookmarks)
        {
            if (!WritePod(out, b.time.frame, err)) return false;
            if (!WritePod(out, b.time.tInFrame, err)) return false;
            const uint8_t labelLen = static_cast<uint8_t>(b.label.size());
            if (!WritePod(out, labelLen, err)) return false;
            if (labelLen != 0)
            {
                out.write(b.label.data(), labelLen);
                if (!out)
                {
                    err = "stream write failed (bookmark label '" + b.label + "')";
                    return false;
                }
            }
        }
    }

    // --- Replay inputs (bits 6 and 7), after every older section so a reader
    // that knows only the older layout stops before them ---
    if (!WriteInputJournalSection(out, _inputJournal.Events(), err))
        return false;
    if (!WriteExternalEventSection(out, _externalEvents.SnapshotEvents(), err))
        return false;

    // --- Port journals (bit 8): the IN journal, then the OUT journal; each
    // is its records, then one cursor per checkpoint in timeline order ---
    if (_portJournalValid)
    {
        std::vector<uint64_t> readCursors;
        std::vector<uint64_t> writeCursors;
        readCursors.reserve(_timeline.size());
        writeCursors.reserve(_timeline.size());
        // A file starts at the journals' first kept record (the history limit
        // may have dropped older ones): cursors relative to it
        for (const TTDCheckpoint& cp : _timeline)
        {
            readCursors.push_back(cp.portReadCursor - _portReads.FirstIndex());
            writeCursors.push_back(cp.portWriteCursor - _portWrites.FirstIndex());
        }
        if (!_portReads.Serialize(out, readCursors, err) || !_portWrites.Serialize(out, writeCursors, err))
            return false;
    }

    // --- Network inputs (bit 10), the last section, only when the session has them ---
    if ((flags & ttd::dump::kFlagsHasNetInputs) &&
        !WriteNetInputSection(out, _inputJournal.Events(), _inputJournal.NetInputs(), _inputJournal.Payload(), err))
        return false;

    return true;
}

bool TimeTravelManager::TurboSoundSessionKindMatches(
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

bool TimeTravelManager::DeserializeSession(std::istream& in, std::string& err)
{
    // A machine where time travel is not available at all (a ZX-Poly member)
    // loads no session; a file search (SearchPortEventsInFile) does not load
    // one, so it is not refused
    if (!_unavailableReason.empty())
    {
        err = _unavailableReason;
        return false;
    }
    const PublishOnExit publish{*this};
    return DeserializeSessionImpl(in, err, nullptr);
}

TTDPortSearchResult TimeTravelManager::SearchPortEventsInFile(const std::string& path, const TTDPortQuery& q)
{
    TTDPortSearchResult result;
    std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
    if (!in)
    {
        result.error = "cannot open " + path;
        return result;
    }
    FilePortJournals journals;
    std::string err;
    if (!DeserializeSessionImpl(in, err, &journals))
    {
        result.error = path + ": " + err;
        return result;
    }
    return ttd::SearchPortEvents(journals.reads, journals.writes, q);
}

bool TimeTravelManager::DeserializeSessionImpl(std::istream& in, std::string& err, FilePortJournals* journalsOnly)
{
    // --- Read + validate header ---
    char magic[4];
    in.read(magic, 4);
    if (!in) { err = "stream read failed (magic)"; return false; }
    if (std::memcmp(magic, ttd::dump::kMagic, 4) != 0)
    {
        err = "bad magic — not a .ttd file";
        return false;
    }

    uint16_t schemaVersion = 0;
    if (!ReadPod(in, schemaVersion, err)) return false;
    if (schemaVersion != ttd::dump::kSchemaVersion)
    {
        err = "unsupported schema v" + std::to_string(schemaVersion) +
              " (this reader requires v" + std::to_string(ttd::dump::kSchemaVersion) +
              "; re-capture the session)";
        return false;
    }

    uint16_t flags = 0;
    if (!ReadPod(in, flags, err)) return false;
    if ((flags & ttd::dump::kFlagsLittleEndian) == 0)
    {
        err = "file is big-endian; only little-endian .ttd files are supported";
        return false;
    }
    const bool hasJournal = (flags & ttd::dump::kFlagsHasWriteJournal) != 0;

    uint8_t modelId = 0;
    uint16_t modelRamPages = 0;
    uint16_t cpuStateSize = 0, chipsetStateSize = 0;
    uint64_t capturedAtMs = 0;
    uint64_t romSignature = 0;
    if (!ReadPod(in, modelId, err)) return false;
    if (!ReadPod(in, modelRamPages, err)) return false;
    if (!ReadPod(in, cpuStateSize, err)) return false;
    if (!ReadPod(in, chipsetStateSize, err)) return false;
    if (!ReadPod(in, romSignature, err)) return false;
    if (!ReadPod(in, capturedAtMs, err)) return false;

    // ROM set must match. Checkpoints store which ROM page is paged in, never
    // the ROM bytes themselves, so replaying against a different ROM set maps
    // the recorded page numbers onto different code. On ProfROM machines this
    // is worse than cosmetic: a plane id only means something relative to the
    // image it was recorded against. Files written before the signature
    // existed, or by a writer with no Memory attached, carry the "unknown"
    // sentinel and skip the check rather than becoming unloadable.
    if (!journalsOnly && romSignature != ttd::dump::kRomSignatureUnknown)
    {
        const uint64_t currentSignature = ComputeRomSignature();
        if (currentSignature != ttd::dump::kRomSignatureUnknown &&
            currentSignature != romSignature)
        {
            err = "ROM set mismatch: session recorded against ROM signature 0x" +
                  ttd::HashToString(romSignature) + ", this machine has 0x" +
                  ttd::HashToString(currentSignature) +
                  " (load the ROM set the session was recorded with)";
            return false;
        }
    }

    // Machine model must match. A checkpoint is raw RAM pages plus a chipset
    // snapshot captured on a specific machine; restoring a Pentagon recording
    // into a 48K instance would push pages the target does not have and read
    // chipset fields that mean something else there. The failure would be
    // silent - a seek that "works" and produces a corrupt machine - so refuse
    // here and let the caller provision the right model (Emulator::Init()
    // applies SetPreferredModel() before any model-dependent subsystem starts).
    if (_context && !journalsOnly)
    {
        const uint8_t currentModel = static_cast<uint8_t>(_context->config.mem_model);
        if (modelId != currentModel)
        {
            err = "model mismatch: file was recorded on model id " + std::to_string(modelId) +
                  ", this emulator is model id " + std::to_string(currentModel) +
                  " - load it into an instance of the recorded model";
            return false;
        }
        if (_context->emulatorState.ttd_clock_units > 1 && (flags & ttd::dump::kFlagsTopClockTime) == 0)
        {
            err = "this session was recorded before TTD time counted at the top CPU clock: on a model with a "
                  "hardware turbo its positions are ambiguous - record it again";
            return false;
        }
    }

    // Drift detection: warn (not fail) if the producer's struct sizes don't
    // match ours. A size mismatch means the producer was built from a different
    // source revision; the field layout may differ even at the same schema
    // version. We refuse rather than risk silent misparse.
    if (cpuStateSize != sizeof(TTDCpuState))
    {
        err = "cpu_state_size mismatch: file has " + std::to_string(cpuStateSize) +
              ", this build has " + std::to_string(sizeof(TTDCpuState));
        return false;
    }
    if (chipsetStateSize != sizeof(TTDChipsetState))
    {
        err = "chipset_state_size mismatch: file has " + std::to_string(chipsetStateSize) +
              ", this build has " + std::to_string(sizeof(TTDChipsetState));
        return false;
    }

    uint8_t emulatorIdLen = 0;
    if (!ReadPod(in, emulatorIdLen, err)) return false;
    std::string emulatorId;
    if (emulatorIdLen != 0)
    {
        emulatorId.resize(emulatorIdLen);
        in.read(&emulatorId[0], emulatorIdLen);
        if (!in) { err = "stream read failed (emulator_id)"; return false; }
    }

    uint8_t sessionState = 0;
    uint64_t sessionStart = 0, sessionEnd = 0;
    uint32_t pageStoreCount = 0, checkpointCount = 0;
    if (!ReadPod(in, sessionState, err)) return false;
    if (!ReadPod(in, sessionStart, err)) return false;
    if (!ReadPod(in, sessionEnd, err)) return false;
    if (!ReadPod(in, pageStoreCount, err)) return false;
    if (!ReadPod(in, checkpointCount, err)) return false;

    // The peripheral mask (kFlagsHasPeripheralMask; zero in older files). The
    // loader does not need it: it checks the devices against the baseline
    // checkpoint's blobs below, and GetSessionInfo reads them from there too.
    uint64_t peripheralMask = 0;
    if (!ReadPod(in, peripheralMask, err)) return false;
    (void)peripheralMask;

    // --- Staging ---
    // The file replaces the current session only once it has been read in
    // full. Everything below parses into these staging objects, so a file
    // that fails anywhere (truncated, corrupt, refused) leaves the current
    // session exactly as it was; the commit at the end cannot fail.
    TTDCodecPageStore stagedStore;
    std::vector<TTDCheckpoint> stagedTimeline;
    std::unique_ptr<TTDWriteJournal> stagedJournal;
    TTDCoverageIndex stagedCoverage;
    TTDBookmarkJournal stagedBookmarks;

    // The loaded session restores model-specific state through these, so they
    // must exist before the first SeekTo — the file may have been recorded on
    // a model whose serializers this instance has not built yet.
    {
        std::string registrationError;
        if (!RegisterModelPeripherals(&registrationError))
        {
            err = "cannot load session: " + registrationError;
            return false;
        }
    }

    // --- Read page store (v2 codec format) ---
    //
    // Each on-disk slot is at compact index [0..pageStoreCount). We re-intern
    // each slot using InternFull / InternXor as appropriate. The codec page
    // store produces sequential indices matching the on-disk layout — we
    // assert that.
    //
    // Refcount bookkeeping: Intern* sets refcount=1. Checkpoint reads below
    // call AddRef for each reference. We Release every slot once after all
    // checkpoints are read; net refcount = number of references across the
    // timeline, matching what SerializeSession would produce for the same
    // content.
    for (uint32_t i = 0; i < pageStoreCount; ++i)
    {
        uint8_t encByte = 0;
        if (!ReadPod(in, encByte, err)) return false;
        uint32_t refcount = 0, prevSlot = 0, crc = 0, payloadSize = 0;
        if (!ReadPod(in, refcount, err)) return false;
        if (!ReadPod(in, prevSlot, err)) return false;
        if (!ReadPod(in, crc, err)) return false;
        if (!ReadPod(in, payloadSize, err)) return false;

        // Sanity: payload is bounded (4 KB page compresses to at most ~4 KB).
        if (payloadSize > ttd::dump::kSubPageSize * 2)
        {
            err = "slot " + std::to_string(i) + ": implausible payload size " +
                  std::to_string(payloadSize);
            return false;
        }

        std::vector<uint8_t> payload(payloadSize);
        if (payloadSize != 0)
        {
            in.read(reinterpret_cast<char*>(payload.data()), payloadSize);
            if (!in)
            {
                err = "stream read failed (slot " + std::to_string(i) + " payload)";
                return false;
            }
        }

        // Load the slot directly using InternDirect — no decompress/recompress.
        // v1 format stores XOR-delta payloads as-is from the codec page store.
        TTDCodecPageStore::Encoding encoding;
        if (encByte == ttd::dump::kEncodingZero)
            encoding = TTDCodecPageStore::Encoding::Zero;
        else if (encByte == ttd::dump::kEncodingFull)
            encoding = TTDCodecPageStore::Encoding::Full;
        else if (encByte == ttd::dump::kEncodingXorPrev)
        {
            encoding = TTDCodecPageStore::Encoding::XorPrev;
            if (prevSlot == ttd::dump::kNeverTouchedSlot || prevSlot >= i)
            {
                err = "slot " + std::to_string(i) +
                      ": invalid prev_slot " + std::to_string(prevSlot) +
                      " (must be < current index)";
                return false;
            }
        }
        else
        {
            err = "slot " + std::to_string(i) + ": unknown encoding " +
                  std::to_string(encByte);
            return false;
        }

        uint32_t newIdx = stagedStore.InternDirect(encoding, prevSlot, crc, payload);

        if (newIdx != i)
        {
            err = "page store layout drift: slot " + std::to_string(i) +
                  " interned as " + std::to_string(newIdx);
            return false;
        }
    }

    // --- Read checkpoints ---
    //
    // For each checkpoint's ram_page_refs we Intern'd every slot already,
    // so a slot referenced by this checkpoint is live in the store. We AddRef
    // for every reference; the Intern's initial refcount=1 is corrected by a
    // single Release per slot after the checkpoint loop.
    // No reserve from the header's count: a corrupt count must not allocate
    for (uint32_t i = 0; i < checkpointCount; ++i)
    {
        TTDCheckpoint cp;

        if (!ReadPod(in, cp.time.frame, err)) return false;
        if (!ReadPod(in, cp.globalT, err)) return false;

        // v2 additions: frame kind + keyframe anchor.
        uint8_t frameKindByte = 0;
        if (!ReadPod(in, frameKindByte, err)) return false;
        cp.frameKind = static_cast<TTDFrameKind>(frameKindByte);
        if (!ReadPod(in, cp.keyFrameAnchor, err)) return false;

        if (!ReadPod(in, cp.cpu, err)) return false;
        if (!ReadPod(in, cp.chipset, err)) return false;

        cp.ramPages.resize(modelRamPages);
        for (uint16_t p = 0; p < modelRamPages; ++p)
        {
            for (uint32_t s = 0; s < ttd::dump::kSubPagesPerEmuPage; ++s)
            {
                uint32_t ref = 0;
                if (!ReadPod(in, ref, err)) return false;
                if (ref == ttd::dump::kNeverTouchedSlot)
                {
                    cp.ramPages[p].pageSlots[s] = TTDPageRef::kNeverTouched;
                }
                else if (ref < pageStoreCount)
                {
                    cp.ramPages[p].pageSlots[s] = ref;
                    // Bump refcount for this reference. The Intern above set
                    // refcount=1; we'll subtract that initial 1 once after the
                    // checkpoint loop. Net: refcount == number of references
                    // across the deserialized timeline.
                    stagedStore.AddRef(ref);
                }
                else
                {
                    err = "checkpoint " + std::to_string(i) + " page " + std::to_string(p) +
                          " sub " + std::to_string(s) +
                          ": slot index " + std::to_string(ref) +
                          " out of range (pageStoreCount=" +
                          std::to_string(pageStoreCount) + ")";
                    return false;
                }
            }
        }

        // Peripheral blobs. Unknown ids are kept verbatim rather than
        // dropped: RestoreAll ignores devices this build has no serializer
        // for, and preserving them keeps a re-serialized file byte-identical.
        {
            uint16_t peripheralBlobCount = 0;
            if (!ReadPod(in, peripheralBlobCount, err)) return false;
            if (peripheralBlobCount > ttd::dump::kMaxPeripheralBlobsPerCheckpoint)
            {
                err = "implausible peripheral blob count " + std::to_string(peripheralBlobCount);
                return false;
            }
            for (uint16_t b = 0; b < peripheralBlobCount; ++b)
            {
                uint8_t id = 0;
                if (!ReadPod(in, id, err)) return false;
                std::vector<uint8_t> blob;
                if (!ReadBlob(in, blob, err)) return false;
                cp.peripheralBlobs.emplace(id, std::move(blob));
            }
        }

        stagedTimeline.push_back(std::move(cp));
    }

    // Correct the Intern's initial refcount=1 for each live slot. After all
    // checkpoints have been materialized, each slot's refcount is
    // (number_of_references + 1). We need it to equal number_of_references
    // so the page store's bookkeeping matches what a fresh in-memory capture
    // of the same content would produce.
    for (uint32_t i = 0; i < pageStoreCount; ++i)
        stagedStore.Release(i);

    // TurboSound-slot session-kind guard (TSFM design §8.2), following the
    // model-id check's philosophy: a session recorded with the other slot
    // device (legacy TurboSound = blob id 0, TSFM = blob id 4) is refused,
    // not loaded. RestoreAll would restore neither device - the live one
    // would keep whatever state it held before the load, a silent divergence
    // with no trail back to this decision. The baseline checkpoint's blob
    // map speaks for the whole session: one device occupies the slot for the
    // instance's lifetime (design §3.1 - no runtime switching).
    if (!journalsOnly && !stagedTimeline.empty() && _context && _context->pSoundManager)
    {
        if (ITurboSoundDevice* slotDevice = _context->pSoundManager->getTurboSound())
        {
            if (!TurboSoundSessionKindMatches(stagedTimeline.front().peripheralBlobs, *slotDevice))
            {
                const uint8_t legacyId = static_cast<uint8_t>(PeripheralId::TurboSound);
                const uint8_t sessionId =
                    stagedTimeline.front().peripheralBlobs.find(legacyId) != stagedTimeline.front().peripheralBlobs.end()
                        ? legacyId
                        : static_cast<uint8_t>(PeripheralId::TSFM);
                const uint8_t liveId = static_cast<uint8_t>(slotDevice->TTDPeripheralId());
                err = "TurboSound slot mismatch: session was recorded with device id " +
                      std::to_string(sessionId) +
                      (sessionId == legacyId ? " (legacy TurboSound)" : " (TSFM)") +
                      ", this instance runs device id " + std::to_string(liveId) +
                      (liveId == legacyId ? " (legacy TurboSound)" : " (TSFM)") +
                      " - set [SOUND] TurboSound to the recorded kind and restart";
                return false;
            }
        }
        else
        {
            // Empty slot (TurboSound = None) but the session carries a slot
            // blob: the recording machine answered the AY ports, this one
            // leaves them on the floating bus - a replay would diverge on the
            // first AY read. Refused for the same reason as a kind mismatch.
            const auto& blobs = stagedTimeline.front().peripheralBlobs;
            const bool sessionHasSlot =
                blobs.find(static_cast<uint8_t>(PeripheralId::TurboSound)) != blobs.end() ||
                blobs.find(static_cast<uint8_t>(PeripheralId::TSFM)) != blobs.end();
            if (sessionHasSlot)
            {
                err = "TurboSound slot mismatch: session was recorded with a TurboSound-slot device, "
                      "this instance has none - set [SOUND] TurboSound to the recorded kind and restart";
                return false;
            }
        }
    }

    // General Sound slot guard (neogs-tdd.md §7.4): a session recorded with one
    // GS-slot personality (classic GS, lightweight player, NeoGS) is refused
    // on an instance fitted with another - RestoreAll would restore neither,
    // leaving the live card's state behind silently. The baseline checkpoint
    // names the personality at the start of the recording; switches inside
    // the session are replayed by UpdatePeripheral. An instance with no GS
    // card keeps the missing-blob report (RestoreAll) rather than a refusal.
    if (!journalsOnly && !stagedTimeline.empty() && _context && _context->pSoundManager)
    {
        if (GeneralSoundCard* liveGs = _context->pSoundManager->getGeneralSound())
        {
            static const struct
            {
                PeripheralId id;
                const char* name;
            } kGsSlot[] = {{PeripheralId::GeneralSound, "GS"},
                           {PeripheralId::GeneralSoundLightweight, "GS lightweight"},
                           {PeripheralId::NeoGS, "NeoGS"}};
            const auto& blobs = stagedTimeline.front().peripheralBlobs;
            const char* recorded = nullptr;
            PeripheralId recordedId = PeripheralId::Count;
            for (const auto& slot : kGsSlot)
            {
                if (blobs.find(static_cast<uint8_t>(slot.id)) != blobs.end())
                {
                    recorded = slot.name;
                    recordedId = slot.id;
                    break;
                }
            }
            if (recorded && recordedId != liveGs->TTDPeripheralId())
            {
                const char* fitted = "another card";
                for (const auto& slot : kGsSlot)
                {
                    if (slot.id == liveGs->TTDPeripheralId())
                        fitted = slot.name;
                }
                err = std::string("General Sound slot mismatch: recorded with ") + recorded + ", fitted: " + fitted +
                      " - set [SOUND] GSType to the recorded card and restart";
                return false;
            }
        }
    }

    // Machine slot guard (Sprinter ISA tdd §9): the model compares its fixed expansion-slot population with
    // the baseline's blobs (the Sprinter's ISA slots, blob 33) - a recording made with another card in a slot
    // is refused, not loaded half-way
    if (!journalsOnly && !stagedTimeline.empty() && _context && _context->pPortDecoder)
    {
        std::string why;
        if (!_context->pPortDecoder->TtdSessionMatches(stagedTimeline.front().peripheralBlobs, why))
        {
            err = why;
            return false;
        }
    }

    // --- Read journal section (v3 additive, TDD §9.3) ---
    if (hasJournal)
    {
        // Its own ring (sync allocation - needed immediately): the live one
        // keeps the current session's records until the commit
        stagedJournal = std::make_unique<TTDWriteJournal>(64u * 1024 * 1024, false);

        uint64_t journalCount = 0;
        if (!ReadPod(in, journalCount, err)) return false;
        if (!stagedJournal->Deserialize(in, journalCount))
        {
            err = "stream read failed (write journal section)";
            return false;
        }
    }

    // --- Coverage index section ---
    // Derived data: a file without it is complete, and a file whose index fails
    // to load is still usable. Refuse the index rather than the session — a
    // half-loaded index would authorise pruning frames it never observed, which
    // silently drops search results.
    if (flags & ttd::dump::kFlagsHasCoverageIndex)
    {
        if (!stagedCoverage.Deserialize(in))
        {
            stagedCoverage.Clear();
            MLOGWARNING("TimeTravelManager::DeserializeSession — coverage index section "
                        "could not be read; reverse queries will fall back to replay");
        }
    }

    // --- Bookmarks section (TD-4) ---
    // Advisory data, same philosophy as the coverage index: a file whose
    // bookmarks cannot be read still loads — the session is complete
    // without annotations, it just loses label-based return. Bookmarks are
    // also the LAST section, so a failed read leaves nothing unread behind
    // it and the session load can proceed.
    if (flags & ttd::dump::kFlagsHasBookmarks)
    {
        bool bookmarksOk = true;
        uint32_t bookmarkCount = 0;
        if (!ReadPod(in, bookmarkCount, err))
        {
            bookmarksOk = false;
        }
        else if (bookmarkCount > 4096)
        {
            // Sanity cap: bookmarks are human/agent-created, dozens at most.
            // A bigger claim is corruption, not an attack on the reader.
            err = "implausible bookmark count " + std::to_string(bookmarkCount);
            bookmarksOk = false;
        }

        for (uint32_t i = 0; bookmarksOk && i < bookmarkCount; ++i)
        {
            uint64_t frame = 0;
            uint32_t tInFrame = 0;
            uint8_t  labelLen = 0;
            if (!ReadPod(in, frame, err) ||
                !ReadPod(in, tInFrame, err) ||
                !ReadPod(in, labelLen, err))
            {
                bookmarksOk = false;
                break;
            }
            if (labelLen == 0 || labelLen > kMaxBookmarkLabelLength)
            {
                err = "bookmark " + std::to_string(i) +
                      ": implausible label length " + std::to_string(labelLen);
                bookmarksOk = false;
                break;
            }

            std::string label(static_cast<size_t>(labelLen), '\0');
            in.read(&label[0], labelLen);
            if (!in)
            {
                err = "stream read failed (bookmark " + std::to_string(i) + " label)";
                bookmarksOk = false;
                break;
            }

            TTDBookmark bookmark;
            bookmark.time.frame    = frame;
            bookmark.time.tInFrame = tInFrame;
            bookmark.label         = std::move(label);
            if (!stagedBookmarks.Add(bookmark))
            {
                // Add only rejects a duplicate label here (length was
                // validated above) — a file carrying two bookmarks with the
                // same label cannot come from a healthy writer.
                err = "bookmark " + std::to_string(i) +
                      ": duplicate label '" + bookmark.label + "'";
                bookmarksOk = false;
                break;
            }
        }

        if (!bookmarksOk)
        {
            // Advisory, but only while nothing follows it: behind a failed
            // read the stream position is unknown, so the replay-input
            // sections after it cannot be found
            if (flags & (ttd::dump::kFlagsHasInputJournal | ttd::dump::kFlagsHasExternalEvents))
            {
                err = "bookmarks section could not be read (" + err +
                      ") and the input journal after it cannot be located";
                return false;
            }
            stagedBookmarks.Clear();
            MLOGWARNING("TimeTravelManager::DeserializeSession — bookmarks section "
                        "could not be read (%s); session loads without bookmarks",
                        err.c_str());
        }
    }

    // --- Replay inputs (bits 6 and 7) ---
    // Replay data, not annotations: a section that fails to read fails the
    // load (the live session is untouched, nothing is committed yet). A file
    // without them predates the sections; it loads, and the session reports
    // its input history incomplete
    std::vector<TTDInputEvent> stagedInputs;
    std::vector<TTDExternalEvent> stagedMarkers;
    const bool hasInputJournal = (flags & ttd::dump::kFlagsHasInputJournal) != 0;
    const bool hasExternalEvents = (flags & ttd::dump::kFlagsHasExternalEvents) != 0;
    if (hasInputJournal && !ReadInputJournalSection(in, stagedInputs, err))
        return false;
    if (hasExternalEvents && !ReadExternalEventSection(in, stagedMarkers, err))
        return false;

    // --- Port journals (bit 8): replay data like the inputs; every block is
    // decompressed and CRC-checked here, so a damaged journal fails the load
    // instead of a replay much later ---
    TTDPortJournal stagedReads(TTDPortJournal::Direction::Read);
    TTDPortJournal stagedWrites(TTDPortJournal::Direction::Write);
    std::vector<uint64_t> stagedReadCursors;
    std::vector<uint64_t> stagedWriteCursors;
    const bool hasPortJournal = (flags & ttd::dump::kFlagsHasPortJournals) != 0;
    const uint32_t timelineCount = static_cast<uint32_t>(stagedTimeline.size());
    if (hasPortJournal && (!stagedReads.Deserialize(in, timelineCount, stagedReadCursors, err) ||
                           !stagedWrites.Deserialize(in, timelineCount, stagedWriteCursors, err)))
        return false;

    // --- Network inputs (bit 10): fields and bytes of the NetEvent inputs ---
    std::vector<TTDNetInput> stagedNet;
    std::vector<uint8_t> stagedNetPayload;
    const bool hasNetInputs = (flags & ttd::dump::kFlagsHasNetInputs) != 0;
    if (hasNetInputs && !ReadNetInputSection(in, stagedInputs, stagedNet, stagedNetPayload, err))
        return false;
    if (!hasNetInputs)
    {
        for (const TTDInputEvent& ev : stagedInputs)
        {
            if (ev.kind == TTDInputKind::NetEvent)
            {
                err = "network events without the network-input section";
                return false;
            }
        }
    }

    // A search of the file: hand the journals out, commit nothing
    if (journalsOnly)
    {
        if (!hasPortJournal)
        {
            err = "the file has no port journals (recorded on TSConf, ZX Next or with NeoGS, before the journals "
                  "existed, or resumed after the machine ran unrecorded)";
            return false;
        }
        journalsOnly->reads = std::move(stagedReads);
        journalsOnly->writes = std::move(stagedWrites);
        return true;
    }

    // --- Commit ---
    // Nothing below can fail. The old session's slots go with its store, so
    // its checkpoints need no per-slot release.
    _timeline      = std::move(stagedTimeline);
    _blobBytes     = 0;
    for (const TTDCheckpoint& cp : _timeline)
        _blobBytes += BlobBytes(cp);
    _pageStore     = std::move(stagedStore);
    _modelRamPages = modelRamPages;
    _inputJournal.Assign(std::move(stagedInputs), std::move(stagedNet), std::move(stagedNetPayload));
    DisarmInputPlayback();
    _externalEvents.Clear();
    for (const TTDExternalEvent& marker : stagedMarkers)
        _externalEvents.Record(marker);
    _inputHistoryComplete = hasInputJournal && hasExternalEvents;
    _portReads = std::move(stagedReads);
    _portWrites = std::move(stagedWrites);
    _portJournalValid = hasPortJournal;
    _portJournalOffReason = hasPortJournal ? std::string() : std::string("the loaded file has no port journal");
    for (size_t i = 0; i < _timeline.size(); ++i)
    {
        _timeline[i].portReadCursor = hasPortJournal ? stagedReadCursors[i] : 0;
        _timeline[i].portWriteCursor = hasPortJournal ? stagedWriteCursors[i] : 0;
    }
    SyncPortJournalHook();
    if (!_inputHistoryComplete)
        MLOGWARNING("TimeTravelManager::DeserializeSession — the file predates saved input and "
                    "external events: replay inside a frame runs without the recorded input and "
                    "may differ from the recording; checkpoint restores stay exact");
    _bookmarks.Clear();
    for (const TTDBookmark& bookmark : stagedBookmarks.Snapshot())
        _bookmarks.Add(bookmark);
    _dirtyScratch.clear();

    // Coverage from a previous recording describes a different timeline; it
    // would let reverse search prune this session's frames with foreign data
    _coverageIndex = std::move(stagedCoverage);
    if (_context)
        _context->ttdCoverageActive = false;

    // Same for the write journal (current-state B2): a file without one must
    // not answer find-last from the previous live recording's writes
    // The journal answers reverse queries only when the writer vouched that it
    // holds every write of the session (current-state B3); otherwise replay
    ClearJournalGap();
    _journalGapless = stagedJournal && (flags & ttd::dump::kFlagsWriteJournalComplete) != 0;
    if (!_journalGapless)
    {
        _journalGapless = true;  // so the gap below is recorded
        MarkJournalGap(stagedJournal ? "the loaded file's write journal is incomplete"
                                     : "the loaded file has no write journal",
                       /*hasPosition=*/false);
    }
    if (stagedJournal)
    {
        _writeJournal = std::move(stagedJournal);
    }
    else if (_writeJournal)
    {
        _writeJournal->WaitReady();
        _writeJournal->Clear();
    }

    // Anything derived from the old session: the decoded frame, the delta
    // base for a later resume, the key-frame anchor and the ever-dirty set
    ClearFrameCache();
    _prevPageCacheValid = false;
    _forceNextKeyFrame = true;
    if (_dirtyTracker)
        _dirtyTracker->ResetSession();

    // Remember where this came from, so status can distinguish a loaded
    // recording from one captured in this process.
    _loadedFromFile   = true;
    _capturedAtUnixMs = capturedAtMs;
    _sessionModelId   = modelId;
    _loadedRomSignature = romSignature;
    _loadedRecordedBy = emulatorId;

    // --- Finalize state ---
    // The file does not carry live recording semantics; force Idle. Callers
    // who want to browse the timeline can call RestoreCheckpointForTesting()
    // or SeekTo() to position the emulator at any captured frame.
    SetState(TTDSessionState::Idle);

    MLOGINFO("TimeTravelManager::DeserializeSession — loaded schema v%u: "
             "%u pages, %zu checkpoints (frames %llu..%llu), model_ram_pages=%u, "
             "coverage=%s",
             static_cast<unsigned>(schemaVersion),
             static_cast<unsigned>(pageStoreCount),
             _timeline.size(),
             static_cast<unsigned long long>(sessionStart),
             static_cast<unsigned long long>(sessionEnd),
             static_cast<unsigned>(_modelRamPages),
             (_coverageIndex.SealedFrameCount(TTDCoverageKind::Executed) > 0
                  ? "loaded" : "absent"));

    return true;
}

TimeTravelManager::SelfTestResult TimeTravelManager::CaptureRestoreSelfTest()
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
    CaptureNow(cp);
    RestoreCheckpoint(cp);
    ReleaseCheckpointRefs(cp);  // Don't leak page refs from the test capture.

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

void TimeTravelManager::SetEnableCoverageIndex(bool enable)
{
    _enableCoverageIndex = enable;

    if (!enable)
        _coverageIndex.Clear();

    // Only mirror into the hot-path flag while a session is live; StartRecording
    // sets it otherwise.
    if (_context && _state == TTDSessionState::Recording)
        _context->ttdCoverageActive = enable;
}

void TimeTravelManager::RecordMemoryWrite(uint16_t addr, uint8_t oldVal, uint8_t newVal,
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

void TimeTravelManager::RecordIoWrite(uint16_t port, uint8_t value, uint16_t m1pc)
{
    // Per-frame decode-cache capture (build replay only).
    if (_frameCaptureActive && _capturingCache && !_capturingCache->entries.empty())
    {
        _capturingCache->accesses.push_back({port, value, TTDAccessKind::PortWrite});
        _capturingCache->entries.back().accessCount++;
    }

    if (_state != TTDSessionState::Recording)
        return;
    if (!_enableWriteJournal)
        return;
    if (!_context || !_writeJournal)
        return;

    TTDWriteRecord rec{};
    rec.globalT  = GlobalT({_context->emulatorState.frame_counter, TInFrameNow()});
    rec.addr     = port;
    rec.isIo     = 1;
    rec.m1pc     = m1pc;
    rec.value    = value;
    rec.physPage = 0;  // IO writes don't have a physical RAM page

    _writeJournal->Append(rec);
}

std::optional<TTDSearchResult>
TimeTravelManager::FindLastAccess(const TTDSearchQuery& q,
                                  TTDExternalEvent* outBlockingMarker,
                                  TTDSearchWindow* outWindow)
{
    // ------------------------------------------------------------------
    // Guards — same shape as SeekTo.
    // ------------------------------------------------------------------
    if (outBlockingMarker)
        *outBlockingMarker = TTDExternalEvent{};

    if (_state == TTDSessionState::Recording)
    {
        MLOGWARNING("TimeTravelManager::FindLastAccess — rejected: session is Recording");
        return std::nullopt;
    }
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::FindLastAccess — no recorded history");
        return std::nullopt;
    }
    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelManager::FindLastAccess — null context or emulator");
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
        beforeGlobalT = static_cast<uint64_t>(now.frame) * frameT + now.tInFrame;
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
    // Step 2: Journal fast path (Write / Io access types only).
    // Read and Execute are not journaled — skip to replay fallback.
    // ------------------------------------------------------------------
    if (q.access == TTDAccessType::Write || q.access == TTDAccessType::Io)
    {
        auto pred = [&](const TTDWriteRecord& rec) -> bool {
            if (q.access == TTDAccessType::Write && rec.isIo) return false;
            if (q.access == TTDAccessType::Io && !rec.isIo) return false;
            if (rec.addr < q.addrFrom || rec.addr > q.addrTo) return false;
            if (q.hasValueFilter && rec.value != q.value) return false;
            if (q.hasPcFilter && (rec.m1pc < q.pcFrom || rec.m1pc > q.pcTo)) return false;
            // Bank-aware match (TDD §9.4). A port record has no page, so the
            // filter only constrains memory writes.
            if (q.hasPhysPageFilter && !rec.isIo && rec.physPage != q.physPage) return false;
            return true;
        };

        // Only a journal that took every write since the session start can
        // answer: with a gap, a later matching write may be missing (a wrong
        // "found") and an earlier one certainly may (a wrong "no match").
        // An empty journal of a session recorded without one is the common
        // case (current-state B3).
        if (!_writeJournal || !_journalGapless)
            goto replay_fallback;

        auto found = _writeJournal->FindLast(beforeGlobalT, pred);
        // The ring may still hold writes from before the history limit's start:
        // those are outside the session now
        const uint64_t startGlobalT = static_cast<uint64_t>(sessionStart.frame) * frameT + sessionStart.tInFrame;
        if (found && found->globalT < startGlobalT)
        {
            reportWindow(sessionStart, std::min(TimePointOf(beforeGlobalT, frameT), SessionEndPosition()));
            return std::nullopt;
        }

        if (found)
        {
            TTDSearchResult result;
            result.time.frame    = static_cast<uint64_t>(found->globalT / frameT);
            result.time.tInFrame = static_cast<uint32_t>(found->globalT % frameT);
            result.pc      = found->m1pc;
            result.value   = found->value;
            result.physPage = found->isIo ? kPhysPageNone : PhysPage{found->physPage};
            result.access   = found->isIo ? TTDAccessType::Io : TTDAccessType::Write;
            reportWindow(result.time, std::min(TimePointOf(beforeGlobalT, frameT), SessionEndPosition()));
            return result;
        }

        // No match: final when the ring still holds the session's first write
        if (!_writeJournal->HasEvictedRecords())
        {
            // The journal is ground truth for the whole session; markers do
            // not limit it
            reportWindow(sessionStart, std::min(TimePointOf(beforeGlobalT, frameT), SessionEndPosition()));
            return std::nullopt;
        }

        // Ring wrapped — older records were lost. Fall through to replay.
        MLOGINFO("TimeTravelManager::FindLastAccess — journal wrapped (oldest=%llu), "
                 "falling back to replay",
                 static_cast<unsigned long long>(_writeJournal->OldestGlobalT()));
    }

    // ------------------------------------------------------------------
    // Step 3: Replay fallback (TDD §9.2).
    //
    // Walk checkpoint intervals backward from the target. For each interval:
    //   a. Check external-event markers — stop if blocked.
    //   b. Restore the checkpoint.
    //   c. Arm the probe with the query.
    //   d. Silent-replay the frame up to the interval end.
    //   e. Extract hits — if any, the last hit is the answer.
    // ------------------------------------------------------------------
replay_fallback:

    // Convert beforeGlobalT to a TTDTimePoint for checkpoint lookup.
    TTDTimePoint targetTime;
    targetTime.frame    = beforeGlobalT / frameT;
    targetTime.tInFrame = static_cast<uint32_t>(beforeGlobalT % frameT);

    // Clamp target frame to session bounds.
    const uint64_t sessionEndFrame = _timeline.back().time.frame;
    if (targetTime.frame > sessionEndFrame)
    {
        targetTime.frame    = sessionEndFrame;
        targetTime.tInFrame = 0;
    }

    // Binary-search for the checkpoint at-or-before the target frame.
    // Same upper_bound trick as SeekToInternal.
    auto upperIt = std::upper_bound(_timeline.begin(), _timeline.end(), targetTime,
        [](const TTDTimePoint& t, const TTDCheckpoint& cp) {
            return t < cp.time;
        });
    if (upperIt == _timeline.begin())
    {
        // Target precedes the first checkpoint — nothing to scan.
        reportWindow(targetTime, targetTime);
        return std::nullopt;
    }
    const size_t targetCpIdx = static_cast<size_t>((upperIt - _timeline.begin()) - 1);

    TTDSearchResult answer;
    bool found = false;
    bool blocked = false;

    // Walk backward from the target checkpoint to the beginning.
    for (size_t i = targetCpIdx + 1; i-- > 0; )
    {
        const TTDCheckpoint& cp = _timeline[i];

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

        if (const TTDExternalEvent* barrier = _externalEvents.FirstMarkerInInterval(cp.time, intervalEnd))
        {
            MLOGINFO("TimeTravelManager::FindLastAccess — marker barrier at "
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
            const TTDCoverageKind coverageKind = (q.access == TTDAccessType::Execute)
                                                     ? TTDCoverageKind::Executed
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

        EnterReplayMode();
        ReplayWithinFrame(cp.time.frame, replayEndT);
        ExitReplayMode();

        auto hits = _context->ttdProbe.ExtractHits();
        _context->ttdProbe.Disarm();

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

    MLOGINFO("TimeTravelManager::FindLastAccess — match at (frame=%llu, tInFrame=%u) "
             "pc=0x%04X value=0x%02X access=%s",
             static_cast<unsigned long long>(answer.time.frame),
             static_cast<unsigned>(answer.time.tInFrame),
             answer.pc, answer.value,
             TTDAccessTypeToString(answer.access));

    return answer;
}

bool TimeTravelManager::StepBackInstruction()
{
    if (_state == TTDSessionState::Recording)
    {
        MLOGWARNING("TimeTravelManager::StepBackInstruction — rejected: session is Recording");
        return false;
    }
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::StepBackInstruction — no recorded history");
        return false;
    }

    // Find the most recent Execute (M1) access strictly before the current
    // position. That is the previous instruction boundary.
    const TTDTimePoint now = CurrentPosition();
    const uint32_t frameT = FrameSpan();  // TTD time units per frame (B4)
    const uint64_t nowGlobalT = static_cast<uint64_t>(now.frame) * frameT + now.tInFrame;

    // Refuse at position (0, 0) — no prior instruction exists.
    if (now.frame == 0 && now.tInFrame == 0)
    {
        MLOGINFO("TimeTravelManager::StepBackInstruction — already at session start");
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
        MLOGINFO("TimeTravelManager::StepBackInstruction — no prior instruction found");
        return false;
    }

    // FindLastAccess already SeekTo'd the answer.
    return true;
}

bool TimeTravelManager::StepForwardInstruction()
{
    if (_state == TTDSessionState::Recording)
    {
        MLOGWARNING("TimeTravelManager::StepForwardInstruction — rejected: session is Recording");
        return false;
    }
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::StepForwardInstruction — no recorded history");
        return false;
    }

    const TTDTimePoint now        = CurrentPosition();
    const TTDTimePoint sessionEnd = SessionEndPosition();

    // Refuse if at or past the session end.
    if (now.frame > sessionEnd.frame ||
        (now.frame == sessionEnd.frame && now.tInFrame >= sessionEnd.tInFrame))
    {
        MLOGINFO("TimeTravelManager::StepForwardInstruction — at or past session end");
        return false;
    }

    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelManager::StepForwardInstruction — null context or emulator");
        return false;
    }

    // Run exactly one instruction via silent replay. RunTStates(1, true)
    // enters the Z80Step loop once — Z80Step executes one complete
    // instruction regardless of its t-state length.
    EnterReplayMode();
    _context->pEmulator->RunTStates(1, true);
    ExitReplayMode();

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

TimeTravelManager::EnumerateResult
TimeTravelManager::EnumerateM1InRange(uint64_t startGlobalT,
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
        MLOGWARNING("TimeTravelManager::EnumerateM1InRange — null _context or pEmulator");
        return result;
    }
    if (_timeline.empty())
    {
        MLOGINFO("TimeTravelManager::EnumerateM1InRange — empty timeline");
        return result;
    }
    if (startGlobalT >= endGlobalT)
    {
        MLOGINFO("TimeTravelManager::EnumerateM1InRange — empty interval "
                 "(start=%llu, end=%llu)",
                 static_cast<unsigned long long>(startGlobalT),
                 static_cast<unsigned long long>(endGlobalT));
        return result;
    }

    const uint32_t frameT = FrameSpan();  // TTD time units per frame (B4)
    const uint64_t sessionEndGlobalT =
        static_cast<uint64_t>(_timeline.back().time.frame) * frameT
        + _timeline.back().time.tInFrame;
    if (endGlobalT > sessionEndGlobalT)
        endGlobalT = sessionEndGlobalT;
    if (startGlobalT >= endGlobalT)
        return result;

    // Decompose endpoints into (frame, tInFrame).
    TTDTimePoint startTime;
    startTime.frame    = startGlobalT / frameT;
    startTime.tInFrame = static_cast<uint32_t>(startGlobalT % frameT);

    TTDTimePoint endTime;
    endTime.frame    = endGlobalT / frameT;
    endTime.tInFrame = static_cast<uint32_t>(endGlobalT % frameT);

    // Find the checkpoint at-or-before endTime. This is the latest interval
    // we'll scan. (Same upper_bound pattern as SeekToInternal.)
    auto endUpperIt = std::upper_bound(_timeline.begin(), _timeline.end(), endTime,
        [](const TTDTimePoint& t, const TTDCheckpoint& cp) {
            return t < cp.time;
        });
    if (endUpperIt == _timeline.begin())
    {
        // endTime precedes the first checkpoint — nothing to scan.
        return result;
    }
    const size_t endCpIdx = static_cast<size_t>((endUpperIt - _timeline.begin()) - 1);

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
        if (const TTDExternalEvent* barrier =
                _externalEvents.FirstMarkerInInterval(cp.time, intervalEnd))
        {
            MLOGINFO("TimeTravelManager::EnumerateM1InRange — marker barrier at "
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
            static_cast<uint64_t>(cp.time.frame) * frameT;
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

        EnterReplayMode();
        ReplayWithinFrame(cp.time.frame, replayEndT);
        ExitReplayMode();

        auto hits = _context->ttdProbe.ExtractHits();
        _context->ttdProbe.Disarm();

        // Convert hits (TTDSearchResult) → TTDM1Record and prepend to keep
        // ascending time order.
        std::vector<TTDM1Record> intervalM1s;
        intervalM1s.reserve(hits.size());
        for (const TTDSearchResult& h : hits)
        {
            TTDM1Record m1;
            m1.globalT  = static_cast<uint64_t>(h.time.frame) * frameT
                          + h.time.tInFrame;
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

    MLOGINFO("TimeTravelManager::EnumerateM1InRange — enumerated %zu M1 records "
             "in [%llu, %llu)%s",
             outM1s.size(),
             static_cast<unsigned long long>(startGlobalT),
             static_cast<unsigned long long>(endGlobalT),
             hitBarrier ? " (stopped at barrier)" : "");
    (void)hitBarrier;
    return result;
}

bool TimeTravelManager::ReverseStepInstructions(uint32_t n)
{
    if (n == 0)
    {
        MLOGWARNING("TimeTravelManager::ReverseStepInstructions — n=0 is a no-op");
        return true;
    }

    // State guards — same shape as StepBackInstruction.
    if (_state == TTDSessionState::Recording)
    {
        MLOGWARNING("TimeTravelManager::ReverseStepInstructions — rejected: session is Recording");
        return false;
    }
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::ReverseStepInstructions — no recorded history");
        return false;
    }
    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelManager::ReverseStepInstructions — null _context or pEmulator");
        return false;
    }

    // Strategy A: small n delegates to repeated StepBackInstruction.
    if (n <= kReverseSeqStepMaxN)
    {
        for (uint32_t i = 0; i < n; ++i)
        {
            if (!StepBackInstruction())
            {
                MLOGINFO("TimeTravelManager::ReverseStepInstructions — ran out of "
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
        static_cast<uint64_t>(now.frame) * frameT + now.tInFrame;

    if (now.frame == 0 && now.tInFrame == 0)
    {
        MLOGINFO("TimeTravelManager::ReverseStepInstructions — already at session start");
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
        MLOGINFO("TimeTravelManager::ReverseStepInstructions — only %zu M1s in "
                 "range, need %u",
                 m1s.size(), n);
        return false;
    }

    // Nth-from-end (0-indexed: size - n).
    const size_t targetIdx = m1s.size() - n;
    const TTDM1Record& target = m1s[targetIdx];

    TTDTimePoint targetTime;
    targetTime.frame    = target.globalT / frameT;
    targetTime.tInFrame = static_cast<uint32_t>(target.globalT % frameT);

    if (!SeekTo(targetTime))
    {
        MLOGWARNING("TimeTravelManager::ReverseStepInstructions — SeekTo(target) failed");
        return false;
    }

    MLOGINFO("TimeTravelManager::ReverseStepInstructions — stepped back %u opcodes "
             "to (frame=%llu, tInFrame=%u) pc=0x%04X",
             n,
             static_cast<unsigned long long>(targetTime.frame),
             static_cast<unsigned>(targetTime.tInFrame),
             target.pc);
    return true;
}

bool TimeTravelManager::ReverseStepTStates(uint64_t n)
{
    // State guards.
    if (_state == TTDSessionState::Recording)
    {
        MLOGWARNING("TimeTravelManager::ReverseStepTStates — rejected: session is Recording");
        return false;
    }
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::ReverseStepTStates — no recorded history");
        return false;
    }
    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelManager::ReverseStepTStates — null _context or pEmulator");
        return false;
    }

    const TTDTimePoint now = CurrentPosition();
    const uint32_t frameT  = FrameSpan();  // TTD time units per frame (B4)
    const uint64_t nowGlobalT =
        static_cast<uint64_t>(now.frame) * frameT + now.tInFrame;

    if (n >= nowGlobalT)
    {
        MLOGINFO("TimeTravelManager::ReverseStepTStates — n=%llu >= nowGlobalT=%llu "
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
        MLOGINFO("TimeTravelManager::ReverseStepTStates — no M1 ≤ targetGlobalT=%llu "
                 "in window",
                 static_cast<unsigned long long>(targetGlobalT));
        return false;
    }

    const TTDM1Record& target = *it;
    TTDTimePoint targetTime;
    targetTime.frame    = target.globalT / frameT;
    targetTime.tInFrame = static_cast<uint32_t>(target.globalT % frameT);

    if (!SeekTo(targetTime))
    {
        MLOGWARNING("TimeTravelManager::ReverseStepTStates — SeekTo(target) failed");
        return false;
    }

    MLOGINFO("TimeTravelManager::ReverseStepTStates — stepped back %llu t-states "
             "to (frame=%llu, tInFrame=%u) pc=0x%04X",
             static_cast<unsigned long long>(n),
             static_cast<unsigned long long>(targetTime.frame),
             static_cast<unsigned>(targetTime.tInFrame),
             target.pc);
    return true;
}

TimeTravelManager::TTDReverseContinueResult
TimeTravelManager::ReverseContinue(const std::vector<uint16_t>& breakpoints)
{
    TTDReverseContinueResult result;

    if (_state == TTDSessionState::Recording)
    {
        MLOGWARNING("TimeTravelManager::ReverseContinue — rejected: session is Recording");
        return result;
    }
    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::ReverseContinue — no recorded history");
        return result;
    }
    if (!_context || !_context->pEmulator)
    {
        MLOGWARNING("TimeTravelManager::ReverseContinue — null _context or pEmulator");
        return result;
    }
    if (breakpoints.empty())
    {
        MLOGINFO("TimeTravelManager::ReverseContinue — empty breakpoint set, no-op");
        return result;
    }

    const TTDTimePoint now = CurrentPosition();
    const uint32_t frameT  = FrameSpan();  // TTD time units per frame (B4)
    const uint64_t nowGlobalT =
        static_cast<uint64_t>(now.frame) * frameT + now.tInFrame;

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
            const uint64_t frameStart = frame * frameT;
            const uint64_t frameEnd =
                std::min<uint64_t>(nowGlobalT, frameStart + frameT);
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
                result.arrivedAt.frame    = hit->globalT / frameT;
                result.arrivedAt.tInFrame = static_cast<uint32_t>(hit->globalT % frameT);
                reportWindow(result.arrivedAt);
                if (frameBarrier.reason[0] != '\0')
                    result.blockingMarker = frameBarrier;

                if (!SeekTo(result.arrivedAt))
                {
                    MLOGWARNING("TimeTravelManager::ReverseContinue — SeekTo(arrivedAt) failed");
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
                MLOGINFO("TimeTravelManager::ReverseContinue — barrier at (frame=%llu, tInFrame=%u) "
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
        const uint64_t prefixEnd = std::min<uint64_t>(nowGlobalT, coverFirst * frameT);
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
                result.arrivedAt.frame    = hit->globalT / frameT;
                result.arrivedAt.tInFrame = static_cast<uint32_t>(hit->globalT % frameT);
                reportWindow(result.arrivedAt);
                if (prefixBarrier.reason[0] != '\0')
                    result.blockingMarker = prefixBarrier;

                if (!SeekTo(result.arrivedAt))
                {
                    MLOGWARNING("TimeTravelManager::ReverseContinue — SeekTo(arrivedAt) failed");
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
            MLOGINFO("TimeTravelManager::ReverseContinue — no PC match in the indexed range "
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
        MLOGINFO("TimeTravelManager::ReverseContinue — barrier at (frame=%llu, tInFrame=%u) "
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
        MLOGINFO("TimeTravelManager::ReverseContinue — no PC match in %zu M1s",
                 m1s.size());
        return result;
    }

    result.matched    = true;
    result.pc         = it->pc;
    result.arrivedAt.frame    = it->globalT / frameT;
    result.arrivedAt.tInFrame = static_cast<uint32_t>(it->globalT % frameT);
    reportWindow(result.arrivedAt);

    if (!SeekTo(result.arrivedAt))
    {
        MLOGWARNING("TimeTravelManager::ReverseContinue — SeekTo(arrivedAt) failed");
        result.matched = false;
        return result;
    }

    MLOGINFO("TimeTravelManager::ReverseContinue — hit PC=0x%04X at "
             "(frame=%llu, tInFrame=%u)",
             result.pc,
             static_cast<unsigned long long>(result.arrivedAt.frame),
             static_cast<unsigned>(result.arrivedAt.tInFrame));
    return result;
}

// ===========================================================================
// Per-frame decode cache (reverse-browsing accelerator)
// ===========================================================================

void TimeTravelManager::ClearFrameCache()
{
    _frameCache.reset();          // frees the entries vector
    _capturingCache = nullptr;
    _frameCaptureActive = false;
}

void TimeTravelManager::CaptureM1(uint16_t pc)
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

void TimeTravelManager::BuildFrameCache(uint64_t frame, TTDFrameCache& out)
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

void TimeTravelManager::SaveLiveState(LiveStateSnapshot& out)
{
    Z80* z80 = (_context && _context->pCore) ? _context->pCore->GetZ80() : nullptr;
    if (z80)
        out.cpu = CaptureCpuState(*static_cast<Z80State*>(z80));
    out.chipset = CaptureChipsetState(_context->emulatorState, z80 ? static_cast<uint32_t>(z80->t) : 0u);
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
        const size_t pageBytes = 4 * TTDCodecPageStore::kPageSize;  // 16 KB
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

void TimeTravelManager::RestoreLiveState(const LiveStateSnapshot& snap)
{
    Z80* z80 = (_context && _context->pCore) ? _context->pCore->GetZ80() : nullptr;

    // Mirror RestoreCheckpoint's ordering (TDD §8.1): CPU, chipset,
    // peripherals, bank rebuild, RAM, screen resync. Peripherals MUST
    // restore before the bank rebuild — model serializers (Scorpion's
    // #1FFD/ProfROM, ATM paging, ...) restore the latches the paging chain
    // reads, so rebuilding banks first pages from stale values and never
    // re-derives (same reasoning as RestoreCheckpoint's step 2a2).
    if (z80)
        RestoreCpuState(snap.cpu, static_cast<Z80State*>(z80));
    RestoreChipsetState(snap.chipset, &_context->emulatorState);
    if (z80)
    {
        z80->t = snap.z80TInFrame;
        z80->RecomputeFrameTiming();  // geometry follows the restored multiplier
    }
    _peripherals.RestoreAll(snap.peripheralBlobs);
    if (_memory)
        _memory->UpdateZ80Banks();

    if (!snap.ram.empty() && _memory)
    {
        const size_t pageBytes = 4 * TTDCodecPageStore::kPageSize;  // 16 KB
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

const TTDFrameCache* TimeTravelManager::GetFrameCache(uint64_t frame)
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

    EnterReplayMode();
    BuildFrameCache(frame, *_frameCache);
    RestoreLiveState(_liveSnapshot);
    ExitReplayMode();

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

TTDCoverageProbeResult TimeTravelManager::QueryCoverageProbe(
    uint64_t frame,
    TTDCoverageKind kind,
    uint16_t addrFrom,
    uint16_t addrTo,
    std::optional<PhysPage> physPage) const
{
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

TTDCoverageScanResult TimeTravelManager::QueryCoverageScan(
    uint64_t fromFrame,
    uint64_t toFrame,
    TTDCoverageKind kind,
    uint16_t addrFrom,
    uint16_t addrTo,
    std::optional<PhysPage> physPage,
    size_t limit) const
{
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

TTDCoverageSummaryResult TimeTravelManager::QueryCoverageSummary(
    uint64_t fromFrame,
    uint64_t toFrame,
    std::optional<TTDCoverageKind> kind,
    uint64_t bucketSize,
    size_t limit) const
{
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

        bucket.hasKeyframe = false;
        auto it = std::lower_bound(_timeline.begin(), _timeline.end(), bucket.frameStart,
            [](const TTDCheckpoint& cp, uint64_t frame) {
                return cp.time.frame < frame;
            });
        while (it != _timeline.end() && it->time.frame <= bucket.frameEnd)
        {
            if (it->frameKind == TTDFrameKind::KeyFrame)
            {
                bucket.hasKeyframe = true;
                break;
            }
            ++it;
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
