// RZX playback on real machines (emulator/rzx/rzxsession.h): the fetch counter
// per instruction class, every IN form fed from the recording, the forced
// interrupt schedule and its conventions, desync detection, the shortcut and
// input locks, the model check, and real RZX Archive recordings checked
// against SkoolKit rzxplay.py (testdata/loaders/rzx/README.md).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/rzxtestbuilder.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "loaders/rzx/rzxreader.h"
#include "loaders/snapshot/loader_z80.h"

using namespace rzx;

namespace
{
    std::string Fixture(const std::string& name)
    {
        return (TestPathHelper::FindProjectRoot() / "testdata" / "loaders" / "rzx" / name).string();
    }

    std::shared_ptr<const File> ParseBytes(const std::vector<uint8_t>& bytes)
    {
        auto file = std::make_shared<File>();
        std::string error;
        EXPECT_TRUE(RzxReader::Parse(bytes.data(), bytes.size(), *file, error)) << error;
        return file;
    }

    class RzxSession_Test : public ::testing::Test
    {
    protected:
        std::shared_ptr<Emulator> _emulator;
        EmulatorContext* _context = nullptr;
        Z80* _cpu = nullptr;

        void Create(const char* model, uint32_t ramKb = 0)
        {
            _emulator = ramKb ? EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM("rzx-test", model, ramKb,
                                                                                              LoggerLevel::LogError)
                              : EmulatorManager::GetInstance()->CreateEmulatorWithModel("rzx-test", model,
                                                                                        LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr) << model;
            _context = _emulator->GetContext();
            _cpu = _context->pCore->GetZ80();
        }

        void TearDown() override
        {
            if (_emulator)
                EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
        }

        /// Play a 48K program at #8000 with the given frames
        PlayResult Play48K(const std::vector<uint8_t>& code, const std::vector<RzxTestFrame>& frames,
                           const Z80TestRegisters& regs = {}, const PlayerOptions& options = {})
        {
            RzxTestBuilder rzx;
            rzx.Creator("unreal-ng test", 1, 0).Snapshot("z80", Z80Snapshot48K(code, 0x8000, regs)).Input(frames);
            return _emulator->PlayRzx(ParseBytes(rzx.Build()), "", options);
        }

        struct StepOutcome
        {
            bool intAccepted = false;  ///< the step took the interrupt that ends an RZX frame
        };

        /// One instruction the way the main loop runs it: a step, and the
        /// frame boundary when the video frame ends (Emulator::ExecuteStep via
        /// RunUntilCondition, no debugger notification)
        StepOutcome Step()
        {
            const uint64_t before = _emulator->GetRzxStatus().player.interrupts;
            _emulator->RunUntilCondition([](const Z80State&) { return true; }, 0, false);
            return {_emulator->GetRzxStatus().player.interrupts > before};
        }

        RzxPlayer& Player()
        {
            return *_context->rzxPlayer;
        }

        /// Plays `frames` frames (0: to the end) and stops where SkoolKit's
        /// --stop N dump is taken: right after the interrupt that ends frame N.
        /// SkoolKit stops where frame N's count is reached and takes the
        /// interrupt only when IFF1 is set; our frame end runs at the start of
        /// the next step, which with IFF1 clear also runs the next instruction.
        /// So: run to the boundary, then step once only for the interrupt
        void PlayToFrame(uint64_t frames)
        {
            RzxPlayer* player = _context->rzxPlayer;
            ASSERT_NE(player, nullptr);
            const uint64_t target = frames ? frames : player->Status().totalFrames;
            ASSERT_GT(target, 0u);

            // Frames of 0 fetches are skipped (no boundary of their own): stop
            // at the last frame with fetches up to the target
            uint64_t boundary = 0;
            uint64_t index = 0;
            for (const InputBlock& input : player->GetFile().inputs)
            {
                for (const Frame& frame : input.frames)
                {
                    if (index < target && frame.fetchCount > 0)
                        boundary = index;
                    index++;
                }
            }
            EmulatorContext* context = _context;
            _emulator->RunUntilCondition(
                [context, player, boundary](const Z80State&) {
                    return context->rzxPlayer == nullptr || (player->FramesDone() == boundary && player->FrameDue());
                },
                0, false);
            ASSERT_NE(_context->rzxPlayer, nullptr) << _emulator->GetRzxStatus().player.stopReason;
            if (_cpu->iff1)
                ASSERT_TRUE(Step().intAccepted);

            const SessionStatus status = _emulator->GetRzxStatus();
            ASSERT_EQ(status.player.desyncs, 0u) << status.player.stopReason;
        }

        /// Registers, every RAM page and #7FFD against a Z80 snapshot of the
        /// expected state, loaded on a second machine of the same model
        void CompareWith(const std::string& oraclePath, const std::string& model, uint32_t ramKb = 0)
        {
            std::shared_ptr<Emulator> reference =
                ramKb ? EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM("rzx-oracle", model, ramKb,
                                                                                     LoggerLevel::LogError)
                      : EmulatorManager::GetInstance()->CreateEmulatorWithModel("rzx-oracle", model,
                                                                               LoggerLevel::LogError);
            ASSERT_NE(reference, nullptr);
            EmulatorContext* refContext = reference->GetContext();
            LoaderZ80 loader(refContext, oraclePath);
            ASSERT_TRUE(loader.load()) << oraclePath;
            const Z80& expected = *refContext->pCore->GetZ80();
            const Z80& actual = *_cpu;

            EXPECT_EQ(actual.pc, expected.pc);
            EXPECT_EQ(actual.sp, expected.sp);
            EXPECT_EQ(actual.af, expected.af);
            EXPECT_EQ(actual.bc, expected.bc);
            EXPECT_EQ(actual.de, expected.de);
            EXPECT_EQ(actual.hl, expected.hl);
            EXPECT_EQ(actual.ix, expected.ix);
            EXPECT_EQ(actual.iy, expected.iy);
            EXPECT_EQ(actual.alt.af, expected.alt.af);
            EXPECT_EQ(actual.alt.bc, expected.alt.bc);
            EXPECT_EQ(actual.alt.de, expected.alt.de);
            EXPECT_EQ(actual.alt.hl, expected.alt.hl);
            EXPECT_EQ(actual.i, expected.i);
            EXPECT_EQ(actual.IR() & 0xFF, expected.IR() & 0xFF) << "R: the fetch count ran in step";
            EXPECT_EQ(actual.iff1, expected.iff1);
            EXPECT_EQ(actual.im, expected.im);

            const bool is48K = _context->config.mem_model == MM_SPECTRUM48;
            const std::vector<uint16_t> pages = is48K ? std::vector<uint16_t>{5, 2, 0}
                                                      : std::vector<uint16_t>{0, 1, 2, 3, 4, 5, 6, 7};
            for (uint16_t page : pages)
            {
                const uint8_t* ours = _context->pMemory->RAMPageAddress(page);
                const uint8_t* theirs = refContext->pMemory->RAMPageAddress(page);
                size_t differences = 0;
                size_t first = 0;
                for (size_t i = 0; i < 0x4000; i++)
                {
                    if (ours[i] != theirs[i] && differences++ == 0)
                        first = i;
                }
                EXPECT_EQ(differences, 0u) << "RAM page " << page << ", first at offset #" << std::hex << first;
            }
            if (!is48K)
                EXPECT_EQ(_context->emulatorState.p7FFD, refContext->emulatorState.p7FFD);

            EmulatorManager::GetInstance()->RemoveEmulator(reference->GetId());
        }

        /// Steps until PC reaches `pc` (at most `limit` steps)
        void RunTo(uint16_t pc, int limit = 1000)
        {
            for (int i = 0; i < limit && _cpu->pc != pc; i++)
                Step();
            ASSERT_EQ(_cpu->pc, pc);
        }
    };
}  // namespace

/// region <Fetch counter>

struct FetchCase
{
    const char* name;
    std::vector<uint8_t> code;  ///< runs from #8000 to #8000 + size
    uint32_t fetches;
};

class RzxFetchCount_Test : public RzxSession_Test, public ::testing::WithParamInterface<FetchCase>
{
};

/// Every opcode and prefix fetch counts one (the R register's increments):
/// the instruction runs from #8000 to its end with the counter checked there
TEST_P(RzxFetchCount_Test, CountsEveryOpcodeAndPrefixFetch)
{
    Create("48K");
    const FetchCase& param = GetParam();
    Z80TestRegisters regs;
    regs.bc = 3;  // block instructions repeat 3 times
    regs.hl = 0x9000;
    regs.de = 0xA000;
    regs.ix = 0x9000;
    regs.a = 0x55;
    ASSERT_TRUE(Play48K(param.code, {{60000, {}}}, regs).Ok());

    RunTo(static_cast<uint16_t>(0x8000 + param.code.size()));
    EXPECT_EQ(Player().Fetches(), param.fetches) << param.name;
}

INSTANTIATE_TEST_SUITE_P(
    InstructionClasses, RzxFetchCount_Test,
    ::testing::Values(FetchCase{"nop", {0x00}, 1}, FetchCase{"ld_bc_nn", {0x01, 0x34, 0x12}, 1},
                      FetchCase{"cb_rlc_b", {0xCB, 0x00}, 2}, FetchCase{"ed_neg", {0xED, 0x44}, 2},
                      FetchCase{"dd_ld_ix_nn", {0xDD, 0x21, 0x34, 0x12}, 2},
                      FetchCase{"ddcb_rlc_ix_d", {0xDD, 0xCB, 0x05, 0x06}, 2},
                      FetchCase{"redundant_prefixes", {0xDD, 0xFD, 0xDD, 0x21, 0x00, 0x00}, 4},
                      FetchCase{"ldir_3", {0xED, 0xB0}, 6}, FetchCase{"ld_r_a", {0xED, 0x4F}, 2},
                      FetchCase{"ld_a_r", {0xED, 0x5F}, 2},
                      FetchCase{"ld_r_a_then_nop", {0xED, 0x4F, 0x00}, 3}),
    [](const ::testing::TestParamInfo<FetchCase>& info) { return std::string(info.param.name); });

/// HALT re-fetches its opcode every 4 T-states: one count per halted step
TEST_F(RzxSession_Test, HaltCountsOneFetchPerCycle)
{
    Create("48K");
    ASSERT_TRUE(Play48K({0x76}, {{60000, {}}}).Ok());
    for (int i = 0; i < 10; i++)
        Step();
    EXPECT_EQ(Player().Fetches(), 10u);
    EXPECT_EQ(_cpu->pc, 0x8000);
}

/// LD R,A replaces R; the count still sees its 2 fetches and R holds A
TEST_F(RzxSession_Test, LdRAKeepsTheCountAndSetsR)
{
    Create("48K");
    Z80TestRegisters regs;
    regs.a = 0x05;
    regs.r = 0x70;
    ASSERT_TRUE(Play48K({0xED, 0x4F, 0x00, 0x00}, {{60000, {}}}, regs).Ok());
    RunTo(0x8004);
    EXPECT_EQ(Player().Fetches(), 4u);
    EXPECT_EQ(_cpu->r_low & 0x7F, 0x07);  // #05 + the 2 NOPs
}

/// endregion </Fetch counter>

/// region <IN substitution>

/// Every IN form returns the next recorded value, whatever the port answers
TEST_F(RzxSession_Test, EveryInFormReturnsTheRecordedValues)
{
    Create("48K");
    const std::vector<uint8_t> code = {
        0xDB, 0xFE,        // IN A,(#FE)
        0x32, 0x00, 0x90,  // LD (#9000),A
        0x01, 0xFE, 0x7F,  // LD BC,#7FFE
        0xED, 0x40,        // IN B,(C)
        0x78,              // LD A,B
        0x32, 0x01, 0x90,  // LD (#9001),A
        0x01, 0xFE, 0x7F,  // LD BC,#7FFE
        0xED, 0x48,        // IN C,(C)
        0xED, 0x50,        // IN D,(C)
        0xED, 0x58,        // IN E,(C)
        0xED, 0x60,        // IN H,(C)
        0xED, 0x68,        // IN L,(C)
        0xED, 0x70,        // IN (C) (flags only)
        0xED, 0x78,        // IN A,(C)
        0x21, 0x10, 0x90,  // LD HL,#9010
        0x01, 0xFE, 0x02,  // LD BC,#02FE
        0xED, 0xA2,        // INI
        0xED, 0xAA,        // IND
        0x21, 0x20, 0x90,  // LD HL,#9020
        0x01, 0xFE, 0x03,  // LD BC,#03FE
        0xED, 0xB2,        // INIR (3 reads)
        0x21, 0x30, 0x90,  // LD HL,#9030
        0x01, 0xFE, 0x02,  // LD BC,#02FE
        0xED, 0xBA,        // INDR (2 reads)
        0x00};
    std::vector<uint8_t> ins;
    for (uint8_t v = 0x10; v < 0x10 + 17; v++)
        ins.push_back(v);
    ASSERT_TRUE(Play48K(code, {{60000, ins}}).Ok());

    RunTo(static_cast<uint16_t>(0x8000 + code.size() - 1));
    Memory& memory = *_context->pMemory;
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9000), 0x10);  // IN A,(n)
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9001), 0x11);  // IN B,(C)
    EXPECT_EQ(_cpu->d, 0x13);
    EXPECT_EQ(_cpu->e, 0x14);
    EXPECT_EQ(_cpu->a, 0x18);                                 // after IN C, D, E, H, L, (C)
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9010), 0x19);  // INI
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9011), 0x1A);  // IND (HL after INI)
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9020), 0x1B);  // INIR
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9021), 0x1C);
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9022), 0x1D);
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x9030), 0x1E);  // INDR
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x902F), 0x1F);
}

/// The devices still see every read (side effects kept): the bus carries the IN
TEST_F(RzxSession_Test, TheDeviceStillSeesTheRead)
{
    Create("48K");
    int ioReads = 0;
    _cpu->busTraceHook = [&ioReads](char type, uint16_t, uint8_t) {
        if (type == 'I')
            ioReads++;
    };
    ASSERT_TRUE(Play48K({0xDB, 0xFE, 0xDB, 0xFE, 0x00}, {{60000, {0x01, 0x02}}}).Ok());
    RunTo(0x8004);
    _cpu->busTraceHook = nullptr;
    EXPECT_EQ(ioReads, 2);
    EXPECT_EQ(_cpu->a, 0x02);
}

/// endregion </IN substitution>

/// region <Interrupt schedule>

/// With IFF1 set, the interrupt comes exactly when the frame's count is reached
TEST_F(RzxSession_Test, InterruptAtTheRecordedCount)
{
    Create("48K");
    Z80TestRegisters regs;
    regs.iff = true;
    regs.im = 1;
    // 5 NOPs, then the interrupt (IM 1: RST #38)
    ASSERT_TRUE(Play48K({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {{5, {}}, {60000, {}}}, regs).Ok());
    for (int i = 0; i < 5; i++)
    {
        const StepOutcome result = Step();
        ASSERT_FALSE(result.intAccepted) << "step " << i;
    }
    EXPECT_EQ(_cpu->pc, 0x8005);
    const StepOutcome result = Step();
    EXPECT_TRUE(result.intAccepted);
    EXPECT_EQ(_cpu->pc, 0x0038);
    EXPECT_EQ(_cpu->iff1, 0);
    EXPECT_EQ(Player().Fetches(), 0u) << "the acknowledge is not a fetch";
    EXPECT_EQ(_emulator->GetRzxStatus().player.frame, 1u);
}

/// With interrupts disabled the frame advances without an interrupt
TEST_F(RzxSession_Test, FrameAdvancesWithoutInterruptUnderDi)
{
    Create("48K");
    ASSERT_TRUE(Play48K({0x00, 0x00, 0x00, 0x00}, {{2, {}}, {60000, {}}}).Ok());
    RunTo(0x8003);
    EXPECT_EQ(_emulator->GetRzxStatus().player.frame, 1u);
    EXPECT_EQ(Player().Fetches(), 1u);
}

/// A HALT waits for the forced interrupt, which returns past it
TEST_F(RzxSession_Test, HaltEndsAtTheForcedInterrupt)
{
    Create("48K");
    Z80TestRegisters regs;
    regs.iff = true;
    regs.sp = 0xFF00;
    ASSERT_TRUE(Play48K({0x76, 0x00}, {{20, {}}, {60000, {}}}, regs).Ok());
    StepOutcome result;
    int steps = 0;
    do
    {
        result = Step();
        steps++;
    } while (!result.intAccepted && steps < 100);
    EXPECT_EQ(steps, 21);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xFEFE), 0x01) << "return address #8001, past the HALT";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xFEFF), 0x80);
}

/// The machine's own frame interrupt never fires during playback: a whole
/// video frame of EI + NOPs sees no interrupt until the recorded count
TEST_F(RzxSession_Test, TheMachineFrameInterruptIsMasked)
{
    Create("48K");
    Z80TestRegisters regs;
    regs.iff = true;
    std::vector<uint8_t> code(0x4000, 0x00);  // NOPs #8000-#BFFF
    ASSERT_TRUE(Play48K(code, {{30000, {}}, {60000, {}}}, regs).Ok());
    int interrupts = 0;
    for (int i = 0; i < 30000; i++)
        interrupts += Step().intAccepted ? 1 : 0;
    EXPECT_EQ(interrupts, 0) << "30000 NOPs span 1.7 video frames";
    EXPECT_TRUE(Step().intAccepted);
}

/// Default convention: an interrupt right after EI is accepted at the frame
/// end; with eiShortFrameBlocksInt a following 1-2 fetch frame blocks it
TEST_F(RzxSession_Test, EiConventions)
{
    Create("48K");
    Z80TestRegisters regs;
    regs.iff = false;
    ASSERT_TRUE(Play48K({0x00, 0xFB, 0x00, 0x00}, {{2, {}}, {2, {}}, {60000, {}}}, regs).Ok());
    Step();  // NOP
    Step();  // EI
    EXPECT_TRUE(Step().intAccepted) << "accepted at every frame end (SkoolKit default)";

    PlayerOptions options;
    options.eiShortFrameBlocksInt = true;
    ASSERT_TRUE(Play48K({0x00, 0xFB, 0x00, 0x00}, {{2, {}}, {2, {}}, {60000, {}}}, regs, options).Ok());
    Step();
    Step();
    EXPECT_FALSE(Step().intAccepted) << "EI + a 2-fetch frame: blocked";
    EXPECT_EQ(_cpu->pc, 0x8003);
}

/// The NMOS LD A,I parity quirk on the frame-end interrupt only by option
TEST_F(RzxSession_Test, LdAIParityQuirkIsAnOption)
{
    Create("48K");
    Z80TestRegisters regs;
    regs.iff = true;
    regs.sp = 0xFF00;
    // LD A,I sets P/V from IFF2 = 1; the frame ends right after it
    ASSERT_TRUE(Play48K({0xED, 0x57, 0x00}, {{2, {}}, {60000, {}}}, regs).Ok());
    Step();
    ASSERT_TRUE(Step().intAccepted);
    EXPECT_NE(_cpu->f & 0x04, 0) << "quirk off by default: P/V stays 1";

    PlayerOptions options;
    options.ldAirParityQuirk = true;
    ASSERT_TRUE(Play48K({0xED, 0x57, 0x00}, {{2, {}}, {60000, {}}}, regs, options).Ok());
    Step();
    ASSERT_TRUE(Step().intAccepted);
    EXPECT_EQ(_cpu->f & 0x04, 0) << "quirk on: P/V reads 0";
}

/// endregion </Interrupt schedule>

/// region <Desync and end>

TEST_F(RzxSession_Test, StrictModeStopsAtAnExtraIn)
{
    Create("48K");
    ASSERT_TRUE(Play48K({0xDB, 0xFE, 0xDB, 0xFE, 0x00}, {{60000, {0x42}}}).Ok());
    RunTo(0x8004);
    const SessionStatus status = _emulator->GetRzxStatus();
    EXPECT_FALSE(status.active);
    EXPECT_EQ(status.player.state, PlayerState::Desynced);
    EXPECT_EQ(status.player.firstDesync.kind, DesyncKind::TooManyIns);
    EXPECT_EQ(status.player.firstDesync.pc, 0x8002);
    EXPECT_EQ(_context->rzxPlayer, nullptr);
    EXPECT_FALSE(_cpu->frameIntMasked) << "the machine continues live";
}

TEST_F(RzxSession_Test, StrictModeStopsAtLeftoverIns)
{
    Create("48K");
    ASSERT_TRUE(Play48K({0xDB, 0xFE, 0x00, 0x00}, {{2, {0x01, 0x02}}, {60000, {}}}).Ok());
    RunTo(0x8003);
    Step();  // the frame ends at the next boundary
    const SessionStatus status = _emulator->GetRzxStatus();
    EXPECT_EQ(status.player.state, PlayerState::Desynced);
    EXPECT_EQ(status.player.firstDesync.kind, DesyncKind::TooFewIns);
    EXPECT_EQ(status.player.firstDesync.expected, 2u);
    EXPECT_EQ(status.player.firstDesync.actual, 1u);
}

TEST_F(RzxSession_Test, TolerantModeCountsAndContinues)
{
    Create("48K");
    PlayerOptions options;
    options.desyncMode = DesyncMode::Tolerant;
    ASSERT_TRUE(Play48K({0xDB, 0xFE, 0xDB, 0xFE, 0x00, 0x00}, {{3, {0x42}}, {60000, {}}}, {}, options).Ok());
    RunTo(0x8005);
    Step();  // the frame ends at the next boundary
    const SessionStatus status = _emulator->GetRzxStatus();
    EXPECT_TRUE(status.active);
    EXPECT_EQ(status.player.desyncs, 1u);
    EXPECT_EQ(_cpu->a, 0x42) << "the extra IN repeats the last value";
    EXPECT_EQ(status.player.frame, 1u);
}

/// The last frame ends with its interrupt; then the machine runs live with
/// its own frame interrupt back
TEST_F(RzxSession_Test, FinishesAndRestoresTheMachine)
{
    Create("48K");
    ASSERT_TRUE(Play48K({0x00, 0x00, 0x00, 0x00}, {{1, {}}, {2, {}}}).Ok());
    EXPECT_TRUE(_emulator->IsRzxPlaying());
    RunTo(0x8003);
    EXPECT_TRUE(_emulator->IsRzxPlaying()) << "the last frame ends at the next boundary";
    Step();
    EXPECT_FALSE(_emulator->IsRzxPlaying());
    const SessionStatus status = _emulator->GetRzxStatus();
    EXPECT_EQ(status.player.state, PlayerState::Finished);
    EXPECT_EQ(status.player.frame, 2u);
    EXPECT_EQ(_context->stepWork.load() & EmulatorContext::kStepWorkRzx, 0);
    EXPECT_FALSE(_cpu->frameIntMasked);
}

/// Repeat frames reuse the previous stored frame's IN values; zero-fetch
/// frames are skipped with their values (SkoolKit)
TEST_F(RzxSession_Test, RepeatAndEmptyFrames)
{
    Create("48K");
    const std::vector<uint8_t> code = {0xDB, 0xFE, 0x32, 0x00, 0x90, 0xDB, 0xFE, 0x32, 0x01, 0x90, 0x00};
    RzxTestFrame repeat;
    repeat.fetchCount = 2;
    repeat.repeat = true;
    ASSERT_TRUE(Play48K(code, {{2, {0x77}}, {0, {0x99}}, repeat, {60000, {}}}).Ok());
    RunTo(0x800A);
    Step();  // the frame ends at the next boundary
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9000), 0x77);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9001), 0x99)
        << "a repeat copies the previous stored frame, a skipped one too (SkoolKit)";
    EXPECT_EQ(_emulator->GetRzxStatus().player.frame, 3u);
}

TEST_F(RzxSession_Test, StopReturnsTheMachineToLive)
{
    Create("48K");
    ASSERT_TRUE(Play48K({0x00, 0x00}, {{60000, {}}}).Ok());
    EXPECT_TRUE(_emulator->StopRzx());
    EXPECT_FALSE(_emulator->IsRzxPlaying());
    EXPECT_EQ(_emulator->GetRzxStatus().player.state, PlayerState::Stopped);
    EXPECT_FALSE(_emulator->StopRzx());
}

/// endregion </Desync and end>

/// region <Locks and model>

TEST_F(RzxSession_Test, ShortcutsAndLiveInputAreLockedWhilePlaying)
{
    Create("48K");
    FeatureManager& features = *_context->pFeatureManager;
    features.setFeature(Features::kFastTape, true);
    ASSERT_TRUE(features.isEnabled(Features::kFastTape));

    ASSERT_TRUE(Play48K({0x00, 0x00}, {{60000, {}}}).Ok());
    EXPECT_FALSE(features.isEnabled(Features::kFastTape));
    EXPECT_FALSE(features.refusalReason(Features::kFastDisk, true).empty());
    DebugKeyboardManager& keys = *_context->pDebugManager->GetKeyboardManager();
    keys.PressKey("SPACE");
    EXPECT_EQ(_context->pKeyboard->HandlePortIn(0x7FFE) & 0x01, 0x01) << "live key refused";

    _emulator->StopRzx();
    EXPECT_TRUE(features.isEnabled(Features::kFastTape));
    keys.PressKey("SPACE");
    EXPECT_EQ(_context->pKeyboard->HandlePortIn(0x7FFE) & 0x01, 0x00) << "live key accepted again";
}

TEST_F(RzxSession_Test, ModelMismatchNamesTheModel)
{
    Create("128K");
    const PlayResult result = Play48K({0x00}, {{60000, {}}});
    EXPECT_EQ(result.error, PlayError::ModelMismatch);
    EXPECT_EQ(result.requiredModel, "48K");
    EXPECT_FALSE(_emulator->IsRzxPlaying());
}

TEST_F(RzxSession_Test, RecordingWithoutSnapshotIsRefused)
{
    Create("48K");
    RzxTestBuilder rzx;
    rzx.Input({{10, {}}});
    const PlayResult result = _emulator->PlayRzx(ParseBytes(rzx.Build()), "");
    EXPECT_EQ(result.error, PlayError::NoSnapshot);
}

/// endregion </Locks and model>

/// region <Seek>

/// Forward: the state after seeking to frame 300 equals SkoolKit's after 300
/// frames; back from 400: the keyframe at 250 plus 50 frames give the same.
/// About 25 M T-states (a real recording is the point)
TEST_F(RzxSession_Test, SeekForwardAndBackMatchesStraightPlay)
{
    Create("48K");
    ASSERT_TRUE(_emulator->PlayRzx(Fixture("archive/ericfloaters.rzx")).Ok());
    std::string error;
    ASSERT_TRUE(_emulator->SeekRzx(300, &error)) << error;
    EXPECT_EQ(Player().FramesDone(), 300u);
    CompareWith(Fixture("oracle/ericfloaters-300.z80"), "48K");

    ASSERT_TRUE(_emulator->SeekRzx(400, &error)) << error;
    ASSERT_TRUE(_emulator->SeekRzx(300, &error)) << error;
    EXPECT_EQ(Player().FramesDone(), 300u);
    CompareWith(Fixture("oracle/ericfloaters-300.z80"), "48K");

    const SessionStatus status = _emulator->GetRzxStatus();
    EXPECT_EQ(status.player.keyframes, 2u) << "frame 0 and the boundary of frame 250";
    EXPECT_EQ(status.player.desyncs, 0u);
}

/// After the end the playback can be sought back into and plays on
TEST_F(RzxSession_Test, SeekBackAfterTheEndResumesThePlayback)
{
    Create("48K");
    ASSERT_TRUE(_emulator->PlayRzx(Fixture("external/ericfloaters-ext.rzx")).Ok());
    std::string error;
    ASSERT_TRUE(_emulator->SeekRzx(300, &error)) << error;
    EXPECT_FALSE(_emulator->IsRzxPlaying()) << "300 of 300 frames: finished";

    ASSERT_TRUE(_emulator->SeekRzx(0, &error)) << error;
    EXPECT_TRUE(_emulator->IsRzxPlaying());
    EXPECT_EQ(Player().FramesDone(), 0u);
    ASSERT_TRUE(_emulator->SeekRzx(300, &error)) << error;
    CompareWith(Fixture("oracle/ericfloaters-300.z80"), "48K");
}

/// The player owns the keyframes: a stop frees them, a new recording starts
/// with its own frame 0 only
TEST_F(RzxSession_Test, KeyframesAreFreedOnStopAndReplacedByANewPlayback)
{
    Create("48K");
    ASSERT_TRUE(_emulator->PlayRzx(Fixture("archive/garfield.rzx")).Ok());
    ASSERT_TRUE(_emulator->SeekRzx(600));
    EXPECT_EQ(_emulator->GetRzxStatus().player.keyframes, 3u);
    EXPECT_GT(_emulator->GetRzxStatus().player.keyframeBytes, 0u);

    ASSERT_TRUE(_emulator->PlayRzx(Fixture("archive/garfield.rzx")).Ok());
    EXPECT_EQ(_emulator->GetRzxStatus().player.keyframes, 1u);

    _emulator->StopRzx();
    EXPECT_EQ(_emulator->GetRzxStatus().player.keyframes, 0u);
    EXPECT_EQ(_emulator->GetRzxStatus().player.keyframeBytes, 0u);
    std::string error;
    EXPECT_FALSE(_emulator->SeekRzx(10, &error)) << "nothing to seek back from after a stop";
}

/// A long seek runs on another thread while the status stays readable (the
/// GUI polls it during a seek: it must never wait for the seek to finish).
/// 1000 frames of play (about 0.5 s): long enough to be seen under way
TEST_F(RzxSession_Test, StatusStaysReadableDuringASeek)
{
    Create("48K");
    ASSERT_TRUE(_emulator->PlayRzx(Fixture("archive/ericfloaters.rzx")).Ok());
    std::atomic<bool> done{false};
    std::thread seek([this, &done]() {
        _emulator->SeekRzx(1000);
        done = true;
    });
    // Frames advance under the running seek, read without blocking
    const bool progressed = TestWait::For([this, &done]() {
        const uint64_t frame = _emulator->GetRzxStatus().player.frame;
        return done || (frame > 0 && frame < 1000);
    });
    const bool sawProgress = !done;
    seek.join();
    EXPECT_TRUE(progressed);
    EXPECT_TRUE(sawProgress) << "the status read waited for the whole seek";
    EXPECT_EQ(_emulator->GetRzxStatus().player.frame, 1000u);
}

/// endregion </Seek>

/// region <Snapshot blocks>

/// A snapshot block between input blocks replaces the machine where the
/// block before it ends (multiload, rollback point): no interrupt there, the
/// playback goes on with the next block's frames
TEST_F(RzxSession_Test, SnapshotBlockReplacesTheMachineBetweenBlocks)
{
    Create("48K");
    Z80TestRegisters second;
    second.pc = 0x9000;
    second.a = 0x42;
    RzxTestBuilder rzx;
    rzx.Creator("test", 1, 0)
        .Snapshot("z80", Z80Snapshot48K({0x18, 0xFE}, 0x8000))  // JR $: one fetch a loop
        .Input({{10, {}}})
        .Snapshot("z80", Z80Snapshot48K({0x00, 0x00, 0x18, 0xFE}, 0x9000, second))
        .Input({{2, {}}, {60000, {}}});
    ASSERT_TRUE(_emulator->PlayRzx(ParseBytes(rzx.Build()), "").Ok());

    for (int i = 0; i < 10; i++)
        Step();
    EXPECT_EQ(_cpu->pc, 0x8000) << "still the first block";
    const StepOutcome boundary = Step();
    EXPECT_FALSE(boundary.intAccepted) << "the snapshot, not an interrupt, ends the block";
    EXPECT_EQ(_cpu->pc, 0x9000);
    EXPECT_EQ(_cpu->a, 0x42);
    EXPECT_TRUE(_cpu->frameIntMasked) << "the playback still owns the interrupt";
    const SessionStatus status = _emulator->GetRzxStatus();
    EXPECT_TRUE(status.active);
    EXPECT_EQ(status.player.snapshotsApplied, 1u);
    EXPECT_EQ(status.player.frame, 1u);
    EXPECT_EQ(status.player.block, 1u) << "the second input block (0-based)";

    Step();
    Step();
    EXPECT_EQ(_cpu->pc, 0x9002);
    EXPECT_EQ(Player().Fetches(), 2u) << "fetches count on in the second block";
}

/// ignore_later_snapshots (SkoolKit flag 4) plays past the snapshot block
TEST_F(RzxSession_Test, IgnoreLaterSnapshotsKeepsTheMachine)
{
    Create("48K");
    Z80TestRegisters second;
    second.pc = 0x9000;
    RzxTestBuilder rzx;
    rzx.Snapshot("z80", Z80Snapshot48K({0x18, 0xFE}, 0x8000))
        .Input({{10, {}}})
        .Snapshot("z80", Z80Snapshot48K({0x00}, 0x9000, second))
        .Input({{60000, {}}});
    PlayerOptions options;
    options.ignoreLaterSnapshots = true;
    ASSERT_TRUE(_emulator->PlayRzx(ParseBytes(rzx.Build()), "", options).Ok());
    for (int i = 0; i < 12; i++)
        Step();
    EXPECT_EQ(_cpu->pc, 0x8000);
    EXPECT_EQ(_emulator->GetRzxStatus().player.snapshotsApplied, 0u);
    EXPECT_TRUE(_emulator->IsRzxPlaying());
}

/// A snapshot block for another machine stops the playback with the reason
TEST_F(RzxSession_Test, SnapshotBlockForAnotherMachineStops)
{
    Create("48K");
    RzxTestBuilder rzx;
    rzx.Snapshot("z80", Z80Snapshot48K({0x18, 0xFE}, 0x8000))
        .Input({{10, {}}})
        .Snapshot("sna", std::vector<uint8_t>(131103, 0))  // a 128K SNA
        .Input({{60000, {}}});
    ASSERT_TRUE(_emulator->PlayRzx(ParseBytes(rzx.Build()), "").Ok());
    for (int i = 0; i < 12; i++)
        Step();
    const SessionStatus status = _emulator->GetRzxStatus();
    EXPECT_FALSE(status.active);
    EXPECT_EQ(status.player.state, PlayerState::Stopped);
    EXPECT_NE(status.player.stopReason.find("another machine"), std::string::npos) << status.player.stopReason;
    EXPECT_FALSE(_cpu->frameIntMasked) << "the machine runs live";
}

/// Eric's first 300 frames, then SkoolKit's state at frame 300 as a snapshot
/// block with the next 300 frames: the machine after 600 frames equals
/// SkoolKit's on the same file; a seek back across the block boundary replays
/// through the snapshot. About 30 M T-states (a real recording)
TEST_F(RzxSession_Test, TwoBlockRecordingMatchesSkoolKit)
{
    Create("48K");
    ASSERT_TRUE(_emulator->PlayRzx(Fixture("cases/multiload-join.rzx")).Ok());
    PlayToFrame(0);
    EXPECT_EQ(_emulator->GetRzxStatus().player.snapshotsApplied, 1u);
    CompareWith(Fixture("cases/multiload-join-600.z80"), "48K");

    // From the end back to 450: the keyframe at 250 lies before the snapshot block
    std::string error;
    ASSERT_TRUE(_emulator->SeekRzx(450, &error)) << error;
    EXPECT_EQ(_emulator->GetRzxStatus().player.frame, 450u);
    CompareWith(Fixture("cases/multiload-join-450.z80"), "48K");
}

/// endregion </Snapshot blocks>

/// region <Real recordings>

struct ArchiveCase
{
    const char* name;
    const char* model;
};

class RzxArchive_Test : public RzxSession_Test, public ::testing::WithParamInterface<ArchiveCase>
{
protected:
    void PlayAndCompare(uint64_t frames, const std::string& oracle)
    {
        const ArchiveCase& param = GetParam();
        Create(param.model);
        const PlayResult result = _emulator->PlayRzx(Fixture(std::string("archive/") + param.name + ".rzx"));
        ASSERT_TRUE(result.Ok()) << result.message;
        PlayToFrame(frames);
        CompareWith(Fixture("oracle/" + oracle), param.model);
    }
};

/// The first 300 frames (6 s of play) match SkoolKit exactly. About 20 M
/// T-states per file: slower than the 50 ms budget, a real recording is the
/// point of the test
TEST_P(RzxArchive_Test, First300FramesMatchSkoolKit)
{
    PlayAndCompare(300, std::string(GetParam().name) + "-300.z80");
}

/// The whole recording (10-20 minutes of play, seconds per file): run with
/// UNREAL_RZX_FULL=1 (tools/verification/rzx)
TEST_P(RzxArchive_Test, WholeRecordingMatchesSkoolKit)
{
    const char* full = std::getenv("UNREAL_RZX_FULL");
    if (full == nullptr || std::strcmp(full, "1") != 0)
        GTEST_SKIP() << "set UNREAL_RZX_FULL=1";
    PlayAndCompare(0, std::string(GetParam().name) + "-end.z80");
}

INSTANTIATE_TEST_SUITE_P(Recordings, RzxArchive_Test,
                         ::testing::Values(ArchiveCase{"ericfloaters", "48K"}, ArchiveCase{"garfield", "48K"},
                                           ArchiveCase{"greenberet", "128K"}, ArchiveCase{"thundercats", "128K"},
                                           ArchiveCase{"dargonscrypt", "PLUS2"}),
                         [](const ::testing::TestParamInfo<ArchiveCase>& info) {
                             return std::string(info.param.name);
                         });


/// Every .rzx in $UNREAL_RZX_CORPUS plays to the end without a desync on the
/// model its start snapshot names; where <name>.end.z80 (SkoolKit's final
/// state, tools/verification/rzx) sits next to it, the final state matches
TEST_F(RzxSession_Test, CorpusFolderPlaysToTheEnd)
{
    const char* folder = std::getenv("UNREAL_RZX_CORPUS");
    if (folder == nullptr)
        GTEST_SKIP() << "set UNREAL_RZX_CORPUS=<folder of .rzx files>";

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() == ".rzx")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& path : files)
    {
        SCOPED_TRACE(path.filename().string());
        auto file = std::make_shared<File>();
        std::string error;
        ASSERT_TRUE(RzxReader::ParseFile(path.string(), *file, error)) << error;
        RzxSession::StartSnapshot start;
        PlayResult resolve;
        if (!RzxSession::ResolveStartSnapshot(*file, path.string(), {}, start, resolve))
        {
            ADD_FAILURE() << resolve.message;
            continue;
        }
        const TMemModel* model = Config::FindModelByEnum(start.machine.model);
        ASSERT_NE(model, nullptr);
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
        Create(model->ShortName, start.machine.ramKb);

        const PlayResult result = _emulator->PlayRzx(path.string());
        if (!result.Ok())
        {
            ADD_FAILURE() << result.message;
            continue;
        }
        PlayToFrame(0);
        std::filesystem::path oracle = path;
        oracle.replace_extension(".end.z80");
        if (std::filesystem::exists(oracle))
            CompareWith(oracle.string(), model->ShortName, start.machine.ramKb);
        std::printf("[ corpus   ] %s: %s, %llu frames, max drift %d T\n", path.filename().string().c_str(),
                    model->ShortName, static_cast<unsigned long long>(_emulator->GetRzxStatus().player.frame),
                    _emulator->GetRzxStatus().player.maxDrift);
    }
}

/// Pentagon 128 (Spectaculator): SkoolKit has no Pentagon, so the check is
/// the strict one alone - every frame's IN count and fetch count as recorded
TEST_F(RzxSession_Test, PentagonRecordingPlaysWithoutDesync)
{
    Create("PENTAGON");
    const PlayResult result = _emulator->PlayRzx(Fixture("archive/darkwingduck.rzx"));
    ASSERT_TRUE(result.Ok()) << result.message;
    const char* full = std::getenv("UNREAL_RZX_FULL");
    PlayToFrame(full && std::strcmp(full, "1") == 0 ? 0 : 300);
    EXPECT_EQ(_emulator->GetRzxStatus().player.desyncs, 0u);
}

/// The start snapshot stored outside the recording is found next to it; the
/// playback equals the embedded one (the same 300 frames of ericfloaters)
TEST_F(RzxSession_Test, ExternalSnapshotIsFoundNextToTheRecording)
{
    Create("48K");
    const PlayResult result = _emulator->PlayRzx(Fixture("external/ericfloaters-ext.rzx"));
    ASSERT_TRUE(result.Ok()) << result.message;
    PlayToFrame(300);
    CompareWith(Fixture("oracle/ericfloaters-300.z80"), "48K");
}

/// BIT n,(HL) takes flags 3 and 5 from MEMPTR, which a taken JR sets to its
/// target: frame 3963 of Dargon's Crypt ends with F = #74 (SkoolKit's fast
/// simulator, without MEMPTR on jumps, says #5C; tools/verification/rzx)
TEST_F(RzxSession_Test, BitHlFlagsComeFromMemptrAfterAJump)
{
    Create("PLUS2");
    const PlayResult result = _emulator->PlayRzx(Fixture("cases/memptr-bit-hl.rzx"));
    ASSERT_TRUE(result.Ok()) << result.message;
    PlayToFrame(1);
    EXPECT_EQ(_cpu->f, 0x74);
    CompareWith(Fixture("cases/memptr-bit-hl-1.z80"), "PLUS2");
}

/// Every place that opens snapshots opens a recording: LoadSnapshot plays it
/// on this machine (no model switch) and the extension is listed
TEST_F(RzxSession_Test, LoadSnapshotOfARecordingPlaysIt)
{
    Create("48K");
    const std::vector<std::string> extensions = Emulator::SupportedSnapshotExtensions();
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "rzx"), extensions.end());

    ASSERT_TRUE(_emulator->LoadSnapshot(Fixture("archive/garfield.rzx")));
    EXPECT_TRUE(_emulator->IsRzxPlaying());
    EXPECT_EQ(_context->coreState.snapshotFilePath.find("garfield.rzx") != std::string::npos, true)
        << "the recording, not its temporary start snapshot, is the loaded file";

    EXPECT_FALSE(_emulator->LoadSnapshot(Fixture("archive/greenberet.rzx"))) << "a 128K recording on a 48K";
}

/// One recording against one expected state (tools/verification/rzx):
/// UNREAL_RZX_FILE=<.rzx>, UNREAL_RZX_ORACLE=<.z80>, UNREAL_RZX_STOP=<frames,
/// 0 or unset: the whole recording>. Bisecting the stop frame against SkoolKit's
/// --stop dumps finds the first frame where two emulators disagree
TEST_F(RzxSession_Test, OneFileAgainstAnOracle)
{
    const char* path = std::getenv("UNREAL_RZX_FILE");
    const char* oracle = std::getenv("UNREAL_RZX_ORACLE");
    if (path == nullptr || oracle == nullptr)
        GTEST_SKIP() << "set UNREAL_RZX_FILE and UNREAL_RZX_ORACLE";
    const char* stop = std::getenv("UNREAL_RZX_STOP");
    const uint64_t frames = stop ? std::strtoull(stop, nullptr, 10) : 0;

    auto file = std::make_shared<File>();
    std::string error;
    ASSERT_TRUE(RzxReader::ParseFile(path, *file, error)) << error;
    RzxSession::StartSnapshot start;
    PlayResult resolve;
    ASSERT_TRUE(RzxSession::ResolveStartSnapshot(*file, path, {}, start, resolve)) << resolve.message;
    const TMemModel* model = Config::FindModelByEnum(start.machine.model);
    ASSERT_NE(model, nullptr);
    Create(model->ShortName, start.machine.ramKb);
    const PlayResult result = _emulator->PlayRzx(path);
    ASSERT_TRUE(result.Ok()) << result.message;
    PlayToFrame(frames);
    CompareWith(oracle, model->ShortName, start.machine.ramKb);
}

/// endregion </Real recordings>
