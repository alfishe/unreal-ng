/// @file ttd_manager.cpp
/// @brief TTDManager — capture orchestrator implementation.
///
/// Per parent TDD §6.3, §7.1. The hot path is OnFrameBoundary: dirty pages
/// are freshly Intern'd, clean pages AddRef the previous checkpoint's slot,
/// CPU/chipset are field-copied via the helpers in ttd_checkpoint.cpp.

#include "ttd_manager.h"

#include <cassert>
#include <cstring>

#include "ttd_checkpoint.h"
#include "ttd_dirty_tracker.h"
#include "ttd_page_store.h"

// Pull in the actual struct definitions for the capture call sites.
#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"            // Z80, Z80State
#include "emulator/emulatorcontext.h"    // EmulatorContext
#include "emulator/memory/memory.h"      // Memory
#include "emulator/platform.h"           // EmulatorState, CONFIG, PAGE_SIZE, MAX_RAM_PAGES
#include "stdafx.h"

namespace ttd {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TTDManager::TTDManager(EmulatorContext* context)
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

TTDManager::~TTDManager()
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

bool TTDManager::StartRecording()
{
    if (_state == TTDSessionState::Recording)
        return true;  // Idempotent

    if (!_context || !_memory || !_dirtyTracker)
    {
        MLOGWARNING("TTDManager::StartRecording — missing dependencies (context=%p memory=%p tracker=%p)",
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
    }

    _modelRamPages = ResolveModelRamPages();
    if (_modelRamPages == 0 || _modelRamPages > MAX_RAM_PAGES)
    {
        MLOGWARNING("TTDManager::StartRecording — implausible modelRamPages=%u, refusing to start",
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

    MLOGINFO("TTDManager::StartRecording — baseline captured: modelRamPages=%u, timeline=1, pageStoreBytes=%zu",
             static_cast<unsigned>(_modelRamPages), _pageStore.GetCapacityBytes());

    return true;
}

void TTDManager::StopRecording()
{
    if (_state != TTDSessionState::Recording)
        return;  // Idempotent
    _state = TTDSessionState::Idle;
    MLOGINFO("TTDManager::StopRecording — timeline retained with %zu checkpoints",
             _timeline.size());
}

void TTDManager::InvalidateSession(const char* reason)
{
    if (_timeline.empty() && _state == TTDSessionState::Idle)
        return;  // Nothing to invalidate

    MLOGINFO("TTDManager::InvalidateSession — reason='%s', dropping %zu checkpoints",
             reason ? reason : "(null)", _timeline.size());

    for (auto& cp : _timeline)
        ReleaseCheckpointRefs(cp);
    _timeline.clear();
    _pageStore.Reset();
    _dirtyScratch.clear();
    _modelRamPages = 0;
    _state = TTDSessionState::Idle;

    // Reset the dirty tracker too — the session-scoped _everDirty set is part
    // of the captured history's validity contract.
    if (_dirtyTracker)
        _dirtyTracker->ResetSession();
}

TTDSessionInfo TTDManager::GetSessionInfo() const
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

void TTDManager::OnFrameBoundary()
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

void TTDManager::CaptureNow(TTDCheckpoint& out)
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
}

void TTDManager::CaptureBaselineRamPages(std::vector<TTDPageRef>& outRamPages)
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

void TTDManager::UpdateRamPages(const std::vector<uint16_t>& dirtyPages,
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

void TTDManager::ReleaseCheckpointRefs(TTDCheckpoint& cp)
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

uint16_t TTDManager::ResolveModelRamPages() const
{
    if (!_context)
        return 0;

    // config.ramsize is in KB; each page is PAGE_SIZE = 16 KB.
    const CONFIG& cfg = _context->config;
    if (cfg.ramsize == 0 || cfg.ramsize > MAX_RAM_PAGES * (PAGE_SIZE / 1024))
    {
        MLOGWARNING("TTDManager::ResolveModelRamPages — implausible ramsize=%u KB, falling back to MAX_RAM_PAGES",
                    cfg.ramsize);
        return MAX_RAM_PAGES;
    }
    return static_cast<uint16_t>(cfg.ramsize / (PAGE_SIZE / 1024));
}

const TTDCheckpoint* TTDManager::GetCheckpoint(size_t idx) const
{
    if (idx >= _timeline.size())
        return nullptr;
    return &_timeline[idx];
}

} // namespace ttd
