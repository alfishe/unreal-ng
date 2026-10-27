// The classic ZX screen drawn on its events (ScreenZX::CatchesUpOnEvents) against the screen drawn after every instruction: the
// frame must be the same pixel for pixel. Two machines run the same program, one in each mode, and every frame is compared.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/zx/screenzx.h"

namespace
{
struct Run
{
    std::string model;
    std::string snapshot;  // a file under testdata, empty: the machine as it boots
    int frames = 0;
    uint8_t multiplier = 1;
    std::function<void(Emulator&)> setup = {};
    std::optional<bool> expectEvents = true;  // the event rule in effect at the start (the ZX renderer family); unset: either
};

void CompareFrames(const Run& run)
{
    Emulator* onEvents = EmulatorTestHelper::CreateStandardEmulator(run.model, LoggerLevel::LogError, RamPowerOn::Zero);
    Emulator* everyStep = EmulatorTestHelper::CreateStandardEmulator(run.model, LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(onEvents, nullptr);
    ASSERT_NE(everyStep, nullptr);
    auto* eventScreen = dynamic_cast<ScreenZX*>(onEvents->GetContext()->pScreen);
    auto* stepScreen = dynamic_cast<ScreenZX*>(everyStep->GetContext()->pScreen);
    ASSERT_NE(eventScreen, nullptr);
    ASSERT_NE(stepScreen, nullptr);
    stepScreen->SetCatchUpOnEvents(false);
    // The test machines draw a frame in one batch at its end (ScreenHQ off); the picture drawn along the beam is what is compared
    for (Emulator* emulator : {onEvents, everyStep})
        emulator->GetContext()->pFeatureManager->setFeature(Features::kScreenHQ, true);

    if (!run.snapshot.empty())
    {
        const std::string path = (TestPathHelper::FindProjectRoot() / "testdata" / run.snapshot).string();
        ASSERT_TRUE(onEvents->LoadSnapshot(path)) << path;
        ASSERT_TRUE(everyStep->LoadSnapshot(path)) << path;
    }
    if (run.setup)
    {
        run.setup(*onEvents);
        run.setup(*everyStep);
    }
    if (run.multiplier != 1)
    {
        ASSERT_TRUE(onEvents->SetSpeedMultiplier(run.multiplier));
        ASSERT_TRUE(everyStep->SetSpeedMultiplier(run.multiplier));
    }

    for (int frame = 0; frame < run.frames; frame++)
    {
        onEvents->RunFrame(true);
        everyStep->RunFrame(true);
        if (frame == 1 && run.expectEvents)
        {
            EXPECT_EQ(eventScreen->CatchesUpOnEvents(), *run.expectEvents) << run.model << " " << run.snapshot;
            EXPECT_FALSE(stepScreen->CatchesUpOnEvents());
        }
        const FramebufferDescriptor& a = eventScreen->GetFramebufferDescriptor();
        const FramebufferDescriptor& b = stepScreen->GetFramebufferDescriptor();
        ASSERT_EQ(a.memoryBufferSize, b.memoryBufferSize);
        if (std::memcmp(a.memoryBuffer, b.memoryBuffer, a.memoryBufferSize) != 0)
        {
            size_t first = 0;
            while (first < a.memoryBufferSize && a.memoryBuffer[first] == b.memoryBuffer[first])
                first++;
            const size_t pixel = first / 4;
            FAIL() << run.model << " " << run.snapshot << " x" << int(run.multiplier) << ": frame " << frame
                   << " differs first at pixel x " << pixel % a.width << " y " << pixel / a.width;
        }
    }
    EmulatorTestHelper::CleanupEmulator(onEvents);
    EmulatorTestHelper::CleanupEmulator(everyStep);
}

/// What the beam racer switches every few dozen bytes: `port` alternates between `first` and `second`
struct Switch
{
    uint16_t port = 0x7FFD;
    uint8_t first = 0x17;   // ROM 1, page 7 at #C000, screen 5
    uint8_t second = 0x1F;  // screen 7
};

/// `code` at #8000, the CPU on it with interrupts off
void Start(Emulator& emulator, const std::vector<uint8_t>& code)
{
    EmulatorContext* context = emulator.GetContext();
    for (size_t i = 0; i < code.size(); i++)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    Z80& z80 = *context->pCore->GetZ80();
    z80.pc = 0x8000;
    z80.iff1 = z80.iff2 = 0;
    z80.halted = 0;
}

/// A loop that races the beam with every input of the classic picture: a random byte (R) into the bitmap and attributes of page 5
/// (#4000) and, complemented, through #C000 into page 7 on the 128K-class machines, the same byte to the border, and every few
/// dozen bytes a port switched over (the screen at #7FFD, a video mode), optionally followed by a read of that port (the 128K
/// latches what the read of #7FFD sees). The run lengths come from R too, so the writes land at a different place in every frame.
/// `snow`: I points into contended memory (the 48K's ULA snow). `delay`: 26 T turns of a wait first. Interrupts are off
std::function<void(Emulator&)> BeamRacer(bool readLatch, bool snow, std::function<Switch(Emulator&)> switchOf = {},
                                         uint16_t delay = 0)
{
    return [readLatch, snow, switchOf, delay](Emulator& emulator) {
        const Switch sw = switchOf ? switchOf(emulator) : Switch{};
        std::vector<uint8_t> code = {
            0xF3,              // di
            0x31, 0xF0, 0xBF,  // ld sp,#BFF0
            0x21, 0x00, 0x40,  // ld hl,#4000
            0x11, 0x00, 0xC0,  // ld de,#C000
            0x0E, sw.first,    // ld c,first
            0x06, 0x20,        // ld b,#20
        };
        if (snow)
            code.insert(code.end(), {0x3E, 0x40, 0xED, 0x47});  // ld a,#40 : ld i,a
        if (delay)
            code.insert(code.end(), {
                                        0xE5,                                                            // push hl
                                        0x21, static_cast<uint8_t>(delay), static_cast<uint8_t>(delay >> 8),  // ld hl,delay
                                        0x2B, 0x7C, 0xB5, 0x20, 0xFB,  // dec hl : ld a,h : or l : jr nz,-5 (26 T a turn)
                                        0xE1,                          // pop hl
                                    });
        const size_t loop = code.size();
        code.insert(code.end(), {
                                    0xED, 0x5F,  // ld a,r
                                    0x77,        // ld (hl),a
                                    0x2F,        // cpl: page 7 the other way round, the two screens differ
                                    0x12,        // ld (de),a
                                    0xD3, 0xFE,  // out (#FE),a
                                    0x23,        // inc hl
                                    0x13,        // inc de
                                    0x7C,        // ld a,h
                                    0xFE, 0x5B,  // cp #5B
                                    0x38, 0x06,  // jr c,+6
                                    0x21, 0x00, 0x40,  // ld hl,#4000
                                    0x11, 0x00, 0xC0,  // ld de,#C000
                                });
        code.push_back(0x10);  // djnz loop
        code.push_back(static_cast<uint8_t>(loop - (code.size() + 1)));
        code.insert(code.end(), {
                                    0x79,                                            // ld a,c
                                    0xEE, static_cast<uint8_t>(sw.first ^ sw.second),  // xor: the other value
                                    0x4F,                                            // ld c,a
                                    0xC5,                                            // push bc
                                    0x01, static_cast<uint8_t>(sw.port), static_cast<uint8_t>(sw.port >> 8),  // ld bc,port
                                    0xED, 0x79,                                      // out (c),a
                                });
        if (readLatch)
            code.insert(code.end(), {0xE3, 0xE3, 0xED, 0x78});  // ex (sp),hl : ex (sp),hl (beam passes) : in a,(c)
        code.insert(code.end(), {
                                    0xC1,        // pop bc
                                    0xED, 0x5F,  // ld a,r
                                    0xE6, 0x3F,  // and #3F
                                    0x3C,        // inc a
                                    0x47,        // ld b,a
                                });
        code.push_back(0x18);  // jr loop
        code.push_back(static_cast<uint8_t>(loop - (code.size() + 1)));

        Start(emulator, code);
    };
}

/// One switch of `port` a period a little longer than a frame (the place in the frame moves ~1500 T a frame), each followed by a
/// column of the page at #C000 written with a random byte from the bottom row up, against the beam (the ATM's 16-colour mode shows
/// that page). One switch at most in a frame: the ATM gives a mode change a new frame buffer, which would drop what came before
std::function<void(Emulator&)> ModeFlipper(std::function<Switch(Emulator&)> switchOf)
{
    return [switchOf](Emulator& emulator) {
        const Switch sw = switchOf(emulator);
        std::vector<uint8_t> code = {
            0xF3,              // di
            0x31, 0xF0, 0xBF,  // ld sp,#BFF0
            0x0E, sw.first,    // ld c,first
        };
        const size_t loop = code.size();
        code.insert(code.end(), {
                                    0x79,                                              // ld a,c
                                    0xEE, static_cast<uint8_t>(sw.first ^ sw.second),  // xor: the other value
                                    0x4F,                                              // ld c,a
                                    0xC5,                                              // push bc
                                    0x01, static_cast<uint8_t>(sw.port), static_cast<uint8_t>(sw.port >> 8),  // ld bc,port
                                    0xED, 0x79,                                        // out (c),a
                                    0x21, 0x18, 0xDF,  // ld hl,#DF18: the 16-colour mode's last row (199 x 40) at #C000
                                    0x11, 0xD8, 0xFF,  // ld de,-40
                                    0x06, 0xC8,        // ld b,200
                                    0xED, 0x5F,        // ld a,r
                                    0x77,              // ld (hl),a: one byte a row
                                    0x19,              // add hl,de
                                    0x10, 0xFC,        // djnz -4
                                    0xC1,              // pop bc
                                    0x21, 0xD8, 0x09,  // ld hl,2520: 26 T a turn, the period ~71400 T
                                    0x2B,              // dec hl
                                    0x7C,              // ld a,h
                                    0xB5,              // or l
                                    0x20, 0xFB,        // jr nz,-5
                                });
        code.push_back(0x18);  // jr loop
        code.push_back(static_cast<uint8_t>(loop - (code.size() + 1)));
        Start(emulator, code);
    };
}

/// The machine boots for `frames` first (the ROM sets up its paging and the screen)
std::function<void(Emulator&)> AfterBoot(int frames, std::function<void(Emulator&)> then)
{
    return [frames, then](Emulator& emulator) {
        for (int frame = 0; frame < frames; frame++)
            emulator.RunFrame(true);
        then(emulator);
    };
}

}  // namespace

// Machine-bound: two machines run whole frames with the picture drawn along the beam (~1 ms each), so these tests are longer than
// 50 ms; the frame counts are the smallest that reach what they test (the boot's first screens; a demo's border and multicolor
// parts). Each was also run for several hundred frames by hand when the rule was introduced (2026-10-16)

TEST(ZxScreenEvents_Test, BootsMatchThePerInstructionDraw)
{
    CompareFrames({"PENTAGON", "", 60});
    CompareFrames({"48K", "", 60});
    CompareFrames({"128K", "", 80});
    CompareFrames({"PLUS3", "", 80});
    CompareFrames({"SCORPION", "", 60});
}

// Boards whose own modes are not the ZX renderer: the rule stays off while they show them, the frames match either way
TEST(ZxScreenEvents_Test, BoardsWithModesOfTheirOwnBootTheSame)
{
    CompareFrames({"PROFI", "", 60, 1, {}, std::nullopt});
    CompareFrames({"ATM710", "", 60, 1, {}, std::nullopt});
}

// The ATM switches between its ZX mode and its 16-colour mode at #FF77 in the middle of frames: the rest of a frame that left the
// ZX renderer is drawn per step, a frame that starts in the other mode has no event rule
TEST(ZxScreenEvents_Test, AModeSwitchInTheMiddleOfAFrameMatchesThePerInstructionDraw)
{
    auto atmModes = [](Emulator& emulator) {
        // Screen 7 and page 3 at #C000: the 16-colour mode shows page 3 with page 7, and no catch-up watches it (the per-step draw
        // does the rest of such a frame)
        EmulatorContext* context = emulator.GetContext();
        context->pPortDecoder->DecodePortOut(0x7FFD, 0x1B, 0);
        EXPECT_EQ(context->pMemory->GetRAMPageForBank(3), 3);
        EmulatorState& state = context->emulatorState;
        // The firmware's menu leaves black and grey in the palette: 16 different colours make every byte show
        for (uint32_t i = 0; i < 16; i++)
            state.atm.palette[i] = 0xFF000000u | (i * 0x00111111u);
        const uint8_t others = state.pFF77 & ~0x07;  // the firmware's menu is in the text mode (6)
        return Switch{static_cast<uint16_t>(state.atm.aFF77), static_cast<uint8_t>(others | 0x03), others};  // ZX (3), 16 colours (0)
    };
    CompareFrames({"ATM710", "", 16, 1, AfterBoot(60, ModeFlipper(atmModes)), std::nullopt});
}

TEST(ZxScreenEvents_Test, BorderAndMulticolorDemosMatchThePerInstructionDraw)
{
    CompareFrames({"PENTAGON", "loaders/sna/across-the-edge-second.sna", 40});
    CompareFrames({"PENTAGON", "loaders/sna/eyeache1.sna", 40});
    CompareFrames({"PENTAGON", "loaders/sna/scroller_by_demarche.sna", 40});
    CompareFrames({"PENTAGON", "loaders/szx/other/spectaculator-pentagon-crazylove.szx", 40});
    CompareFrames({"128K", "loaders/szx/other/zx-m8xxx-128k-shock.szx", 40});
}

TEST(ZxScreenEvents_Test, BeamRacingWritesBorderAndScreenSwitchesMatchThePerInstructionDraw)
{
    CompareFrames({"PENTAGON", "", 12, 1, AfterBoot(10, BeamRacer(false, false))});
    CompareFrames({"128K", "", 12, 1, AfterBoot(10, BeamRacer(false, false))});
    CompareFrames({"48K", "", 12, 1, AfterBoot(10, BeamRacer(false, false))});
}

TEST(ZxScreenEvents_Test, TheReadClockedPagingLatchMatchesThePerInstructionDraw)
{
    // The switch selects screen 5, then the read latches what the bus shows (#FF outside the paper: screen 7 and the lock, so mostly
    // once): started at eight places over the frame
    auto screen5First = [](Emulator& emulator) {
        // Screen 7 a pattern of its own everywhere: what the screen switch changes shows wherever the beam is
        uint8_t* page7 = emulator.GetContext()->pMemory->RAMPageAddress(7);
        for (size_t i = 0; i < 6912; i++)
            page7[i] = static_cast<uint8_t>(i < 6144 ? 0x55 : 0x16);
        return Switch{0x7FFD, 0x1F, 0x17};
    };
    for (uint16_t delay = 1; delay < 2700; delay += 337)
        CompareFrames({"128K", "", 2, 1, AfterBoot(10, BeamRacer(true, false, screen5First, delay))});
}

TEST(ZxScreenEvents_Test, UlaSnowMatchesThePerInstructionDraw)
{
    CompareFrames({"48K", "", 12, 1, AfterBoot(10, BeamRacer(false, true))});
}

// A faster CPU against the same beam: several steps end inside one T of the beam, and the border write's 1T compensation on the
// Pentagon can land on the step's own end
TEST(ZxScreenEvents_Test, TurboClocksMatchThePerInstructionDraw)
{
    for (uint8_t multiplier : {2, 4})
    {
        CompareFrames({"PENTAGON", "", 8, multiplier, AfterBoot(10, BeamRacer(false, false))});
        CompareFrames({"128K", "", 8, multiplier, AfterBoot(10, BeamRacer(true, false))});
        CompareFrames({"PENTAGON", "loaders/sna/across-the-edge-second.sna", 20, multiplier});
    }
}
