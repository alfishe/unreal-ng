/// @file timetravelmanager_display_test.cpp
/// @brief What a TTD position shows (display rule,
/// docs/inprogress/2026-09-28-ttd-positioning-and-display/design.md §3).
///
/// - Positioning by frame number shows the frame's final picture.
/// - Positioning at a T-state inside a frame shows what the beam drew from
///   the frame's start up to that point, over the previous frame's picture.
///
/// Both are checked against the live machine: the framebuffer a TTD position
/// shows must equal the framebuffer the live run had at the same point, pixel
/// for pixel. The guest program changes the border every 34 T-states, so every
/// frame has its own stripe pattern and a static memory decode (one border
/// color) can never pass.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

class TTD_Display_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;
    Screen* _screen = nullptr;
    Z80* _z80 = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        _memory = _context->pMemory;
        _screen = _context->pScreen;
        _z80 = _context->pCore->GetZ80();
        ASSERT_NE(_ttd, nullptr);
        ASSERT_NE(_screen, nullptr);
        ASSERT_NE(_z80, nullptr);

        FeatureManager* fm = _emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        fm->setFeature(Features::kScreenHQ, true);  // per-T rendering: border stripes exist at all
        _memory->UpdateFeatureCache();

        // Border stripe generator at 0x8000:
        //   DI; LD A,0; loop: OUT (#FE),A; INC A; AND 7; JR loop
        const uint8_t program[] = {0xF3, 0x3E, 0x00, 0xD3, 0xFE, 0x3C, 0xE6, 0x07, 0x18, 0xF9};
        for (size_t i = 0; i < sizeof(program); ++i)
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        _z80->pc = 0x8000;
        _z80->sp = 0xBFF0;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    std::vector<uint32_t> Framebuffer()
    {
        uint32_t* fb = nullptr;
        size_t size = 0;
        _screen->GetFramebufferData(&fb, &size);
        return std::vector<uint32_t>(fb, fb + size / sizeof(uint32_t));
    }

    /// Distinct colors in the top border row — 1 for a static decode.
    static size_t TopRowColors(const std::vector<uint32_t>& fb, size_t width)
    {
        std::map<uint32_t, int> colors;
        for (size_t x = 0; x < width; ++x)
            colors[fb[x]]++;
        return colors.size();
    }

    size_t Width() { return _screen->GetFramebufferDescriptor().width; }

    /// Record `frames` frames; returns the live final picture of each frame,
    /// keyed by frame number.
    std::map<uint64_t, std::vector<uint32_t>> RecordFinalPictures(unsigned frames)
    {
        std::map<uint64_t, std::vector<uint32_t>> pictures;
        EXPECT_TRUE(_ttd->StartRecording());
        for (unsigned i = 0; i < frames; ++i)
        {
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            pictures[_context->emulatorState.frame_counter - 1] = Framebuffer();
        }
        _ttd->StopRecording();
        return pictures;
    }
};

TEST_F(TTD_Display_Test, SeekByFrame_ShowsFramesFinalPicture)
{
    const auto live = RecordFinalPictures(8);

    for (uint64_t f = live.begin()->first + 1; f < live.rbegin()->first; ++f)
    {
        ASSERT_TRUE(_ttd->SeekTo({f, 0}));
        const auto shown = Framebuffer();
        EXPECT_GT(TopRowColors(shown, Width()), 1u) << "static decode shown for frame " << f;
        EXPECT_EQ(shown, live.at(f)) << "frame " << f << " is not the live final picture";
    }
}

TEST_F(TTD_Display_Test, FrameSteps_LandOnBoundaryAndShowFinalPicture)
{
    const auto live = RecordFinalPictures(10);
    const uint64_t first = live.begin()->first + 1;
    const uint64_t last = live.rbegin()->first;

    ASSERT_TRUE(_ttd->SeekTo({first, 0}));
    for (uint64_t f = first + 1; f <= last; ++f)
    {
        ASSERT_TRUE(_ttd->StepForwardFrame());
        EXPECT_EQ(_ttd->CurrentPosition().frame, f);
        EXPECT_LT(_z80->t, 32u) << "frame step drifted into the frame (t=" << _z80->t << ")";
        EXPECT_EQ(Framebuffer(), live.at(f)) << "step forward to frame " << f;
    }
    for (uint64_t f = last - 1; f >= first; --f)
    {
        ASSERT_TRUE(_ttd->StepBackFrame());
        EXPECT_EQ(_ttd->CurrentPosition().frame, f);
        EXPECT_LT(_z80->t, 32u);
        EXPECT_EQ(Framebuffer(), live.at(f)) << "step back to frame " << f;
    }
}

TEST_F(TTD_Display_Test, SeekInsideFrame_ShowsBeamUpToThatPoint)
{
    // Live: pause at several points inside frames and keep what the live
    // machine shows there (beam flushed to the current T-state).
    struct Probe
    {
        ttd::TTDTimePoint at;
        std::vector<uint32_t> picture;
    };
    std::vector<Probe> probes;

    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, true);
    for (uint32_t t : {9000u, 30000u, 50000u})
    {
        _emulator->RunTStates(t, true);
        _screen->UpdateScreen();
        probes.push_back({{_context->emulatorState.frame_counter, static_cast<uint32_t>(_z80->t)}, Framebuffer()});
        _emulator->RunNFrames(1, true);
    }
    _emulator->RunNFrames(1, true);
    _ttd->StopRecording();

    for (const auto& p : probes)
    {
        ASSERT_TRUE(_ttd->SeekTo(p.at));
        EXPECT_EQ(_z80->t, p.at.tInFrame);
        EXPECT_EQ(Framebuffer(), p.picture)
            << "position (" << p.at.frame << ", " << p.at.tInFrame << ") differs from the live machine";
    }
}

TEST_F(TTD_Display_Test, PathIndependence_StepsEqualDirectSeek)
{
    const auto live = RecordFinalPictures(8);
    const uint64_t first = live.begin()->first + 1;

    ASSERT_TRUE(_ttd->SeekTo({first, 0}));
    ASSERT_TRUE(_ttd->StepForwardFrame());
    ASSERT_TRUE(_ttd->StepForwardFrame());
    ASSERT_TRUE(_ttd->StepBackFrame());
    const auto viaSteps = Framebuffer();
    const auto posViaSteps = _ttd->CurrentPosition();

    ASSERT_TRUE(_ttd->SeekTo({first + 1, 0}));
    EXPECT_EQ(Framebuffer(), viaSteps);
    EXPECT_EQ(_ttd->CurrentPosition().frame, posViaSteps.frame);
    EXPECT_EQ(_ttd->CurrentPosition().tInFrame, posViaSteps.tInFrame);
}

TEST_F(TTD_Display_Test, FrameCacheBuild_DoesNotChangeDisplay)
{
    const auto live = RecordFinalPictures(8);
    const uint64_t f = live.begin()->first + 3;

    ASSERT_TRUE(_ttd->SeekTo({f, 0}));
    const auto before = Framebuffer();
    const auto posBefore = _ttd->CurrentPosition();

    ASSERT_NE(_ttd->GetFrameCache(f - 2), nullptr);

    EXPECT_EQ(Framebuffer(), before) << "a search/cache build repainted the display";
    EXPECT_EQ(_ttd->CurrentPosition().frame, posBefore.frame);
    EXPECT_EQ(_ttd->CurrentPosition().tInFrame, posBefore.tInFrame);
}

TEST_F(TTD_Display_Test, SeekByFrame_PlaneBMatchesLive)
{
    _emulator->GetFeatureManager()->setFeature(Features::kZXDLSS, true);
    ASSERT_TRUE(_screen->IsPlaneBEnabled());

    auto planeB = [this]() {
        size_t count = 0;
        const uint16_t* p = _screen->GetPlaneB(&count);
        return std::vector<uint16_t>(p, p + count);
    };
    std::map<uint64_t, std::vector<uint16_t>> live;
    ASSERT_TRUE(_ttd->StartRecording());
    for (int i = 0; i < 8; ++i)
    {
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        live[_context->emulatorState.frame_counter - 1] = planeB();
    }
    _ttd->StopRecording();

    for (uint64_t f = live.begin()->first + 1; f < live.rbegin()->first; ++f)
    {
        ASSERT_TRUE(_ttd->SeekTo({f, 0}));
        const auto shown = planeB();
        std::set<uint16_t> borderColors;
        for (uint16_t v : shown)
            if ((v & Screen::kPlaneBRoleMask) == Screen::kPlaneBRoleBorder)
                borderColors.insert((v >> 8) & 0xF);
        EXPECT_GT(borderColors.size(), 1u) << "frame " << f << ": plane B lost the border stripes";
        EXPECT_EQ(shown, live.at(f)) << "frame " << f << ": plane B differs from the live frame";
    }
}

TEST_F(TTD_Display_Test, SeekByFrame_IndependentOfPreviousPixels)
{
    const auto live = RecordFinalPictures(8);
    const uint64_t f = live.begin()->first + 3;

    ASSERT_TRUE(_ttd->SeekTo({f, 0}));
    const auto base = Framebuffer();
    _screen->FillBorderWithColor(3);
    _screen->SetActiveScreen(SCREEN_SHADOW);
    _screen->RenderOnlyMainScreen();

    ASSERT_TRUE(_ttd->SeekTo({f, 0}));
    EXPECT_EQ(Framebuffer(), base) << "leftover pixels leaked into the composed picture";
}

TEST_F(TTD_Display_Test, Seek_MapsC000PageFromRestoredLatch)
{
    PortDecoder* pd = _context->pPortDecoder;
    ASSERT_NE(pd, nullptr);

    ASSERT_TRUE(_ttd->StartRecording());
    pd->DecodePortOut(0x7FFD, 0x01, 0x0000);  // RAM page 1 at #C000
    _emulator->RunNFrames(2, true);
    const uint64_t framePage1 = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(1, true);
    pd->DecodePortOut(0x7FFD, 0x06, 0x0000);  // RAM page 6 at #C000
    _emulator->RunNFrames(3, true);
    _ttd->StopRecording();

    // The live machine has page 6 mapped; the checkpoint says page 1
    ASSERT_TRUE(_ttd->SeekTo({framePage1, 0}));
    EXPECT_EQ(_context->emulatorState.p7FFD & 0x07, 1);
    EXPECT_EQ(_memory->MapZ80AddressToPhysicalAddress(0xC000), _memory->RAMPageAddress(1))
        << "#C000 kept the previously mapped page instead of the restored #7FFD latch";
}
