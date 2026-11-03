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
#include "debugger/analyzers/analysis/l1builder.h"
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

/// A character code, AND A and LD L,A after its load, the low byte of the glyph addresses: an index; the message
/// pointer, a word, none
TEST_F(AnalysisL1_Test, DataFlowIndexUse)
{
    const L1Result l1 = Run("print-proportional.asm", 1);
    ASSERT_FALSE(HasFailure());
    const uint16_t message = At("message");
    bool width = false, glyph = false;
    for (const IndexUse& u : l1.indexes)
    {
        EXPECT_NE(u.z80, At("next")) << "the message pointer is no index";
        if (u.selectorZ80 < message || u.selectorZ80 >= message + 17)
            continue;
        const uint8_t code = static_cast<uint8_t>("PROPORTIONAL TEXT"[u.selectorZ80 - message]);
        if (u.z80 == At("width"))
        {
            width = true;
            EXPECT_EQ(u.lo, 0xC000 + code);
        }
        if (u.z80 == At("glyph"))
        {
            glyph = true;
            EXPECT_EQ(u.lo, 0xC100 + code);
            EXPECT_EQ(u.hi, 0xC300 + code);
        }
    }
    EXPECT_TRUE(width);
    EXPECT_TRUE(glyph);
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

TEST_F(AnalysisL1_Test, CodeArrivalsOnePerWriter)
{
    const L1Result l1 = Run("stages.asm", 5);
    ASSERT_FALSE(HasFailure());
    const uint16_t stage1 = At("stage1");
    const auto stage1Bytes = static_cast<uint16_t>(At("stage1_end") - stage1);
    ASSERT_EQ(l1.arrivals.size(), 2u) << "the copied stage and the depacked one; running on through either is no new "
                                         "arrival, nor the depacker's second writer, nor coming back from a call";
    const CodeArrival& first = l1.arrivals[0];
    EXPECT_EQ(first.z80, 0xA000);
    EXPECT_EQ(first.writerZ80, At("copy1"));
    EXPECT_EQ(first.fromZ80, static_cast<uint16_t>(At("copy1") + 2)) << "the JP after the LDIR";
    EXPECT_EQ(first.writtenStart, 0xA000);
    EXPECT_EQ(first.writtenEnd, static_cast<uint16_t>(0xA000 + stage1Bytes - 1));
    const CodeArrival& second = l1.arrivals[1];
    EXPECT_EQ(second.z80, 0xB000);
    EXPECT_EQ(second.writerZ80, static_cast<uint16_t>(0xA000 + At("put2") - stage1)) << "LD (DE),A wrote its opcode";
    EXPECT_GT(second.frame * 100000 + second.tInFrame, first.frame * 100000 + first.tInFrame);
    // The code loaded before it ran is no self-modification
    const ByteUse* use = l1.UseAt(l1.StartKey(0xB000));
    ASSERT_NE(use, nullptr);
    EXPECT_EQ(use->flags & use::kSmc, 0) << "written, then run: not modified after it ran";

    // The heatmap counters in the JSON: the counter stage 2 increments, read and written once a pass
    const PhysKey counter = l1.StartKey(At("counter"));
    const ByteUse* counted = l1.UseAt(counter);
    ASSERT_NE(counted, nullptr);
    ASSERT_GT(counted->writes, 1u);
    const StateNode doc = l1.ToStateNode();
    const StateNode* pages = doc.find("byte_counts");
    ASSERT_NE(pages, nullptr);
    bool found = false;
    for (const StateNode& page : pages->items)
    {
        const StateNode* number = page.find("page");
        if (number == nullptr || number->i != KeyPage(counter))
            continue;
        found = true;
        ASSERT_EQ(page.find("writes")->items.size(), 0x4000u);
        EXPECT_EQ(page.find("writes")->items[KeyOffset(counter)].i, static_cast<int64_t>(counted->writes));
        EXPECT_EQ(page.find("reads")->items[KeyOffset(counter)].i, static_cast<int64_t>(counted->reads));
        EXPECT_GE(page.find("last_frame")->items[KeyOffset(counter)].i, 2) << "still counting in the last frames";
    }
    EXPECT_TRUE(found);
}

// A host trap runs as one instruction: the fast tape load writes a whole block at the ROM loader's entry. Every
// write is the instruction's, past the per-instruction event buffer too.
TEST(AnalysisL1Builder_Test, TrapWritesPastTheEventBuffer)
{
    L1Builder builder([](uint16_t z80) { return static_cast<uint16_t>(z80 >> 14); }, 1);
    L1Builder::Regs regs;
    constexpr int kWrites = 200;
    static_assert(kWrites > L1Builder::kMaxEvents, "more than one instruction's events");
    regs.pc = 0x0556;
    builder.OnStart(0x0556, 0, regs, 0, 0, false, false, false, 0);
    builder.OnBus('R', 0x0556, 0x00);  // the fetch of a NOP, then the trap's stores
    for (int i = 0; i < kWrites; ++i)
        builder.OnBus('W', static_cast<uint16_t>(0x9000 + i), static_cast<uint8_t>(i));
    regs.pc = 0x0557;
    builder.OnStart(0x0557, 0, regs, 100, 0, false, false, false, 0);
    builder.OnBus('R', 0x0557, 0x00);
    regs.pc = 0x0558;
    builder.Flush(regs, 104);
    ReplaySummary summary;
    summary.lastFrame = 1;
    summary.frames = 1;
    const L1Result l1 = builder.Build(summary, [](uint16_t page) { return "page " + std::to_string(page); });
    for (int i : {0, L1Builder::kMaxEvents, kWrites - 1})
    {
        const ByteUse* use = l1.UseAt(MakeKey(2, static_cast<uint16_t>(0x9000 + i)));
        ASSERT_NE(use, nullptr) << "byte " << i;
        EXPECT_EQ(use->writes, 1u) << "byte " << i;
        EXPECT_EQ(use->lastWriter, MakeKey(0, 0x0556)) << "byte " << i;
    }
}

TEST_F(AnalysisL1_Test, ToolEditWritesAreUses)
{
    // A tool edit (the fast tape load, a script's poke) reaches memory past the bus. The replay applies it from the
    // recording; L1 sees the bytes it changed as writes of the instruction pending at the edit
    Load("stages.asm");
    ASSERT_FALSE(HasFatalFailure());
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_TRUE(ttd->StartRecording()) << ttd->LastStartError();
    _emulator->RunNFrames(1);
    ASSERT_GE(_z80->pc, At("wait0")) << "the edit lands in the first wait";
    ASSERT_LT(_z80->pc, At("helper"));
    ttd->BeginToolEdit();
    for (uint16_t k = 0; k < 4; ++k)
        _memory->ToolWriteToZ80Memory(static_cast<uint16_t>(0xC100 + k), static_cast<uint8_t>(k + 1));
    _memory->ToolWriteToZ80Memory(0xC104, 0x00);  // the value it held: no change, no write
    ttd->EndToolEdit("test");
    _emulator->RunNFrames(2);
    std::string error;
    ASSERT_TRUE(TraceBundle::Capture(*_emulator, _bundle, error)) << error;
    const L1Result l1 = Build();
    for (uint16_t k = 0; k < 4; ++k)
    {
        const ByteUse* use = l1.UseAt(l1.StartKey(static_cast<uint16_t>(0xC100 + k)));
        ASSERT_NE(use, nullptr) << "byte " << k;
        EXPECT_EQ(use->writes, 1u) << "byte " << k;
        EXPECT_GE(use->lastWriter, l1.StartKey(At("wait0"))) << "byte " << k;
        EXPECT_LT(use->lastWriter, l1.StartKey(At("helper"))) << "byte " << k;
    }
    const ByteUse* same = l1.UseAt(l1.StartKey(0xC104));
    EXPECT_TRUE(same == nullptr || same->writes == 0) << "a byte the edit left as it was";
}

TEST_F(AnalysisL1_Test, FastTapeLoadWritesAreUses)
{
    // The fast tape load is a trap at the ROM's LD-BYTES entry ($0556): the recording keeps it as a tool edit with
    // the block's bytes. The replay applies the edit; every loaded byte, in each bank the block crosses, is a write
    // of the trapped instruction
    Load("stages.asm");
    ASSERT_FALSE(HasFatalFailure());
    constexpr uint16_t kAt = 0x7F80;  // banks 1 and 2
    constexpr uint16_t kLength = 0x0100;
    std::vector<uint8_t> block(kLength + 2);
    block[0] = 0xFF;
    uint8_t checksum = 0xFF;
    for (size_t i = 1; i <= kLength; ++i)
    {
        block[i] = static_cast<uint8_t>(i * 7 + 3);
        checksum ^= block[i];
    }
    block[kLength + 1] = checksum;
    const std::string tap = TestPathHelper::GetUniqueTestScratchPath("fastload-uses.tap");
    {
        std::ofstream out(tap, std::ios::binary);
        const uint8_t length[2] = {static_cast<uint8_t>(block.size() & 0xFF), static_cast<uint8_t>(block.size() >> 8)};
        out.write(reinterpret_cast<const char*>(length), 2);
        out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()));
    }
    ASSERT_TRUE(_emulator->LoadTape(tap));
    // The trap checks the ROM's LD-BYTES signature
    uint8_t* rom = _memory->GetPhysicalAddressForZ80Page(0);
    const uint8_t signature1[8] = {0x14, 0x08, 0x15, 0xF3, 0x3E, 0x0F, 0xD3, 0xFE};
    const uint8_t signature2[3] = {0xDB, 0xFE, 0x1F};
    std::memcpy(rom + 0x0556, signature1, sizeof(signature1));
    std::memcpy(rom + 0x0556 + 12, signature2, sizeof(signature2));
    // LD A,#FF; LD DE,kLength; LD IX,kAt; SCF; CALL #0556; loop: JR loop
    const uint8_t program[] = {0x3E, 0xFF, 0x11, kLength & 0xFF, kLength >> 8, 0xDD, 0x21, kAt & 0xFF, kAt >> 8,
                               0x37, 0xCD, 0x56, 0x05, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(program); ++i)
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xC000 + i), program[i]);
    _z80->pc = 0xC000;
    ASSERT_TRUE(_emulator->GetFeatureManager()->setFeature(Features::kFastTape, true));
    Record(2);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(_memory->DirectReadFromZ80Memory(kAt), block[1]) << "the trap loaded the block";
    const L1Result l1 = Build();
    for (const uint16_t address : {kAt, static_cast<uint16_t>(0x7FFF), static_cast<uint16_t>(0x8000),
                                   static_cast<uint16_t>(kAt + kLength - 1)})
    {
        const ByteUse* use = l1.UseAt(l1.StartKey(address));
        ASSERT_NE(use, nullptr) << std::hex << address;
        EXPECT_EQ(use->writes, 1u) << std::hex << address;
        // The trap runs the loader at its entry's fetch and returns: the writer is the CALL into it ($C00A)
        EXPECT_EQ(use->lastWriter, l1.StartKey(0xC00A)) << std::hex << address;
        EXPECT_EQ(use->flags & use::kStackWrite, 0) << "a data write, not a push: " << std::hex << address;
    }
}

TEST_F(AnalysisL1_Test, PatchIsNotOverlay)
{
    const L1Result l1 = Run("patch-overlay.asm", 2);
    ASSERT_FALSE(HasFailure());
    const ByteUse* patched = l1.UseAt(l1.StartKey(static_cast<uint16_t>(At("patched") + 1)));
    ASSERT_NE(patched, nullptr);
    EXPECT_NE(patched->flags & use::kSmc, 0) << "written after it ran";
    EXPECT_FALSE(patched->overlaid) << "and run again after each write: a patch";
    for (uint16_t a = At("once"); a < At("once_end"); ++a)
    {
        const ByteUse* use = l1.UseAt(l1.StartKey(a));
        ASSERT_NE(use, nullptr);
        EXPECT_NE(use->flags & use::kSmc, 0) << std::hex << a;
        EXPECT_TRUE(use->overlaid) << "ran once, then the buffer: never run again " << std::hex << a;
    }
    EXPECT_NE(l1.ToJson().find("\"byte_use\""), std::string::npos);
}

TEST_F(AnalysisL1_Test, WindowFromAnArrival)
{
    // Its own recording: the window replays _bundle, which a cached Run does not set
    Load("stages.asm");
    ASSERT_FALSE(HasFatalFailure());
    Record(5);
    ASSERT_FALSE(HasFatalFailure());
    const L1Result whole = Build();
    ASSERT_EQ(whole.arrivals.size(), 2u);
    const CodeArrival& second = whole.arrivals[1];
    L1Result l1;
    ReplayOptions options;
    options.fromFrame = second.frame;
    options.fromTInFrame = second.tInFrame;
    std::string error;
    ASSERT_TRUE(ReplayBuild(_bundle, options, l1, error)) << error;
    EXPECT_EQ(l1.replay.diverged, 0u);
    EXPECT_EQ(l1.replay.firstFrame, second.frame);
    uint8_t value = 0;
    ASSERT_TRUE(l1.StartByte(l1.StartKey(0xB000), value));
    EXPECT_EQ(value, 0x01) << "the start image is the memory at the arrival: stage 2's LD BC,nn is in place";
    EXPECT_EQ(RoutineAt(l1, At("start")), nullptr) << "the loader ran before the window";
    EXPECT_TRUE(l1.arrivals.empty()) << "nothing arrives after the last stage";
    ReplayOptions empty;
    empty.fromFrame = 1000;
    EXPECT_FALSE(ReplayBuild(_bundle, empty, l1, error));
    EXPECT_NE(error.find("window"), std::string::npos) << error;
}

TEST_F(AnalysisL1_Test, PassOverTheImageIsNoUse)
{
    const L1Result l1 = Run("sweep.asm", 8);
    ASSERT_FALSE(HasFailure());
    // The read and the write that leave every byte as it was are a pass over the image
    const Loop* sweep = LoopAt(l1, At("sweep"));
    ASSERT_NE(sweep, nullptr);
    const Stride* read = StrideAt(*sweep, At("sweep"));
    ASSERT_NE(read, nullptr);
    EXPECT_TRUE(read->sweep);
    const Stride* write = StrideAt(*sweep, static_cast<uint16_t>(At("sweep") + 4));
    ASSERT_NE(write, nullptr);
    EXPECT_TRUE(write->sweep);
    // A loop that changes the bytes uses them
    const Loop* decrypt = LoopAt(l1, At("decrypt"));
    ASSERT_NE(decrypt, nullptr);
    for (const Stride& s : decrypt->strides)
        EXPECT_FALSE(s.sweep) << s.z80;
    const ByteUse* changed = l1.UseAt(l1.StartKey(0xB800));
    ASSERT_NE(changed, nullptr);
    EXPECT_NE(changed->flags & use::kWrite, 0u);

    // Only the sweep touched $A800: untouched now; the table keeps its one real reader
    const ByteUse* unused = l1.UseAt(l1.StartKey(0xA800));
    ASSERT_NE(unused, nullptr);
    EXPECT_EQ(unused->flags, 0u);
    EXPECT_EQ(unused->firstReader, kNoKey);
    EXPECT_EQ(unused->firstWriter, kNoKey);
    const ByteUse* table = l1.UseAt(l1.StartKey(At("table")));
    ASSERT_NE(table, nullptr);
    EXPECT_NE(table->flags & use::kRead, 0u);
    EXPECT_EQ(table->flags & use::kWrite, 0u);
    ASSERT_NE(l1.InstructionAt(table->firstReader), nullptr);
    EXPECT_EQ(l1.InstructionAt(table->firstReader)->z80, At("readtab"));
    EXPECT_EQ(table->firstReader, table->lastReader);
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

TEST_F(AnalysisL1_Test, ToolEditPageWritesThroughTheEditsPaging)
{
    // A tool edit's RAM page images are reported as writes where the CPU sees them after the edit (its paging
    // record follows the pages): a page the edit maps in counts, a page mapped in two banks counts once
    Load("stages.asm", "128K");
    ASSERT_FALSE(HasFatalFailure());
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_TRUE(ttd->StartRecording()) << ttd->LastStartError();
    _emulator->RunNFrames(1);
    // Paging through the 7FFD latch, which the edit records with the chipset (a direct bank switch is not recorded)
    const auto page = [this](uint8_t ram) {
        uint8_t& latch = _context->emulatorState.p7FFD;
        latch = static_cast<uint8_t>((latch & ~0x07) | ram);
        _memory->UpdateZ80Banks();
    };
    // Page 5 in bank 3 as well as in bank 1
    ASSERT_TRUE(_emulator->EditMemoryFromTool("test", [&] { page(5); }).empty());
    _emulator->RunNFrames(1);
    ASSERT_TRUE(_emulator->EditMemoryFromTool("test", [this] {
                    for (uint16_t k = 0; k < 4; ++k)
                        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xC100 + k), static_cast<uint8_t>(k + 1));
                }).empty());
    _emulator->RunNFrames(1);
    // Page 3 into bank 3 by the edit that writes it: not mapped before the edit
    ASSERT_TRUE(_emulator->EditMemoryFromTool("test", [&] {
                    page(3);
                    for (uint16_t k = 0; k < 4; ++k)
                        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xC200 + k), static_cast<uint8_t>(k + 9));
                }).empty());
    const uint16_t page3 = _memory->GetPageForBank(3);
    _emulator->RunNFrames(1);
    std::string error;
    ASSERT_TRUE(TraceBundle::Capture(*_emulator, _bundle, error)) << error;
    const L1Result l1 = Build();
    for (uint16_t k = 0; k < 4; ++k)
    {
        // Bank 1 maps page 5 from the start
        const ByteUse* twice = l1.UseAt(l1.StartKey(static_cast<uint16_t>(0x4100 + k)));
        ASSERT_NE(twice, nullptr) << "page 5 byte " << k;
        EXPECT_EQ(twice->writes, 1u) << "page 5 (banks 1 and 3) byte " << k;
        const ByteUse* mapped = l1.UseAt(MakeKey(page3, static_cast<uint16_t>(0xC200 + k)));
        ASSERT_NE(mapped, nullptr) << "page 3 byte " << k;
        EXPECT_EQ(mapped->writes, 1u) << "page 3 byte " << k;
    }
}

// A buffer the PUSH trick cleared (LD SP,buf_end / PUSH HL), then filled with pointers read back through IX: a PUSH
// wrote its bytes and nothing popped them. Its words are pointers in memory, not the stack's slots
TEST_F(AnalysisL1_Test, PushClearedBufferKeepsItsPointers)
{
    const L1Result l1 = Run("push-cleared-pointers.asm", 1);
    ASSERT_FALSE(HasFailure());
    const auto wordAt = [&l1](uint16_t z80) {
        return std::any_of(l1.pointers.begin(), l1.pointers.end(), [&](const PointerUse& p) {
            return p.form == PointerForm::Word && p.origin == l1.StartKey(z80);
        });
    };
    const ByteUse* buf = l1.UseAt(l1.StartKey(At("buf")));
    ASSERT_NE(buf, nullptr);
    ASSERT_NE(buf->flags & use::kStackWrite, 0) << "the PUSH cleared it";
    EXPECT_TRUE(wordAt(At("buf")));
    EXPECT_TRUE(wordAt(static_cast<uint16_t>(At("buf") + 2)));
}

// LD HL,(table) / LD (list),HL: the list remembers the table its word came from; a PUSH over the list writes other
// bytes. Read back and used as an address, the list's word is a pointer, the table's is not
TEST_F(AnalysisL1_Test, PushOverACopyForgetsIt)
{
    const L1Result l1 = Run("pushed-over-list.asm", 1);
    ASSERT_FALSE(HasFailure());
    const auto wordAt = [&l1](uint16_t z80) {
        return std::any_of(l1.pointers.begin(), l1.pointers.end(), [&](const PointerUse& p) {
            return p.form == PointerForm::Word && p.origin == l1.StartKey(z80);
        });
    };
    EXPECT_TRUE(wordAt(At("list")));
    EXPECT_FALSE(wordAt(At("table"))) << "the push wrote over the copy of the table's word";
}

// The same with a tool edit (a script's poke) over the list: the list's word holds its own value, as a pointer and as
// the source of the byte drawn from it
TEST_F(AnalysisL1_Test, ToolEditOverACopyForgetsIt)
{
    Load("edit-over-list.asm");
    ASSERT_FALSE(HasFatalFailure());
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_TRUE(ttd->StartRecording()) << ttd->LastStartError();
    _emulator->RunNFrames(1);
    ASSERT_GE(_z80->pc, At("wait")) << "the edit lands in the wait for the flag";
    ASSERT_LT(_z80->pc, At("main"));
    ttd->BeginToolEdit();
    _memory->ToolWriteToZ80Memory(At("list"), 0x00);
    _memory->ToolWriteToZ80Memory(static_cast<uint16_t>(At("list") + 1), 0x58);
    _memory->ToolWriteToZ80Memory(At("flag"), 0x01);
    ttd->EndToolEdit("test");
    _emulator->RunNFrames(1);
    std::string error;
    ASSERT_TRUE(TraceBundle::Capture(*_emulator, _bundle, error)) << error;
    const L1Result l1 = Build();
    const auto wordAt = [&l1](uint16_t z80) {
        return std::any_of(l1.pointers.begin(), l1.pointers.end(), [&](const PointerUse& p) {
            return p.form == PointerForm::Word && p.origin == l1.StartKey(z80);
        });
    };
    const auto drawnFrom = [&l1](uint16_t z80) {
        return std::any_of(l1.drawn.begin(), l1.drawn.end(),
                           [&](const DrawnByte& d) { return d.key == l1.StartKey(z80); });
    };
    EXPECT_TRUE(wordAt(At("list")));
    EXPECT_FALSE(wordAt(At("table"))) << "the edit wrote over the copy of the table's word";
    EXPECT_TRUE(drawnFrom(At("list"))) << "the edited byte is its own source";
    EXPECT_FALSE(drawnFrom(At("table")));
}

// 128K: one copier writes the same stage to $C000 in RAM 1, then in RAM 3, and calls it in each: two arrivals, told
// apart by the page
TEST_F(AnalysisL1_Test, ArrivalsInTwoPagesAtOneAddress)
{
    const L1Result l1 = Run("banked-stages.asm", 1, "128K");
    ASSERT_FALSE(HasFailure());
    std::vector<const CodeArrival*> at;
    for (const CodeArrival& a : l1.arrivals)
        if (a.z80 == 0xC000)
            at.push_back(&a);
    ASSERT_EQ(at.size(), 2u) << "one per page: the same copier, the same Z80 address";
    EXPECT_NE(KeyPage(at[0]->key), KeyPage(at[1]->key));
    EXPECT_EQ(at[0]->writerZ80, At("copy"));
    EXPECT_EQ(at[1]->writerZ80, At("copy"));
}
