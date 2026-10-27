// The TS-Conf screen drawn on its events (ScreenTSConf::CatchesUpOnEvents) against the screen drawn after every instruction: the
// frame must be the same pixel for pixel. Two machines run the same program, one in each mode, and every frame is compared.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/tsconf/screentsconf.h"

namespace
{
/// Frames of `program` (a file of testdata/machines/tsconf, empty: the machine as it boots) on both machines; false with the first
/// frame that differs
void CompareFrames(const std::string& program, int frames, const std::function<void(Emulator&)>& setup = {})
{
    Emulator* onEvents = EmulatorTestHelper::CreateStandardEmulator("TSL", LoggerLevel::LogError, RamPowerOn::Zero);
    Emulator* everyStep = EmulatorTestHelper::CreateStandardEmulator("TSL", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(onEvents, nullptr);
    ASSERT_NE(everyStep, nullptr);
    auto* eventScreen = dynamic_cast<ScreenTSConf*>(onEvents->GetContext()->pScreen);
    auto* stepScreen = dynamic_cast<ScreenTSConf*>(everyStep->GetContext()->pScreen);
    ASSERT_NE(eventScreen, nullptr);
    ASSERT_NE(stepScreen, nullptr);
    ASSERT_TRUE(eventScreen->CatchesUpOnEvents()) << "the default";
    stepScreen->SetCatchUpOnEvents(false);
    // The test machines draw a frame in one batch at its end (ScreenHQ off); the picture drawn along the beam is what is compared
    for (Emulator* emulator : {onEvents, everyStep})
        emulator->GetContext()->pFeatureManager->setFeature(Features::kScreenHQ, true);
    ASSERT_TRUE(eventScreen->IsScreenHQEnabled());
    ASSERT_TRUE(stepScreen->IsScreenHQEnabled());

    if (!program.empty())
    {
        const std::string path = (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "tsconf" / program).string();
        ASSERT_TRUE(onEvents->LoadSnapshot(path)) << path;
        ASSERT_TRUE(everyStep->LoadSnapshot(path)) << path;
    }
    if (setup)
    {
        // the firmware first: it sets the TS video mode up (a machine that has not run draws nothing)
        for (int frame = 0; frame < 20; frame++)
        {
            onEvents->RunFrame(true);
            everyStep->RunFrame(true);
        }
        setup(*onEvents);
        setup(*everyStep);
    }
    for (int frame = 0; frame < frames; frame++)
    {
        onEvents->RunFrame(true);
        everyStep->RunFrame(true);
        const FramebufferDescriptor& a = eventScreen->GetFramebufferDescriptor();
        const FramebufferDescriptor& b = stepScreen->GetFramebufferDescriptor();
        ASSERT_EQ(a.memoryBufferSize, b.memoryBufferSize);
        if (std::memcmp(a.memoryBuffer, b.memoryBuffer, a.memoryBufferSize) != 0)
        {
            size_t first = 0;
            while (first < a.memoryBufferSize && a.memoryBuffer[first] == b.memoryBuffer[first])
                first++;
            const size_t pixel = first / 4;
            FAIL() << program << ": frame " << frame << " differs first at pixel x " << pixel % a.width << " y " << pixel / a.width;
        }
    }
    EmulatorTestHelper::CleanupEmulator(onEvents);
    EmulatorTestHelper::CleanupEmulator(everyStep);
}

}  // namespace

// Machine-bound: two TS-Conf machines run whole frames with the picture drawn along the beam (a few ms each), so these tests are
// longer than 50 ms; the frame counts are the smallest that reach the pictures (the sprite loop; the first slide, ~60 frames; the
// firmware's first screen, ~60 frames). Each was also run for 300 frames by hand when the mode was introduced (2026-10-13)
TEST(TsConfScreenEvents_Test, SpritesOverTilesMatchThePerInstructionDraw)
{
    CompareFrames("spg/sprites.spg", 25);
}

TEST(TsConfScreenEvents_Test, SlideShowWithItsUnpackingMatchesThePerInstructionDraw)
{
    CompareFrames("spg/slideshow.spg", 70);
}

TEST(TsConfScreenEvents_Test, TheBootMatchesThePerInstructionDraw)
{
    CompareFrames("", 70);
}

// The DMA copies into the page on the screen while the beam draws it, started again and again at a moving place in the frame by a
// loop that writes no memory: what the beam passed before a DMA write shows the old bytes (the catch-up before the DMA moves anything,
// TsConfEngine::SetVideoCatchUp). The source is random bytes, a different stretch for each copy
TEST(TsConfScreenEvents_Test, DmaIntoTheShownPageMatchesThePerInstructionDraw)
{
    CompareFrames("", 12, [](Emulator& emulator) {
        EmulatorContext* context = emulator.GetContext();
        Memory& memory = *context->pMemory;
        uint32_t seed = 0x0D3A51ABu;
        for (size_t i = 0; i < 2 * 0x4000; i++)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            memory.RAMBase()[0x20 * 0x4000 + i] = static_cast<uint8_t>(seed);
        }
        std::vector<uint8_t> code = {0xF3, 0x0E, 0xAF};  // di : ld c,#AF
        auto reg = [&code](uint8_t number, uint8_t value) {
            code.insert(code.end(), {0x06, number, 0x3E, value, 0xED, 0x79});  // ld b,number : ld a,value : out (c),a
        };
        reg(0x00, 0x00);  // V_CONFIG: the ZX screen, 256 x 192
        reg(0x01, 0x05);  // V_PAGE 5
        const size_t loop = code.size();
        reg(0x1A, 0x00);  // DMA source: page #20, the high byte moves with D
        code.insert(code.end(), {0x06, 0x1B, 0x7A, 0xE6, 0x3F, 0xED, 0x79});  // ld b,#1B : ld a,d : and #3F : out (c),a
        reg(0x1C, 0x20);
        reg(0x1D, 0x00);  // destination: page 5, the bitmap
        reg(0x1E, 0x00);
        reg(0x1F, 0x05);
        reg(0x26, 0xFF);  // 256 words a burst
        reg(0x28, 0x0D);  // 14 bursts: 7168 bytes, the bitmap and the attributes
        reg(0x27, 0x01);  // RAM to RAM, go
        code.push_back(0x14);  // inc d
        code.insert(code.end(), {0x21, 0x35, 0x07});  // ld hl,#0735: a wait that writes nothing, not a whole number of frames
        const size_t wait = code.size();
        code.insert(code.end(), {0x2B, 0x7C, 0xB5});  // dec hl : ld a,h : or l
        code.insert(code.end(), {0x20, static_cast<uint8_t>(wait - (code.size() + 2))});  // jr nz,wait
        code.insert(code.end(), {0x18, static_cast<uint8_t>(loop - (code.size() + 2))});  // jr loop
        for (size_t i = 0; i < code.size(); i++)
            memory.DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
        Z80& z80 = *context->pCore->GetZ80();
        z80.pc = 0x8000;
        z80.sp = 0xBFF0;
        z80.iff1 = z80.iff2 = 0;
        z80.halted = 0;
    });
}
