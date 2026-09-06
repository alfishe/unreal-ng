#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/video/screen.h"

/// Turbo render decimation (MainLoop): while turbo mode is active, only 1 of
/// every MainLoop::TURBO_RENDER_DECIMATION frames performs rendering work
/// (contingent UpdateScreen, framebuffer latch). Machine timing is untouched,
/// so a decimated run and a normal run share the exact same machine state.
///
/// These tests pin three properties:
/// 1. CADENCE - the presented framebuffer stays constant across the skipped
///    stretch and changes exactly at the decimation boundary.
/// 2. FIDELITY - the rendered frame at a decimation boundary is pixel-identical
///    to the same frame of a normal-speed run (full ScreenHQ path, rendered
///    from t=0 via ResetPrevTstate; border and multicolor latching intact).
///    Equality also proves skipping UpdateScreen has no effect on machine
///    state - rendering must stay a pure observer.
/// 3. RESUME - disengaging turbo restores per-frame rendering immediately.
///
/// Workload: the ROM boot sequence (border flashing + paper fill), which is
/// deterministic and visually active for well over a hundred frames.

namespace
{

uint64_t Fnv1aHash(const uint8_t* data, size_t size)
{
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

class TurboRenderDecimation_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    MainLoop_CUT* _mainLoop = nullptr;
    Screen* _screen = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";

        _context = _emulator->GetContext();
        _screen = _context->pScreen;
        ASSERT_NE(_screen, nullptr);

        _mainLoop = reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
        ASSERT_NE(_mainLoop, nullptr);

        // Low-latency presentation so the presented frame equals the last
        // latch - the decimation cadence becomes directly observable
        _screen->SetPresentDelayFrames(0);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Hash of the currently presented framebuffer (last completed latch)
    uint64_t PresentedHash()
    {
        FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
        std::vector<uint8_t> out(fb.memoryBufferSize, 0);
        EXPECT_TRUE(_screen->CopyPresentedFramebuffer(out.data(), out.size()));
        return Fnv1aHash(out.data(), out.size());
    }

    /// Run frames one at a time, recording the presented hash after each.
    /// Returns hashes[i] = hash observed after running frame i + 1.
    std::vector<uint64_t> RunFramesAndCollectHashes(size_t frameCount)
    {
        std::vector<uint64_t> hashes;
        hashes.reserve(frameCount);
        for (size_t i = 0; i < frameCount; i++)
        {
            _mainLoop->RunFrame();
            hashes.push_back(PresentedHash());
        }
        return hashes;
    }

    /// Index (0-based, frame i + 1) of the n-th observed latch change.
    /// Returns empty vector-style sentinel SIZE_MAX when not found in budget.
    size_t FindNthChangeIndex(const std::vector<uint64_t>& hashes, size_t n, size_t fromIndex = 0)
    {
        size_t seen = 0;
        for (size_t i = fromIndex + 1; i < hashes.size(); i++)
        {
            if (hashes[i] != hashes[i - 1])
            {
                seen++;
                if (seen == n)
                    return i;
            }
        }
        return SIZE_MAX;
    }
};

TEST_F(TurboRenderDecimation_Test, TurboSkipsRenderingBetweenDecimationBoundaries)
{
    const size_t decimation = MainLoop::TURBO_RENDER_DECIMATION;
    const size_t budget = 2 * decimation + 2;

    _emulator->EnableTurboMode();
    std::vector<uint64_t> hashes = RunFramesAndCollectHashes(budget);

    // First latch change = first decimation boundary that renders new content
    size_t first = FindNthChangeIndex(hashes, 1);
    ASSERT_NE(first, SIZE_MAX) << "No rendered-frame change observed - workload inactive or rendering dead";

    // The skipped stretch after a rendered frame keeps the previous latch
    for (size_t i = first + 1; i < first + decimation && i < hashes.size(); i++)
    {
        EXPECT_EQ(hashes[i], hashes[first]) << "Frame " << i + 1 << " latched during a skipped stretch";
    }

    // The next decimation boundary renders fresh content again
    size_t second = FindNthChangeIndex(hashes, 1, first);
    ASSERT_NE(second, SIZE_MAX) << "Second decimation boundary never rendered";
    EXPECT_EQ(second - first, decimation)
        << "Render cadence must be exactly TURBO_RENDER_DECIMATION frames, got " << second - first;
}

TEST_F(TurboRenderDecimation_Test, RenderedTurboFrameMatchesNormalMode)
{
    const size_t decimation = MainLoop::TURBO_RENDER_DECIMATION;
    const size_t budget = 2 * decimation + 2;

    _emulator->EnableTurboMode();
    std::vector<uint64_t> turboHashes = RunFramesAndCollectHashes(budget);

    size_t first = FindNthChangeIndex(turboHashes, 1);
    ASSERT_NE(first, SIZE_MAX) << "No rendered-frame change observed in turbo run";

    // Reference: identical machine, same frame count, normal speed - every
    // frame renders and latches. Same ROM boot => same deterministic state.
    Emulator* reference = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(reference, nullptr);
    Screen* referenceScreen = reference->GetContext()->pScreen;
    ASSERT_NE(referenceScreen, nullptr);
    referenceScreen->SetPresentDelayFrames(0);
    auto* referenceLoop = reinterpret_cast<MainLoop_CUT*>(reference->GetContext()->pMainLoop);
    ASSERT_NE(referenceLoop, nullptr);

    std::vector<uint64_t> normalHashes;
    for (size_t i = 0; i <= first; i++)
    {
        referenceLoop->RunFrame();
        normalHashes.push_back(0); // placeholder, single final compare below
    }

    FramebufferDescriptor& fb = referenceScreen->GetFramebufferDescriptor();
    std::vector<uint8_t> out(fb.memoryBufferSize, 0);
    ASSERT_TRUE(referenceScreen->CopyPresentedFramebuffer(out.data(), out.size()));
    uint64_t normalHash = Fnv1aHash(out.data(), out.size());

    EmulatorTestHelper::CleanupEmulator(reference);

    // The turbo-rendered frame at the decimation boundary must be
    // pixel-identical to the normal-speed render of the same frame
    EXPECT_EQ(turboHashes[first], normalHash)
        << "Decimated frame at boundary diverged from normal-mode rendering";
}

TEST_F(TurboRenderDecimation_Test, TurboDisengageRestoresPerFrameRendering)
{
    const size_t decimation = MainLoop::TURBO_RENDER_DECIMATION;

    // Accumulate a skipped stretch in turbo, then disengage
    _emulator->EnableTurboMode();
    RunFramesAndCollectHashes(decimation + 5);
    _emulator->DisableTurboMode();

    // The boot screen is visually static at this point, so border color -
    // rendering-only state, exactly what the latch pipeline produces - is
    // forced to a distinct value before every frame. If per-frame rendering
    // resumed, each presented hash must differ from the previous one.
    std::vector<uint64_t> hashes;
    const uint8_t borderColors[] = { 0, 2, 4, 1 };
    for (uint8_t color : borderColors)
    {
        _screen->SetBorderColor(color);
        _mainLoop->RunFrame();
        hashes.push_back(PresentedHash());
    }

    for (size_t i = 1; i < hashes.size(); i++)
    {
        EXPECT_NE(hashes[i], hashes[i - 1]) << "Frame " << i << " after turbo disengage did not refresh the presented image";
    }
}
