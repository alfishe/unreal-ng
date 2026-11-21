/// @file profileanalyzer_test.cpp
/// @brief The CPU time profile (ProfileAnalyzer + profile::Execute, docs/emulator/analysis/cpu-profile.md) on a 48K
/// running small programs at $8000 (uncontended, so every T-state is the instruction's own): the T-states booked to
/// each routine found at run time, the attribution rules (calls, JP into a routine, PUSH + RET dispatch, discarded
/// return addresses, a dispatch from the root), interrupts with a halted CPU, marks, the per-frame series and the
/// operations' checks and exports.

#include "pch.h"

#include <gtest/gtest.h>

#include <json/json.h>

#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../../../automation/cli/src/commands/cli-cpuprofile.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/profile/profileanalyzer.h"
#include "debugger/analyzers/profile/profilecontrol.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

namespace
{

class ProfileAnalyzer_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("profile-test", "48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _manager = _context->pDebugManager->GetAnalyzerManager();
        _analyzer = _manager->getAnalyzer<ProfileAnalyzer>(ProfileAnalyzer::kId);
        ASSERT_NE(_analyzer, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    void Poke(uint16_t address, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _context->pMemory->DirectWriteToZ80Memory(address++, b);
    }
    /// PC at $8000, the code there first: DI; LD SP,$FF00
    void Origin()
    {
        Poke(0x8000, {0xF3, 0x31, 0x00, 0xFF});
        _context->pCore->GetZ80()->pc = 0x8000;
    }
    void Steps(int count)
    {
        for (int i = 0; i < count; ++i)
            _emulator->RunSingleCPUCycle(true);
    }
    profile::Reply Op(const std::string& op, const profile::Options& options = {})
    {
        return profile::Execute(*_emulator, op, options);
    }
    void Start(const profile::Options& options = {{"frames", "false"}})
    {
        const profile::Reply reply = Op("start", options);
        ASSERT_TRUE(reply.Ok()) << reply.message;
    }
    static const profile::RoutineView* At(const profile::Snapshot& s, uint16_t address)
    {
        for (size_t id = 1; id < s.routines.size(); ++id)
            if (s.routines[id].address == address)
                return &s.routines[id];
        return nullptr;
    }

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    AnalyzerManager* _manager = nullptr;
    ProfileAnalyzer* _analyzer = nullptr;
};

}  // namespace

/// Off costs nothing: no CPU step subscriber exists until start, none after stop
TEST_F(ProfileAnalyzer_Test, OffHasNoStepSubscriber)
{
    EXPECT_FALSE(_manager->hasCPUStepSubscribers());
    Start();
    EXPECT_TRUE(_manager->hasCPUStepSubscribers());
    EXPECT_FALSE(_manager->hasIdleStepSubscribers()) << "the profile needs no idle fetch stepped: a halted CPU idles in one go";
    ASSERT_TRUE(Op("stop").Ok());
    EXPECT_FALSE(_manager->hasCPUStepSubscribers());
    EXPECT_FALSE(_analyzer->ReadTotals().active);
}

/// A CALL'd routine: its own T-states are its self and inclusive, the CALL and the loop the root's
TEST_F(ProfileAnalyzer_Test, CallBooksTheCalleeAndTheCaller)
{
    Origin();
    Poke(0x8004, {0xCD, 0x00, 0x81, 0x18, 0xFB});  // CALL $8100; JR $8004
    Poke(0x8100, {0x00, 0xC9});                    // NOP; RET
    Start();
    constexpr int n = 40;
    Steps(2 + 4 * n);  // ends after the JR: the run's end books it

    const profile::Snapshot s = _analyzer->Read("", false);
    const profile::RoutineView* sub = At(s, 0x8100);
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(sub->entries[profile::EntryCall], uint64_t(n));
    EXPECT_EQ(sub->self, uint64_t(14 * n));
    EXPECT_EQ(sub->inclusive, uint64_t(14 * n));
    ASSERT_EQ(sub->callers.size(), 1u);
    EXPECT_EQ(sub->callers[0].first, 0u) << "called from the root";
    EXPECT_EQ(s.routines[0].self, uint64_t(14 + 29 * n)) << "DI, LD SP, then CALL 17 + JR 12 per loop";
    EXPECT_EQ(s.totals.tstates, uint64_t(14 + 43 * n));
    EXPECT_EQ(s.totals.routines, 1u);
}

/// JP into a known routine (a tail call): the time stays with the routine that jumped, the arrival is counted
TEST_F(ProfileAnalyzer_Test, JumpIntoARoutineStaysWithTheJumper)
{
    Origin();
    Poke(0x8004, {0xCD, 0x00, 0x81, 0xCD, 0x00, 0x82, 0x18, 0xFB});  // CALL $8100; loop: CALL $8200; JR loop
    Poke(0x8100, {0x00, 0xC9});                                      // NOP; RET
    Poke(0x8200, {0xC3, 0x00, 0x81});                                // JP $8100
    Start();
    constexpr int n = 30;
    Steps(5 + 5 * n);

    const profile::Snapshot s = _analyzer->Read("", false);
    const profile::RoutineView* sub = At(s, 0x8100);
    const profile::RoutineView* tail = At(s, 0x8200);
    ASSERT_NE(sub, nullptr);
    ASSERT_NE(tail, nullptr);
    EXPECT_EQ(sub->entries[profile::EntryCall], 1u);
    EXPECT_EQ(sub->jumpIns, uint64_t(n));
    EXPECT_EQ(sub->self, 14u) << "only the one call";
    EXPECT_EQ(tail->entries[profile::EntryCall], uint64_t(n));
    EXPECT_EQ(tail->self, uint64_t(24 * n)) << "JP 10 + the jumped-to NOP + RET 14";
    EXPECT_EQ(s.totals.depth, 0u);
}

/// PUSH + RET dispatch: the target is a routine entered by dispatch, the dispatcher counts it, both leave together
TEST_F(ProfileAnalyzer_Test, PushRetDispatchIsAnEntry)
{
    Origin();
    Poke(0x8004, {0xCD, 0x00, 0x81, 0x18, 0xFB});  // CALL $8100; JR $8004
    Poke(0x8100, {0x21, 0x00, 0x82, 0xE5, 0xC9});  // LD HL,$8200; PUSH HL; RET
    Poke(0x8200, {0x00, 0xC9});                    // NOP; RET (to the CALL's return)
    Start();
    constexpr int n = 25;
    Steps(2 + 7 * n);

    const profile::Snapshot s = _analyzer->Read("", false);
    const profile::RoutineView* dispatcher = At(s, 0x8100);
    const profile::RoutineView* target = At(s, 0x8200);
    ASSERT_NE(dispatcher, nullptr);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->entries[profile::EntryDispatch], uint64_t(n));
    EXPECT_EQ(dispatcher->dispatchesOut, uint64_t(n));
    EXPECT_EQ(dispatcher->self, uint64_t(31 * n));
    EXPECT_EQ(target->self, uint64_t(14 * n));
    EXPECT_EQ(dispatcher->inclusive, uint64_t(45 * n)) << "the dispatched routine runs inside the dispatcher";
    EXPECT_EQ(dispatcher->abandoned, 0u);
    EXPECT_EQ(s.totals.depth, 0u);
}

/// A RET chain (three addresses pushed, then RET into the first, each target RETs into the next): each target replaces
/// the one before (it returned into it), so the chain is one frame over the routine that started it, the last return
/// pops both
TEST_F(ProfileAnalyzer_Test, RetChainIsOneFrameDeep)
{
    Origin();
    Poke(0x8004, {0xCD, 0x00, 0x81, 0x18, 0xFB});  // CALL $8100; JR $8004
    // LD HL,$8220; PUSH HL; LD HL,$8210; PUSH HL; LD HL,$8200; PUSH HL; RET
    Poke(0x8100, {0x21, 0x20, 0x82, 0xE5, 0x21, 0x10, 0x82, 0xE5, 0x21, 0x00, 0x82, 0xE5, 0xC9});
    for (uint16_t target : {0x8200, 0x8210, 0x8220})
        Poke(target, {0x00, 0xC9});  // NOP; RET
    Start();
    constexpr int n = 20;
    Steps(2 + 15 * n);  // ends after the JR

    const profile::Snapshot s = _analyzer->Read("", false);
    const profile::RoutineView* starter = At(s, 0x8100);
    ASSERT_NE(starter, nullptr);
    EXPECT_EQ(starter->self, uint64_t(73 * n)) << "3 x (LD HL 10 + PUSH 11) + RET 10";
    EXPECT_EQ(starter->inclusive, uint64_t((73 + 3 * 14) * n)) << "the chain runs inside the routine that started it";
    for (uint16_t target : {0x8200, 0x8210, 0x8220})
    {
        const profile::RoutineView* r = At(s, target);
        ASSERT_NE(r, nullptr) << target;
        EXPECT_EQ(r->entries[profile::EntryDispatch], uint64_t(n)) << target;
        EXPECT_EQ(r->self, uint64_t(14 * n)) << target;
        EXPECT_EQ(r->inclusive, uint64_t(14 * n)) << target << ": left when it returned into the next";
        EXPECT_EQ(r->maxDepth, 2u) << target;
    }
    EXPECT_EQ(s.totals.depth, 0u);
    EXPECT_EQ(s.totals.stackOverflows, 0u);
}

/// A routine that drops its return address (POP) and jumps back: each frame is left, counted, and the stack stays flat
TEST_F(ProfileAnalyzer_Test, DiscardedReturnAddressAbandonsTheFrame)
{
    Origin();
    Poke(0x8004, {0xCD, 0x00, 0x81});        // CALL $8100
    Poke(0x8100, {0xE1, 0xC3, 0x04, 0x80});  // POP HL; JP $8004
    Start();
    constexpr int n = 50;
    Steps(2 + 3 * n + 2);  // n loops, then CALL and POP (booked at the run's end)

    const profile::Snapshot s = _analyzer->Read("", false);
    const profile::RoutineView* sub = At(s, 0x8100);
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(sub->entries[profile::EntryCall], uint64_t(n + 1));
    EXPECT_EQ(sub->abandoned, uint64_t(n)) << "each new CALL lands on the slot of the frame before";
    EXPECT_EQ(s.totals.depth, 1u);
    EXPECT_EQ(s.totals.stackOverflows, 0u);
    EXPECT_EQ(sub->self, uint64_t(37 * n + 10))
        << "POP 10 + JP 10 + the CALL it jumped back to 17 (still in its frame), and the last POP";
}

/// A dispatch from outside any frame (a RET-driven main loop) replaces the routine it came from: no frames pile up
TEST_F(ProfileAnalyzer_Test, RootDispatchDoesNotPileUp)
{
    Origin();
    Poke(0x8004, {0x21, 0x00, 0x81, 0xE5, 0xC9});  // LD HL,$8100; PUSH HL; RET
    Poke(0x8100, {0xC3, 0x04, 0x80});              // JP $8004
    Start();
    constexpr int n = 40;
    Steps(2 + 4 * n + 1);

    const profile::Snapshot s = _analyzer->Read("", false);
    const profile::RoutineView* target = At(s, 0x8100);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->entries[profile::EntryDispatch], uint64_t(n));
    EXPECT_EQ(s.totals.depth, 1u);
    EXPECT_EQ(s.totals.stackOverflows, 0u);
}

/// The call stack from the CPU's flow events: taken and untaken conditional calls and returns, nested calls, a PUSH +
/// RET dispatch. The stack is exact right after the step of a CALL or a RET (a run ending on one reads it at once)
TEST_F(ProfileAnalyzer_Test, ConditionalTransfersAndADispatch)
{
    constexpr int loops = 30;
    Origin();
    // loop: XOR A; CALL $8100; CALL NZ,$8200 (not taken); CALL Z,$8300; JR loop
    Poke(0x8004, {0xAF, 0xCD, 0x00, 0x81, 0xC4, 0x00, 0x82, 0xCC, 0x00, 0x83, 0x18, 0xF4});
    Poke(0x8100, {0xCD, 0x00, 0x82, 0xC9});        // CALL $8200; RET
    Poke(0x8200, {0x00, 0xC8, 0xC9});              // NOP; RET Z (taken)
    Poke(0x8300, {0x21, 0x00, 0x82, 0xE5, 0xC9});  // LD HL,$8200; PUSH HL; RET (a dispatch)
    Start({{"frames", "false"}, {"reset", "true"}});

    // Read after each step of the first pass: the transfer of the step just run is on the stack already
    const std::vector<uint32_t> depths = {1, 2, 2, 1, 0};  // CALL $8100, CALL $8200, NOP, RET Z, RET
    Steps(2 + 1);                                          // to the loop, XOR A
    for (size_t i = 0; i < depths.size(); ++i)
    {
        Steps(1);
        EXPECT_EQ(_analyzer->Read("", false).totals.depth, depths[i]) << "after step " << i;
    }

    Steps(14 * loops - 5);  // ends after XOR A
    const profile::Snapshot s = _analyzer->Read("", false);
    ASSERT_EQ(s.totals.routines, 3u);
    EXPECT_EQ(s.totals.depth, 0u);
    const profile::RoutineView* first = At(s, 0x8100);
    const profile::RoutineView* dispatched = At(s, 0x8200);
    const profile::RoutineView* dispatcher = At(s, 0x8300);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(dispatched, nullptr);
    ASSERT_NE(dispatcher, nullptr);
    EXPECT_EQ(first->entries[profile::EntryCall], uint64_t(loops));
    EXPECT_EQ(dispatcher->entries[profile::EntryCall], uint64_t(loops)) << "CALL Z taken, CALL NZ not";
    EXPECT_EQ(dispatched->entries[profile::EntryCall], uint64_t(loops));
    EXPECT_EQ(dispatched->entries[profile::EntryDispatch], uint64_t(loops));
    EXPECT_EQ(dispatcher->dispatchesOut, uint64_t(loops));
    EXPECT_EQ(s.totals.stackOverflows, 0u);
}

/// IM 2 with a halted main loop over frames: the handler is an interrupt entry, every T-state is booked once (the
/// selves add up to the total), the HALT holds the idle time, the series has one record per frame, marks subtract
TEST_F(ProfileAnalyzer_Test, InterruptsHaltMarksAndFrames)
{
    Origin();
    // LD A,$90; LD I,A; IM 2; EI; loop: HALT; JR loop
    Poke(0x8004, {0x3E, 0x90, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
    for (uint32_t a = 0x9000; a <= 0x9100; ++a)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(a), 0x92);
    Poke(0x9292, {0xFB, 0xC9});  // EI; RET
    Start({{"frames", "true"}, {"ring_kib", "64"}});
    _emulator->RunNFrames(4, true);
    ASSERT_TRUE(Op("mark", {{"name", "late"}}).Ok());
    _emulator->RunNFrames(2, true);

    const profile::Snapshot s = _analyzer->Read("", true);
    const profile::RoutineView* handler = At(s, 0x9292);
    ASSERT_NE(handler, nullptr);
    EXPECT_GE(handler->entries[profile::EntryInterrupt], 4u);
    EXPECT_GE(s.totals.acknowledges, 4u);
    EXPECT_EQ(s.totals.discontinuities, 0u);
    uint64_t selves = 0;
    for (const profile::RoutineView& r : s.routines)
        selves += r.self;
    EXPECT_EQ(selves, s.totals.tstates) << "every T-state in exactly one self";
    uint64_t halt = 0, cells = 0;
    for (const profile::CellView& c : s.cells)
    {
        cells += c.tstates;
        if (c.address == 0x800B)
            halt = c.tstates;
    }
    EXPECT_EQ(cells, s.totals.tstates);
    EXPECT_GT(halt, s.totals.tstates * 9 / 10) << "a halted CPU's time is its HALT's";
    EXPECT_LT(s.totals.instructions, s.totals.frames * 20) << "the halted fetches are no instructions";

    const profile::Snapshot late = _analyzer->Read("late", false);
    EXPECT_EQ(late.sinceFrames, 2u);
    EXPECT_GT(late.sinceT, uint64_t(s.totals.frameT));
    EXPECT_LT(late.sinceT, uint64_t(s.totals.frameT) * 3);

    const auto frames = _analyzer->ReadFrames(50);
    ASSERT_GE(frames.size(), 3u);
    for (size_t i = frames.size() - 2; i < frames.size(); ++i)
    {
        EXPECT_EQ(frames[i].frame, frames[i - 1].frame + 1);
        // an instruction is booked whole in the frame it started in: a frame's total is the frame's T-states give or
        // take the instruction over the boundary
        EXPECT_NEAR(frames[i].total, s.totals.frameT, 23u) << "a steady frame books the frame's T-states";
    }
}

/// A stop closes the window: the last instruction's T-states and the frame that ended after it are booked, so N frames
/// run are N frames and N frame lengths in the window (the kit export's denominator), not N - 1 and a frame short
TEST_F(ProfileAnalyzer_Test, StopClosesTheLastFrame)
{
    constexpr unsigned kFrames = 3;
    constexpr uint64_t kLongestInstructionT = 23;  // a step may start at most one instruction past the frame start
    Origin();
    Poke(0x8004, {0x18, 0xFE});  // loop: JR loop
    Start();
    _emulator->RunNFrames(kFrames, true);
    ASSERT_TRUE(Op("stop").Ok());

    const profile::Snapshot s = _analyzer->Read("", true);
    EXPECT_EQ(s.totals.frames, kFrames);
    EXPECT_EQ(s.totals.discontinuities, 0u);
    EXPECT_NEAR(static_cast<double>(s.totals.tstates), static_cast<double>(uint64_t(s.totals.frameT) * kFrames),
                static_cast<double>(kLongestInstructionT));
    uint64_t cells = 0;
    for (const profile::CellView& c : s.cells)
        cells += c.tstates;
    EXPECT_EQ(cells, s.totals.tstates) << "the settled time is in a cell too";
    uint64_t selves = 0;
    for (const profile::RoutineView& r : s.routines)
        selves += r.self;
    EXPECT_EQ(selves, s.totals.tstates) << "and in a routine's self";

    // a second stop / start pair adds nothing twice
    Start();
    ASSERT_TRUE(Op("stop").Ok());
    EXPECT_EQ(_analyzer->ReadTotals().tstates, s.totals.tstates);
}

/// A run whose last instruction crossed a frame end: the frame end is booked at the frame end, the instruction at the
/// run's end, so a report read right after the run (the profile still running) counts N frames and every T-state the
/// run ran, and the next run books nothing twice
TEST_F(ProfileAnalyzer_Test, RunEndingOnAFramesFirstInstructionCountsItsFrame)
{
    constexpr unsigned kFrames = 3;
    Origin();
    Poke(0x8004, {0x18, 0xFE});  // loop: JR loop (12 T: DI + LD SP 14, so a JR crosses each 48K frame end)
    Start({{"frames", "true"}, {"ring_kib", "64"}});
    const Emulator::DirectRunResult first = _emulator->RunNFrames(kFrames, true);
    ASSERT_TRUE(first.reached);
    ASSERT_EQ(first.frames, kFrames);
    const uint32_t t = _context->pCore->GetZ80()->t;
    ASSERT_GT(t, 0u);
    ASSERT_LT(t, 12u) << "the run ends on the instruction that crossed the frame end: the edge";

    const auto check = [&](const char* what, uint64_t frames, uint64_t ran) {
        const profile::Snapshot s = _analyzer->Read("", true);
        EXPECT_EQ(s.totals.frames, frames) << what;
        EXPECT_EQ(s.totals.tstates, ran) << what << ": every T-state the runs ran";
        EXPECT_EQ(s.totals.discontinuities, 0u) << what;
        uint64_t cells = 0, selves = 0;
        for (const profile::CellView& c : s.cells)
            cells += c.tstates;
        for (const profile::RoutineView& r : s.routines)
            selves += r.self;
        EXPECT_EQ(cells, s.totals.tstates) << what;
        EXPECT_EQ(selves, s.totals.tstates) << what;
        EXPECT_EQ(_analyzer->ReadFrames(0).size(), frames) << what << ": one series record per frame end";
    };
    check("after run_frames", kFrames, first.tstates);

    // a run ending mid-frame, then one ending on the next frame's first instruction
    const Emulator::DirectRunResult mid = _emulator->RunTStates(1000, true);
    check("after run_tstates mid-frame", kFrames, first.tstates + mid.tstates);
    const Emulator::DirectRunResult rest = _emulator->RunNFrames(1, true);
    check("after the next frame", kFrames + 1, first.tstates + mid.tstates + rest.tstates);
    ASSERT_TRUE(Op("stop").Ok());
    EXPECT_EQ(_analyzer->ReadTotals().tstates, first.tstates + mid.tstates + rest.tstates) << "stop adds nothing twice";
}

/// A run that ends on an interrupt's acknowledge (the step that accepted it, the handler not run yet): the acknowledge
/// waits for the handler's first instruction, which pushes the interrupt's frame and carries it
TEST_F(ProfileAnalyzer_Test, RunEndingOnAnAcknowledgeKeepsItForTheHandler)
{
    Origin();
    // LD A,$90; LD I,A; IM 2; EI; loop: HALT; JR loop
    Poke(0x8004, {0x3E, 0x90, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
    for (uint32_t a = 0x9000; a <= 0x9100; ++a)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(a), 0x92);
    Poke(0x9292, {0xFB, 0xC9});  // EI; RET
    Start();
    Z80* cpu = _context->pCore->GetZ80();
    _emulator->RunNFrames(1, true);  // into the HALT loop
    const uint64_t acknowledges = cpu->acknowledges;
    const uint64_t acknowledgesBooked = _analyzer->ReadTotals().acknowledges;
    // the run ends with the step that accepted the next interrupt: the handler's first instruction not run yet
    _emulator->RunUntilInterrupt(true);
    ASSERT_EQ(cpu->acknowledges, acknowledges + 1);
    ASSERT_EQ(cpu->pc, 0x9292) << "the run ended on the acknowledge";
    const profile::Snapshot atAckSnapshot = _analyzer->Read("", false);
    const profile::Totals& atAck = atAckSnapshot.totals;
    const profile::RoutineView* handlerAtAck = At(atAckSnapshot, 0x9292);
    EXPECT_EQ(atAck.acknowledges, acknowledgesBooked) << "the acknowledge waits for its handler";
    Steps(1);  // EI in the handler: the acknowledge and the EI
    const profile::Snapshot s = _analyzer->Read("", true);
    EXPECT_EQ(s.totals.tstates - atAck.tstates, uint64_t(cpu->lastAcknowledgeT) + 4u);
    EXPECT_EQ(s.totals.acknowledges, acknowledgesBooked + 1);
    EXPECT_EQ(s.totals.acknowledgeT - atAck.acknowledgeT, uint64_t(cpu->lastAcknowledgeT));
    const profile::RoutineView* handler = At(s, 0x9292);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->entries[profile::EntryInterrupt], acknowledgesBooked + 1);
    EXPECT_TRUE(handler->onStack) << "the acknowledge pushed the interrupt's frame";
    EXPECT_EQ(handler->self - (handlerAtAck ? handlerAtAck->self : 0), uint64_t(cpu->lastAcknowledgeT) + 4u)
        << "the acknowledge and the EI are the handler's";
    uint64_t cells = 0, selves = 0;
    for (const profile::CellView& c : s.cells)
        cells += c.tstates;
    for (const profile::RoutineView& r : s.routines)
        selves += r.self;
    EXPECT_EQ(cells, s.totals.tstates);
    EXPECT_EQ(selves, s.totals.tstates);
}

/// The operations' checks: unknown op / option, a mark's name, an unknown mark
TEST_F(ProfileAnalyzer_Test, OperationsRefuseBadInput)
{
    EXPECT_EQ(Op("jump").error, profile::Error::BadRequest);
    EXPECT_EQ(Op("start", {{"turbo", "1"}}).error, profile::Error::BadRequest);
    EXPECT_EQ(Op("start", {{"stack", "maybe"}}).error, profile::Error::BadRequest);
    EXPECT_EQ(Op("start", {{"ring_kib", "1"}}).error, profile::Error::BadRequest);
    EXPECT_EQ(Op("mark").error, profile::Error::BadRequest);
    EXPECT_EQ(Op("mark", {{"name", "x"}, {"drop", "true"}}).error, profile::Error::NotFound);
    EXPECT_EQ(Op("report", {{"since", "nowhere"}}).error, profile::Error::NotFound);
    EXPECT_EQ(Op("export", {{"format", "pdf"}}).error, profile::Error::BadRequest);
    const profile::Reply status = Op("status");
    ASSERT_TRUE(status.Ok());
    EXPECT_FALSE(status.text.empty());

    // a refused start leaves a running profile running, with its window
    Origin();
    Poke(0x8004, {0x18, 0xFE});  // loop: JR loop
    Start();
    Steps(10);
    const uint64_t before = _analyzer->ReadTotals().instructions;
    EXPECT_EQ(Op("start", {{"ring_kib", "1"}}).error, profile::Error::BadRequest);
    EXPECT_EQ(Op("start", {{"stack", "false"}, {"beam", "true"}}).error, profile::Error::BadRequest);
    EXPECT_TRUE(_analyzer->ReadTotals().active) << "the refusal did not switch the profile off";
    Steps(10);
    EXPECT_GT(_analyzer->ReadTotals().instructions, before);
}

/// The report names routines without labels by address, and the exports carry the same routines
TEST_F(ProfileAnalyzer_Test, ReportAndExports)
{
    Origin();
    Poke(0x8004, {0xCD, 0x00, 0x81, 0x18, 0xFB});
    Poke(0x8100, {0x00, 0xC9});
    Start();
    Steps(2 + 4 * 10);

    const profile::Reply report = Op("report", {{"top", "5"}});
    ASSERT_TRUE(report.Ok()) << report.message;
    EXPECT_NE(report.text.find("sub_8100"), std::string::npos) << report.text;

    const profile::Reply sym = Op("export", {{"format", "sym"}});
    ASSERT_TRUE(sym.Ok());
    EXPECT_NE(sym.text.find("sub_8100 = $8100"), std::string::npos) << sym.text;
    const profile::Reply ann = Op("export", {{"format", "ann"}});
    ASSERT_TRUE(ann.Ok());
    EXPECT_NE(ann.text.find("$8100 c sub_8100"), std::string::npos) << ann.text;

    const profile::Reply kit = Op("export", {{"format", "kit"}});
    ASSERT_TRUE(kit.Ok());
    Json::Value doc;
    Json::CharReaderBuilder builder;
    std::string errors;
    const StateNode* contentNode = kit.body.find("content");
    ASSERT_NE(contentNode, nullptr);
    const std::string& content = contentNode->s;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    ASSERT_TRUE(reader->parse(content.data(), content.data() + content.size(), &doc, &errors)) << errors;
    EXPECT_EQ(doc["format"].asString(), "unreal-ng analysis L1");
    EXPECT_EQ(doc["routines"].size(), 1u);
    EXPECT_EQ(doc["routines"][0]["entry"].asString(), "$8100");
    EXPECT_EQ(doc["routines"][0]["profile"]["tstates"].asUInt64(), 140u);
    bool sawCall = false;
    for (const Json::Value& row : doc["instructions"])
        if (row[0].asString() == "$8004")
        {
            sawCall = true;
            EXPECT_EQ(row[2].asString(), "CD0081") << "the instruction's bytes";
            EXPECT_EQ(row[4].asUInt64(), 10u);
            EXPECT_EQ(row[5].asUInt64(), 170u);
        }
    EXPECT_TRUE(sawCall);
}

/// reset starts a fresh window: counters and routines gone, marks dropped
TEST_F(ProfileAnalyzer_Test, ResetStartsAFreshWindow)
{
    Origin();
    Poke(0x8004, {0xCD, 0x00, 0x81, 0x18, 0xFB});
    Poke(0x8100, {0x00, 0xC9});
    Start();
    Steps(20);
    ASSERT_TRUE(Op("mark", {{"name", "a"}}).Ok());
    ASSERT_TRUE(Op("reset").Ok());
    const profile::Totals t = _analyzer->ReadTotals();
    EXPECT_EQ(t.tstates, 0u);
    EXPECT_EQ(t.routines, 0u);
    EXPECT_TRUE(_analyzer->Marks().empty());
    EXPECT_TRUE(t.active);
}

/// The CLI's words: the operation, the bare words of each operation, name=value options, --json
TEST_F(ProfileAnalyzer_Test, CliWordsBecomeOptions)
{
    std::string op, error;
    std::map<std::string, std::string> options;
    bool json = false;
    ASSERT_TRUE(CliCpuProfile::ParseArgs({}, op, options, json, error));
    EXPECT_EQ(op, "status");
    ASSERT_TRUE(CliCpuProfile::ParseArgs({"start", "nostack", "reset", "ring_kib=128"}, op, options, json, error));
    EXPECT_EQ(op, "start");
    EXPECT_EQ(options["stack"], "false");
    EXPECT_EQ(options["reset"], "true");
    EXPECT_EQ(options["ring_kib"], "128");
    ASSERT_TRUE(CliCpuProfile::ParseArgs({"mark", "level2", "drop"}, op, options, json, error));
    EXPECT_EQ(options["name"], "level2");
    EXPECT_EQ(options["drop"], "true");
    ASSERT_TRUE(CliCpuProfile::ParseArgs({"export", "kit", "p.json", "since=level2", "--json"}, op, options, json, error));
    EXPECT_EQ(options["format"], "kit");
    EXPECT_EQ(options["path"], "p.json");
    EXPECT_EQ(options["since"], "level2");
    EXPECT_TRUE(json);
    EXPECT_FALSE(CliCpuProfile::ParseArgs({"stop", "now"}, op, options, json, error));
}
