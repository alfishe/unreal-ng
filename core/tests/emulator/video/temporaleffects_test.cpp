#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "emulator/video/temporaleffects.h"
#include "emulator/video/zxdlss/algorithm.h"

/// Temporal effects (ZX DLSS in the core): the worker's output is the
/// algorithm's own output for the right frame, gaps restart it, and on a live
/// machine the present queue and the audio take the algorithm's delay.

namespace
{

constexpr int W = 352;
constexpr int H = 288;

/// A ZX palette as the emulator hands it (RGBA8888, 0xAABBGGRR): any 16 colors
/// will do - the algorithm must mix in whatever it is given
constexpr uint32_t kPalette[16] = {0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700,
                                   0xFF2AC8CC, 0xFFCACACA, 0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF,
                                   0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF};

/// A plane B frame (color index bits 8..11, ink bit 12, attribute bits 0..7):
/// border color 1, and a paper box whose ink alternates between two colors
/// every frame (GigaScreen-style flicker) over a static paper color.
std::vector<uint16_t> FlickerFrame(uint64_t n)
{
    std::vector<uint16_t> frame(static_cast<size_t>(W) * H, static_cast<uint16_t>(1 << 8));
    for (int y = 48; y < 48 + 192; ++y)
        for (int x = 48; x < 48 + 256; ++x)
        {
            const bool box = x >= 100 && x < 200 && y >= 90 && y < 150;
            const bool ink = box && ((x ^ y) & 1);
            const uint8_t inkColor = (n & 1) ? 2 : 6;
            const uint8_t attr = static_cast<uint8_t>((7 << 3) | inkColor);
            const uint8_t color = ink ? inkColor : 7;
            frame[static_cast<size_t>(y) * W + x] = static_cast<uint16_t>(attr | (color << 8) | (ink ? 1 << 12 : 0));
        }
    return frame;
}

/// Worker outputs by serial
struct Collected
{
    std::mutex mutex;
    std::map<uint64_t, std::vector<uint8_t>> rgb;
    std::atomic<uint64_t> count{0};
};

}  // namespace

TEST(TemporalEffects_Test, UnknownAlgorithmIsRefused)
{
    TemporalEffects effects([](uint64_t, const uint8_t*, int, int, const zxdlss::FrameReport&) { return TemporalEffects::WriteResult::Written; });
    EXPECT_FALSE(effects.SetAlgorithm("no-such-algorithm"));
    EXPECT_EQ(effects.GetAlgorithm(), "");
    EXPECT_TRUE(effects.SetAlgorithm("mod-tpgwafsd"));
    EXPECT_EQ(effects.GetAlgorithm(), "mod-tpgwafsd");
}

TEST(TemporalEffects_Test, OutputIsTheAlgorithmsOutputForTheFrameDelayAgo)
{
    // The same frames through the algorithm directly: output k belongs to frame k - delay
    std::unique_ptr<zxdlss::Algorithm> direct = zxdlss::createAlgorithm("mod-tpgwafsd");
    ASSERT_NE(direct, nullptr);
    const int delay = direct->delay();
    constexpr uint64_t kFrames = 20;
    std::map<uint64_t, std::vector<uint8_t>> expected;  // by serial (1-based, like the present queue)
    std::vector<uint8_t> plane, attr, ink;
    for (uint64_t n = 0; n < kFrames; ++n)
    {
        const std::vector<uint16_t> pb = FlickerFrame(n);
        zxdlss::decodePlaneB(pb.data(), pb.size(), plane, attr, ink);
        zxdlss::FrameInput in;
        in.width = W;
        in.height = H;
        in.plane = plane.data();
        in.attr = attr.data();
        in.ink = ink.data();
        in.palette = kPalette;
        zxdlss::RGBImage out;
        direct->process(in, out);
        if (n >= static_cast<uint64_t>(delay))
            expected[n + 1 - delay] = out;
    }

    Collected got;
    TemporalEffects effects([&got](uint64_t serial, const uint8_t* rgb, int width, int height, const zxdlss::FrameReport&) {
        std::lock_guard<std::mutex> lock(got.mutex);
        got.rgb[serial].assign(rgb, rgb + static_cast<size_t>(width) * height * 3);
        got.count++;
        return TemporalEffects::WriteResult::Written;
    });
    ASSERT_TRUE(effects.SetAlgorithm("mod-tpgwafsd"));
    for (uint64_t n = 0; n < kFrames; ++n)
    {
        const std::vector<uint16_t> pb = FlickerFrame(n);
        EXPECT_EQ(effects.Submit(n + 1, pb.data(), W, H, kPalette), delay + 1);
        // one frame at a time: a worker behind by the queue limit would restart
        ASSERT_TRUE(TestWait::For([&] { return effects.GetStats().processed == n + 1; }))
            << "worker did not process frame " << n;
    }

    ASSERT_EQ(got.count.load(), kFrames - delay);
    std::lock_guard<std::mutex> lock(got.mutex);
    for (const auto& [serial, rgb] : expected)
    {
        ASSERT_TRUE(got.rgb.count(serial)) << "no output for frame serial " << serial;
        EXPECT_TRUE(got.rgb[serial] == rgb) << "output of frame serial " << serial << " differs";
    }
    const TemporalEffects::Stats stats = effects.GetStats();
    EXPECT_TRUE(stats.active);
    EXPECT_EQ(stats.videoDelayFrames, delay + 1);
    EXPECT_EQ(stats.written, kFrames - delay);
    EXPECT_EQ(stats.restarts, 0u);
    // The box flickers every frame: detections, a mask, a pattern
    EXPECT_GT(stats.correctedFrames, 0u);
    EXPECT_TRUE(zxdlss::corrected(stats.lastFrame));
    EXPECT_STRNE(zxdlss::patternName(stats.lastFrame), "none");
}

/// A frame the algorithm changed nothing in is no correction (the LED stays dark)
TEST(TemporalEffects_Test, StaticPictureIsNoCorrection)
{
    TemporalEffects effects([](uint64_t, const uint8_t*, int, int, const zxdlss::FrameReport&) {
        return TemporalEffects::WriteResult::Written;
    });
    ASSERT_TRUE(effects.SetAlgorithm("mod-tpgwafsd"));
    const std::vector<uint16_t> still = FlickerFrame(0);
    for (uint64_t n = 0; n < 12; ++n)
    {
        effects.Submit(n + 1, still.data(), W, H, kPalette);
        ASSERT_TRUE(TestWait::For([&] { return effects.GetStats().processed == n + 1; }));
    }
    const TemporalEffects::Stats stats = effects.GetStats();
    EXPECT_GT(stats.written, 0u);
    EXPECT_EQ(stats.correctedFrames, 0u) << "nothing flickers: every frame passes unchanged";
    EXPECT_TRUE(stats.lastFrame.valid);
    EXPECT_FALSE(zxdlss::corrected(stats.lastFrame));
    EXPECT_STREQ(zxdlss::patternName(stats.lastFrame), "none");
}

TEST(TemporalEffects_Test, InactiveWithoutPlaneBOrOnANonZxFrame)
{
    TemporalEffects effects([](uint64_t, const uint8_t*, int, int, const zxdlss::FrameReport&) { return TemporalEffects::WriteResult::Written; });
    ASSERT_TRUE(effects.SetAlgorithm("mod-tpgwafsd"));
    EXPECT_EQ(effects.Submit(1, nullptr, W, H, kPalette), 0);
    EXPECT_FALSE(effects.GetStats().active);
    EXPECT_NE(effects.GetStats().inactiveReason.find("plane B"), std::string::npos);

    std::vector<uint16_t> odd(512 * 240, 0);
    EXPECT_EQ(effects.Submit(2, odd.data(), 512, 240, kPalette), 0);
    EXPECT_NE(effects.GetStats().inactiveReason.find("512x240"), std::string::npos);
    EXPECT_EQ(effects.VideoDelayFrames(), 0);
}

TEST(TemporalEffects_Test, ResetRestartsTheAlgorithm)
{
    Collected got;
    TemporalEffects effects([&got](uint64_t serial, const uint8_t*, int, int, const zxdlss::FrameReport&) {
        std::lock_guard<std::mutex> lock(got.mutex);
        got.rgb[serial];
        got.count++;
        return TemporalEffects::WriteResult::Written;
    });
    ASSERT_TRUE(effects.SetAlgorithm("mod-tpgwafsd"));
    const int delay = effects.Submit(1, FlickerFrame(0).data(), W, H, kPalette) - 1;
    ASSERT_TRUE(TestWait::For([&] { return effects.GetStats().processed == 1; }));
    effects.Reset();
    EXPECT_EQ(effects.GetStats().restarts, 1u);

    // After the restart the algorithm warms up again: no output for the first delay frames
    for (uint64_t n = 1; n <= static_cast<uint64_t>(delay); ++n)
    {
        effects.Submit(n + 1, FlickerFrame(n).data(), W, H, kPalette);
        ASSERT_TRUE(TestWait::For([&] { return effects.GetStats().processed == n + 1; }));
    }
    EXPECT_EQ(got.count.load(), 0u) << "a restarted algorithm must not output before its look-ahead fills";
}

/// The algorithm mixes in the palette it is handed with each frame (the emulator's
/// live one): another palette, another picture - a grey palette mixes to grey
TEST(TemporalEffects_Test, MixesInThePaletteHandedWithTheFrame)
{
    uint32_t greyscale[16];
    for (int c = 0; c < 16; ++c)
    {
        const uint32_t v = static_cast<uint32_t>(c * 17);
        greyscale[c] = 0xFF000000u | v | (v << 8) | (v << 16);
    }
    auto run = [](const uint32_t* palette) {
        std::unique_ptr<zxdlss::Algorithm> alg = zxdlss::createAlgorithm("mod-tpgwafsd");
        std::vector<uint8_t> plane, attr, ink;
        zxdlss::RGBImage out;
        for (uint64_t n = 0; n < 10; ++n)
        {
            const std::vector<uint16_t> pb = FlickerFrame(n);
            zxdlss::decodePlaneB(pb.data(), pb.size(), plane, attr, ink);
            zxdlss::FrameInput in;
            in.width = W;
            in.height = H;
            in.plane = plane.data();
            in.attr = attr.data();
            in.ink = ink.data();
            in.palette = palette;
            alg->process(in, out);
        }
        return out;
    };
    const zxdlss::RGBImage colored = run(kPalette);
    const zxdlss::RGBImage grey = run(greyscale);
    EXPECT_FALSE(colored == grey) << "the output must follow the palette";
    for (size_t p = 0; p < grey.size(); p += 3)
        ASSERT_TRUE(grey[p] == grey[p + 1] && grey[p + 1] == grey[p + 2]) << "pixel " << p / 3 << " is not grey";
}

/// Selecting an algorithm switches on what it needs (plane B: zxdlss, per-T
/// renderer: screenhq); switching it off restores what the user had
TEST(TemporalEffects_Test, SwitchesTheFeaturesItNeedsAndRestoresThem)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    FeatureManager* features = emulator->GetContext()->pFeatureManager;
    Screen* screen = emulator->GetContext()->pScreen;

    ASSERT_TRUE(features->setFeature(Features::kZXDLSS, false));
    ASSERT_TRUE(features->setFeature(Features::kScreenHQ, false));
    ASSERT_TRUE(screen->SetTemporalAlgorithm("mod-tpgwafsd"));
    EXPECT_TRUE(features->isEnabled(Features::kZXDLSS));
    EXPECT_TRUE(features->isEnabled(Features::kScreenHQ));
    ASSERT_TRUE(screen->SetTemporalAlgorithm("mod-tpgwa"));   // another algorithm: still on
    EXPECT_TRUE(features->isEnabled(Features::kZXDLSS));
    ASSERT_TRUE(screen->SetTemporalAlgorithm(""));
    EXPECT_FALSE(features->isEnabled(Features::kZXDLSS)) << "was off before: off again";
    EXPECT_FALSE(features->isEnabled(Features::kScreenHQ)) << "was off before: off again";

    // Features the user had on stay on
    ASSERT_TRUE(features->setFeature(Features::kZXDLSS, true));
    ASSERT_TRUE(features->setFeature(Features::kScreenHQ, true));
    ASSERT_TRUE(screen->SetTemporalAlgorithm("mod-tpgwafsd"));
    ASSERT_TRUE(screen->SetTemporalAlgorithm(""));
    EXPECT_TRUE(features->isEnabled(Features::kZXDLSS));
    EXPECT_TRUE(features->isEnabled(Features::kScreenHQ));

    EXPECT_FALSE(screen->SetTemporalAlgorithm("no-such-algorithm"));
    EXPECT_EQ(screen->GetTemporalAlgorithm(), "");

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A live Pentagon: the present queue serves the processed frame delay + 1 frames
/// back, and the audio waits the frames beyond the configured A/V delay
TEST(TemporalEffects_Test, LiveMachineDelaysVideoAndAudioByTheAlgorithmsLookAhead)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kScreenHQ, true));
    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kZXDLSS, true));
    Screen* screen = context->pScreen;
    ASSERT_TRUE(screen->SetTemporalAlgorithm("mod-tpgwafsd"));
    const uint8_t base = screen->GetPresentDelayFrames();
    // Plane B switches on at the next frame start: the first frame has none (inactive)
    emulator->RunFrame();
    ASSERT_NE(screen->GetTemporalStats().inactiveReason.find("plane B"), std::string::npos);
    const uint64_t before = screen->GetTemporalStats().processed;

    // RunFrame crosses the frame boundary through MainLoop::CompleteFrame, which latches
    // the frame (one Submit per frame; a second latch here would overflow the worker)
    for (int f = 0; f < 12; ++f)
    {
        emulator->RunFrame();
        ASSERT_TRUE(TestWait::For([&] { return screen->GetTemporalStats().processed >= before + f + 1; }))
            << "frame " << f << " not processed: " << screen->GetTemporalStats().inactiveReason;
    }

    const TemporalEffects::Stats stats = screen->GetTemporalStats();
    EXPECT_TRUE(stats.active) << stats.inactiveReason;
    EXPECT_EQ(stats.videoDelayFrames, 7) << "mod-tpgwafsd: 6 frames of look-ahead + 1 for the worker";
    EXPECT_GT(stats.written, 0u) << "processed " << stats.processed << " late " << stats.late << " restarts "
                                 << stats.restarts;
    EXPECT_EQ(stats.restarts, 0u);
    EXPECT_EQ(stats.late, 0u);
    EXPECT_EQ(stats.shownRaw, 0u) << "nothing read the queue while the worker ran: no frame was shown raw";
    EXPECT_TRUE(stats.lastFrame.valid) << "the algorithm reports what it did to its frames";
    EXPECT_EQ(stats.lastFrame.pixels, 352u * 288u);

    // A reader is served the frame 7 back - processed by now (the LED's state)
    std::vector<uint8_t> shown(352 * 288 * 4);
    ASSERT_TRUE(screen->CopyPresentedFramebuffer(shown.data(), shown.size()));
    const TemporalEffects::Stats onScreen = screen->GetTemporalStats();
    EXPECT_TRUE(onScreen.showingProcessed);
    EXPECT_TRUE(onScreen.shownFrame.valid) << "the report travels with the frame's present slot";
    EXPECT_EQ(onScreen.correcting, zxdlss::corrected(onScreen.shownFrame))
        << "the LED means a detection in the frame on screen";
    EXPECT_EQ(screen->GetEffectivePresentDelayFrames(), 7);
    EXPECT_EQ(context->pSoundManager->getOutputDelayFrames(), 7 - base);

    // Off: the configured delay again, the audio catches up
    ASSERT_TRUE(screen->SetTemporalAlgorithm(""));
    emulator->RunFrame();
    EXPECT_EQ(screen->GetEffectivePresentDelayFrames(), base);
    EXPECT_EQ(context->pSoundManager->getOutputDelayFrames(), 0);

    EmulatorTestHelper::CleanupEmulator(emulator);
}
