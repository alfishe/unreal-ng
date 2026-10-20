// The plain step's idle fast-forward (Z80::RunHaltedIdle) changes nothing: the same pictures, sound, registers (R
// included), T-states, RAM and sound card states as one idle fetch per step. Two machines run the same thing, one
// with the fast-forward off, and are compared at every checkpoint through every driver that allows it: RunNFrames,
// RunTStates chunks that end inside idle stretches, and MainLoop's frame (the GUI's run).
//
// Boot-bound: every case boots two machines (turbo) and runs about 200 frames on each.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"

namespace
{
uint64_t Fnv(const void* data, size_t size, uint64_t h = 1469598103934665603ull)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

/// EI : HALT, then a frame counter in #9000 written to the border and the beeper (bit 4) - relocatable (JR back)
const std::vector<uint8_t> kHaltLoop = {
    0xFB,              // +0  EI
    0x76,              // +1  HALT
    0x3A, 0x00, 0x90,  // +2  LD A,(#9000)
    0x3C,              // +5  INC A
    0x32, 0x00, 0x90,  // +6  LD (#9000),A
    0xE6, 0x17,        // +9  AND #17
    0xD3, 0xFE,        // +11 OUT (#FE),A
    0x18, 0xF1,        // +13 JR +0
};
}  // namespace

class HaltedIdle_Test : public ::testing::Test
{
protected:
    struct Run
    {
        Emulator* emulator = nullptr;
        EmulatorContext* context = nullptr;
        Z80* z80 = nullptr;
        std::vector<uint64_t> trace;  ///< per checkpoint: the picture, the sound frame, the CPU
    };

    void TearDown() override
    {
        for (Run* run : {&_perStep, &_inOneGo})
        {
            if (run->emulator)
                EmulatorTestHelper::CleanupEmulator(run->emulator);
            run->emulator = nullptr;
        }
    }

    void Create(Run& run, const char* model, bool inOneGo)
    {
        run.emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(run.emulator, nullptr) << model;
        run.context = run.emulator->GetContext();
        run.z80 = run.context->pCore->GetZ80();
        run.z80->haltedIdleInOneGo = inOneGo;
    }

    /// Boot to the ROM's idle in turbo, then the program (a snapshot, or the HALT loop at `loopAt`)
    void Start(Run& run, const char* model, bool inOneGo, const char* snapshot, uint16_t loopAt)
    {
        Create(run, model, inOneGo);
        if (HasFatalFailure())
            return;
        run.emulator->EnableTurboMode();
        EmulatorTestHelper::RunFramesFast(run.emulator, 100);
        // A boot that tried the floppy: its controller runs per step until it sleeps (IdleStepsInert waits for it)
        for (int frame = 0; frame < 1000 && !run.context->pBetaDisk->IsStepInert(); frame++)
            EmulatorTestHelper::RunFramesFast(run.emulator, 1);
        ASSERT_TRUE(run.context->pBetaDisk->IsStepInert());
        run.emulator->DisableTurboMode();
        if (snapshot)
        {
            const std::string path = (TestPathHelper::FindProjectRoot() / "testdata" / snapshot).string();
            ASSERT_TRUE(run.emulator->LoadSnapshot(path)) << path;
            return;
        }
        Memory* memory = run.context->pMemory;
        for (size_t i = 0; i < kHaltLoop.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(loopAt + i), kHaltLoop[i]);
        memory->DirectWriteToZ80Memory(0x9000, 0);
        run.z80->pc = loopAt;
    }

    static uint64_t CpuHash(const EmulatorContext* context)
    {
        const Z80* z = context->pCore->GetZ80();
        const uint32_t words[] = {z->af, z->bc, z->de, z->hl, z->ix, z->iy, z->sp, z->pc, z->i, Z80::RegisterR(z),
                                  z->iff1, z->iff2, z->im, z->halted, z->t, z->q, z->prev_pc, z->opcode};
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
    /// MainLoop's frame
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
        run.emulator->RunTStates(run.z80->_frameLimit - run.z80->t, true);
        auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(run.context->pMainLoop);
        for (int frame = 0; frame < frames / 2; frame++)
        {
            mainLoop->RunFramePublic();
            Checkpoint(run);
        }
    }

    static uint64_t DeviceHash(const ttd::TTDSerializable* device) { return device ? device->TTDHashState() : 0; }

    void ExpectSame()
    {
        ASSERT_EQ(_inOneGo.trace.size(), _perStep.trace.size());
        for (size_t i = 0; i < _perStep.trace.size(); i++)
        {
            static const char* kWhat[] = {"picture", "sound", "CPU (registers, R, T, frame)"};
            ASSERT_EQ(_inOneGo.trace[i], _perStep.trace[i]) << kWhat[i % 3] << " differs at checkpoint " << i / 3;
        }
        Memory* a = _inOneGo.context->pMemory;
        Memory* b = _perStep.context->pMemory;
        for (uint16_t page = 0; page < 8; page++)
            EXPECT_EQ(Fnv(a->RAMPageAddress(page), PAGE_SIZE), Fnv(b->RAMPageAddress(page), PAGE_SIZE)) << "RAM page " << page;
        const SoundManager* sa = _inOneGo.context->pSoundManager;
        const SoundManager* sb = _perStep.context->pSoundManager;
        EXPECT_EQ(DeviceHash(sa->getGeneralSound()), DeviceHash(sb->getGeneralSound())) << "GS state";
        EXPECT_EQ(DeviceHash(sa->getMoonSound()), DeviceHash(sb->getMoonSound())) << "MoonSound state";
        EXPECT_EQ(DeviceHash(dynamic_cast<const ttd::TTDSerializable*>(sa->getTurboSound())),
                  DeviceHash(dynamic_cast<const ttd::TTDSerializable*>(sb->getTurboSound())))
            << "TurboSound slot state";
    }

    /// Both machines through the same program; `expectInOneGo`: the fast-forward must have run (else it must not)
    void Compare(const char* model, const char* snapshot, uint16_t loopAt, bool expectInOneGo, int frames = 100,
                 uint64_t perFrame = 1000, bool anyInOneGo = false)
    {
        Start(_perStep, model, false, snapshot, loopAt);
        if (HasFatalFailure())
            return;
        Start(_inOneGo, model, true, snapshot, loopAt);
        if (HasFatalFailure())
            return;
        const uint64_t before = _inOneGo.z80->haltedIdleFetchesInOneGo;
        Drive(_perStep, frames);
        Drive(_inOneGo, frames);
        EXPECT_EQ(_perStep.z80->haltedIdleFetchesInOneGo, 0u);
        const uint64_t ran = _inOneGo.z80->haltedIdleFetchesInOneGo - before;
        if (anyInOneGo)
            ;  // either way: the board decides (its wait rules)
        else if (expectInOneGo)
            EXPECT_GT(ran, perFrame * static_cast<uint64_t>(frames)) << model << ": the idle fetches ran in one go";
        else
            EXPECT_EQ(ran, 0u) << model << ": a fetch with side effects runs one per step";
        ExpectSame();
    }

    Run _perStep;
    Run _inOneGo;
};

// The HALT in fast memory: every model whose plain step runs it (no machine engine, no interrupt source)
TEST_F(HaltedIdle_Test, HaltLoopPentagon) { Compare("PENTAGON", nullptr, 0x8000, true); }
TEST_F(HaltedIdle_Test, HaltLoop48K) { Compare("48K", nullptr, 0x8000, true); }
TEST_F(HaltedIdle_Test, HaltLoop128K) { Compare("128K", nullptr, 0x8000, true); }
TEST_F(HaltedIdle_Test, HaltLoopPlus3) { Compare("PLUS3", nullptr, 0x8000, true); }
// The Scorpion's Even M1: a fetch that starts on an odd T is a T longer
TEST_F(HaltedIdle_Test, HaltLoopScorpion) { Compare("SCORPION", nullptr, 0x8000, true); }
// The Profi v5's waits follow the paper fetch window, like contention: the fetch runs in one go only without them
TEST_F(HaltedIdle_Test, HaltLoopProfi) { Compare("PROFI", nullptr, 0x8000, true, 100, 1000, true); }
TEST_F(HaltedIdle_Test, HaltLoopAtm3) { Compare("ATM3", nullptr, 0x8000, true); }

// The HALT in slow memory: the fetch after it is contended, so it runs one per step (on the Pentagon it is not)
TEST_F(HaltedIdle_Test, HaltLoopInContendedMemory48K) { Compare("48K", nullptr, 0x6000, false); }
TEST_F(HaltedIdle_Test, HaltLoopInContendedMemory128K) { Compare("128K", nullptr, 0x6000, false); }
TEST_F(HaltedIdle_Test, HaltLoopAt6000Pentagon) { Compare("PENTAGON", nullptr, 0x6000, true); }

// Demos with music, with the shipped sound cards (TSFM, GS, MoonSound as the models' configs fit them)
TEST_F(HaltedIdle_Test, DemoPentagonShippedCards)
{
    SoundCardScope shippedSound;
    Compare("PENTAGON", "loaders/sna/7threality.sna", 0, true);
}
TEST_F(HaltedIdle_Test, Demo128KShippedCards)
{
    SoundCardScope shippedSound;
    Compare("128K", "loaders/sna/across-the-edge-second.sna", 0, true, 100, 300);
}
TEST_F(HaltedIdle_Test, DemoScorpionShippedCards)
{
    SoundCardScope shippedSound;
    Compare("SCORPION", "loaders/sna/eyeache1.sna", 0, true);
}
