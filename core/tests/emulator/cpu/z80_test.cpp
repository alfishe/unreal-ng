/// @file Z80 instruction-start bookkeeping (Z80::m1_cycle /
/// Z80::RecordInstructionStart).
///
/// The instruction-start work - m1_pc (the address every memory access of the
/// instruction is attributed to: memory tracker, TTD write journal and probes,
/// calltrace), the M1 trace hook (TTD per-frame instruction capture),
/// execution coverage and the TTD execute probe - runs once per instruction,
/// at its first byte. The M1s inside an instruction (the byte after CB, ED,
/// DD, FD, DDCB) must not run it again. A redundant DD/FD is an instruction of
/// its own, so the prefix after it does start one.

#include "stdafx.h"
#include "pch.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

#include <set>
#include <utility>
#include <vector>

class InstructionStart_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _z80 = _emulator->GetContext()->pCore->GetZ80();
        _memory = _emulator->GetContext()->pMemory;
    }

    void TearDown() override
    {
        if (_z80)
            _z80->m1TraceHook = nullptr;
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

TEST_F(InstructionStart_Test, OncePerInstructionAtItsFirstByte)
{
    // Every prefix form, each at a known address
    struct Instr
    {
        uint16_t start;
        const char* text;
        bool writesMemory;
    };
    const std::vector<uint8_t> code = {
        0x00,                          // 8000 NOP
        0xDD, 0x77, 0x05,              // 8001 LD (IX+5),A
        0xCB, 0xC6,                    // 8004 SET 0,(HL)
        0xDD, 0xCB, 0x05, 0xC6,        // 8006 SET 0,(IX+5)
        0xFD,                          // 800A FD (redundant: an instruction of its own)
        0xDD, 0x21, 0x34, 0x12,        // 800B LD IX,1234h
        0xED, 0x44,                    // 800F NEG
        0xDD, 0xED, 0x44,              // 8011 NEG (DD ignored)
        0xFD, 0x7E, 0x05,              // 8014 LD A,(IY+5)
    };
    const std::vector<Instr> instructions = {
        {0x8000, "NOP", false},          {0x8001, "LD (IX+5),A", true}, {0x8004, "SET 0,(HL)", true},
        {0x8006, "SET 0,(IX+5)", true},  {0x800A, "FD (redundant)", false}, {0x800B, "LD IX,nn", false},
        {0x800F, "NEG", false},          {0x8011, "DD NEG", false},     {0x8014, "LD A,(IY+5)", false},
    };
    for (size_t i = 0; i < code.size(); i++)
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);

    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _z80->ix = 0x9000;
    _z80->iy = 0x9000;
    _z80->hl = 0x9100;
    _z80->iff1 = _z80->iff2 = 0;
    _z80->boundary = Z80_BOUNDARY_NONE;

    std::vector<std::pair<uint16_t, uint32_t>> starts;  // (pc, t) per hook call
    _z80->m1TraceHook = [&](uint16_t pc) { starts.emplace_back(pc, static_cast<uint32_t>(_z80->t)); };

    std::set<uint32_t> boundaries{_z80->t};  // T at every step boundary
    for (const Instr& in : instructions)
    {
        _z80->Z80Step();
        boundaries.insert(_z80->t);
        if (in.writesMemory)
            EXPECT_EQ(_z80->m1_pc, in.start)
                << in.text << ": its memory accesses must be attributed to the instruction start";
    }
    EXPECT_EQ(_z80->pc, 0x8017u) << "program ran to its end";

    // One start per instruction, in order, at the first byte
    ASSERT_EQ(starts.size(), instructions.size()) << "hooks must fire once per instruction";
    for (size_t i = 0; i < instructions.size(); i++)
        EXPECT_EQ(starts[i].first, instructions[i].start) << instructions[i].text;

    // Each recorded start is a state TTD can seek to: a step boundary
    for (const auto& [pc, t] : starts)
        EXPECT_TRUE(boundaries.count(t)) << "start at " << std::hex << pc << " recorded mid-step (t=" << std::dec << t << ")";
}

/// Scorpion "Even M1" (docs/inprogress/2026-09-28-m1-contention/contention-by-machine.md section 6): an opcode
/// fetch from RAM that would start on an odd T-state waits one T-state; fetches from ROM, turbo mode and the
/// other machines do not
class EvenM1_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;

    void Create(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// T-states one instruction takes when its fetch starts at `t` (a NOP placed at `pc` unless `place` is false)
    uint32_t StepAt(uint16_t pc, uint32_t t, bool place = true)
    {
        if (place)
            _memory->DirectWriteToZ80Memory(pc, 0x00);
        _z80->pc = pc;
        _z80->iff1 = 0;
        _z80->t = t;
        _z80->Z80Step();
        return _z80->t - t;
    }
};

TEST_F(EvenM1_Test, ScorpionRamFetchOnAnOddTWaitsOne)
{
    Create("SCORPION");
    ASSERT_TRUE(_context->config.even_M1);
    EXPECT_EQ(StepAt(0x8000, 20000), 4u) << "even T: no wait";
    EXPECT_EQ(StepAt(0x8000, 20001), 5u) << "odd T: one wait";
    EXPECT_EQ(StepAt(0x4000, 20001), 5u) << "any RAM address";
}

TEST_F(EvenM1_Test, ScorpionRomFetchNeverWaits)
{
    Create("SCORPION");
    ASSERT_TRUE(_memory->IsBank0ROM());
    const uint8_t opcode = _memory->DirectReadFromZ80Memory(0x0000);
    ASSERT_EQ(opcode, 0xF3) << "the ROM starts with DI (4 T)";
    EXPECT_EQ(StepAt(0x0000, 20001, false), 4u) << "ROM: no wait on an odd T";
}

TEST_F(EvenM1_Test, ScorpionRamAtZeroWaits)
{
    Create("SCORPION");
    _context->pPortDecoder->DecodePortOut(0x1FFD, 0x01, 0x8000);  // RAM page 0 at #0000
    ASSERT_FALSE(_memory->IsBank0ROM());
    EXPECT_EQ(StepAt(0x0000, 20001), 5u) << "RAM select, not the address";
}

TEST_F(EvenM1_Test, ScorpionTurboHasNoEvenM1)
{
    Create("SCORPION");
    _context->emulatorState.hw_turbo_shift = 1;
    EXPECT_EQ(StepAt(0x8000, 20001), 4u) << "the Even M1 terms are gated off in turbo";
    _context->emulatorState.hw_turbo_shift = 0;
}

TEST_F(EvenM1_Test, OtherMachinesNeverAlign)
{
    for (const char* model : { "PENTAGON", "48K" })
    {
        Create(model);
        EXPECT_FALSE(_context->config.even_M1) << model;
        EXPECT_EQ(StepAt(0x8000, 20001), 4u) << model;
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
}

/// region <Machine step hook (IMachineStepHook, TSConf INF-5)>

/// A machine engine that must advance with the CPU (TSConf TSU / DMA / line
/// events) is called after every step, on every frame - also the frames the
/// turbo mode does not render - and is told when the frame counter rebases.
namespace
{
struct FakeStepHook : IMachineStepHook
{
    uint64_t steps = 0;
    uint64_t stepsThisFrame = 0;
    uint32_t lastT = 0;
    bool monotonic = true;
    std::vector<uint64_t> stepsPerFrame;
    std::vector<uint32_t> rollovers;

    void OnMachineStep(uint32_t t) override
    {
        if (t < lastT)
            monotonic = false;
        lastT = t;
        steps++;
        stepsThisFrame++;
    }
    void OnMachineFrameRollover(uint32_t frameLength) override
    {
        rollovers.push_back(frameLength);
        stepsPerFrame.push_back(stepsThisFrame);
        stepsThisFrame = 0;
        lastT = 0;
    }
};
} // namespace

class MachineStepHook_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Z80* _z80 = nullptr;
    FakeStepHook _hook;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _z80 = _emulator->GetContext()->pCore->GetZ80();
        _z80->SetMachineStepHook(&_hook);
    }

    void TearDown() override
    {
        if (_z80)
            _z80->SetMachineStepHook(nullptr);
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

TEST_F(MachineStepHook_Test, CalledAfterEveryStepWithTheReachedT)
{
    for (int i = 0; i < 200; i++)
    {
        _z80->StepInstruction(true);
        ASSERT_EQ(_hook.lastT, _z80->t) << "step " << i;
    }
    EXPECT_EQ(_hook.steps, 200u);
}

TEST_F(MachineStepHook_Test, RunsOnFramesTheTurboModeDoesNotRender)
{
    // Turbo decimation skips the screen on most frames; the engine must not
    // notice. Driven through the main loop's own frame, as the GUI runs it:
    // with the fixed decimation (TURBO_RENDER_DECIMATION = 50) at most one of
    // these 4 frames is rendered
    MainLoop_CUT* mainLoop = reinterpret_cast<MainLoop_CUT*>(_emulator->GetContext()->pMainLoop);
    _emulator->GetMainLoop()->SetTurboRenderAdaptive(false);
    _emulator->EnableTurboMode();
    const size_t frames = 4;
    static_assert(MainLoop::TURBO_RENDER_DECIMATION > 4);
    for (size_t i = 0; i < frames; i++)
        mainLoop->RunFramePublic();

    ASSERT_EQ(_hook.rollovers.size(), frames);
    for (size_t i = 0; i < frames; i++)
    {
        EXPECT_EQ(_hook.rollovers[i], 71680u) << "frame " << i;
        EXPECT_GT(_hook.stepsPerFrame[i], 5000u) << "frame " << i << ": every step of every frame";
    }
    EXPECT_TRUE(_hook.monotonic) << "t only grows between rollovers";
}

TEST_F(MachineStepHook_Test, RolloverFollowsTheFrameCounterRebase)
{
    Core* core = _emulator->GetContext()->pCore;
    _z80->t = 71680 + 7;
    core->AdjustFrameCounters();
    ASSERT_EQ(_hook.rollovers.size(), 1u);
    EXPECT_EQ(_hook.rollovers[0], 71680u);
    EXPECT_EQ(_z80->t, 7u);

    _z80->t = 100;
    core->AdjustFrameCounters();
    EXPECT_EQ(_hook.rollovers.size(), 1u) << "no rollover mid-frame";
}

/// endregion </Machine step hook>

/// region <Per-step work gate (EmulatorContext::stepWork)>

/// Every rare per-step job shares one gate, so a machine that uses none pays
/// one load and one branch per instruction for all of them together; each
/// setter owns exactly its bit
namespace
{
struct IdleSource : IInterruptSource
{
    bool IsIntAsserted(uint32_t) override { return false; }
    uint8_t AcknowledgeInterrupt(uint32_t) override { return 0xFF; }
};
} // namespace

TEST_F(MachineStepHook_Test, TheGateIsZeroOnAClassicMachineAndEachSetterOwnsItsBit)
{
    EmulatorContext* context = _emulator->GetContext();
    _z80->SetMachineStepHook(nullptr);
    EXPECT_EQ(context->stepWork.load(), 0u) << "a classic machine runs the plain step";

    IdleSource source;
    _z80->SetInterruptSource(&source);
    _z80->SetMachineStepHook(&_hook);
    context->SetStepWork(EmulatorContext::kStepWorkTtdInput, true);
    EXPECT_EQ(context->stepWork.load(), EmulatorContext::kStepWorkTtdInput | EmulatorContext::kStepWorkInterruptSource |
                                            EmulatorContext::kStepWorkMachineStep);

    context->SetStepWork(EmulatorContext::kStepWorkTtdInput, false);
    EXPECT_TRUE(context->HasStepWork(EmulatorContext::kStepWorkInterruptSource)) << "TTD clears only its own bit";
    EXPECT_TRUE(context->HasStepWork(EmulatorContext::kStepWorkMachineStep));

    _z80->SetInterruptSource(nullptr);
    EXPECT_EQ(context->stepWork.load(), EmulatorContext::kStepWorkMachineStep);
    _z80->SetMachineStepHook(nullptr);
    EXPECT_EQ(context->stepWork.load(), 0u);
}

TEST_F(MachineStepHook_Test, WithoutItsBitTheHookIsNotCalled)
{
    // The gate is the only switch: the hook runs through StepInstructionWithWork
    _emulator->GetContext()->SetStepWork(EmulatorContext::kStepWorkMachineStep, false);
    for (int i = 0; i < 10; i++)
        _z80->StepInstruction(true);
    EXPECT_EQ(_hook.steps, 0u);
}

/// endregion </Per-step work gate>
