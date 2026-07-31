/// @file timetravelmanager.cpp
/// @brief TimeTravelManager — capture orchestrator implementation.
///
/// Per parent TDD §6.3, §7.1. The hot path is OnFrameBoundary: dirty pages
/// are freshly Intern'd, clean pages AddRef the previous checkpoint's slot,
/// CPU/chipset are field-copied via the helpers in ttd_checkpoint.cpp.

#include "timetravelmanager.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <unordered_map>

#include "ttd_checkpoint.h"
#include "ttd_dirty_tracker.h"
#include "ttd_dump_format.h"     // .ttd binary format constants
#include "ttd_codec_page_store.h"
#include "ttd_compression.h"    // codec::Compress / Decompress / Crc32C

#include "machine_state_hash.h"  // CaptureSnapshot / HashSnapshot (self-test)

// Pull in the actual struct definitions for the capture call sites.
#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"            // Z80, Z80State
#include "emulator/emulator.h"           // Emulator::RunTStates (seek engine)
#include "emulator/emulatorcontext.h"    // EmulatorContext
#include "emulator/io/fdc/wd1793.h"      // WD1793 (peripheral, P1.5)
#include "emulator/io/tape/tape.h"        // Tape (peripheral, P1.5)
#include "emulator/memory/memory.h"      // Memory
#include "emulator/platform.h"           // EmulatorState, CONFIG, PAGE_SIZE, MAX_RAM_PAGES
#include "emulator/sound/chips/soundchip_turbosound.h"  // SoundChip_TurboSound (AY peripheral, P1.5)
#include "emulator/video/screen.h"       // Screen, SpectrumScreenEnum (SetActiveScreen / SetBorderColor on restore)
#include "emulator/sound/covox.h"                        // Covox (peripheral, P1.5)
#include "emulator/sound/soundmanager.h"                 // SoundManager
#include "stdafx.h"

// Static assertion: the .ttd v1 format assumes a little-endian host. Every
// multi-byte field in the header / cpu_state / chipset_state is written in
// host order. If we ever port to a big-endian platform we must either add
// byte-swapping or bump the schema version and document the new convention.
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              ".ttd v1 schema assumes little-endian host; add endian conversion if porting");

namespace
{
/// @brief Serialize a TTDSerializable peripheral into its checkpoint blob.
///
/// Helper used by CaptureNow for every peripheral slot (AY → tape → FDC →
/// Covox, per implementation-plan §3.A1 item 5). When the device is absent
/// the blob is cleared — restore then reads nothing, which is the correct
/// no-op for an unimplemented/unused device on the active model.
///
/// Runs on the emulator thread at frame boundaries. The resize() after the
/// first capture is effectively free (capacity stabilizes immediately); the
/// TDD's "no allocation in TTDSaveState" constraint applies to the device's
/// serializer itself, not to this caller-side buffer management.
inline void CapturePeripheral(ttd::TTDSerializable* dev, std::vector<uint8_t>& outBlob)
{
    if (dev != nullptr)
    {
        const size_t sz = dev->TTDStateSize();
        outBlob.resize(sz);
        if (sz != 0)
            dev->TTDSaveState(outBlob.data());
    }
    else
    {
        outBlob.clear();
    }
}

/// @brief Restore a TTDSerializable peripheral from its checkpoint blob.
///
/// Helper used by RestoreCheckpoint for every peripheral slot. When the
/// device is absent or the blob is empty the call is a no-op (an empty blob
/// is the valid representation of an unimplemented/unused device on the
/// active model, per CapturePeripheral's contract).
///
/// Runs on the control thread with the emulator paused (parent TDD §7.2).
inline void RestorePeripheral(ttd::TTDSerializable* dev, const std::vector<uint8_t>& blob)
{
    if (dev != nullptr && !blob.empty())
    {
        // Defensive: the captured blob's size is the device's own TTDStateSize()
        // at capture time. If the device's size has somehow changed since
        // (it shouldn't — sizes are stable for the device lifetime per the
        // TTDSerializable contract), refuse to restore rather than read OOB.
        const size_t expected = dev->TTDStateSize();
        if (blob.size() == expected)
        {
            dev->TTDLoadState(blob.data());
        }
        // Size mismatch is silent at this call site — it's logged by the
        // restore orchestrator if it represents a real session-corruption
        // event. (Currently it cannot happen because sessions don't survive
        // device reconfiguration — P1.6 invalidates on Reset/Load.)
    }
}
} // anonymous namespace

namespace ttd {

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

bool TimeTravelManager::StartRecording()
{
    if (_state == TTDSessionState::Recording)
        return true;  // Idempotent

    // Fresh session — clear any stale auto-pause signal from a previous
    // Detached window.
    _autoPauseRequested.store(false, std::memory_order_release);

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
    // FeatureManager::onFeatureChanged -> UseDebugMemoryInterface +
    // Memory::UpdateFeatureCache, so the gating cache is coherent before
    // we capture the baseline.
    FeatureManager* fm = _context->pFeatureManager;
    _toggledDebugModeOn = false;
    _toggledTimeTravelOn = false;
    if (fm)
    {
        if (!fm->isEnabled(Features::kTimeTravel))
        {
            fm->setFeature(Features::kTimeTravel, true);
            _toggledTimeTravelOn = true;
            MLOGINFO("TimeTravelManager::StartRecording — auto-enabled feature '%s' (required for TTD capture)",
                     Features::kTimeTravel);
        }
        if (!fm->isEnabled(Features::kDebugMode))
        {
            fm->setFeature(Features::kDebugMode, true);
            _toggledDebugModeOn = true;
            MLOGINFO("TimeTravelManager::StartRecording — auto-enabled feature '%s' (required to route writes through MemoryWriteDebug -> MarkDirty)",
                     Features::kDebugMode);
        }
    }
    else
    {
        MLOGWARNING("TimeTravelManager::StartRecording — FeatureManager is null; cannot verify debug/ttd flags. Capture will be a no-op if debug memory interface is inactive.");
    }

    // Clear any prior history (StartRecording always begins a fresh session).
    if (!_timeline.empty())
    {
        for (auto& cp : _timeline)
            ReleaseCheckpointRefs(cp);
        _timeline.clear();
        _pageStore.Reset();
        _dirtyTracker->ResetSession();
        _dirtyScratch.clear();
        _inputJournal.Clear();  // Phase 2 Item 3 — drop any prior input events
        _externalEvents.Clear();  // Phase 2 Item 6 — drop any prior markers
    }

    _modelRamPages = ResolveModelRamPages();
    if (_modelRamPages == 0 || _modelRamPages > MAX_RAM_PAGES)
    {
        MLOGWARNING("TimeTravelManager::StartRecording — implausible modelRamPages=%u, refusing to start",
                    static_cast<unsigned>(_modelRamPages));
        _modelRamPages = 0;
        // Restore emulator run state before bailing.
        if (wasRunning && emu)
            emu->Resume(false);
        return false;
    }

    // Capture the baseline checkpoint so the timeline always has at least
    // one entry. This is the only place we pay the full model-RAM copy cost
    // up front (v1 strategy — see the header doc for the v2 fast-path plan).
    TTDCheckpoint baseline;
    CaptureNow(baseline);
    _timeline.push_back(std::move(baseline));

    _state = TTDSessionState::Recording;

    MLOGINFO("TimeTravelManager::StartRecording — baseline captured: modelRamPages=%u, timeline=1, pageStoreBytes=%zu, debugMemIf=%s",
             static_cast<unsigned>(_modelRamPages), _pageStore.GetCapacityBytes(),
             (_toggledDebugModeOn ? "switched-on" : "already-on"));

    // Resume the emulator if we paused it. The recording OnFrameBoundary
    // hook will now see dirty bits being set correctly.
    if (wasRunning && emu)
        emu->Resume(false);

    return true;
}

void TimeTravelManager::StopRecording()
{
    if (_state != TTDSessionState::Recording)
        return;  // Idempotent
    _state = TTDSessionState::Idle;
    MLOGINFO("TimeTravelManager::StopRecording — timeline retained with %zu checkpoints",
             _timeline.size());

    // Pause the emulator while we restore feature flags (same race concern
    // as StartRecording — MemIf swap must not race with CPU execution).
    Emulator* emu = _context ? _context->pEmulator : nullptr;
    const bool wasRunning = emu && emu->IsRunning() && !emu->IsPaused();
    if (wasRunning)
    {
        emu->Pause(false);
        emu->WaitForPauseConfirmation(1000);
    }

    // Restore feature flags we toggled in StartRecording. Only flip back the
    // ones we actually turned ON — pre-existing user/debugger debug mode is
    // left intact.
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

    if (wasRunning && emu)
        emu->Resume(false);
}

void TimeTravelManager::InvalidateSession(const char* reason)
{
    if (_timeline.empty() && _state == TTDSessionState::Idle)
        return;  // Nothing to invalidate

    MLOGINFO("TimeTravelManager::InvalidateSession — reason='%s', dropping %zu checkpoints",
             reason ? reason : "(null)", _timeline.size());

    for (auto& cp : _timeline)
        ReleaseCheckpointRefs(cp);
    _timeline.clear();
    _pageStore.Reset();
    _dirtyScratch.clear();
    _inputJournal.Clear();  // Phase 2 Item 3 — input history invalidates with the timeline
    _externalEvents.Clear();  // Phase 2 Item 6 — markers invalidate with the timeline
    _modelRamPages = 0;
    _state = TTDSessionState::Idle;

    // Reset Phase 5 codec state.
    _lastKeyFrameIdx = 0;
    _forceNextKeyFrame = true;
    _ramCache.valid = false;
    _ramCache.ram.clear();
    _ramCache.frame = 0;

    // Reset the dirty tracker too — the session-scoped _everDirty set is part
    // of the captured history's validity contract.
    if (_dirtyTracker)
        _dirtyTracker->ResetSession();
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

    return info;
}

size_t TimeTravelManager::EstimateSessionHeapBytes() const
{
    // Page store: count the allocated vector backing. The COW store grows
    // by one slot per Intern-that-can't-reuse and never shrinks until
    // Reset, so capacity is the right measure of "what the process is
    // consuming right now" even though some of those slots are on the
    // free list. GetCapacityBytes returns _pages.size() which is exactly
    // the backing vector's byte size.
    size_t total = _pageStore.GetCapacityBytes();

    // Per-checkpoint: the struct itself + every vector's allocated backing
    // (capacity, not size — capacity is what's actually on the heap).
    //sizeof(TTDCheckpoint) covers time, globalT, cpu, chipset, journal
    // offsets, and the std::vector headers (pointer/size/capacity triple).
    // The vector capacity × element-size additions below account for the
    // heap allocations those vector headers point at.
    for (const TTDCheckpoint& cp : _timeline)
    {
        total += sizeof(TTDCheckpoint);
        total += cp.ayState.capacity()    * sizeof(uint8_t);
        total += cp.fdcState.capacity()   * sizeof(uint8_t);
        total += cp.tapeState.capacity()  * sizeof(uint8_t);
        total += cp.covoxState.capacity() * sizeof(uint8_t);
        total += cp.ramPages.capacity()   * sizeof(TTDPageRef);
    }

    // Input journal + external-event journal — same pattern: capacity is
    // what's allocated, size is what's logically used.
    total += _inputJournal.Events().capacity()   * sizeof(TTDInputEvent);
    total += _externalEvents.Events().capacity() * sizeof(TTDExternalEvent);

    // Session-scope dirty-page scratch buffer (reused every frame — counted
    // once because there's only one).
    total += _dirtyScratch.capacity() * sizeof(uint16_t);

    return total;
}

// ---------------------------------------------------------------------------
// Capture (emulator thread)
// ---------------------------------------------------------------------------

void TimeTravelManager::OnFrameBoundary()
{
    if (!_context)
        return;

    // ------------------------------------------------------------------
    // Recording: append a checkpoint for the just-completed frame.
    // ------------------------------------------------------------------
    if (_state == TTDSessionState::Recording)
    {
        if (!_memory || !_dirtyTracker)
            return;  // Defensive — should not happen if StartRecording succeeded

        TTDCheckpoint cp;
        CaptureNow(cp);
        _timeline.push_back(std::move(cp));
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
    // This runs on the emulator thread (called from MainLoop::OnFrameEnd
    // and Emulator::RunFrame's boundary handler). Emulator::Pause() is
    // safe to call here — it just sets the _isPaused flag, which the
    // MainLoop checks at the top of its next iteration.
    // ------------------------------------------------------------------
    if (_state == TTDSessionState::Detached && !_timeline.empty())
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
    out.chipset = CaptureChipsetState(st);

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

    // Invalidate the materialized-RAM cache — live RAM is changing under us.
    _ramCache.valid = false;

    // --- Peripherals (P1.5 — parent TDD §6.1, §6.4) ---
    // Each device implements TTDSerializable and serializes itself into the
    // corresponding checkpoint blob. Devices land one at a time (AY first);
    // unimplemented slots stay empty vectors (valid no-op on restore).
    //
    // AY / TurboSound: the SoundManager exposes a TurboSound (two AY chips) on
    // all v1-supported models (Pentagon 128/512). The TurboSound is itself a
    // TTDSerializable that serializes both chips + the active-chip selector.
    if (_context->pSoundManager)
    {
        SoundChip_TurboSound* turboSound = _context->pSoundManager->getTurboSound();
        CapturePeripheral(turboSound, out.ayState);
    }
    else
    {
        out.ayState.clear();
    }

    // Tape: per parent TDD §4 row 3 we checkpoint playback position only
    // (content is invariant within a session; tape-control commands
    // invalidate the session via P1.6 hooks).
    CapturePeripheral(_context->pTape, out.tapeState);

    // Covox: 4-channel 8-bit DAC. Only the four DAC latches are machine
    // state (4 bytes); everything else is host-side audio pipeline.
    if (_context->pSoundManager)
    {
        CapturePeripheral(_context->pSoundManager->getCovox(), out.covoxState);
    }
    else
    {
        out.covoxState.clear();
    }

    // FDC subsystem: WD1793 controller + 4 FDDs. Per parent TDD §4 row 4,
    // "FDC internal state (state machine phase, track/sector regs, DRQ/INTRQ
    // timers) must be fully serialized". WD1793's serializer delegates to
    // each FDD's TTDSerializable. Per TDD §12.2, sector writes invalidate
    // the session in v1 (this is enforced elsewhere — not the serializer's
    // concern).
    //
    // Only models with a Beta Disk controller populate pBetaDisk. Other
    // models (pure Spectrum 48/128 without BDI) leave it nullptr and the
    // blob is empty (valid no-op on restore).
    CapturePeripheral(_context->pBetaDisk, out.fdcState);
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
            outRamPages[p].slots[s] = _pageStore.InternFull(sub);
        }
    }
}

void TimeTravelManager::UpdateRamPages(const std::vector<uint16_t>& dirtyPages,
                                const std::vector<TTDPageRef>& prevRamPages,
                                std::vector<TTDPageRef>& outRamPages,
                                bool isKeyFrame)
{
    // ------------------------------------------------------------------
    // Refcount invariant: every slot index that appears in any
    // _timeline checkpoint's ramPages[*].slots[s] MUST have a matching
    // live refcount in the page store, INCLUDING the delta-chain refs
    // that XorPrev slots hold against their prevSlot (the latter are
    // managed internally by InternXor / Release — see
    // ttd_codec_page_store.cpp).
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
        const bool dirty = (dirtyCursor < dirtyPages.size() && dirtyPages[dirtyCursor] == p);
        if (dirty)
            ++dirtyCursor;

        if (!dirty)
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
                    if (prevRef.slots[s] != TTDPageRef::kNeverTouched)
                    {
                        _pageStore.AddRef(prevRef.slots[s]);
                    }
                }
                // outRamPages[p].slots[s] already copied from prevRef above.
            }
            // else: was NEVER_TOUCHED, still NEVER_TOUCHED — nothing to do.
            continue;
        }

        // Dirty page: re-intern each sub-page. I-frame uses InternFull so
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
            const uint32_t prevSlot = prevRamPages[p].slots[s];

            if (isKeyFrame || prevSlot == TTDPageRef::kNeverTouched)
            {
                // I-frame dirty page, or first-ever capture of this sub-page:
                // emit an independent Full snapshot so future P-frames can
                // build fresh XOR chains off a known-good anchor.
                outRamPages[p].slots[s] = _pageStore.InternFull(sub);
            }
            else
            {
                // P-frame dirty page: XOR against prev. InternXor returns
                // either a new XorPrev slot (delta non-zero) or prevSlot
                // itself with refcount++ (delta was zero — page effectively
                // unchanged). In both cases the assignment below is correct;
                // we must NOT Release prevSlot — that would drop prevRamPages's
                // own ref and break restore of the previous checkpoint.
                outRamPages[p].slots[s] = _pageStore.InternXor(prevSlot, sub);
            }
        }
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
                if (ref.slots[s] != TTDPageRef::kNeverTouched)
                {
                    _pageStore.Release(ref.slots[s]);
                    ref.slots[s] = TTDPageRef::kNeverTouched;
                }
            }
        }
    }
}

uint16_t TimeTravelManager::ResolveModelRamPages() const
{
    if (!_context)
        return 0;

    // config.ramsize is in KB; each page is PAGE_SIZE = 16 KB.
    const CONFIG& cfg = _context->config;
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

    RestoreCheckpoint(_timeline[idx]);
    return true;
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
    Z80* cpu = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (cpu)
    {
        RestoreCpuState(cp.cpu, static_cast<Z80State*>(cpu));
    }

    // --- Step 2: Chipset port latches + counters (TDD §8.1 step 2b) ---
    // RestoreChipsetState is a pure field copy into emulatorState. It does
    // NOT re-run the port decoder — that's the next sub-step.
    RestoreChipsetState(cp.chipset, &_context->emulatorState);

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

    // --- Step 4: Peripherals (TDD §8.1 step 2d) ---
    // Each device implements TTDSerializable. RestorePeripheral is a null-
    // checking, size-checking forwarder to TTDLoadState. Devices that have
    // no captured blob (empty vector) are no-ops — valid for models that
    // don't populate them (e.g., Covox absent on 48K, FDC absent on
    // non-Beta-Disk models).
    if (_context->pSoundManager)
    {
        RestorePeripheral(_context->pSoundManager->getTurboSound(), cp.ayState);
    }
    RestorePeripheral(_context->pTape, cp.tapeState);
    if (_context->pSoundManager)
    {
        RestorePeripheral(_context->pSoundManager->getCovox(), cp.covoxState);
    }
    RestorePeripheral(_context->pBetaDisk, cp.fdcState);

    // --- Step 5: Screen (TDD §8.1 step 2e) ---
    //
    // The screen renderer caches three pieces of derived state that the
    // RestoreChipsetState field-copy above does NOT update:
    //
    //   1. _activeScreenMemoryOffset — points to bank 5 (normal) or bank 7
    //      (shadow) depending on bit 3 of p7FFD. The port decoder's
    //      Port_7FFD_Out keeps this in sync on every real port write by
    //      calling SetActiveScreen(); RestoreCheckpoint bypasses the
    //      decoder, so we must do it explicitly. Without this call the
    //      renderer reads pixels from whichever bank was active when the
    //      PREVIOUS frame ran — typically garbage after a seek.
    //
    //   2. _borderColor — derived from bits 0-2 of pFE. Same situation:
    //      the field copy restores pFE in emulatorState but the cached
    //      screen field is stale until SetBorderColor runs.
    //
    //   3. The BORDER PIXELS IN THE FRAMEBUFFER. RenderOnlyMainScreen only
    //      repaints the inner 256x192 screen area; the border around it is
    //      painted separately — either by per-t-state Draw() calls during
    //      normal MainLoop execution, or by an explicit FillBorderWithColor
    //      call. When the emulator is paused (as it is here, post-seek),
    //      MainLoop doesn't run, so the framebuffer border keeps whatever
    //      pixels were left by the previous render. Without an explicit
    //      FillBorderWithColor, seeking to a frame whose border color
    //      differs from the live pre-seek state shows STALE BORDER PIXELS.
    //      (User-visible bug: recorded a demo with black border, seeked to
    //      a frame, got a white border from a prior render.)
    //
    // After re-syncing the cached fields, InitFrame resets the renderer's
    // frame-local counters so the next rendered frame starts from a clean
    // state matching the restored beam position. RenderOnlyMainScreen then
    // rebuilds the inner 256x192 RGBA pixels in one batch from the freshly-
    // restored screen memory, and FillBorderWithColor repaints the border
    // with the restored pFE bits 0-2. The snapshot loader (loader_z80.cpp)
    // uses the same pattern for the same reason.
    if (_context->pScreen)
    {
        // Sync active screen bank from restored p7FFD bit 3.
        const uint8_t p7FFD = _context->emulatorState.p7FFD;
        const SpectrumScreenEnum screen = (p7FFD & 0b0000'1000)
                                            ? SCREEN_SHADOW   // bit 3 set → bank 7
                                            : SCREEN_NORMAL;  // bit 3 clear → bank 5
        _context->pScreen->SetActiveScreen(screen);

        // Sync border color from restored pFE bits 0-2.
        const uint8_t borderColor = _context->emulatorState.pFE & 0b0000'0111;
        _context->pScreen->SetBorderColor(borderColor);

        _context->pScreen->InitFrame();
        _context->pScreen->RenderOnlyMainScreen();

        // Repaint the framebuffer border to match the restored border color.
        // RenderOnlyMainScreen above only touches the inner 256x192 screen
        // area; without this call the border pixels keep whatever the
        // previous render left there, producing visible artifacts when the
        // restored border color differs from the live pre-seek color.
        // FillBorderWithColor also re-calls SetBorderColor internally, but
        // we set it explicitly above for clarity and to keep the cached
        // field correct even if a future FillBorderWithColor impl forgets.
        _context->pScreen->FillBorderWithColor(borderColor);
    }

    // t_states and frame_counter were already restored by RestoreChipsetState.
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
            const uint32_t slot = ref.slots[s];
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
    ev.time.tInFrame = z80 ? z80->t : 0;
    ev.key           = key;
    ev.pressed       = pressed;
    _inputJournal.Record(ev);
}

size_t TimeTravelManager::InjectDueInputEvents(const TTDTimePoint& now)
{
    // Defensive no-op when not in replay mode — Item 4's seek engine should
    // already be inside an EnterReplayMode/ExitReplayMode pair, but a stray
    // call from somewhere else shouldn't crash or corrupt the live keyboard.
    if (!_context || !_context->ttdReplayActive)
        return 0;

    if (!_context->pKeyboard)
    {
        MLOGWARNING("TimeTravelManager::InjectDueInputEvents — no keyboard attached, "
                    "skipping %zu journal events at (frame=%llu, tInFrame=%u)",
                    _inputJournal.Size(),
                    static_cast<unsigned long long>(now.frame),
                    static_cast<unsigned>(now.tInFrame));
        return 0;
    }

    return _inputJournal.InjectDueEvents(*_context->pKeyboard, now);
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
    ev.time.tInFrame = z80 ? z80->t : 0;
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
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    pos.tInFrame = z80 ? z80->t : 0;
    return pos;
}

TTDTimePoint TimeTravelManager::SessionEndPosition() const
{
    if (_timeline.empty())
        return TTDTimePoint{};
    return _timeline.back().time;
}

bool TimeTravelManager::SeekTo(const TTDTimePoint& target, TTDSeekResult* outResult)
{
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

    return SeekToInternal(target, outResult);
}

bool TimeTravelManager::SeekToInternal(const TTDTimePoint& target, TTDSeekResult* outResult)
{
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
                    TTDSessionStateToString(_state));
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
    // frame_counter set to the checkpoint's frame boundary. The Z80
    // accumulator (z80.t) is NOT in the captured field set (it's host-
    // side per the field-exclusion list in ttd_checkpoint.h) so we sync
    // it explicitly — checkpoints always sit at frame boundaries, where
    // z80.t == 0 (post-AdjustFrameCounters reset).
    // ------------------------------------------------------------------
    RestoreCheckpoint(cp);

    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (z80)
        z80->t = 0;

    // ------------------------------------------------------------------
    // Step 3: intra-frame silent replay if target.tInFrame > 0.
    //
    // Phase 2 Item 6 (parent TDD §5.1): check for external-event markers in
    // the replay interval (cp.time, target]. If any marker falls there, the
    // seek must stop at the earliest such marker — replay cannot reproduce
    // the marker's nondeterministic effect, so crossing it silently would
    // produce a misleading "this is the state at target" claim.
    //
    // Frame-aligned targets never trigger this check: with target.tInFrame
    // == 0, no intra-frame replay runs, and the chosen checkpoint already
    // reflects any markers at or before that frame boundary.
    // ------------------------------------------------------------------
    if (target.tInFrame > 0)
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

            _state = TTDSessionState::Detached;

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
    _state = TTDSessionState::Detached;

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

    // Defensive: clamp targetTInFrame to the per-frame budget. If a caller
    // hands us something larger we'd loop forever inside RunTStates' frame
    // boundary handler.
    const uint32_t frameT = _context->config.frame;
    if (targetTInFrame > frameT)
    {
        MLOGWARNING("TimeTravelManager::ReplayWithinFrame — targetTInFrame=%u > "
                    "config.frame=%u, clamping",
                    static_cast<unsigned>(targetTInFrame),
                    static_cast<unsigned>(frameT));
        targetTInFrame = frameT;
    }

    // ------------------------------------------------------------------
    // Engage silent-replay mode for the duration of the loop. EnterReplayMode
    // is idempotent and saves the host audio mute state so we can restore
    // it on exit (TDD §8.2).
    // ------------------------------------------------------------------
    EnterReplayMode();

    // ------------------------------------------------------------------
    // Walk the input journal for events scheduled inside [0, targetTInFrame]
    // of the target frame. Replay runs in chunks: advance the emulator by
    // (next_event.tInFrame - current.tInFrame) t-states, inject the event(s)
    // scheduled at that time, repeat. After the last in-interval event, run
    // the remaining delta to targetTInFrame.
    //
    // Single-pass linear scan of the journal. We could binary-search for
    // the first event in the target frame, but the journal is small
    // (typically a few hundred events) and the linear scan exits early on
    // the first event past the target.
    // ------------------------------------------------------------------
    uint32_t currentTInFrame = 0;

    for (const auto& ev : _inputJournal.Events())
    {
        // Skip events from other frames. We only care about the target
        // frame — events before are already encoded in the restored
        // checkpoint's RAM/CPU state; events after are out of range.
        if (ev.time.frame != targetFrame)
            continue;

        // Skip events past the target — they belong to a future position.
        if (ev.time.tInFrame > targetTInFrame)
            break;

        // Skip events we've already passed (shouldn't happen since we walk
        // in ascending order, but defensive against journal corruption).
        if (ev.time.tInFrame < currentTInFrame)
            continue;

        // Run the emulator from currentTInFrame to ev.time.tInFrame.
        const uint32_t delta = ev.time.tInFrame - currentTInFrame;
        if (delta > 0)
        {
            _context->pEmulator->RunTStates(delta, /*skipBreakpoints=*/true);
            currentTInFrame = ev.time.tInFrame;
        }

        // Inject this event (and any other events at the same TTDTimePoint).
        InjectDueInputEvents(ev.time);
    }

    // Run the remaining delta to reach targetTInFrame.
    if (currentTInFrame < targetTInFrame)
    {
        const uint32_t delta = targetTInFrame - currentTInFrame;
        _context->pEmulator->RunTStates(delta, /*skipBreakpoints=*/true);
        // currentTInFrame = targetTInFrame;  // (unused after this point)
    }

    ExitReplayMode();
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

    TTDTimePoint target;
    target.frame    = current.frame - 1;
    target.tInFrame = current.tInFrame;
    return SeekTo(target);
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

    TTDTimePoint target;
    target.frame    = current.frame + 1;
    target.tInFrame = current.tInFrame;
    return SeekTo(target);
}

// ---------------------------------------------------------------------------
// Resume-from-past (Phase 2 Item 5; parent TDD §8.3)
// ---------------------------------------------------------------------------

bool TimeTravelManager::ResumeRecordingFrom(const TTDTimePoint& from)
{
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

    // ------------------------------------------------------------------
    // Step 3: truncate input journal after `from`. Events exactly at `from`
    // are kept (they happened at the resume point, not after it).
    // ------------------------------------------------------------------
    _inputJournal.DropAfter(from);
    _externalEvents.DropAfter(from);  // Phase 2 Item 6 — markers past `from` are dead future

    // ------------------------------------------------------------------
    // Step 4: return to Recording. Next OnFrameBoundary will append a fresh
    // checkpoint at frame `from.frame + 1` (the live emulator's frame
    // counter is set by SeekTo).
    // ------------------------------------------------------------------
    _state = TTDSessionState::Recording;

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
        ReleaseCheckpointRefs(*it);

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
// Format (see core/src/debugger/ttd/ttd.ksy for the canonical schema):
//
//   header:
//     magic               (4 bytes: 'T' 'T' 'D' 'D')
//     schema_version      (u16)
//     flags               (u16, bit 0 = little-endian)
//     model_id            (u8)
//     model_ram_pages     (u8)
//     cpu_state_size      (u16)
//     chipset_state_size  (u16)
//     captured_at_unix_ms (u64)
//     emulator_id_len     (u8)
//     emulator_id         (emulator_id_len bytes, UTF-8)
//     session_state       (u8)
//     session_start_frame (u64)
//     session_end_frame   (u64)
//     page_store_count    (u32)
//     checkpoint_count    (u32)
//     reserved            (8 bytes, zero)
//
//   page_store:           page_store_count slots, each 16384 bytes raw
//
//   checkpoints:          checkpoint_count records, each:
//     frame               (u64)
//     global_t            (u64)
//     cpu_state           (raw TTDCpuState, cpu_state_size bytes)
//     chipset_state       (raw TTDChipsetState, chipset_state_size bytes)
//     ram_page_refs       (model_ram_pages × u32)
//     ay_size, fdc_size, tape_size, covox_size (u32 each)
//     ay_blob, fdc_blob, tape_blob, covox_blob (variable)
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
bool WriteBlob(std::ostream& out, const std::vector<uint8_t>& blob, std::string& err)
{
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
    // Defensive sanity cap — individual peripheral blobs are tiny (AY=64,
    // FDC=~200, Tape=16, Covox=4). A claim of >1 MB is certainly corruption.
    if (sz > (1u << 20))
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

    // model_ram_pages is u8 in the .ksy format. _modelRamPages is uint16_t in
    // the engine (could exceed 255 only on >4 MB machines, which no v1-supported
    // model approaches). Truncate defensively.
    const uint8_t modelRamPagesOut = static_cast<uint8_t>(_modelRamPages);

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
    std::unordered_map<uint32_t, uint32_t> slotRemap;
    slotRemap.reserve(_pageStore.GetCapacity());
    for (uint32_t idx = 0; idx < _pageStore.GetCapacity(); ++idx)
    {
        if (_pageStore.GetRefCount(idx) > 0)
        {
            slotRemap.emplace(idx, static_cast<uint32_t>(slotRemap.size()));
        }
    }
    const uint32_t liveSlotCount = static_cast<uint32_t>(slotRemap.size());

    // --- Write header ---
    out.write(ttd::dump::kMagic, 4);
    if (!out) { err = "stream write failed (magic)"; return false; }

    const uint16_t schemaVersion = ttd::dump::kSchemaVersion;
    if (!WritePod(out, schemaVersion, err)) return false;

    const uint16_t flags = ttd::dump::kFlagsLittleEndian;
    if (!WritePod(out, flags, err)) return false;

    if (!WritePod(out, modelId, err)) return false;
    if (!WritePod(out, modelRamPagesOut, err)) return false;
    if (!WritePod(out, cpuStateSize, err)) return false;
    if (!WritePod(out, chipsetStateSize, err)) return false;
    if (!WritePod(out, capturedAtMs, err)) return false;

    const uint8_t emulatorIdLen = static_cast<uint8_t>(emulatorId.size());
    if (!WritePod(out, emulatorIdLen, err)) return false;
    if (emulatorIdLen != 0)
    {
        out.write(emulatorId.data(), emulatorIdLen);
        if (!out) { err = "stream write failed (emulator_id)"; return false; }
    }

    const uint8_t sessionState = static_cast<uint8_t>(_state);
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
    for (int i = 0; i < 8; ++i)
    {
        const char zero = 0;
        out.write(&zero, 1);
        if (!out) { err = "stream write failed (reserved)"; return false; }
    }

    // --- Write page store (only live slots, in remapped order) ---
    //
    // v2 layout per slot:
    //   u8  encoding       (0=Full, 1=XorPrev, 2=Zero)
    //   u32 refcount       (informational; reader uses timeline-derived refcount)
    //   u32 prev_slot      (compact index; 0xFFFFFFFF when encoding != XorPrev)
    //   u32 crc32c         (always 0 on write — reader recomputes from decompressed bytes)
    //   u32 payload_size   (bytes of zstd-compressed payload)
    //   u8[payload_size]   payload
    //
    // We re-derive the payload via Compress(GetPage(idx)) because the codec
    // page store doesn't yet expose its internal compressed payload. This is
    // a redundant ~10 us per slot on serialize (a future optimization adds
    // GetPayload/GetCrc32C accessors).
    for (uint32_t idx = 0; idx < _pageStore.GetCapacity(); ++idx)
    {
        if (_pageStore.GetRefCount(idx) == 0)
            continue;

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

        // CRC field: 0 on write. Reader verifies by recomputing.
        const uint32_t crcPlaceholder = 0;
        if (!WritePod(out, crcPlaceholder, err)) return false;

        // Payload: empty for Zero; zstd-compressed reconstructed page otherwise.
        if (encoding == TTDCodecPageStore::Encoding::Zero)
        {
            const uint32_t payloadSize = 0;
            if (!WritePod(out, payloadSize, err)) return false;
        }
        else
        {
            // Reconstruct the 4 KB page (handles XorPrev recursively).
            uint8_t pageBuf[ttd::dump::kSubPageSize];
            if (!_pageStore.GetPage(idx, pageBuf))
            {
                err = "CRC mismatch on slot " + std::to_string(idx) +
                      " during serialize — corrupt live slot";
                return false;
            }
            auto payload = ttd::codec::Compress(pageBuf, ttd::dump::kSubPageSize);
            if (payload.empty())
            {
                err = "zstd compress failed on slot " + std::to_string(idx);
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
                    if (ref.slots[s] != TTDPageRef::kNeverTouched)
                    {
                        auto it = slotRemap.find(ref.slots[s]);
                        if (it == slotRemap.end())
                        {
                            err = "checkpoint " + std::to_string(cp.time.frame) +
                                  " references page " + std::to_string(p) + " sub " +
                                  std::to_string(s) + " slot " +
                                  std::to_string(ref.slots[s]) +
                                  " which is not in the live remap (corrupt timeline?)";
                            return false;
                        }
                        refOut = it->second;
                    }
                }
                if (!WritePod(out, refOut, err)) return false;
            }
        }

        // Peripheral blobs (length-prefixed).
        if (!WriteBlob(out, cp.ayState, err))    return false;
        if (!WriteBlob(out, cp.fdcState, err))   return false;
        if (!WriteBlob(out, cp.tapeState, err))  return false;
        if (!WriteBlob(out, cp.covoxState, err)) return false;
    }

    return true;
}

bool TimeTravelManager::DeserializeSession(std::istream& in, std::string& err)
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
    if (schemaVersion > ttd::dump::kMaxSupportedSchemaVersion)
    {
        err = "file is schema v" + std::to_string(schemaVersion) +
              ", this reader supports up to v" +
              std::to_string(ttd::dump::kMaxSupportedSchemaVersion);
        return false;
    }
    if (schemaVersion != ttd::dump::kSchemaVersion)
    {
        // v2 is the only version we know how to read. Future versions will
        // branch here. v1 files are refused without migration — the codec
        // format change is breaking by design (no v1 support).
        err = "unsupported schema v" + std::to_string(schemaVersion) +
              " (only v" + std::to_string(ttd::dump::kSchemaVersion) +
              " implemented; v1 files are not supported — re-capture the session)";
        return false;
    }

    uint16_t flags = 0;
    if (!ReadPod(in, flags, err)) return false;
    if ((flags & ttd::dump::kFlagsLittleEndian) == 0)
    {
        err = "file is big-endian; only little-endian .ttd files are supported";
        return false;
    }

    uint8_t modelId = 0, modelRamPages = 0;
    uint16_t cpuStateSize = 0, chipsetStateSize = 0;
    uint64_t capturedAtMs = 0;
    if (!ReadPod(in, modelId, err)) return false;
    if (!ReadPod(in, modelRamPages, err)) return false;
    if (!ReadPod(in, cpuStateSize, err)) return false;
    if (!ReadPod(in, chipsetStateSize, err)) return false;
    if (!ReadPod(in, capturedAtMs, err)) return false;

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

    // Skip reserved bytes.
    char reserved[8];
    in.read(reserved, 8);
    if (!in) { err = "stream read failed (reserved)"; return false; }

    // --- Clear existing state ---
    // The file completely replaces the current session. Release all page
    // refs held by the existing timeline before resetting the store.
    for (auto& cp : _timeline)
        ReleaseCheckpointRefs(cp);
    _timeline.clear();
    _pageStore.Reset();
    _inputJournal.Clear();
    _externalEvents.Clear();
    _dirtyScratch.clear();
    _modelRamPages = modelRamPages;

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

        // Materialize the slot in the codec page store.
        // The codec page store API doesn't have a "bulk load" path; we use
        // InternFull / InternXor to recreate each slot.
        //
        // Future optimization: add a TTDCodecPageStore::LoadSlot() that
        // stores the pre-compressed payload directly without re-decoding /
        // re-compressing.
        uint32_t newIdx = 0;
        if (encByte == ttd::dump::kEncodingZero)
        {
            // Reconstruct the zero page and InternFull it (codec detects zero).
            uint8_t zeroBuf[ttd::dump::kSubPageSize];
            std::memset(zeroBuf, 0, sizeof(zeroBuf));
            newIdx = _pageStore.InternFull(zeroBuf);
        }
        else if (encByte == ttd::dump::kEncodingFull)
        {
            // Decompress payload → 4 KB page → InternFull.
            uint8_t pageBuf[ttd::dump::kSubPageSize];
            if (!ttd::codec::Decompress(payload, ttd::dump::kSubPageSize, pageBuf))
            {
                err = "zstd decompress failed on slot " + std::to_string(i);
                return false;
            }
            newIdx = _pageStore.InternFull(pageBuf);
        }
        else if (encByte == ttd::dump::kEncodingXorPrev)
        {
            // The on-disk payload is Compress(GetPage(idx)) — i.e. the FULL
            // reconstructed 4 KB page, NOT a delta (see SerializeSession).
            // We decompress and pass the full page to InternXor, which will
            // internally compute the XOR-against-prev delta and pick the
            // smaller of (delta, full) for the in-memory encoding.
            //
            // NB: an earlier version of this code XORed the decompressed
            // payload with prev_page before calling InternXor, treating the
            // payload as if it were a delta. That double-XORed on GetPage
            // and produced systematically wrong page content for every
            // XorPrev slot. Round-trip tests caught it.
            if (prevSlot == ttd::dump::kNeverTouchedSlot || prevSlot >= i)
            {
                err = "slot " + std::to_string(i) +
                      ": invalid prev_slot " + std::to_string(prevSlot) +
                      " (must be < current index)";
                return false;
            }
            uint8_t pageBuf[ttd::dump::kSubPageSize];
            if (!ttd::codec::Decompress(payload, ttd::dump::kSubPageSize, pageBuf))
            {
                err = "zstd decompress failed on XorPrev slot " + std::to_string(i);
                return false;
            }
            newIdx = _pageStore.InternXor(prevSlot, pageBuf);
        }
        else
        {
            err = "slot " + std::to_string(i) + ": unknown encoding " +
                  std::to_string(encByte);
            return false;
        }

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
    _timeline.reserve(checkpointCount);
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

        cp.ramPages.resize(_modelRamPages);
        for (uint16_t p = 0; p < _modelRamPages; ++p)
        {
            for (uint32_t s = 0; s < ttd::dump::kSubPagesPerEmuPage; ++s)
            {
                uint32_t ref = 0;
                if (!ReadPod(in, ref, err)) return false;
                if (ref == ttd::dump::kNeverTouchedSlot)
                {
                    cp.ramPages[p].slots[s] = TTDPageRef::kNeverTouched;
                }
                else if (ref < pageStoreCount)
                {
                    cp.ramPages[p].slots[s] = ref;
                    // Bump refcount for this reference. The Intern above set
                    // refcount=1; we'll subtract that initial 1 once after the
                    // checkpoint loop. Net: refcount == number of references
                    // across the deserialized timeline.
                    _pageStore.AddRef(ref);
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

        if (!ReadBlob(in, cp.ayState, err))    return false;
        if (!ReadBlob(in, cp.fdcState, err))   return false;
        if (!ReadBlob(in, cp.tapeState, err))  return false;
        if (!ReadBlob(in, cp.covoxState, err)) return false;

        _timeline.push_back(std::move(cp));
    }

    // Correct the Intern's initial refcount=1 for each live slot. After all
    // checkpoints have been materialized, each slot's refcount is
    // (number_of_references + 1). We need it to equal number_of_references
    // so the page store's bookkeeping matches what a fresh in-memory capture
    // of the same content would produce.
    for (uint32_t i = 0; i < pageStoreCount; ++i)
        _pageStore.Release(i);

    // --- Finalize state ---
    // The file does not carry live recording semantics; force Idle. Callers
    // who want to browse the timeline can call RestoreCheckpointForTesting()
    // or SeekTo() to position the emulator at any captured frame.
    _state = TTDSessionState::Idle;

    MLOGINFO("TimeTravelManager::DeserializeSession — loaded schema v%u: "
             "%u pages, %zu checkpoints (frames %llu..%llu), model_ram_pages=%u",
             static_cast<unsigned>(schemaVersion),
             static_cast<unsigned>(pageStoreCount),
             _timeline.size(),
             static_cast<unsigned long long>(sessionStart),
             static_cast<unsigned long long>(sessionEnd),
             static_cast<unsigned>(_modelRamPages));

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
                                              preRamDigest);
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
                                               postRamDigest);
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

} // namespace ttd
