// The Sprinter's fast paths - a halted CPU's idle cycles in one go (Z84C15Engine::RunIdleCycles), the INT question
// answered from a kept "no" (ChainSource::IsIntAsserted) and the screen drawn on its own events instead of after every
// step (ScreenSprinter::CatchesUpOnEvents) - change nothing: the same pictures, sound, registers (R included),
// T-states and memory as one cycle per step asking every boundary and drawing after every step. Two machines run the same
// thing, one with the fast paths off, and are compared at
// every checkpoint, through every driver that allows it: RunNFrames, RunTStates chunks that end inside idle
// stretches, and MainLoop's frame (the GUI's run).
//
// Boot-bound: the BIOS start twice (a second); the dontBlink case boots DSS 1.71 from the hard disk twice and runs
// the demo (the MAME pack's system disk, UNREAL_SPRINTER_HDD; skipped without it).

#include "stdafx.h"
#include "pch.h"

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/mainloop.h>
#include <emulator/memory/memory.h>
#include <emulator/ports/models/portdecoder_sprinter.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/video/screen.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/z84c15/z84c15engine.h"
#include "emulator/sound/sprinter/covoxblaster.h"
#include "emulator/video/sprinter/screensprinter.h"
#include "sprinterzxsession.h"

namespace
{
uint64_t Fnv(const void* data, size_t size, uint64_t h = 1469598103934665603ull)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

/// The program (window 2, #8000): IM 2 with the table at #8100 (257 x #82 -> the handler at #8282, for the CTC's
/// vectors and the PLD's #FF alike), CTC 2 counting the 875 kHz TRG2 by 112, CTC 3 counting ZC/TO2 by 10 with its
/// interrupt (781 Hz: about 16 a frame), then EI : HALT : JR back. The handler counts in #8800, writes the border
/// and returns with EI : RETI. The idle fetch is the JR (#18) after the HALT
const std::vector<uint8_t> kMain = {
    0xF3,              // #8000 DI
    0x31, 0x00, 0x90,  // LD SP,#9000
    0x3E, 0x81,        // LD A,#81
    0xED, 0x47,        // LD I,A
    0xED, 0x5E,        // IM 2
    0x3E, 0x00,        // LD A,0
    0xD3, 0x10,        // OUT (#10),A    CTC vector base
    0x3E, 0x47,        // LD A,#47
    0xD3, 0x12,        // OUT (#12),A    CTC 2: counter, time constant follows, reset
    0x3E, 0x70,        // LD A,112
    0xD3, 0x12,        // OUT (#12),A
    0x3E, 0xC7,        // LD A,#C7
    0xD3, 0x13,        // OUT (#13),A    CTC 3: interrupt, counter, time constant follows, reset
    0x3E, 0x0A,        // LD A,10
    0xD3, 0x13,        // OUT (#13),A
    0xFB,              // #801E EI
    0x76,              // #801F HALT
    0x18, 0xFD,        // #8020 JR #801F
};
/// The same start, then a busy loop instead of the HALT: the INT question at every boundary of running code
const std::vector<uint8_t> kBusyTail = {
    0x21, 0x00, 0x89,  // #801F LD HL,#8900
    0x34,              // #8022 INC (HL)
    0x18, 0xFD,        // #8023 JR #8022
};
const std::vector<uint8_t> kHandler = {
    0xF5,              // #8282 PUSH AF
    0xE5,              // PUSH HL
    0x2A, 0x00, 0x88,  // LD HL,(#8800)
    0x23,              // INC HL
    0x22, 0x00, 0x88,  // LD (#8800),HL
    0x7D,              // LD A,L
    0xE6, 0x07,        // AND 7
    0xD3, 0xFE,        // OUT (#FE),A    the border
    0xE1,              // POP HL
    0xF1,              // POP AF
    0xFB,              // EI
    0xED, 0x4D,        // RETI
};
}  // namespace

class SprinterIdleCycles_Test : public ::testing::Test
{
protected:
    struct Run
    {
        std::shared_ptr<Emulator> emulator;
        EmulatorContext* context = nullptr;
        Z84C15Engine* engine = nullptr;
        std::vector<uint64_t> trace;  ///< per checkpoint: the picture, the sound frame, the CPU
    };

    EmulatorManager* _manager = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }

    void TearDown() override
    {
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    /// A Sprinter on the shipped BIOS (fast start), the fast paths on or off
    void Create(Run& run, const char* id, bool inOneGo)
    {
        run.emulator = _manager->CreateEmulatorWithModelAndRAM(id, "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(run.emulator, nullptr);
        run.context = run.emulator->GetContext();
        auto* decoder = dynamic_cast<PortDecoder_Sprinter*>(run.context->pPortDecoder);
        ASSERT_NE(decoder, nullptr);
        decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC
        run.context->config.sprinter.fast_start = 1;
        run.engine = decoder->GetCpuEngine();
        ASSERT_NE(run.engine, nullptr);
        run.engine->SetIdleCyclesInOneGo(inOneGo);
        run.engine->SetIntAnswerKept(inOneGo);
        auto* screen = dynamic_cast<ScreenSprinter*>(run.context->pScreen);
        ASSERT_NE(screen, nullptr);
        screen->SetCatchUpOnEvents(inOneGo);
    }

    /// Reset; pages 5 and 7 power up random (Memory::RandomizeMemoryContent): the same start for both machines
    static void Reset(Run& run)
    {
        run.emulator->Reset();
        std::memset(run.context->pMemory->RAMPageAddress(5), 0, PAGE_SIZE);
        std::memset(run.context->pMemory->RAMPageAddress(7), 0, PAGE_SIZE);
    }

    /// The BIOS start with no disk, then the program at #8000
    void StartProgram(Run& run, const char* id, bool inOneGo, bool busy = false)
    {
        Create(run, id, inOneGo);
        if (HasFatalFailure())
            return;
        Reset(run);
        run.emulator->EnableTurboMode();
        EmulatorTestHelper::RunFramesFast(run.emulator.get(), 200);
        // The BIOS tried the floppy: its controller sleeps about 120 frames later (it runs per step until then)
        for (int frame = 0; frame < 1000 && !run.context->pBetaDisk->IsStepInert(); frame++)
            EmulatorTestHelper::RunFramesFast(run.emulator.get(), 1);
        ASSERT_TRUE(run.context->pBetaDisk->IsStepInert());
        run.emulator->DisableTurboMode();

        Memory* memory = run.context->pMemory;
        for (size_t i = 0; i < kMain.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kMain[i]);
        if (busy)
        {
            for (size_t i = 0; i < kBusyTail.size(); i++)
                memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x801F + i), kBusyTail[i]);
        }
        for (uint16_t i = 0; i <= 0x100; i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8100 + i), 0x82);
        for (size_t i = 0; i < kHandler.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8282 + i), kHandler[i]);
        memory->DirectWriteToZ80Memory(0x8800, 0);
        memory->DirectWriteToZ80Memory(0x8801, 0);
        ASSERT_EQ(memory->DirectReadFromZ80Memory(0x8000), 0xF3) << "window 2 must hold RAM after the BIOS start";
        ASSERT_EQ(memory->DirectReadFromZ80Memory(static_cast<uint16_t>(0x8282 + kHandler.size() - 1)), 0x4D);
        run.context->pCore->GetZ80()->pc = 0x8000;
    }

    static uint64_t CpuHash(const EmulatorContext* context)
    {
        const Z80* z = context->pCore->GetZ80();
        const uint32_t words[] = {z->af, z->bc, z->de, z->hl, z->ix, z->iy, z->sp, z->pc, z->i, Z80::RegisterR(z),
                                  z->iff1, z->iff2, z->im, z->halted, z->t};
        return Fnv(words, sizeof(words), context->emulatorState.frame_counter);
    }

    static void Checkpoint(Run& run)
    {
        EmulatorContext* context = run.context;
        uint32_t* fb = nullptr;
        size_t size = 0;
        context->pScreen->GetFramebufferData(&fb, &size);
        const AudioFrameDescriptor& audio = context->pSoundManager->getAudioBufferDescriptor();
        run.trace.push_back(Fnv(fb, size));
        run.trace.push_back(Fnv(audio.memoryBuffer, sizeof(audio.memoryBuffer)));
        run.trace.push_back(CpuHash(context));
    }

    /// The same driving for both machines: whole frames, RunTStates chunks that end inside idle stretches, then
    /// MainLoop's frame (the GUI's run: the idle cycles may go up to the frame end)
    static void Drive(Run& run, int frames)
    {
        for (int frame = 0; frame < frames; frame++)
        {
            run.emulator->RunNFrames(1, true);
            Checkpoint(run);
        }
        for (int chunk = 0; chunk < frames / 2; chunk++)
        {
            run.emulator->RunTStates(9973u + 131u * static_cast<uint32_t>(chunk), true);
            Checkpoint(run);
        }
        // To the frame end, then whole frames the way MainLoop::Run executes them
        const Z80* z80 = run.context->pCore->GetZ80();
        run.emulator->RunTStates(z80->_frameLimit - z80->t, true);
        auto* mainLoop = reinterpret_cast<MainLoopCUT*>(run.context->pMainLoop);
        for (int frame = 0; frame < frames / 2; frame++)
        {
            mainLoop->RunFramePublic();
            Checkpoint(run);
        }
    }

    static void ExpectSameTrace(const Run& perStep, const Run& inOneGo)
    {
        ASSERT_EQ(inOneGo.trace.size(), perStep.trace.size());
        for (size_t i = 0; i < perStep.trace.size(); i++)
        {
            static const char* kWhat[] = {"picture", "sound", "CPU (registers, R, T, frame)"};
            ASSERT_EQ(inOneGo.trace[i], perStep.trace[i]) << kWhat[i % 3] << " differs at checkpoint " << i / 3;
        }
        // The whole RAM (4 MB) at the end
        EXPECT_EQ(Fnv(inOneGo.context->pMemory->RAMBase(), 4096u * 1024u),
                  Fnv(perStep.context->pMemory->RAMBase(), 4096u * 1024u));
    }

    static uint16_t Counter(const Run& run)
    {
        Memory* memory = run.context->pMemory;
        return static_cast<uint16_t>(memory->DirectReadFromZ80Memory(0x8800) | memory->DirectReadFromZ80Memory(0x8801) << 8);
    }
};

// At 21 MHz (the BIOS leaves the turbo on): the idle cycle's length follows the 6-clock wait phase
TEST_F(SprinterIdleCycles_Test, HaltedCtcLoop_RunsAsOneCyclePerStep)
{
    Run perStep;
    Run inOneGo;
    StartProgram(perStep, "sprinter-idle-per-step", false);
    if (HasFatalFailure())
        return;
    StartProgram(inOneGo, "sprinter-idle-in-one-go", true);
    if (HasFatalFailure())
        return;
    EXPECT_EQ(inOneGo.context->emulatorState.current_z80_frequency_multiplier, 6u) << "the turbo's waits are the case";

    const uint64_t before = inOneGo.engine->IdleCyclesRunInOneGo();
    Drive(perStep, 120);
    Drive(inOneGo, 120);

    EXPECT_GT(Counter(perStep), 2000u) << "the CTC 3 handler runs about 16 times a frame";
    EXPECT_EQ(Counter(inOneGo), Counter(perStep));
    EXPECT_EQ(perStep.engine->IdleCyclesRunInOneGo(), 0u);
    EXPECT_GT(inOneGo.engine->IdleCyclesRunInOneGo() - before, 150u * 30000u) << "about 35 000 idle cycles in each of ~185 frames";
    ExpectSameTrace(perStep, inOneGo);
}

// Running code (no HALT): the INT question at every boundary answered from the kept "no" between source events
TEST_F(SprinterIdleCycles_Test, BusyCtcLoop_RunsAsAskingEveryBoundary)
{
    Run perStep;
    Run fast;
    StartProgram(perStep, "sprinter-busy-per-step", false, true);
    if (HasFatalFailure())
        return;
    StartProgram(fast, "sprinter-busy-fast", true, true);
    if (HasFatalFailure())
        return;

    const uint64_t keptBefore = fast.engine->IntAnswersKept();
    Drive(perStep, 120);
    Drive(fast, 120);

    EXPECT_GT(Counter(perStep), 2000u) << "the CTC 3 handler runs about 16 times a frame";
    EXPECT_EQ(Counter(fast), Counter(perStep));
    EXPECT_EQ(perStep.engine->IntAnswersKept(), 0u);
    EXPECT_GT(fast.engine->IntAnswersKept() - keptBefore, 150u * 12000u) << "most of ~15 000 boundaries a frame answered from the kept no";
    ExpectSameTrace(perStep, fast);
}

// Sprinter demos from the MAME pack's system disk (C:\DEMOS), each started by SYSTEM.BAT with the shipped sound cards:
// boot and load in turbo (the CPU compared), then 200 frames at normal speed through every driver
class SprinterFastPathsDemo_Test : public SprinterIdleCycles_Test
{
protected:
    void RunDemo(const std::string& name, int loadFrames)
    {
        const char* path = std::getenv("UNREAL_SPRINTER_HDD");
        if (!path || !FileHelper::FileExists(path))
            GTEST_SKIP() << "UNREAL_SPRINTER_HDD (the raw sp_hdd_sys.img) not set";
        SoundCardScope shippedSound;

        auto start = [&](Run& run, const char* id, bool inOneGo) {
            Create(run, id, inOneGo);
            ASSERT_FALSE(HasFatalFailure());
            MediaSource source;
            source.path = path;
            InsertOptions options;
            options.immediate = true;
            options.access = AccessMode::Session;
            const auto result = run.context->pMediaManager->Insert("ide0.master", source, options);
            ASSERT_TRUE(result.Ok()) << result.message;
            // SYSTEM.BAT runs the demo (same length: the session copy is written, the image stays as it is)
            Medium* medium = run.context->pMediaManager->GetMedium("ide0.master");
            ASSERT_NE(medium, nullptr);
            FatInPlace disk(*medium->Block());
            ASSERT_TRUE(disk.Open());
            std::vector<uint8_t> bat;
            ASSERT_TRUE(disk.Read("/SYSTEM.BAT", bat));
            std::string text = "@echo off\r\ncd \\demos\\" + name + "\r\n" + name + "\r\nrem ";
            ASSERT_LT(text.size() + 2, bat.size());
            text.append(bat.size() - text.size() - 2, ' ');
            text += "\r\n";
            ASSERT_TRUE(disk.Overwrite("/SYSTEM.BAT", std::vector<uint8_t>(text.begin(), text.end())));
            Reset(run);
            run.emulator->EnableTurboMode();
        };
        Run perStep;
        Run fast;
        start(perStep, "sprinter-demo-per-step", false);
        if (HasFatalFailure())
            return;
        start(fast, "sprinter-demo-fast", true);
        if (HasFatalFailure())
            return;

        // DSS boots and the demo loads (turbo, no sound): the CPU compared every 50 frames. Not the picture: turbo
        // renders only the frames the host's clock lets it (MainLoop's render decimation), which differs between runs
        for (int block = 0; block < loadFrames / 50; block++)
        {
            EmulatorTestHelper::RunFramesFast(perStep.emulator.get(), 50);
            EmulatorTestHelper::RunFramesFast(fast.emulator.get(), 50);
            ASSERT_EQ(CpuHash(fast.context), CpuHash(perStep.context)) << "CPU differs after " << 50 * (block + 1) << " frames";
        }
        perStep.emulator->DisableTurboMode();
        fast.emulator->DisableTurboMode();
        Drive(perStep, 100);
        Drive(fast, 100);
        ExpectSameTrace(perStep, fast);

        // The demo ran: its pictures move
        std::set<uint64_t> pictures;
        for (size_t i = 0; i < fast.trace.size(); i += 3)
            pictures.insert(fast.trace[i]);
        EXPECT_GT(pictures.size(), 10u) << "the demo moves";
        _fast = fast.engine;
        _ringWrites = dynamic_cast<PortDecoder_Sprinter*>(fast.context->pPortDecoder)->GetCovoxBlaster().State().ringWrites;
    }

    Z84C15Engine* _fast = nullptr;
    uint64_t _ringWrites = 0;
};

// deMarche's dontBlink: the CTC tick, the frame INT, the accelerator, the Covox-Blaster streaming from the hard disk
TEST_F(SprinterFastPathsDemo_Test, DontBlink)
{
    RunDemo("dntblink", 1200);
    if (HasFatalFailure() || IsSkipped())
        return;
    EXPECT_GT(_ringWrites, 100000u) << "the music streams from the disk";
    EXPECT_GT(_fast->IdleCyclesRunInOneGo(), 100u * 20000u) << "the demo waits in HALT most of a frame";
}

// Busy demos: graphics every frame, little HALT
TEST_F(SprinterFastPathsDemo_Test, Rotozoom) { RunDemo("rotozoom", 700); }
TEST_F(SprinterFastPathsDemo_Test, Plasma2) { RunDemo("plasma2", 700); }
TEST_F(SprinterFastPathsDemo_Test, BadApple) { RunDemo("badapple", 700); }
