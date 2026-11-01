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
#include <functional>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
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
        if (_profiWaitPhase >= 0)
        {
            // The board's power-on phase of the v5 wait, reinstalled as ProfiWaitOverlay_Test does
            auto* profi = dynamic_cast<PortDecoder_Profi*>(run.context->pPortDecoder);
            ASSERT_NE(profi, nullptr) << model;
            run.context->config.profi_wait_phase = static_cast<uint8_t>(_profiWaitPhase);
            run.context->config.profi_wait_pentagon = 1;
            profi->SyncWaits();
            run.context->config.profi_wait_pentagon = 0;
            profi->SyncWaits();
        }
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
        }
        else
        {
            Memory* memory = run.context->pMemory;
            for (size_t i = 0; i < kHaltLoop.size(); i++)
                memory->DirectWriteToZ80Memory(static_cast<uint16_t>(loopAt + i), kHaltLoop[i]);
            memory->DirectWriteToZ80Memory(0x9000, 0);
            run.z80->pc = loopAt;
        }
        if (_setup)
            _setup(run);
        // Turbo draws only the frames the host clock allows, and RunNFrames is not frame-aligned: two whole frames
        // without turbo, so no pixel of the first checkpoint comes from the boot (it differed under load)
        run.emulator->RunNFrames(2, true);
    }

    static uint64_t CpuHash(const EmulatorContext* context)
    {
        const Z80* z = context->pCore->GetZ80();
        const uint32_t words[] = {z->af, z->bc, z->de, z->hl, z->ix, z->iy, z->sp, z->pc, z->i, Z80::RegisterR(z),
                                  z->iff1, z->iff2, z->im, z->halted, z->t, z->q, z->prev_pc, z->opcode};
        return Fnv(words, sizeof(words), context->emulatorState.frame_counter);
    }

    /// The TS-Conf engine's and interrupt controller's state: the DRAM budget the idle fetches count in, the line
    /// latches, the latched INTs, the cache (0 on other machines)
    static uint64_t MachineHash(const EmulatorContext* context)
    {
        const auto* tsconf = dynamic_cast<const PortDecoder_TSConf*>(context->pPortDecoder);
        if (!tsconf)
            return 0;
        const TsConfState& ts = tsconf->GetState();
        const uint32_t words[] = {ts.cpuLineAccesses, ts.cpuAccesses, ts.budgetRaster, ts.engNextLine, ts.cntRow,
                                  ts.intPending, ts.intLastRaster, static_cast<uint32_t>(ts.intFrameRaster),
                                  static_cast<uint32_t>(ts.intFrameAdjust), ts.dmaCredit};
        return Fnv(ts.cacheWord, sizeof(ts.cacheWord), Fnv(ts.cacheTag, sizeof(ts.cacheTag), Fnv(words, sizeof(words))));
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
        run.trace.push_back(CpuHash(context) ^ MachineHash(context));
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
            static const char* kWhat[] = {"picture", "sound", "CPU (registers, R, T, frame) or TS-Conf state"};
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
    int _profiWaitPhase = -1;  ///< >= 0: the Profi's [PROFI] WaitPhase for both machines
    std::function<void(Run&)> _setup;  ///< the machine's settings once the program is in

    /// TS-Conf register `reg` through its port (#rrAF)
    static void TsConfWrite(Run& run, uint8_t reg, uint8_t value)
    {
        run.context->pPortDecoder->DecodePortOut(static_cast<uint16_t>((reg << 8) | 0xAF), value, 0);
    }
};

// The HALT in fast memory: every model whose plain step runs it (no machine engine, no interrupt source)
TEST_F(HaltedIdle_Test, HaltLoopPentagon) { Compare("PENTAGON", nullptr, 0x8000, true); }
TEST_F(HaltedIdle_Test, HaltLoop48K) { Compare("48K", nullptr, 0x8000, true); }
TEST_F(HaltedIdle_Test, HaltLoop128K) { Compare("128K", nullptr, 0x8000, true); }
TEST_F(HaltedIdle_Test, HaltLoopPlus3) { Compare("PLUS3", nullptr, 0x8000, true); }
// The Scorpion's Even M1: a fetch that starts on an odd T is a T longer
TEST_F(HaltedIdle_Test, HaltLoopScorpion) { Compare("SCORPION", nullptr, 0x8000, true); }
// The Profi v5's waits follow the paper fetch window by the start's parity: the fetches run in one go per stretch
// (a line's window, the border between two), their lengths learned again in each
TEST_F(HaltedIdle_Test, HaltLoopProfi) { Compare("PROFI", nullptr, 0x8000, true); }
TEST_F(HaltedIdle_Test, HaltLoopProfiWaitPhase0)
{
    _profiWaitPhase = 0;
    Compare("PROFI", nullptr, 0x8000, true);
}
TEST_F(HaltedIdle_Test, HaltLoopProfiWaitPhase1)
{
    _profiWaitPhase = 1;
    Compare("PROFI", nullptr, 0x8000, true);
}
TEST_F(HaltedIdle_Test, HaltLoopProfiWaitPhase2)
{
    _profiWaitPhase = 2;
    Compare("PROFI", nullptr, 0x8000, true);
}
TEST_F(HaltedIdle_Test, HaltLoopAtm3) { Compare("ATM3", nullptr, 0x8000, true); }
// TS-Conf: its own INT logic (NextAssertT) and engine. A fetch from DRAM counts in the line's TSU budget: the
// fetches run in one go per line. TS-BIOS leaves the CPU at 14 MHz: 3.5 MHz set
TEST_F(HaltedIdle_Test, HaltLoopTsConf)
{
    _setup = [](Run& run) { TsConfWrite(run, TsConfReg::SysConfig, 0x00); };
    Compare("TSL", nullptr, 0x8000, true);
}
// A cache hit takes no DRAM cycle: in one go up to the INT
TEST_F(HaltedIdle_Test, HaltLoopTsConfCached)
{
    _setup = [](Run& run) {
        TsConfWrite(run, TsConfReg::SysConfig, 0x00);
        TsConfWrite(run, TsConfReg::CacheConfig, 0x04);
    };
    Compare("TSL", nullptr, 0x8000, true);
}
TEST_F(HaltedIdle_Test, HaltLoopTsConf7MHz)
{
    _setup = [](Run& run) { TsConfWrite(run, TsConfReg::SysConfig, 0x01); };
    Compare("TSL", nullptr, 0x8000, true);
}
// 14 MHz: every DRAM fetch waits for the arbiter, one per step
TEST_F(HaltedIdle_Test, HaltLoopTsConf14MHz)
{
    _setup = [](Run& run) { TsConfWrite(run, TsConfReg::SysConfig, 0x02); };
    Compare("TSL", nullptr, 0x8000, false);
}
// The frame and the line INT in IM 2 (EI : RET at #A200): the CPU wakes at every line end
TEST_F(HaltedIdle_Test, HaltLoopTsConfLineInt)
{
    _setup = [](Run& run) {
        Memory* memory = run.context->pMemory;
        for (uint16_t vector : {0xA0FD, 0xA0FF})
        {
            memory->DirectWriteToZ80Memory(vector, 0x00);
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(vector + 1), 0xA2);
        }
        memory->DirectWriteToZ80Memory(0xA200, 0xFB);  // EI
        memory->DirectWriteToZ80Memory(0xA201, 0xC9);  // RET
        run.z80->i = 0xA0;
        run.z80->im = 2;
        TsConfWrite(run, TsConfReg::SysConfig, 0x00);
        TsConfWrite(run, TsConfReg::IntMask, 0x03);
    };
    Compare("TSL", nullptr, 0x8000, true, 100, 100);
}

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

// Debug mode and a TTD recording (docs/inprogress/2026-10-28-ttd-analysis-feedback/phase-2-plan.md, C): the idle
// runs while no breakpoint, tap or access count watches the HALT, and changes nothing there either
namespace
{
template <class Run>   // HaltedIdle_Test::Run
void DebugMode(Run& run)
{
    run.context->pFeatureManager->setFeature(Features::kDebugMode, true);
    run.context->pMemory->UpdateFeatureCache();
}
}  // namespace

TEST_F(HaltedIdle_Test, HaltLoopPentagonDebugMode)
{
    _setup = [](Run& run) { DebugMode(run); };
    Compare("PENTAGON", nullptr, 0x8000, true);
}

TEST_F(HaltedIdle_Test, HaltLoopPentagonTtdRecording)
{
    _setup = [](Run& run) {
        DebugMode(run);
        run.context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        run.context->pMemory->UpdateFeatureCache();
        ASSERT_TRUE(run.context->pTimeTravelController->StartRecording());
    };
    Compare("PENTAGON", nullptr, 0x8000, true);
    if (HasFailure())
        return;

    // The coverage index holds the HALT in every frame of both sessions, the idle's included
    ttd::TimeTravelController* perStep = _perStep.context->pTimeTravelController;
    ttd::TimeTravelController* inOneGo = _inOneGo.context->pTimeTravelController;
    perStep->StopRecording();
    inOneGo->StopRecording();
    ASSERT_EQ(inOneGo->GetCheckpointCount(), perStep->GetCheckpointCount());
    const auto scan = [](ttd::TimeTravelController* ttd) {
        return ttd->QueryCoverageScan(0, UINT64_MAX, ttd::TTDCoverageKind::Executed, 0x8001, 0x8001, std::nullopt,
                                      100000, false, false);
    };
    const ttd::TTDCoverageScanResult a = scan(perStep);
    const ttd::TTDCoverageScanResult b = scan(inOneGo);
    EXPECT_GT(a.matchingFrames, 100u);
    EXPECT_EQ(b.frames, a.frames);
}

// A replay idles as the recording did: seeks into the middle of frames (a checkpoint restore and a replay to the
// point) land on the same CPU and memory with the idle and without, and the idle ran during the replays
TEST_F(HaltedIdle_Test, HaltLoopPentagonTtdReplay)
{
    _setup = [](Run& run) {
        DebugMode(run);
        run.context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        run.context->pMemory->UpdateFeatureCache();
        ASSERT_TRUE(run.context->pTimeTravelController->StartRecording());
    };
    Compare("PENTAGON", nullptr, 0x8000, true, 20);
    if (HasFailure())
        return;

    const auto replay = [](Run& run) {
        ttd::TimeTravelController* ttd = run.context->pTimeTravelController;
        ttd->StopRecording();
        const uint64_t first = ttd->GetSessionInfo().sessionStartFrame;
        const uint64_t last = ttd->SessionEndPosition().frame;
        std::vector<uint64_t> states;
        for (uint64_t f = first; f < last; f += 3)
            for (const uint32_t tin : {1000u, 30000u, 69000u})
            {
                EXPECT_TRUE(ttd->SeekTo({f, tin}));
                const uint8_t* ram = run.context->pMemory->RAMPageAddress(0);
                states.push_back(Fnv(ram, size_t(8) * 0x4000, CpuHash(run.context)));
            }
        return states;
    };
    const uint64_t before = _inOneGo.z80->haltedIdleFetchesInOneGo;
    const std::vector<uint64_t> perStep = replay(_perStep);
    const std::vector<uint64_t> inOneGo = replay(_inOneGo);
    EXPECT_EQ(_perStep.z80->haltedIdleFetchesInOneGo, 0u);
    EXPECT_GT(_inOneGo.z80->haltedIdleFetchesInOneGo - before, 1000u) << "the replays idled";
    ASSERT_EQ(inOneGo.size(), perStep.size());
    for (size_t i = 0; i < perStep.size(); ++i)
        EXPECT_EQ(inOneGo[i], perStep[i]) << "seek " << i;
}

// A replay that plays recorded keys idles up to each key's time and applies it there: keys pressed and released
// inside halted stretches, read by the loop after every interrupt (the A-G half-row summed into #9000)
TEST_F(HaltedIdle_Test, HaltLoopPentagonTtdReplayWithInput)
{
    static const uint8_t kKeyLoop[] = {
        0xFB,              // +0  EI
        0x76,              // +1  HALT
        0x3E, 0xFD,        // +2  LD A,#FD
        0xDB, 0xFE,        // +4  IN A,(#FE)
        0x21, 0x00, 0x90,  // +6  LD HL,#9000
        0x86,              // +9  ADD A,(HL)
        0x77,              // +10 LD (HL),A
        0xE6, 0x17,        // +11 AND #17
        0xD3, 0xFE,        // +13 OUT (#FE),A
        0x18, 0xEF,        // +15 JR +0
    };
    const auto record = [](Run& run) {
        DebugMode(run);
        run.context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        run.context->pMemory->UpdateFeatureCache();
        for (size_t i = 0; i < sizeof(kKeyLoop); i++)
            run.context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kKeyLoop[i]);
        run.z80->pc = 0x8000;
        ttd::TimeTravelController* ttd = run.context->pTimeTravelController;
        ASSERT_TRUE(ttd->StartRecording());
        DebugKeyboardManager* keys = run.context->pDebugManager->GetKeyboardManager();
        static const ZXKeysEnum kKeys[] = {ZXKEY_A, ZXKEY_S, ZXKEY_D, ZXKEY_F, ZXKEY_G};
        for (uint32_t frame = 0; frame < 20; frame++)
        {
            const ZXKeysEnum key = kKeys[frame % 5];
            run.emulator->RunTStates(5000u + 2311u * frame, true);  // inside the HALT: the loop runs about 120 T
            keys->PressKey(key);
            run.emulator->RunTStates(9000u + 517u * frame, true);
            if (frame % 3 != 2)
                keys->ReleaseKey(key);  // else held over the interrupt and read
            run.emulator->RunTStates(run.z80->_frameLimit - run.z80->t, true);
        }
        ttd->StopRecording();
        EXPECT_GE(ttd->GetInputJournal().Size(), 30u);
    };
    for (Run* run : {&_perStep, &_inOneGo})
    {
        Start(*run, "PENTAGON", run == &_inOneGo, nullptr, 0x8000);
        ASSERT_FALSE(HasFatalFailure());
        record(*run);
        ASSERT_FALSE(HasFatalFailure());
    }
    ASSERT_NE(_perStep.context->pMemory->DirectReadFromZ80Memory(0x9000), 0) << "the loop never read a key";

    const auto replay = [](Run& run) {
        ttd::TimeTravelController* ttd = run.context->pTimeTravelController;
        const uint64_t first = ttd->GetSessionInfo().sessionStartFrame;
        const uint64_t last = ttd->SessionEndPosition().frame;
        std::vector<uint64_t> states;
        for (uint64_t f = first; f < last; f++)
            for (const uint32_t tin : {4000u, 12000u, 26000u, 69000u})
            {
                EXPECT_TRUE(ttd->SeekTo({f, tin}));
                uint8_t rows[8];
                for (uint8_t r = 0; r < 8; r++)
                    rows[r] = run.context->pKeyboard->MatrixRow(r);
                const uint8_t* ram = run.context->pMemory->RAMPageAddress(0);
                states.push_back(Fnv(rows, sizeof(rows), Fnv(ram, size_t(8) * 0x4000, CpuHash(run.context))));
            }
        return states;
    };
    const uint64_t before = _inOneGo.z80->haltedIdleFetchesInOneGo;
    const std::vector<uint64_t> perStep = replay(_perStep);
    const std::vector<uint64_t> inOneGo = replay(_inOneGo);
    EXPECT_EQ(_perStep.z80->haltedIdleFetchesInOneGo, 0u);
    EXPECT_GT(_inOneGo.z80->haltedIdleFetchesInOneGo - before, 1000u) << "the replays idled";
    ASSERT_EQ(inOneGo.size(), perStep.size());
    for (size_t i = 0; i < perStep.size(); ++i)
        EXPECT_EQ(inOneGo[i], perStep[i]) << "seek " << i / 4 << " at " << i % 4;
}

// A breakpoint on the HALT or a read watch on the byte after it sees every idle fetch: one per step
TEST_F(HaltedIdle_Test, HaltLoopDebugModeWatchedRunsPerStep)
{
    for (const bool exec : {true, false})
    {
        Start(_inOneGo, "PENTAGON", true, nullptr, 0x8000);
        ASSERT_FALSE(HasFatalFailure());
        DebugMode(_inOneGo);
        BreakpointManager* brk = _inOneGo.context->pDebugManager->GetBreakpointsManager();
        const uint16_t id = exec ? brk->AddExecutionBreakpoint(0x8001) : brk->AddMemReadBreakpoint(0x8002);
        ASSERT_NE(id, BRK_INVALID);
        const uint64_t before = _inOneGo.z80->haltedIdleFetchesInOneGo;
        _inOneGo.emulator->RunNFrames(20, /*skipBreakpoints=*/true);
        EXPECT_EQ(_inOneGo.z80->haltedIdleFetchesInOneGo, before) << (exec ? "execution breakpoint" : "read watch");
        EmulatorTestHelper::CleanupEmulator(_inOneGo.emulator);
        _inOneGo = Run{};
    }
}
