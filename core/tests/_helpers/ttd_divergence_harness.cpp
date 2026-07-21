//
// ttd_divergence_harness.cpp — implementation for the harness header.
//
// Test-only. Lives next to the header in tests/_helpers/ and is linked into
// every corpus test that wants to share the oracle.
//

#include "ttd_divergence_harness.h"

#include "_helpers/test_path_helper.h"

#include <common/filehelper.h>
#include <common/stringhelper.h>
#include <debugger/ttd/ttd_checkpoint.h>
#include <debugger/ttd/timetravelmanager.h>
#include <emulator/cpu/z80.h>
#include <emulator/cpu/core.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>

#include <filesystem>
#include <sstream>

namespace ttd {

// =====================================================================
// DivergenceHistory
// =====================================================================

void DivergenceHistory::Reserve(size_t n)
{
    frames.reserve(n);
}

size_t DivergenceHistory::Size() const
{
    return frames.size();
}

bool DivergenceHistory::Empty() const
{
    return frames.empty();
}

void DivergenceHistory::Clear()
{
    frames.clear();
    frames.shrink_to_fit();
}

// =====================================================================
// TTDDivergenceHarness
// =====================================================================

TTDDivergenceHarness::TTDDivergenceHarness(Emulator* emulator)
    : _emulator(emulator)
{
    if (_emulator)
    {
        _context = _emulator->GetContext();
        if (_context)
        {
            _memory = _context->pMemory;
        }
    }
}

bool TTDDivergenceHarness::LoadSnapshot(const std::string& relativeOrAbsolutePath)
{
    if (!_emulator)
        return false;

    // Resolve the snapshot path. Three locations are searched, in order:
    //   1. The path as-is (absolute or CWD-relative).
    //   2. <project_root>/testdata/<path> — test fixtures.
    //   3. <project_root>/<path>          — corpus data (e.g. data/testsoft).
    // The first that exists wins.
    std::string path = relativeOrAbsolutePath;
    if (!std::filesystem::exists(path))
    {
        std::string candidate = TestPathHelper::GetTestDataPath(relativeOrAbsolutePath);
        if (std::filesystem::exists(candidate))
        {
            path = candidate;
        }
        else
        {
            // Try under project root directly (handles data/testsoft/...).
            std::filesystem::path root = TestPathHelper::findProjectRoot();
            candidate = (root / relativeOrAbsolutePath).string();
            if (std::filesystem::exists(candidate))
                path = candidate;
        }
    }

    if (_emulator->LoadSnapshot(path))
    {
        _lastSnapshotPath = path;
        _snapshotLoaded   = true;
        return true;
    }
    return false;
}

bool TTDDivergenceHarness::ReloadSnapshotForReplayPhase()
{
    if (!_emulator || _lastSnapshotPath.empty())
        return false;
    return _emulator->LoadSnapshot(_lastSnapshotPath);
}

// ---------------------------------------------------------------------
// Live run + per-frame capture
// ---------------------------------------------------------------------

DivergenceHistory TTDDivergenceHarness::RunLiveAndCapture(size_t frames)
{
    DivergenceHistory history;
    history.Reserve(frames);
    if (!_emulator)
        return history;

    for (size_t i = 0; i < frames; ++i)
    {
        _emulator->RunFrame(/*skipBreakpoints=*/true);
        history.frames.push_back(CaptureCurrentFrame());
    }

    return history;
}

DivergenceHistory TTDDivergenceHarness::ExtractHashesFromTimeline()
{
    DivergenceHistory history;
    if (!_context || !_context->pTimeTravelManager)
        return history;

    TimeTravelManager* ttd = _context->pTimeTravelManager;
    const size_t count = ttd->GetCheckpointCount();
    history.Reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const TTDCheckpoint* cp = ttd->GetCheckpoint(i);
        if (!cp)
            continue;

        DivergenceFrame f;
        f.frameCounter = cp->time.frame;
        f.t_states     = cp->globalT;

        // Compute RAM digest from the page store — restore semantics in
        // miniature. Pages marked NEVER_TOUCHED hash as zero (their live
        // content at capture time was the baseline, and the baseline is
        // whatever the snapshot set up).
        uint64_t ram_digest = 0;
        const uint16_t pages = std::min<uint16_t>(
            ttd->GetModelRamPages(),
            static_cast<uint16_t>(cp->ramPages.size()));
        for (uint16_t p = 0; p < pages; ++p)
        {
            const TTDPageRef& ref = cp->ramPages[p];
            if (ref.IsNeverTouched())
            {
                // Hash the current live page content — matches what
                // RestoreRamPages leaves in place for never-touched pages.
                uint8_t* page = _memory ? _memory->RAMPageAddress(p) : nullptr;
                if (page)
                    ram_digest = HashCombine(ram_digest, page, PAGE_SIZE);
                continue;
            }
            const uint8_t* stored = ttd->GetPageStore().GetPage(ref.storeIndex);
            if (stored)
                ram_digest = HashCombine(ram_digest, stored, PAGE_SIZE);
        }
        f.ram_digest = ram_digest;

        // Hash the raw captured bytes — matches CaptureCurrentFrame's
        // computation, so the two histories are directly comparable.
        const uint8_t* cpuBytes     = reinterpret_cast<const uint8_t*>(&cp->cpu);
        const uint8_t* chipsetBytes = reinterpret_cast<const uint8_t*>(&cp->chipset);

        f.snapshot.frame_counter = cp->time.frame;
        f.snapshot.t_states      = cp->globalT;
        f.snapshot.ram_digest    = ram_digest;

        uint64_t composite = HashBytes(cpuBytes, sizeof(TTDCpuState));
        composite = HashCombine(composite, chipsetBytes, sizeof(TTDChipsetState));
        composite = HashCombine(composite,
                                reinterpret_cast<const uint8_t*>(&ram_digest),
                                sizeof(ram_digest));
        f.hash = composite;

        history.frames.push_back(std::move(f));
    }

    return history;
}

// ---------------------------------------------------------------------
// TTD recording + checkpoint capture
// ---------------------------------------------------------------------

bool TTDDivergenceHarness::StartRecordingAndCaptureTimeline(size_t frames)
{
    if (!_emulator || !_context)
        return false;

    // Critical: re-load the snapshot so the recording starts from the
    // SAME initial state as the live run. This makes the timeline's
    // frame_counter values line up 1:1 with the live history's indices,
    // so VerifyReplayMatchesLive can SeekTo by absolute frame number.
    if (!ReloadSnapshotForReplayPhase())
        return false;

    TimeTravelManager* ttd = _context->pTimeTravelManager;
    if (!ttd)
        return false;

    if (!ttd->StartRecording())
        return false;

    for (size_t i = 0; i < frames; ++i)
        _emulator->RunFrame(/*skipBreakpoints=*/true);

    // Sanity: timeline should have at least one checkpoint per frame.
    return ttd->GetCheckpointCount() >= 1;
}

// ---------------------------------------------------------------------
// Replay verification
// ---------------------------------------------------------------------

bool TTDDivergenceHarness::VerifyReplayMatchesLive(size_t frameIndex,
                                                    const DivergenceHistory& expected,
                                                    std::string* failureMsg)
{
    if (frameIndex >= expected.Size())
    {
        if (failureMsg)
            *failureMsg = "frameIndex out of range";
        return false;
    }

    if (!_context || !_context->pTimeTravelManager)
    {
        if (failureMsg)
            *failureMsg = "TimeTravelManager unavailable";
        return false;
    }

    const DivergenceFrame& exp = expected.frames[frameIndex];

    // Build the target TTDTimePoint from the captured frame counter.
    // frameIndex == 0 is the first captured frame; the timeline stores
    // checkpoints at the post-RunFrame boundary, so frameIndex maps 1:1.
    TTDTimePoint target;
    target.frame    = exp.frameCounter;
    target.tInFrame = 0;

    if (!_context->pTimeTravelManager->SeekTo(target))
    {
        if (failureMsg)
            *failureMsg = StringHelper::Format(
                "SeekTo({frame=%llu, tInFrame=0}) returned false",
                static_cast<unsigned long long>(target.frame));
        return false;
    }

    // Capture the post-seek state and compare.
    const DivergenceFrame actual = CaptureCurrentFrame();

    if (actual.ram_digest != exp.ram_digest)
    {
        if (failureMsg)
        {
            std::ostringstream oss;
            oss << "RAM digest mismatch at frame " << frameIndex
                << ": expected=" << HashToString(exp.ram_digest)
                << " actual=" << HashToString(actual.ram_digest);
            *failureMsg = oss.str();
        }
        return false;
    }

    if (actual.hash != exp.hash)
    {
        if (failureMsg)
        {
            std::ostringstream oss;
            oss << "Snapshot hash mismatch at frame " << frameIndex
                << ": expected=" << HashToString(exp.hash)
                << " actual=" << HashToString(actual.hash);
            *failureMsg = oss.str();
        }
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------
// Sample-frame selection
// ---------------------------------------------------------------------

std::vector<size_t> TTDDivergenceHarness::PickSampleFrames(size_t totalFrames, size_t step) const
{
    std::vector<size_t> result;
    if (totalFrames == 0 || step == 0)
        return result;

    for (size_t i = 0; i < totalFrames; i += step)
        result.push_back(i);

    // Always include the last frame even if step doesn't land on it.
    if (result.empty() || result.back() != totalFrames - 1)
        result.push_back(totalFrames - 1);

    return result;
}

// =====================================================================
// Internal helpers
// =====================================================================

DivergenceFrame TTDDivergenceHarness::CaptureCurrentFrame()
{
    DivergenceFrame f;
    if (!_context || !_context->pCore)
        return f;

    Z80* z80 = _context->pCore->GetZ80();
    if (!z80)
        return f;

    const uint64_t ram_digest = HashRamInUse();
    f.ram_digest = ram_digest;

    // Use the SAME capture path as the TTD checkpoint: CaptureCpuState /
    // CaptureChipsetState produce TTDCpuState / TTDChipsetState from the
    // live Z80State / EmulatorState. Hashing these raw bytes matches
    // ExtractHashesFromTimeline's hash of the stored checkpoint, so the
    // two are directly comparable.
    const TTDCpuState     cpu     = CaptureCpuState(*static_cast<const Z80State*>(z80));
    const TTDChipsetState chipset = CaptureChipsetState(_context->emulatorState);

    uint64_t composite = HashBytes(reinterpret_cast<const uint8_t*>(&cpu), sizeof(TTDCpuState));
    composite = HashCombine(composite,
                            reinterpret_cast<const uint8_t*>(&chipset),
                            sizeof(TTDChipsetState));
    composite = HashCombine(composite,
                            reinterpret_cast<const uint8_t*>(&ram_digest),
                            sizeof(ram_digest));
    f.hash = composite;

    f.frameCounter = _context->emulatorState.frame_counter;
    f.t_states     = _context->emulatorState.t_states;

    return f;
}

uint64_t TTDDivergenceHarness::HashRamInUse() const
{
    if (!_memory || !_context)
        return 0;

    // Hash every model-RAM page the emulator has. Pentagon 128 = 8 pages of
    // 16 KB = 128 KB. Spectrum 48 = 3 pages = 48 KB. Other models vary.
    // Page count is read from the TTD manager when available so the digest
    // matches the per-checkpoint capture scope.
    uint16_t pages = 0;
    if (_context->pTimeTravelManager)
        pages = _context->pTimeTravelManager->GetModelRamPages();

    if (pages == 0)
    {
        // Fallback when TTD is not Recording (e.g. during the live run).
        // Derive from config.ramsize (KB). Each page = PAGE_SIZE = 16 KB.
        const unsigned ramKb = _context->config.ramsize;
        if (ramKb > 0)
            pages = static_cast<uint16_t>(ramKb / (PAGE_SIZE / 1024));
        if (pages == 0)
            pages = 8; // safe default for 128K-class models
    }

    uint64_t h = 0;
    for (uint16_t p = 0; p < pages; ++p)
    {
        uint8_t* page = _memory->RAMPageAddress(p);
        if (page)
            h = HashCombine(h, page, PAGE_SIZE);
    }

    return h;
}

} // namespace ttd
