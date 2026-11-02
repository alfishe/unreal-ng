/// @file ttdtimeline_screen_test.cpp
/// @brief The timeline's `screen` tag follows the picture's memory captured at each boundary
/// (Screen::DescribeDisplayedMemory), byte exact: a write to RAM page 4 is no screen write in the ZX mode of a Pentagon
/// and one in its 16-colour mode (the 1024K board, which decodes #EFF7), which reads pages 4 and 5; a 48K system
/// variable next to the attributes (FRAMES, #5C78) is no screen write; a device region (the Sprinter's video RAM) is
/// named by its region.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

class TTDTimelineScreen_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    bool Make(const char* model, unsigned ramKb = 0)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Random,
                                                               [ramKb](CONFIG& c) {
                                                                   if (ramKb)
                                                                       c.ramsize = ramKb;
                                                               });
        if (!_emulator)
            return false;
        _context = _emulator->GetContext();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _ttd = _context->pTimeTravelController;
        return _ttd != nullptr;
    }

    /// Per checkpoint: the frame, the layout's byte ranges, the 4 KB pieces it changed
    std::string Diagnostics() const
    {
        std::string out = "mode " + std::to_string(_context->pScreen->GetVideoMode()) + "\n";
        const ttd::TimeTravelEngine& engine = _ttd->GetEngine();
        for (size_t i = 0; i < engine.CheckpointCount(); ++i)
            if (const ttd::TTDEngineCheckpoint* cp = engine.Checkpoint(i))
            {
                out += "cp " + std::to_string(i) + " frame " + std::to_string(cp->position.frame) + " layout";
                if (const auto* l = engine.ScreenLayoutOf(*cp))
                    for (const ttd::TTDScreenRange& r : *l)
                        out += " " + std::to_string(r.region) + ":" + std::to_string(r.begin) + "-" +
                               std::to_string(r.end);
                out += " changed";
                for (const auto& c : engine.ChangesAt(i, 0))
                    out += " " + std::to_string(c.first);
                out += "\n";
            }
        return out;
    }
    std::string _diagnostics;

    /// Frames of the recording the timeline marks `screen`, after @p code placed at #8000 ran 6 frames
    uint64_t ScreenFramesAfter(const std::vector<uint8_t>& code)
    {
        for (size_t i = 0; i < code.size(); ++i)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
        _context->pCore->GetZ80()->pc = 0x8000;
        EXPECT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(6, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ttd::TTDTimelineOptions o;
        o.ports = false;
        o.minPhaseMs = 20;
        const ttd::TTDTimelineResult r = _ttd->QueryTimeline(o);
        EXPECT_TRUE(r.error.empty()) << r.error;
        for (const ttd::TTDTimelineMissing& m : r.missing)
            EXPECT_NE(m.fact, "screen") << m.reason;
        uint64_t screen = 0;
        for (const ttd::TTDTimelinePhase& p : r.phases)
            screen += p.facts.screenFrames;
        _diagnostics = Diagnostics();
        return screen;
    }

    /// A program that sets 7FFD to page 4 at #C000, #EFF7 to @p eff7, writes one byte of page 4 and then spins with
    /// interrupts off
    uint64_t ScreenFramesAfterAPage4Write(uint8_t eff7)
    {
        return ScreenFramesAfter({
            0xF3,                    // di
            0x01, 0xF7, 0xEF,        // ld bc,#EFF7
            0x3E, eff7,              // ld a,eff7
            0xED, 0x79,              // out (c),a
            0x01, 0xFD, 0x7F,        // ld bc,#7FFD
            0x3E, 0x14,              // ld a,#14: page 4, the 48K ROM
            0xED, 0x79,              // out (c),a
            0x21, 0x00, 0xC0,        // ld hl,#C000
            0x34,                    // inc (hl)
            0x18, 0xFE});            // jr $
    }

    /// A program that increments the byte at @p address over and over with interrupts off
    uint64_t ScreenFramesAfterALoopWriting(uint16_t address)
    {
        return ScreenFramesAfter({
            0xF3,                                                              // di
            0x21, static_cast<uint8_t>(address), static_cast<uint8_t>(address >> 8),   // ld hl,address
            0x34,                                                              // inc (hl)
            0x18, 0xFD});                                                      // jr inc
    }
};

TEST_F(TTDTimelineScreen_Test, APage4WriteIsNoScreenWriteInTheZxMode)
{
    ASSERT_TRUE(Make("PENTAGON", 1024));   // the 1024K board decodes #EFF7
    EXPECT_EQ(ScreenFramesAfterAPage4Write(0x00), 0u) << _diagnostics;
}

TEST_F(TTDTimelineScreen_Test, APage4WriteIsAScreenWriteInTheSixteenColourMode)
{
    ASSERT_TRUE(Make("PENTAGON", 1024));
    EXPECT_EQ(ScreenFramesAfterAPage4Write(EFF7_4BPP), 1u) << _diagnostics;
}

TEST_F(TTDTimelineScreen_Test, TheSprinterLayoutNamesItsVideoRam)
{
    ASSERT_TRUE(Make("SPRINTER"));
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    _ttd->StopRecording();
    const ttd::TimeTravelEngine& engine = _ttd->GetEngine();
    ASSERT_GT(engine.CheckpointCount(), 0u);
    const ttd::TTDEngineCheckpoint* cp = engine.Checkpoint(engine.CheckpointCount() - 1);
    ASSERT_NE(cp, nullptr);
    const std::vector<ttd::TTDScreenRange>* layout = engine.ScreenLayoutOf(*cp);
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->size(), 1u);
    EXPECT_EQ(engine.Regions()[layout->front().region].name, "sprinter.vram");
    EXPECT_EQ(layout->front().begin, 0u);
    EXPECT_EQ(layout->front().end, 0x40000u);
}

TEST_F(TTDTimelineScreen_Test, AFortyEightKSystemVariableIsNoScreenWrite)
{
    ASSERT_TRUE(Make("48K"));
    EXPECT_EQ(ScreenFramesAfterALoopWriting(0x5C78), 0u)
        << "FRAMES shares a 4 KB piece with the attributes, not a byte\n"
        << _diagnostics;
}

TEST_F(TTDTimelineScreen_Test, AFortyEightKPixelWriteIsAScreenWriteEveryFrame)
{
    ASSERT_TRUE(Make("48K"));
    EXPECT_GE(ScreenFramesAfterALoopWriting(0x4000), 5u) << _diagnostics;
}
