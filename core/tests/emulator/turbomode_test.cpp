#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testtiminghelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/video/screen.h"
#include "base/featuremanager.h"
#include "emulator/sound/soundmanager.h"
#include "3rdparty/message-center/messagecenter.h"

/// Tests for emulator turbo mode and speed control:
/// 1. State control: EnableTurboMode, DisableTurboMode, IsTurboMode, SetSpeedMultiplier, GetSpeedMultiplier
/// 2. Notification dispatch: NC_SPEED_CHANGED posted on turbo toggle or multiplier change
/// 3. Sound DSP management: turbo forces low-quality DSP and suppresses synthesis when audio is unrequested
/// 4. Render decimation: cadence, fidelity, resume, adaptive decimation rules
///
/// Turbo render decimation (MainLoop): while turbo mode is active, only 1 of
/// every MainLoop::TURBO_RENDER_DECIMATION frames performs rendering work
/// (contingent UpdateScreen, framebuffer latch). Machine timing is untouched,
/// so a decimated run and a normal run share the exact same machine state.

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

class TurboMode_Test : public ::testing::Test
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

        // Cadence tests assert the fixed default; adaptive decimation would
        // re-derive it from wall-clock speed and make them timing dependent
        _emulator->GetMainLoop()->SetTurboRenderAdaptive(false);
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

using TurboRenderDecimation_Test = TurboMode_Test;

// ---------------------------------------------------------------------------
// Basic Turbo Mode & Speed Multiplier State Controls
// ---------------------------------------------------------------------------

TEST_F(TurboMode_Test, EnableDisable_TogglesTurboState)
{
    EXPECT_FALSE(_emulator->IsTurboMode());

    _emulator->EnableTurboMode(false);
    EXPECT_TRUE(_emulator->IsTurboMode());

    _emulator->DisableTurboMode();
    EXPECT_FALSE(_emulator->IsTurboMode());

    _emulator->EnableTurboMode(true);
    EXPECT_TRUE(_emulator->IsTurboMode());

    _emulator->DisableTurboMode();
    EXPECT_FALSE(_emulator->IsTurboMode());
}

TEST_F(TurboMode_Test, SpeedMultiplier_GetAndSet)
{
    EXPECT_EQ(_emulator->GetSpeedMultiplier(), 1);

    // Multiplier is queued and applies at the start of the next frame
    _emulator->SetSpeedMultiplier(2);
    _mainLoop->RunFrame();
    EXPECT_EQ(_emulator->GetSpeedMultiplier(), 2);

    _emulator->SetSpeedMultiplier(4);
    _mainLoop->RunFrame();
    EXPECT_EQ(_emulator->GetSpeedMultiplier(), 4);

    _emulator->SetSpeedMultiplier(8);
    _mainLoop->RunFrame();
    EXPECT_EQ(_emulator->GetSpeedMultiplier(), 8);

    _emulator->SetSpeedMultiplier(16);
    _mainLoop->RunFrame();
    EXPECT_EQ(_emulator->GetSpeedMultiplier(), 16);

    _emulator->SetSpeedMultiplier(1);
    _mainLoop->RunFrame();
    EXPECT_EQ(_emulator->GetSpeedMultiplier(), 1);
}

// ---------------------------------------------------------------------------
// Speed multiplier and turbo mode notifications
// ---------------------------------------------------------------------------

TEST_F(TurboMode_Test, SetSpeedMultiplierPostsNotification)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    std::atomic<int> receivedCount{0};
    uint8_t capturedMultiplier = 0;
    bool capturedTurbo = true;
    unreal::UUID capturedId;

    uint64_t obsId = mc.AddObserver(NC_SPEED_CHANGED, [&](int, Message* msg) {
        if (!msg) return;
        auto* payload = dynamic_cast<SpeedChangedPayload*>(msg->obj);
        if (payload && payload->emulatorId == _context->emulatorId)
        {
            capturedId = payload->emulatorId;
            capturedMultiplier = payload->multiplier;
            capturedTurbo = payload->turboMode;
            receivedCount.fetch_add(1);
        }
    });

    _emulator->SetSpeedMultiplier(2);

    EXPECT_TRUE(WaitForCondition([&] { return receivedCount.load() >= 1; }));
    EXPECT_EQ(receivedCount.load(), 1);
    EXPECT_EQ(capturedId, _context->emulatorId);
    EXPECT_EQ(capturedMultiplier, 2);
    EXPECT_FALSE(capturedTurbo);

    mc.RemoveObserverById(NC_SPEED_CHANGED, obsId);
}

TEST_F(TurboMode_Test, TurboModePostsNotification)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    std::atomic<int> receivedCount{0};
    bool capturedTurbo = false;

    uint64_t obsId = mc.AddObserver(NC_SPEED_CHANGED, [&](int, Message* msg) {
        if (!msg) return;
        auto* payload = dynamic_cast<SpeedChangedPayload*>(msg->obj);
        if (payload && payload->emulatorId == _context->emulatorId)
        {
            capturedTurbo = payload->turboMode;
            receivedCount.fetch_add(1);
        }
    });

    // Enable turbo
    _emulator->EnableTurboMode();

    EXPECT_TRUE(WaitForCondition([&] { return receivedCount.load() >= 1; }));
    EXPECT_EQ(receivedCount.load(), 1);
    EXPECT_TRUE(capturedTurbo);

    // Disable turbo
    _emulator->DisableTurboMode();

    EXPECT_TRUE(WaitForCondition([&] { return receivedCount.load() >= 2; }));
    EXPECT_EQ(receivedCount.load(), 2);
    EXPECT_FALSE(capturedTurbo);

    mc.RemoveObserverById(NC_SPEED_CHANGED, obsId);
}

// ---------------------------------------------------------------------------
// Render Decimation Cadence & Fidelity
// ---------------------------------------------------------------------------

TEST_F(TurboMode_Test, TurboSkipsRenderingBetweenDecimationBoundaries)
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

TEST_F(TurboMode_Test, RenderedTurboFrameMatchesNormalMode)
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

TEST_F(TurboMode_Test, TurboDisengageRestoresPerFrameRendering)
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

// ---------------------------------------------------------------------------
// Turbo mode forces low-quality sound DSP without touching the soundhq feature
// ---------------------------------------------------------------------------

TEST_F(TurboMode_Test, TurboOverridesSoundHQAndRestoresPreviousState)
{
    ASSERT_NE(_context->pSoundManager, nullptr);
    ASSERT_NE(_context->pFeatureManager, nullptr);
    FeatureManager& features = *_context->pFeatureManager;
    SoundManager& sound = *_context->pSoundManager;

    // HQ on before turbo: turbo forces LQ, the feature stays on, and LQ ends with turbo
    features.setFeature(Features::kSoundHQ, true);
    ASSERT_TRUE(sound.isHQActive());

    _emulator->EnableTurboMode();
    EXPECT_FALSE(sound.isHQActive()) << "Turbo must run the low-quality DSP path";
    EXPECT_TRUE(features.isEnabled(Features::kSoundHQ)) << "The user's soundhq setting must be untouched";

    // A feature change while in turbo is remembered but still overridden
    features.setFeature(Features::kSoundHQ, false);
    EXPECT_FALSE(sound.isHQActive());
    features.setFeature(Features::kSoundHQ, true);
    EXPECT_FALSE(sound.isHQActive());

    _emulator->DisableTurboMode();
    EXPECT_TRUE(sound.isHQActive()) << "Leaving turbo restores the previous (HQ) state";

    // HQ off before turbo: stays off after turbo
    features.setFeature(Features::kSoundHQ, false);
    _emulator->EnableTurboMode();
    EXPECT_FALSE(sound.isHQActive());
    _emulator->DisableTurboMode();
    EXPECT_FALSE(sound.isHQActive()) << "Leaving turbo must not enable HQ the user had off";
    EXPECT_FALSE(features.isEnabled(Features::kSoundHQ));
}

// ---------------------------------------------------------------------------
// Turbo mode (audio not requested) suppresses every sound synthesis path
// ---------------------------------------------------------------------------

TEST_F(TurboMode_Test, TurboSuppressesSoundSynthesisAndResumesAfter)
{
    ASSERT_NE(_context->pSoundManager, nullptr);
    SoundManager& sound = *_context->pSoundManager;

    // Normal speed: synthesis active
    RunFramesAndCollectHashes(1);
    EXPECT_FALSE(sound.isSynthesisSuppressed());
    EXPECT_FALSE(sound.getBeeper().isSynthesisSuppressed());

    // Turbo without audio: decided at the next frame start, propagated to the devices
    _emulator->EnableTurboMode(false);
    RunFramesAndCollectHashes(1);
    EXPECT_TRUE(sound.isSynthesisSuppressed());
    EXPECT_TRUE(sound.getBeeper().isSynthesisSuppressed());
    if (sound.getCovox())
        EXPECT_TRUE(sound.getCovox()->isSynthesisSuppressed());

    // Turbo with audio requested (recording use case): synthesis stays on
    _emulator->DisableTurboMode();
    _emulator->EnableTurboMode(true);
    RunFramesAndCollectHashes(1);
    EXPECT_FALSE(sound.isSynthesisSuppressed());

    // Back to normal speed: synthesis resumes
    _emulator->DisableTurboMode();
    RunFramesAndCollectHashes(1);
    EXPECT_FALSE(sound.isSynthesisSuppressed());
    EXPECT_FALSE(sound.getBeeper().isSynthesisSuppressed());
}

// ---------------------------------------------------------------------------
// Adaptive decimation keeps the rendered cadence within [target, 2 x target)
// ---------------------------------------------------------------------------

TEST(TurboMode, AdaptiveDecimationTracksMeasuredRate)
{
    // 5008 fps on a 50.08 Hz machine: render every 100th frame -> 50.08 rendered fps
    EXPECT_EQ(MainLoop::ComputeTurboRenderDecimation(5008.0, 50.08), 100u);
    // Pentagon at 1000 fps: floor(1000 / 48.83) = 20 -> 50.0 rendered fps
    EXPECT_EQ(MainLoop::ComputeTurboRenderDecimation(1000.0, 48.83), 20u);
    // Rendered rate stays below 2 x target for any measured rate above target
    for (double fps = 48.83; fps < 20000.0; fps *= 1.37)
    {
        const uint64_t n = MainLoop::ComputeTurboRenderDecimation(fps, 48.83);
        const double rendered = fps / static_cast<double>(n);
        EXPECT_GE(rendered, 48.83 - 1e-9) << "fps=" << fps;
        EXPECT_LT(rendered, 2 * 48.83) << "fps=" << fps;
    }
    // Slower than real time: render every frame
    EXPECT_EQ(MainLoop::ComputeTurboRenderDecimation(30.0, 48.83), 1u);
    // No measurement yet: fixed default
    EXPECT_EQ(MainLoop::ComputeTurboRenderDecimation(0.0, 48.83), MainLoop::TURBO_RENDER_DECIMATION);
    // Absurd rates clamp
    EXPECT_EQ(MainLoop::ComputeTurboRenderDecimation(1e9, 48.83), MainLoop::TURBO_RENDER_DECIMATION_MAX);
}

// With a display bound the rendered rate tracks the panel: <= refresh rate, > half of it
TEST(TurboMode, DisplayBoundKeepsRenderedRateAtPanelRate)
{
    // 5008 fps on a 120 Hz ProMotion panel: every 42nd frame -> 119.2 rendered fps
    EXPECT_EQ(MainLoop::ComputeTurboRenderDecimation(5008.0, 50.08, 120.0), 42u);
    // Same on a 60 Hz panel: every 84th frame -> 59.6 rendered fps
    EXPECT_EQ(MainLoop::ComputeTurboRenderDecimation(5008.0, 50.08, 60.0), 84u);
    // Mild turbo below the panel rate: render every frame
    EXPECT_EQ(MainLoop::ComputeTurboRenderDecimation(100.0, 48.83, 120.0), 1u);
    // A bound at or below target falls back to the [target, 2 x target) rule
    EXPECT_EQ(MainLoop::ComputeTurboRenderDecimation(1000.0, 48.83, 48.0), 20u);
    for (double fps = 130.0; fps < 20000.0; fps *= 1.31)
    {
        const uint64_t n = MainLoop::ComputeTurboRenderDecimation(fps, 48.83, 120.0);
        const double rendered = fps / static_cast<double>(n);
        EXPECT_LE(rendered, 120.0 + 1e-9) << "fps=" << fps;
        EXPECT_GT(rendered, 60.0) << "fps=" << fps;
    }
}
