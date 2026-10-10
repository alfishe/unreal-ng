//
// ttddivergenceharness.cpp — implementation for the harness header.
//
// Test-only. Lives next to the header in tests/_helpers/ and is linked into
// every corpus test that wants to share the oracle.
//

#include "ttddivergenceharness.h"

#include "_helpers/testpathhelper.h"

#include <common/filehelper.h>
#include <common/stringhelper.h>
#include <debugger/ttd/ttdcheckpoint.h>
#include <debugger/ttd/timetravelcontroller.h>
#include <debugger/ttd/timetravelengine.h>
#include <emulator/cpu/z80.h>
#include <emulator/cpu/core.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <emulator/video/screen.h>       // Screen::GetFramebufferData (framebuffer digest)

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
            std::filesystem::path root = TestPathHelper::FindProjectRoot();
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
    if (!_context || !_context->pTimeTravelController)
        return history;

    TimeTravelController* ttd = _context->pTimeTravelController;
    const size_t count = ttd->GetCheckpointCount();
    history.Reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const TTDCheckpoint* cp = ttd->GetCheckpoint(i);
        if (!cp)
            continue;

        DivergenceFrame f;
        f.frameCounter = cp->time.frame;
        f.tStates      = cp->globalT;

        // The RAM as the engine holds it at this checkpoint (the machine RAM
        // region, restored into a buffer: what a seek restores), page by page
        uint64_t ram_digest = 0;
        const TimeTravelEngine& engine = ttd->GetEngine();
        const size_t engineIndex = engine.FirstCheckpoint() + i;
        uint32_t ramRegion = 0;
        while (ramRegion < engine.Regions().size() && engine.Regions()[ramRegion].name != "ram")
            ++ramRegion;
        if (ramRegion < engine.Regions().size() && engineIndex < engine.CheckpointCount())
        {
            const TTDRegionDesc& desc = engine.Regions()[ramRegion];
            std::vector<uint8_t> ram(size_t(desc.pieces) * kTTDPieceSize);
            if (engine.RestoreRegion(engineIndex, ramRegion, ram.data()).Ok())
            {
                const uint16_t pages = std::min<uint16_t>(ttd->GetModelRamPages(),
                                                          static_cast<uint16_t>(ram.size() / PAGE_SIZE));
                for (uint16_t p = 0; p < pages; ++p)
                    ram_digest = HashCombine(ram_digest, ram.data() + size_t(p) * PAGE_SIZE, PAGE_SIZE);
            }
        }
        f.ramDigest = ram_digest;

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

    TimeTravelController* ttd = _context->pTimeTravelController;
    if (!ttd)
        return false;

    if (!ttd->StartRecording())
        return false;

    for (size_t i = 0; i < frames; ++i)
        _emulator->RunFrame(/*skipBreakpoints=*/true);

    // Stop recording so VerifyReplayMatchesLive can SeekTo (scrubbing during
    // Recording is rejected by the engine to prevent timeline corruption).
    // History is retained — StopRecording only flips the state machine.
    ttd->StopRecording();

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

    if (!_context || !_context->pTimeTravelController)
    {
        if (failureMsg)
            *failureMsg = "TimeTravelController unavailable";
        return false;
    }

    const DivergenceFrame& exp = expected.frames[frameIndex];

    // Build the target TTDTimePoint from the captured frame counter.
    // frameIndex == 0 is the first captured frame; the timeline stores
    // checkpoints at the post-RunFrame boundary, so frameIndex maps 1:1.
    TTDTimePoint target;
    target.frame    = exp.frameCounter;
    target.tInFrame = 0;

    if (!_context->pTimeTravelController->SeekTo(target))
    {
        if (failureMsg)
            *failureMsg = StringHelper::Format(
                "SeekTo({frame=%llu, tInFrame=0}) returned false",
                static_cast<unsigned long long>(target.frame));
        return false;
    }

    // Capture the post-seek state and compare.
    const DivergenceFrame actual = CaptureCurrentFrame();

    if (actual.ramDigest != exp.ramDigest)
    {
        if (failureMsg)
        {
            std::ostringstream oss;
            oss << "RAM digest mismatch at frame " << frameIndex
                << ": expected=" << HashToString(exp.ramDigest)
                << " actual=" << HashToString(actual.ramDigest);
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

    // Framebuffer digest — this is the check that catches screen-rendering
    // restore bugs. The RAM/CPU/chipset may all match bit-for-bit, but if
    // the screen renderer's cached bank pointer wasn't resynced from p7FFD
    // after the restore, the framebuffer will read from the wrong bank
    // (typically garbage from the live pre-seek state) and the digest will
    // differ.
    //
    // Skipped when the expected entry has no framebuffer digest (== 0).
    // ExtractHashesFromTimeline doesn't seek, so it cannot populate the
    // framebuffer digest; tests that want framebuffer verification must
    // either use RunLiveAndCapture (which runs live and captures each
    // frame's real framebuffer) or call VerifyReplayMatchesLive after
    // pre-populating expected via SeekTo + CaptureCurrentFrame.
    if (exp.framebufferDigest != 0 && actual.framebufferDigest != exp.framebufferDigest)
    {
        if (failureMsg)
        {
            std::ostringstream oss;
            oss << "Framebuffer digest mismatch at frame " << frameIndex
                << ": expected=" << HashToString(exp.framebufferDigest)
                << " actual=" << HashToString(actual.framebufferDigest)
                << " (RAM/CPU/chipset all matched — renderer state out of sync with restored ports)";
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
    f.ramDigest = ram_digest;

    // Use the SAME capture path as the TTD checkpoint: CaptureCpuState /
    // CaptureChipsetState produce TTDCpuState / TTDChipsetState from the
    // live Z80State / EmulatorState. Hashing these raw bytes matches
    // ExtractHashesFromTimeline's hash of the stored checkpoint, so the
    // two are directly comparable.
    const TTDCpuState cpu = CaptureCpuState(*static_cast<const Z80State*>(z80));
    TTDChipsetState chipset = CaptureChipsetState(_context->emulatorState, static_cast<uint32_t>(z80->t));

    uint64_t composite = HashBytes(reinterpret_cast<const uint8_t*>(&cpu), sizeof(TTDCpuState));
    composite = HashCombine(composite,
                            reinterpret_cast<const uint8_t*>(&chipset),
                            sizeof(TTDChipsetState));
    composite = HashCombine(composite,
                            reinterpret_cast<const uint8_t*>(&ram_digest),
                            sizeof(ram_digest));
    f.hash = composite;

    f.frameCounter = _context->emulatorState.frame_counter;
    f.tStates      = _context->emulatorState.t_states;

    // Framebuffer digest — catches restore bugs that leave the renderer
    // pointing at the wrong bank (e.g. p7FFD bit 3 not synced) or stale
    // framebuffer content. The renderer must have called RenderOnlyMainScreen
    // before this is meaningful; the harness invokes it explicitly during
    // VerifyReplayMatchesLive via the engine's SeekTo path.
    f.framebufferDigest = HashFramebuffer();

    return f;
}

uint64_t TTDDivergenceHarness::HashFramebuffer() const
{
    if (!_context || !_context->pScreen)
        return 0;

    uint32_t* buf  = nullptr;
    size_t    size = 0;
    _context->pScreen->GetFramebufferData(&buf, &size);
    if (!buf || size == 0)
        return 0;

    return HashBytes(reinterpret_cast<const uint8_t*>(buf), size);
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
    if (_context->pTimeTravelController)
        pages = _context->pTimeTravelController->GetModelRamPages();

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
