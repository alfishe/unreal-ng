/// @file analysisl1_test.cpp
/// @brief L1 of the analysis layers (docs/emulator/analysis/analysis-layers.md, "L1: code behaviour") end to end: a
/// fixture program (testdata/analysis/fixtures/*.asm, assembled here with Z80TextAssembler) runs a few frames under a
/// TTD recording, the recording is captured as a TraceBundle and replayed on an analysis instance (ReplayBuild), and
/// the L1 it folds is checked: basic blocks, the call graph (tail jumps, JP (HL) targets, PUSH + RET dispatch,
/// interrupts), loops (trips, induction, strides, delay loops), base + offset fields, the data flow answers, branch
/// conditions, live registers, paging and determinism.

#include "pch.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/assembler/z80textassembler.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

using namespace analysis;

namespace
{

struct Program
{
    AsmResult assembled;
    uint32_t Symbol(const std::string& name) const
    {
        const auto it = assembled.symbols.find(name);
        EXPECT_NE(it, assembled.symbols.end()) << "no label " << name;
        return it != assembled.symbols.end() ? it->second : 0;
    }
};

std::string ReadFixture(const std::string& name)
{
    std::ifstream file(TestPathHelper::GetTestDataPath("analysis/fixtures/" + name), std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

class AnalysisL1_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    Z80* _z80 = nullptr;
    Program _program;
    TraceBundle _bundle;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    /// Make the machine, load the fixture and point the CPU at its `start`
    void Load(const std::string& fixture, const std::string& model = "48K")
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _memory = _context->pMemory;
        _z80 = _context->pCore->GetZ80();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _emulator->GetFeatureManager()->setFeature(Features::kSoundGeneration, false);
        if (_context->pSoundManager)
            _context->pSoundManager->mute();
        _memory->UpdateFeatureCache();

        _program.assembled = Z80TextAssembler().Assemble(ReadFixture(fixture), 0x8000);
        ASSERT_TRUE(_program.assembled.ok) << fixture << ":" << _program.assembled.error.line << ": "
                                           << _program.assembled.error.message;
        for (const AsmSourceLine& line : _program.assembled.lines)
            for (size_t i = 0; i < line.bytes.size(); ++i)
                _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(line.address + i), line.bytes[i]);
        _z80->pc = static_cast<uint16_t>(_program.Symbol("start"));
        _z80->sp = 0x9F00;
        _z80->iff1 = 0;
        _z80->iff2 = 0;
    }

    /// Record @p frames frames of the loaded program and capture the bundle
    void Record(unsigned frames)
    {
        ttd::TimeTravelController* ttd = _context->pTimeTravelController;
        ASSERT_NE(ttd, nullptr);
        ASSERT_TRUE(ttd->StartRecording()) << ttd->LastStartError();
        _emulator->RunNFrames(frames);
        std::string error;
        ASSERT_TRUE(TraceBundle::Capture(*_emulator, _bundle, error)) << error;
    }

    L1Result Build(uint64_t salt = 0)
    {
        L1Result l1;
        ReplayOptions options;
        options.salt = salt;
        std::string error;
        EXPECT_TRUE(ReplayBuild(_bundle, options, l1, error)) << error;
        EXPECT_EQ(l1.replay.diverged, 0u) << "the replay left the recording at frame " << l1.replay.firstDivergence;
        return l1;
    }

    /// Load, record and build once per fixture and length: tests reading the same fixture share its L1 (the
    /// machine, the recording and the replay are ~25 ms)
    L1Result Run(const std::string& fixture, unsigned frames = 3, const std::string& model = "48K",
                 const std::function<void()>& prepare = {})
    {
        const std::string key = fixture + "/" + std::to_string(frames) + "/" + model;
        if (const auto it = Cache().find(key); it != Cache().end())
        {
            _program = it->second.first;
            return it->second.second;
        }
        Load(fixture, model);
        if (::testing::Test::HasFatalFailure())
            return {};
        if (prepare)
            prepare();
        Record(frames);
        if (::testing::Test::HasFatalFailure())
            return {};
        L1Result l1 = Build();
        if (!::testing::Test::HasFailure())
            Cache().emplace(key, std::make_pair(_program, l1));
        return l1;
    }

    static std::map<std::string, std::pair<Program, L1Result>>& Cache()
    {
        static std::map<std::string, std::pair<Program, L1Result>> cache;
        return cache;
    }
    static void TearDownTestSuite() { Cache().clear(); }

    uint16_t At(const std::string& label) const { return static_cast<uint16_t>(_program.Symbol(label)); }

    static const Routine* RoutineAt(const L1Result& l1, uint16_t z80)
    {
        for (const Routine& r : l1.routines)
            if (r.z80 == z80)
                return &r;
        return nullptr;
    }
    static const Block* BlockAt(const L1Result& l1, uint16_t z80)
    {
        for (const Block& b : l1.blocks)
            if (b.z80 == z80)
                return &b;
        return nullptr;
    }
    static const Loop* LoopAt(const L1Result& l1, uint16_t z80)
    {
        for (const Loop& loop : l1.loops)
            if (loop.z80 == z80)
                return &loop;
        return nullptr;
    }
    static const CallEdge* CallBetween(const L1Result& l1, const Routine* caller, const Routine* callee, CallKind kind)
    {
        if (caller == nullptr || callee == nullptr)
            return nullptr;
        for (const CallEdge& e : l1.calls)
            if (e.caller == l1.IndexOf(*caller) && e.callee == l1.IndexOf(*callee) && e.kind == kind)
                return &e;
        return nullptr;
    }
    static int32_t StepOf(const Loop& loop, const std::string& reg)
    {
        for (const Induction& i : loop.induction)
            if (i.reg == reg)
                return i.step;
        return 0;
    }
    static const Stride* StrideAt(const Loop& loop, uint16_t z80)
    {
        for (const Stride& s : loop.strides)
            if (s.z80 == z80)
                return &s;
        return nullptr;
    }
};

TEST_F(AnalysisL1_Test, CfgBasicBlocks)
{
    const L1Result l1 = Run("cfg.asm", 1);
    ASSERT_FALSE(HasFailure());
    std::set<uint16_t> expected;
    for (const auto& [name, value] : _program.assembled.symbols)
        if (name.rfind("blk_", 0) == 0)
            expected.insert(static_cast<uint16_t>(value));
    std::set<uint16_t> starts;
    for (const Block& b : l1.blocks)
        starts.insert(b.z80);
    EXPECT_EQ(starts, expected);

    const Routine* routine = RoutineAt(l1, At("start"));
    ASSERT_NE(routine, nullptr);
    EXPECT_EQ(l1.routines.size(), 1u);
    EXPECT_EQ(routine->blocks.size(), expected.size());

    // The loop header has two predecessors' edges into it: the fall from main, the DJNZ back edge
    const Block* next = BlockAt(l1, At("blk_next"));
    ASSERT_NE(next, nullptr);
    bool back = false, fall = false;
    for (const Edge& e : next->successors)
    {
        back |= l1.blocks[e.to].z80 == At("blk_loop") && e.kind == EdgeKind::Taken;
        fall |= l1.blocks[e.to].z80 == At("blk_again") && e.kind == EdgeKind::Fallthrough;
    }
    EXPECT_TRUE(back);
    EXPECT_TRUE(fall);
}

TEST_F(AnalysisL1_Test, CallGraphTailJump)
{
    const L1Result l1 = Run("calls.asm", 1);
    ASSERT_FALSE(HasFailure());
    const Routine* main = RoutineAt(l1, At("start"));
    const Routine* a = RoutineAt(l1, At("sub_a"));
    const Routine* b = RoutineAt(l1, At("sub_b"));
    ASSERT_NE(main, nullptr);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(a->kinds & entry::kCall);
    EXPECT_TRUE(b->kinds & entry::kCall);
    EXPECT_TRUE(b->kinds & entry::kJump) << EntryKindNames(b->kinds);
    const CallEdge* tail = CallBetween(l1, a, b, CallKind::Tail);
    ASSERT_NE(tail, nullptr);
    EXPECT_GT(tail->count, 0u);
    ASSERT_NE(CallBetween(l1, main, a, CallKind::Call), nullptr);
    const CallEdge* direct = CallBetween(l1, main, b, CallKind::Call);
    ASSERT_NE(direct, nullptr);
    // sub_b returns to main for both ways in: one entry per call made
    EXPECT_EQ(b->profile.entries, direct->count + tail->count);
    EXPECT_EQ(a->profile.minDepth, 1);
}

TEST_F(AnalysisL1_Test, IndirectTargets)
{
    const L1Result l1 = Run("indirect.asm", 1);
    ASSERT_FALSE(HasFailure());
    const Block* dispatch = BlockAt(l1, At("dispatch"));
    ASSERT_NE(dispatch, nullptr);
    std::set<uint16_t> targets;
    for (const auto& [target, count] : dispatch->indirectTargets)
    {
        targets.insert(target);
        EXPECT_GT(count, 0u);
    }
    EXPECT_EQ(targets, (std::set<uint16_t>{At("h0"), At("h1"), At("h2"), At("h3")}));
    const Routine* from = l1.RoutineOf(dispatch->start);
    ASSERT_NE(from, nullptr);
    for (const char* name : {"h0", "h1", "h2", "h3"})
    {
        const Routine* handler = RoutineAt(l1, At(name));
        ASSERT_NE(handler, nullptr) << name;
        EXPECT_TRUE(handler->kinds & entry::kIndirect) << name;
        EXPECT_NE(CallBetween(l1, from, handler, CallKind::Indirect), nullptr) << name;
    }
}

TEST_F(AnalysisL1_Test, PushRetDispatch)
{
    const L1Result l1 = Run("pushret.asm", 1);
    ASSERT_FALSE(HasFailure());
    // t0 and t1 jump back to main, so main is a routine of its own (code two routines run into)
    const Routine* main = l1.RoutineAtZ80(At("go"));
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->z80, At("main"));
    EXPECT_TRUE(main->kinds & entry::kShared) << EntryKindNames(main->kinds);
    for (const char* name : {"t0", "t1"})
    {
        const Routine* target = RoutineAt(l1, At(name));
        ASSERT_NE(target, nullptr) << name;
        EXPECT_TRUE(target->kinds & entry::kDispatch) << name << ": " << EntryKindNames(target->kinds);
        const CallEdge* edge = CallBetween(l1, main, target, CallKind::Dispatch);
        ASSERT_NE(edge, nullptr) << name;
        EXPECT_GT(edge->count, 0u);
    }
}

TEST_F(AnalysisL1_Test, LoopTripAndStride)
{
    const L1Result l1 = Run("loops.asm", 1);
    ASSERT_FALSE(HasFailure());
    const Loop* copy = LoopAt(l1, At("copy"));
    ASSERT_NE(copy, nullptr);
    EXPECT_GT(copy->instances, 1u);
    EXPECT_EQ(copy->minTrips, 10u);
    EXPECT_EQ(copy->maxTrips, 10u);
    ASSERT_EQ(copy->trips.size(), 1u);
    EXPECT_EQ(copy->trips[0].first, 10u);
    EXPECT_EQ(StepOf(*copy, "HL"), 1);
    EXPECT_EQ(StepOf(*copy, "DE"), 2);
    EXPECT_EQ(StepOf(*copy, "B"), -1);
    const Stride* read = StrideAt(*copy, At("copy"));
    ASSERT_NE(read, nullptr);
    EXPECT_FALSE(read->write);
    EXPECT_TRUE(read->constant);
    EXPECT_EQ(read->stride, 1);
    const Stride* write = StrideAt(*copy, static_cast<uint16_t>(At("copy") + 1));
    ASSERT_NE(write, nullptr);
    EXPECT_TRUE(write->write);
    EXPECT_TRUE(write->constant);
    EXPECT_EQ(write->stride, 2);
    EXPECT_FALSE(copy->delay);

    const Loop* six = LoopAt(l1, At("six"));
    ASSERT_NE(six, nullptr);
    EXPECT_EQ(six->minTrips, 6u);
    EXPECT_EQ(six->maxTrips, 6u);
    EXPECT_EQ(StepOf(*six, "HL"), -1);
}

TEST_F(AnalysisL1_Test, DelayLoops)
{
    const L1Result l1 = Run("loops.asm", 1);
    ASSERT_FALSE(HasFailure());
    const Loop* delay = LoopAt(l1, At("delay"));
    ASSERT_NE(delay, nullptr);
    EXPECT_TRUE(delay->delay);
    EXPECT_EQ(delay->minTrips, 40u);
    const Loop* copy = LoopAt(l1, At("copy"));
    ASSERT_NE(copy, nullptr);
    EXPECT_FALSE(copy->delay);
    const Loop* shift = LoopAt(l1, At("shift"));
    ASSERT_NE(shift, nullptr);
    EXPECT_FALSE(shift->delay) << "it writes E: a computation, not a delay";
}

TEST_F(AnalysisL1_Test, BaseOffsetIx)
{
    const L1Result l1 = Run("fields.asm", 1);
    ASSERT_FALSE(HasFailure());
    const Routine* walk = RoutineAt(l1, At("walk_ix"));
    ASSERT_NE(walk, nullptr);
    const BaseOffsetPattern* found = nullptr;
    for (const BaseOffsetPattern& p : l1.patterns)
        if (p.routine == l1.IndexOf(*walk) && p.base == BaseReg::IX)
            found = &p;
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->offsets, (std::vector<int16_t>{0, 2, 3}));
    EXPECT_EQ(found->basesSeen, 4u);
    EXPECT_EQ(found->step, 5);
    EXPECT_TRUE(found->reads);
    EXPECT_TRUE(found->writes);
    ASSERT_FALSE(found->bases.empty());
    EXPECT_EQ(found->bases.front(), At("records"));
}

TEST_F(AnalysisL1_Test, BaseOffsetHl)
{
    const L1Result l1 = Run("fields.asm", 1);
    ASSERT_FALSE(HasFailure());
    const Routine* walk = RoutineAt(l1, At("walk_hl"));
    ASSERT_NE(walk, nullptr);
    const BaseOffsetPattern* found = nullptr;
    for (const BaseOffsetPattern& p : l1.patterns)
        if (p.routine == l1.IndexOf(*walk) && p.base == BaseReg::HL)
            found = &p;
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->offsets, (std::vector<int16_t>{0, 1, 4}));
    EXPECT_EQ(found->step, 5);
    EXPECT_TRUE(found->reads);
    EXPECT_TRUE(found->writes);
}

TEST_F(AnalysisL1_Test, DataFlowAddressImmediate)
{
    const L1Result l1 = Run("dataflow.asm", 1);
    ASSERT_FALSE(HasFailure());
    const ImmediateAddress* found = nullptr;
    for (const ImmediateAddress& a : l1.immediateAddresses)
        if (a.z80 == At("imm"))
            found = &a;
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->value, 0xC000);
    EXPECT_EQ(found->useZ80, At("use"));
    EXPECT_GT(found->uses, 0u);
}

TEST_F(AnalysisL1_Test, DataFlowCompareConstant)
{
    const L1Result l1 = Run("dataflow.asm", 1);
    ASSERT_FALSE(HasFailure());
    const ConstantCompare* found = nullptr;
    for (const ConstantCompare& c : l1.constantCompares)
        if (c.z80 == At("cmp"))
            found = &c;
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->constant, 0x20);
    EXPECT_EQ(found->source, SourceKind::Memory);
    EXPECT_EQ(found->address, 0xC010);
    EXPECT_EQ(found->loaderZ80, At("load"));
}

TEST_F(AnalysisL1_Test, DataFlowPageTable)
{
    const L1Result l1 = Run("dataflow.asm", 1);
    ASSERT_FALSE(HasFailure());
    const PageTable* found = nullptr;
    for (const PageTable& t : l1.pageTables)
        if (t.z80 == At("page"))
            found = &t;
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->table, 0xC100);
    EXPECT_EQ(found->readerZ80, At("read"));
    EXPECT_GT(found->count, 0u);
}

TEST_F(AnalysisL1_Test, BranchConditionVariable)
{
    const L1Result l1 = Run("dataflow.asm", 1);
    ASSERT_FALSE(HasFailure());
    const BranchCondition* found = nullptr;
    for (const BranchCondition& b : l1.branches)
        if (b.z80 == At("branch"))
            found = &b;
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->cond, 0);  // NZ
    EXPECT_GT(found->taken, 0u);
    EXPECT_GT(found->notTaken, 0u);
    EXPECT_EQ(found->producerZ80, At("test"));
    EXPECT_EQ(found->producerClass, OpClass::Alu);
    EXPECT_EQ(found->tested, SourceKind::Memory);
    EXPECT_EQ(found->address, At("flag"));
}

TEST_F(AnalysisL1_Test, InterruptProfile)
{
    const unsigned frames = 6;
    const L1Result l1 = Run("interrupt.asm", frames, "48K", [this] {
        for (uint16_t a = 0x9000; a <= 0x9100; ++a)
            _memory->DirectWriteToZ80Memory(a, 0x91);
    });
    ASSERT_FALSE(HasFailure());
    const Routine* handler = RoutineAt(l1, At("handler"));
    ASSERT_NE(handler, nullptr);
    EXPECT_TRUE(handler->kinds & entry::kInterrupt) << EntryKindNames(handler->kinds);
    EXPECT_GE(handler->profile.entries, frames - 1u);
    EXPECT_LE(handler->profile.entries, frames + 1u);
    EXPECT_GE(handler->profile.activeFrames, frames - 1u);
    ASSERT_EQ(handler->profile.portsIn.size(), 1u);
    EXPECT_EQ(handler->profile.portsIn[0] & 0xFF, 0xFE);
    EXPECT_EQ(l1.replay.interrupts, handler->profile.entries);
    // The interrupted code (main's HALT) to the handler
    const Routine* main = RoutineAt(l1, At("start"));
    EXPECT_NE(CallBetween(l1, main, handler, CallKind::Interrupt), nullptr);
    // The handler writes ticks: one write interval holding it
    bool writesTicks = false;
    for (const Interval& w : handler->profile.writes)
        writesTicks |= w.z80 <= At("ticks") && At("ticks") <= w.z80 + (w.hi - w.lo);
    EXPECT_TRUE(writesTicks);
}

TEST_F(AnalysisL1_Test, RegisterLiveInOut)
{
    const L1Result l1 = Run("registers.asm", 1);
    ASSERT_FALSE(HasFailure());
    const Routine* add = RoutineAt(l1, At("add_ab"));
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(L1Result::RegNames(add->profile.liveIn), "A B");
    EXPECT_EQ(L1Result::RegNames(add->profile.liveOut), "A");
    const Routine* keep = RoutineAt(l1, At("keep_bc"));
    ASSERT_NE(keep, nullptr);
    EXPECT_EQ(keep->profile.written & (reg::kB | reg::kC), 0) << L1Result::RegNames(keep->profile.written);
    EXPECT_EQ(keep->profile.liveIn & (reg::kB | reg::kC), 0) << "a PUSH that saves a register is no use of it";
}

TEST_F(AnalysisL1_Test, PagedRoutinesSeparate)
{
    const L1Result l1 = Run("paged.asm", 1, "128K", [this] {
        const uint16_t code1 = At("code1");
        const uint16_t code3 = At("code3");
        uint8_t* page1 = _memory->RAMPageAddress(1);
        uint8_t* page3 = _memory->RAMPageAddress(3);
        ASSERT_NE(page1, nullptr);
        ASSERT_NE(page3, nullptr);
        for (uint16_t i = 0; i < code3 - code1; ++i)
            page1[i] = _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(code1 + i));
        for (uint16_t i = 0; i < 3; ++i)
            page3[i] = _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(code3 + i));
    });
    ASSERT_FALSE(HasFailure());
    std::vector<const Routine*> at;
    for (const Routine& r : l1.routines)
        if (r.z80 == 0xC000)
            at.push_back(&r);
    ASSERT_EQ(at.size(), 2u);
    EXPECT_NE(KeyPage(at[0]->entry), KeyPage(at[1]->entry));
    EXPECT_EQ(at[0]->instructions + at[1]->instructions, 5u);
    EXPECT_LE(std::max(at[0]->profile.entries, at[1]->profile.entries) -
                  std::min(at[0]->profile.entries, at[1]->profile.entries),
              1u)
        << "called in turn (the frame may end between the two)";
    // Both are explained apart by page
    EXPECT_NE(l1.RoutineAtZ80(0xC000, KeyPage(at[0]->entry)), l1.RoutineAtZ80(0xC000, KeyPage(at[1]->entry)));
}

TEST_F(AnalysisL1_Test, ReplayDeterministic)
{
    Load("determinism.asm");
    ASSERT_FALSE(HasFatalFailure());
    Record(2);
    ASSERT_FALSE(HasFatalFailure());
    const L1Result first = Build(0x1234);
    const L1Result second = Build(0x9876FEDC);
    const std::string a = first.ToJson();
    const std::string b = second.ToJson();
    EXPECT_GT(a.size(), 1000u);
    EXPECT_GE(first.loops.size(), 24u);
    if (a != b)
    {
        // Only the first difference, not two whole documents
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at])
            ++at;
        const size_t from = at > 80 ? at - 80 : 0;
        ADD_FAILURE() << "two replays of one recording serialize differently at byte " << at << ":\n  "
                      << a.substr(from, 200) << "\n  " << b.substr(from, 200);
    }
}

TEST_F(AnalysisL1_Test, FactSheetBudget)
{
    const L1Result l1 = Run("fields.asm", 1);
    ASSERT_FALSE(HasFailure());
    for (const Routine& r : l1.routines)
    {
        const std::string sheet = l1.FactSheet(
            r, [](const Instruction&) { return std::string("ld a,(ix+$00)"); }, [](uint16_t) { return std::string(); });
        EXPECT_LE(sheet.size(), 1024u);
        EXPECT_NE(sheet.find("Routine"), std::string::npos);
        const auto lines = static_cast<size_t>(std::count(sheet.begin(), sheet.end(), '\n'));
        EXPECT_LE(lines, 24u + 12u);
    }
}
