/// @file timetravelmanager.cpp
/// @brief TimeTravelManager — capture orchestrator implementation.
///
/// Per parent TDD §6.3, §7.1. The hot path is OnFrameBoundary: dirty pages
/// are freshly Intern'd, clean pages AddRef the previous checkpoint's slot,
/// CPU/chipset are field-copied via the helpers in ttd_checkpoint.cpp.

#include "timetravelmanager.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "ttd_checkpoint.h"
#include "ttd_dirty_tracker.h"
#include "ttd_page_store.h"

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
#include "emulator/sound/covox.h"                        // Covox (peripheral, P1.5)
#include "emulator/sound/soundmanager.h"                 // SoundManager
#include "emulator/video/screen.h"                       // Screen::InitFrame (restore path)
#include "stdafx.h"

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

    if (!_context || !_memory || !_dirtyTracker)
    {
        MLOGWARNING("TimeTravelManager::StartRecording — missing dependencies (context=%p memory=%p tracker=%p)",
                    (void*)_context, (void*)_memory, (void*)_dirtyTracker);
        return false;
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
    }

    _modelRamPages = ResolveModelRamPages();
    if (_modelRamPages == 0 || _modelRamPages > MAX_RAM_PAGES)
    {
        MLOGWARNING("TimeTravelManager::StartRecording — implausible modelRamPages=%u, refusing to start",
                    static_cast<unsigned>(_modelRamPages));
        _modelRamPages = 0;
        return false;
    }

    // Capture the baseline checkpoint so the timeline always has at least
    // one entry. This is the only place we pay the full model-RAM copy cost
    // up front (v1 strategy — see the header doc for the v2 fast-path plan).
    TTDCheckpoint baseline;
    CaptureNow(baseline);
    _timeline.push_back(std::move(baseline));

    _state = TTDSessionState::Recording;

    MLOGINFO("TimeTravelManager::StartRecording — baseline captured: modelRamPages=%u, timeline=1, pageStoreBytes=%zu",
             static_cast<unsigned>(_modelRamPages), _pageStore.GetCapacityBytes());

    return true;
}

void TimeTravelManager::StopRecording()
{
    if (_state != TTDSessionState::Recording)
        return;  // Idempotent
    _state = TTDSessionState::Idle;
    MLOGINFO("TimeTravelManager::StopRecording — timeline retained with %zu checkpoints",
             _timeline.size());
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
    _modelRamPages = 0;
    _state = TTDSessionState::Idle;

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
    info.baselineFramesCaptured = 0;  // Diagnostic hook for v2 (working-set-proportional mode)

    if (!_timeline.empty())
    {
        const auto& first = _timeline.front();
        const auto& last  = _timeline.back();
        info.sessionStartFrame = first.time.frame;
        info.currentEndFrame   = last.time.frame;
    }

    return info;
}

// ---------------------------------------------------------------------------
// Capture (emulator thread)
// ---------------------------------------------------------------------------

void TimeTravelManager::OnFrameBoundary()
{
    if (_state != TTDSessionState::Recording)
        return;

    if (!_context || !_memory || !_dirtyTracker)
        return;  // Defensive — should not happen if StartRecording succeeded

    TTDCheckpoint cp;
    CaptureNow(cp);
    _timeline.push_back(std::move(cp));
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
    // First capture of a session: Intern every model-RAM page as the baseline.
    // Subsequent captures: dirty → Intern, clean → AddRef previous checkpoint's slot.
    if (_timeline.empty())
    {
        CaptureBaselineRamPages(out.ramPages);
    }
    else
    {
        const TTDCheckpoint& prev = _timeline.back();

        // Collect dirty pages from the tracker. The buffer is reused across
        // frames to avoid per-frame allocation (CollectAndClear appends).
        _dirtyScratch.clear();
        _dirtyTracker->CollectAndClear(_dirtyScratch);

        UpdateRamPages(_dirtyScratch, prev.ramPages, out.ramPages);
    }

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
    outRamPages.resize(_modelRamPages);
    for (uint16_t p = 0; p < _modelRamPages; ++p)
    {
        // Memory owns the RAM backing; RAMPageAddress returns a host pointer
        // to the 16 KB page. We Intern a copy.
        const uint8_t* pageData = _memory->RAMPageAddress(p);
        uint32_t idx = (pageData != nullptr)
            ? _pageStore.Intern(pageData)
            : _pageStore.Intern(static_cast<const uint8_t*>(nullptr));  // Should never happen; Intern asserts
        outRamPages[p].storeIndex = idx;
    }
}

void TimeTravelManager::UpdateRamPages(const std::vector<uint16_t>& dirtyPages,
                                const std::vector<TTDPageRef>& prevRamPages,
                                std::vector<TTDPageRef>& outRamPages)
{
    // Output has same shape as previous checkpoint's ramPages.
    outRamPages.assign(prevRamPages.begin(), prevRamPages.end());

    // dirtyPages is in ascending order (CollectAndClear guarantee), so a
    // two-pointer walk avoids a hash-set lookup per page. We'll move a
    // cursor through dirtyPages as we iterate pages in [0, _modelRamPages).
    size_t dirtyCursor = 0;
    for (uint16_t p = 0; p < _modelRamPages; ++p)
    {
        bool dirty = (dirtyCursor < dirtyPages.size() && dirtyPages[dirtyCursor] == p);
        if (dirty)
            ++dirtyCursor;

        // Pages beyond _modelRamPages are NEVER_TOUCHED; this loop doesn't touch them.

        if (dirty)
        {
            // Page changed this frame — Intern the new content. The previous
            // checkpoint's slot for this page is released implicitly when the
            // previous checkpoint itself is dropped (by thinning or session
            // invalidation). AddRef is the COW path; Intern is the dirty path.
            const uint8_t* pageData = _memory->RAMPageAddress(p);
            outRamPages[p].storeIndex = (pageData != nullptr)
                ? _pageStore.Intern(pageData)
                : _pageStore.Intern(static_cast<const uint8_t*>(nullptr));
        }
        else
        {
            // Page clean since last checkpoint — share the previous slot.
            // AddRef so the slot survives even if the previous checkpoint
            // is thinned out before this one.
            const TTDPageRef& prevRef = prevRamPages[p];
            if (!prevRef.IsNeverTouched())
            {
                _pageStore.AddRef(prevRef.storeIndex);
                // outRamPages[p].storeIndex already copied from prevRef above.
            }
            else
            {
                // Page was NEVER_TOUCHED in the previous checkpoint and is
                // still clean — leave it NEVER_TOUCHED.
                // (Restoration reads live RAM for this page; if the live
                // RAM has been overwritten since session start, this is
                // incorrect — but that case can't happen in v1 because the
                // baseline capture Intern'd every model-RAM page, so no
                // model-RAM page is ever NEVER_TOUCHED. The branch is here
                // for the v2 working-set-proportional optimization.)
            }
        }
    }

    // Any dirty pages beyond _modelRamPages are anomalies (dirty tracker
    // shouldn't mark pages the model doesn't have, but defensive logging
    // belongs in the dirty tracker, not here).
}

void TimeTravelManager::ReleaseCheckpointRefs(TTDCheckpoint& cp)
{
    for (auto& ref : cp.ramPages)
    {
        if (!ref.IsNeverTouched())
        {
            _pageStore.Release(ref.storeIndex);
            ref.storeIndex = TTDPageRef::kNeverTouched;
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

    if (_state != TTDSessionState::Recording && _state != TTDSessionState::Detached)
    {
        MLOGWARNING("TimeTravelManager::RestoreCheckpointForTesting — state is %s, expected Recording or Detached",
                    TTDSessionStateToString(_state));
        return false;
    }

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
    // InitFrame resets the renderer's frame-local counters so the next
    // rendered frame starts from a clean state matching the restored beam
    // position. Without this, the next frame would inherit dirty counters
    // from whatever frame was running before the restore.
    if (_context->pScreen)
    {
        _context->pScreen->InitFrame();
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

        const uint8_t* stored = _pageStore.GetPage(ref.storeIndex);
        if (!stored)
        {
            MLOGWARNING("TimeTravelManager::RestoreRamPages — null page data for storeIndex=%u",
                        ref.storeIndex);
            continue;
        }

        std::memcpy(pageData, stored, TTDPageStore::kPageSize);
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

bool TimeTravelManager::SeekTo(const TTDTimePoint& target)
{
    // ------------------------------------------------------------------
    // Validate preconditions.
    // ------------------------------------------------------------------
    if (!_context)
    {
        MLOGWARNING("TimeTravelManager::SeekTo — null _context");
        return false;
    }

    if (_state == TTDSessionState::Idle)
    {
        MLOGWARNING("TimeTravelManager::SeekTo — session is Idle (no history)");
        return false;
    }

    if (_timeline.empty())
    {
        MLOGWARNING("TimeTravelManager::SeekTo — timeline is empty");
        return false;
    }

    const TTDTimePoint sessionEnd = _timeline.back().time;
    if (sessionEnd < target)
    {
        MLOGWARNING("TimeTravelManager::SeekTo — target (frame=%llu, tInFrame=%u) "
                    "is beyond session end (frame=%llu, tInFrame=%u)",
                    static_cast<unsigned long long>(target.frame),
                    static_cast<unsigned>(target.tInFrame),
                    static_cast<unsigned long long>(sessionEnd.frame),
                    static_cast<unsigned>(sessionEnd.tInFrame));
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
    // ------------------------------------------------------------------
    if (target.tInFrame > 0)
    {
        ReplayWithinFrame(cp.time.frame, target.tInFrame);
    }

    // ------------------------------------------------------------------
    // Step 4: transition to Detached (TDD §4.2).
    // ------------------------------------------------------------------
    _state = TTDSessionState::Detached;

    MLOGINFO("TimeTravelManager::SeekTo — arrived at (frame=%llu,tInFrame=%u), state=Detached",
             static_cast<unsigned long long>(target.frame),
             static_cast<unsigned>(target.tInFrame));

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
    if (_state == TTDSessionState::Idle || _timeline.empty())
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
    if (_state == TTDSessionState::Idle || _timeline.empty())
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

    // ------------------------------------------------------------------
    // Step 1: ensure the emulator is positioned at `from`. SeekTo handles
    // binary search, RestoreCheckpoint, intra-frame silent replay, and the
    // Detached transition. If the caller already SeekTo'd to `from` this
    // is a re-restore (deterministic — same machine state results).
    //
    // Doing the seek inside Resume (rather than requiring the caller to
    // SeekTo first) makes the API self-contained and matches the TDD's
    // "atomic under pause" requirement.
    // ------------------------------------------------------------------
    if (!SeekTo(from))
    {
        // SeekTo already logged the specific failure.
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

    // ------------------------------------------------------------------
    // Step 4: return to Recording. Next OnFrameBoundary will append a fresh
    // checkpoint at frame `from.frame + 1` (the live emulator's frame
    // counter is set by SeekTo).
    // ------------------------------------------------------------------
    _state = TTDSessionState::Recording;

    MLOGINFO("TimeTravelManager::ResumeRecordingFrom — resumed at "
             "(frame=%llu, tInFrame=%u); timeline %zu→%zu checkpoints, "
             "journal %zu→%zu events, state=Recording",
             static_cast<unsigned long long>(from.frame),
             static_cast<unsigned>(from.tInFrame),
             preTimelineSize, _timeline.size(),
             preJournalSize, _inputJournal.Size());

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

} // namespace ttd
