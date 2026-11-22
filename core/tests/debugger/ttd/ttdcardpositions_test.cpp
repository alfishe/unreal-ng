#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/ttdcardbench.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/ttdslotcards.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

/// TTD positions on a sound card's CPU (gs-debugger design §8, owner decision 2026-11-17): the card's instruction
/// count is a seek target; every card move is a replay to it; the main CPU stands at its last instruction start at
/// or before the card's moment, with the T-states into the instruction; the machine runs on from the main CPU's
/// boundary with the card as the boundary had it.
///
/// The oracle is a forward run: the recording itself, with the card's debug loop copying the card's whole state at
/// one instruction count (GSCardTarget's scan, the same copy a card move keeps). A card move must give that state
/// byte for byte: registers (shadow set, IFF, IM, MEMPTR), the card's TTD blob and its memory regions.
///
/// The main CPU runs `LDIR` over 1 KB and reads the card's status port in a loop: most card moments fall inside a
/// 21 T LDIR step.
///
/// Runtime justification: each test boots a machine with the card, records 5-24 frames and replays a frame or two
/// per card move in tight mode (the card's debug loop at 12-24 MHz): about 0.1-0.5 s per test.
namespace
{
using namespace ttdtest;

class TTD_CardPositions_Test : public ::testing::TestWithParam<Card>
{
};

}  // namespace

/// The acceptance: 5 card instructions back from a card position give the forward run's card (registers, blob,
/// memory) and its main CPU boundary; 5 forward give the first position again
TEST_P(TTD_CardPositions_Test, ReverseStepFiveCardInstructionsMatchesTheForwardRun)
{
    Bench b;
    ASSERT_TRUE(Build(b, GetParam()));
    const uint64_t perFrame = PerFrame(b);
    ASSERT_GT(perFrame, 1000u) << "the card's CPU runs";
    const uint64_t target = b.Count() + perFrame * 2 + perFrame / 3;
    const GSCardTarget::ScanCapture oracle = Record(b, 5, target);
    ASSERT_TRUE(oracle.captured) << "the recording did not reach card instruction " << target;

    const ttd::TTDCardMoveResult at = b.ttd->SeekToCardInstruction(b.cpu, target + 5);
    ASSERT_TRUE(at.Ok()) << at.message;
    EXPECT_EQ(at.position.instruction, target + 5);
    EXPECT_EQ(b.ttd->GetState(), ttd::TTDSessionState::Detached);
    const GSCardTarget::CardState first = b.CardNow();
    const ttd::TTDTimePoint firstMain = b.ttd->CurrentPosition();

    const ttd::TTDCardMoveResult back = b.ttd->StepCardInstructions(b.cpu, -5);
    ASSERT_TRUE(back.Ok()) << back.message;
    EXPECT_EQ(back.from, target + 5);
    EXPECT_EQ(back.position.instruction, target);
    ExpectAtCapture(b, oracle, std::string(CardName(GetParam())) + " 5 back");

    const ttd::TTDCardMoveResult forward = b.ttd->StepCardInstructions(b.cpu, 5);
    ASSERT_TRUE(forward.Ok()) << forward.message;
    ExpectSameCard(b.CardNow(), first, "5 forward again");
    EXPECT_EQ(b.ttd->CurrentPosition().frame, firstMain.frame);
    EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, firstMain.tInFrame);
}

INSTANTIATE_TEST_SUITE_P(Cards, TTD_CardPositions_Test, ::testing::Values(Card::GS, Card::NeoGS, Card::MultiSound),
                         [](const ::testing::TestParamInfo<Card>& info) { return std::string(CardName(info.param)); });

/// The count at every frame's checkpoint places a card instruction in the frame it ran in: the recording ran
/// without the card's debug loop (the card's idle sleep counts what it skips), the replays with it
TEST(TTD_CardPositions_Frames_Test, EveryFramesCardInstructionsLieInThatFrame)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    Record(b, 5);
    const uint64_t first = b.ttd->GetEarliestRecordedFrame();
    const uint64_t end = b.ttd->SessionEndPosition().frame;
    std::vector<uint64_t> counts;
    for (uint64_t f = first; f <= end; f++)
    {
        ASSERT_TRUE(b.ttd->SeekTo({f, 0}));
        counts.push_back(b.ttd->CardPositionNow(b.cpu).instruction);
    }
    for (size_t i = 0; i + 1 < counts.size(); i++)
    {
        ASSERT_LT(counts[i], counts[i + 1]) << "frame " << first + i;
        const uint64_t mid = counts[i] + (counts[i + 1] - counts[i]) / 2;
        const ttd::TTDCardMoveResult r = b.ttd->SeekToCardInstruction(b.cpu, mid);
        ASSERT_TRUE(r.Ok()) << r.message;
        EXPECT_EQ(r.position.mainBoundary.frame, first + i) << "card instruction " << mid;
        EXPECT_EQ(b.Count(), mid);
    }
}

/// Stepping back over a frame's checkpoint: the card instruction before the frame's first, and a few more
TEST(TTD_CardPositions_Frames_Test, StepsBackCrossCheckpointsAndFrameBoundaries)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::GS));
    b.emulator->RunNFrames(1);
    Record(b, 5);
    const uint64_t frame = b.ttd->GetEarliestRecordedFrame() + 3;
    ASSERT_TRUE(b.ttd->SeekTo({frame, 0}));
    const uint64_t frameStart = b.ttd->CardPositionNow(b.cpu).instruction;

    // A direct seek to each moment first: the steps must give the same machine
    std::map<uint64_t, GSCardTarget::CardState> direct;
    std::map<uint64_t, ttd::TTDTimePoint> mains;
    for (const uint64_t n : {frameStart + 2, frameStart + 1, frameStart, frameStart - 1, frameStart - 9})
    {
        const ttd::TTDCardMoveResult r = b.ttd->SeekToCardInstruction(b.cpu, n);
        ASSERT_TRUE(r.Ok()) << n << ": " << r.message;
        direct[n] = b.CardNow();
        mains[n] = b.ttd->CurrentPosition();
    }
    EXPECT_EQ(mains[frameStart - 9].frame, frame - 1) << "before the frame's first card instruction: the frame before";

    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, frameStart + 2).Ok());
    uint64_t at = frameStart + 2;
    for (const int64_t step : {-1, -1, -1, -8})
    {
        const ttd::TTDCardMoveResult r = b.ttd->StepCardInstructions(b.cpu, step);
        ASSERT_TRUE(r.Ok()) << r.message;
        at = static_cast<uint64_t>(static_cast<int64_t>(at) + step);
        ExpectSameCard(b.CardNow(), direct[at], "stepped to " + std::to_string(at));
        EXPECT_EQ(b.ttd->CurrentPosition().frame, mains[at].frame);
        EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, mains[at].tInFrame);
    }
}

/// Both CPUs stepped in turn: a card round trip, a main round trip, the card position again
TEST(TTD_CardPositions_Interleave_Test, RoundTripsOnBothCpus)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    const uint64_t perFrame = PerFrame(b);
    const uint64_t target = b.Count() + perFrame * 2 + 777;
    Record(b, 5);

    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, target).Ok());
    const GSCardTarget::CardState a = b.CardNow();
    const ttd::TTDTimePoint mainA = b.ttd->CurrentPosition();
    EXPECT_TRUE(b.ttd->AtCardPosition());

    ASSERT_TRUE(b.ttd->StepCardInstructions(b.cpu, -3).Ok());
    ASSERT_TRUE(b.ttd->StepCardInstructions(b.cpu, 3).Ok());
    ExpectSameCard(b.CardNow(), a, "card -3 +3");

    // The main CPU's own steps leave the card position (its instruction runs with the card behind it)
    ASSERT_TRUE(b.ttd->StepBackInstruction());
    EXPECT_FALSE(b.ttd->AtCardPosition());
    EXPECT_FALSE(b.ttd->CardPositionNow(b.cpu).cardPosition);
    ASSERT_TRUE(b.ttd->StepForwardInstruction());
    EXPECT_EQ(b.ttd->CurrentPosition().frame, mainA.frame);
    EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, mainA.tInFrame);
    ASSERT_TRUE(b.ttd->StepForwardInstruction());
    ASSERT_TRUE(b.ttd->ReverseStepInstructions(2));

    // A card step from there counts from the card's own count at the main CPU's moment
    const ttd::TTDCardMoveResult fromMain = b.ttd->StepCardInstructions(b.cpu, -1);
    ASSERT_TRUE(fromMain.Ok()) << fromMain.message;
    EXPECT_EQ(fromMain.position.instruction + 1, fromMain.from);
    EXPECT_LE(fromMain.position.mainBoundary.frame, mainA.frame);

    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, target).Ok());
    ExpectSameCard(b.CardNow(), a, "the card position again");
    EXPECT_EQ(b.ttd->CurrentPosition().frame, mainA.frame);
    EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, mainA.tInFrame);
}

/// A move in time is no stop of the debugger's: the last stop (its reason, its number) stays as it was, so a debugger
/// that brings a stopped processor's tab to the front does not take a card's Step Back for a main CPU step
TEST(TTD_CardPositions_Interleave_Test, MovesInTimeLeaveTheLastStop)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    const uint64_t target = b.Count() + PerFrame(b) * 2 + 777;
    Record(b, 4);
    b.emulator->RunNCPUCycles(1);   // a real stop: a direct run of the main CPU
    const Emulator::DebugStop stop = b.emulator->LastStop();
    EXPECT_EQ(stop.reason, Emulator::DebugStop::Reason::Step);

    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, target).Ok());
    ASSERT_TRUE(b.ttd->StepCardInstructions(b.cpu, -2).Ok());
    ASSERT_TRUE(b.ttd->StepCardInstructions(b.cpu, 1).Ok());
    ASSERT_TRUE(b.ttd->StepBackInstruction());
    EXPECT_EQ(b.emulator->LastStop().serial, stop.serial) << "the card's and the main CPU's moves in time: no stop";
    EXPECT_EQ(b.emulator->LastStop().reason, stop.reason);

    b.emulator->RunNCPUCycles(1);   // a real step again counts
    EXPECT_EQ(b.emulator->LastStop().serial, stop.serial + 1);
}

/// A card moment inside the main CPU's LDIR step: the main CPU shown at the step's start with the T-states into it;
/// a resume runs on from that boundary with the card as the boundary had it
TEST(TTD_CardPositions_Inside_Test, InsideLdirTheMainCpuStandsAtItsBoundaryAndResumesFromThere)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    const uint64_t perFrame = PerFrame(b);
    Record(b, 4);
    const uint64_t base = b.Count() - perFrame * 2;

    // A card moment the main CPU spends in LDIR, not at its start
    ttd::TTDCardMoveResult r;
    uint64_t target = base;
    for (int attempt = 0; attempt < 20; attempt++, target += 997)
    {
        r = b.ttd->SeekToCardInstruction(b.cpu, target);
        ASSERT_TRUE(r.Ok()) << r.message;
        if (r.position.mainPc == kLdirPc && r.position.inside)
            break;
    }
    ASSERT_EQ(r.position.mainPc, kLdirPc);
    ASSERT_TRUE(r.position.inside);
    EXPECT_GT(r.position.tOffset, 0u);
    EXPECT_LE(r.position.tOffset, 30u) << "an LDIR step is 21 T";
    EXPECT_EQ(b.z80()->pc, kLdirPc) << "the main CPU's registers are the boundary's";
    const ttd::TTDTimePoint boundary = r.position.mainBoundary;
    EXPECT_EQ(b.ttd->CurrentPosition().frame, boundary.frame);
    EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, boundary.tInFrame);

    // The card at the boundary without the card position (a plain seek)
    ASSERT_TRUE(b.ttd->SeekTo(boundary));
    const GSCardTarget::CardState natural = b.CardNow();
    EXPECT_FALSE(b.ttd->AtCardPosition());

    // The resume: the card as the boundary had it, the machine records on from the boundary
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, target).Ok());
    ASSERT_TRUE(b.ttd->AtCardPosition());
    const ttd::TTDReply resume = ttd::TTDControl(b.context).Execute({"resume", {}});
    ASSERT_TRUE(resume.Ok()) << resume.message;
    EXPECT_FALSE(b.ttd->AtCardPosition());
    ExpectSameCard(b.CardNow(), natural, "after the resume");
    EXPECT_EQ(b.ttd->CurrentPosition().frame, boundary.frame);
    EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, boundary.tInFrame);
    EXPECT_TRUE(b.ttd->IsRecording());
    b.emulator->RunNFrames(1);
    b.ttd->StopRecording();
}

/// The machine run on from a card position (not a resume): the first instruction puts the card back as the main
/// CPU's boundary had it
TEST(TTD_CardPositions_Inside_Test, RunningOnLeavesTheCardPositionFirst)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::GS));
    const uint64_t perFrame = PerFrame(b);
    Record(b, 4);
    const ttd::TTDCardMoveResult r = b.ttd->SeekToCardInstruction(b.cpu, b.Count() - perFrame * 2);
    ASSERT_TRUE(r.Ok()) << r.message;
    ASSERT_TRUE(b.ttd->SeekTo(r.position.mainBoundary));
    const GSCardTarget::CardState natural = b.CardNow();
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, r.position.instruction).Ok());
    ASSERT_TRUE(b.ttd->AtCardPosition());

    // One main instruction from the boundary: the card left its copy before it ran
    const uint16_t pc = b.z80()->pc;
    b.emulator->RunNCPUCycles(1);
    EXPECT_FALSE(b.ttd->AtCardPosition());
    EXPECT_NE(b.z80()->pc == pc && b.ttd->CurrentPosition() == r.position.mainBoundary, true) << "the main CPU ran";
    uint64_t count = 0;
    ASSERT_TRUE(b.card()->cardInstructionCount(count));
    const GSCardTarget::CardState now = b.CardNow();
    const Z80CpuRegisters regs = b.RegistersOf(now);
    (void)regs;
    // The card did not jump back from its copy: its count is the boundary's or later, never the copy's ahead
    uint64_t naturalCount = 0;
    {
        const GSCardTarget::CardState keep = b.CardNow();
        GSCardTarget::LoadCardState(b.card(), natural);
        b.card()->cardInstructionCount(naturalCount);
        GSCardTarget::LoadCardState(b.card(), keep);
    }
    EXPECT_GE(count, naturalCount);
}

/// Before the history and past it: a clear refusal, nothing moved
TEST(TTD_CardPositions_Edges_Test, TheHistorysStartAndEndRefuseCleanly)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));

    // No history yet
    EXPECT_EQ(b.ttd->SeekToCardInstruction(b.cpu, 1).status, ttd::TTDCardMoveResult::Status::NoHistory);
    Record(b, 3);

    const uint64_t first = b.ttd->GetEarliestRecordedFrame();
    ASSERT_TRUE(b.ttd->SeekTo({first, 0}));
    const uint64_t startCount = b.ttd->CardPositionNow(b.cpu).instruction;
    const ttd::TTDTimePoint end = b.ttd->SessionEndPosition();
    ASSERT_TRUE(b.ttd->SeekTo({end.frame, 0}));
    const uint64_t endCount = b.ttd->CardPositionNow(b.cpu).instruction;

    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, startCount + 100).Ok());
    const GSCardTarget::CardState before = b.CardNow();
    const ttd::TTDTimePoint mainBefore = b.ttd->CurrentPosition();
    auto unchanged = [&](const char* what) {
        ExpectSameCard(b.CardNow(), before, what);
        EXPECT_EQ(b.ttd->CurrentPosition().frame, mainBefore.frame) << what;
        EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, mainBefore.tInFrame) << what;
        EXPECT_TRUE(b.ttd->AtCardPosition()) << what;
        EXPECT_EQ(b.ttd->CardPositionNow(b.cpu).instruction, startCount + 100) << what;
    };

    const ttd::TTDCardMoveResult early = b.ttd->SeekToCardInstruction(b.cpu, startCount - 1);
    EXPECT_EQ(early.status, ttd::TTDCardMoveResult::Status::AtStart) << early.message;
    EXPECT_FALSE(early.message.empty());
    unchanged("before the history");

    const ttd::TTDCardMoveResult stepEarly = b.ttd->StepCardInstructions(b.cpu, -200);
    EXPECT_EQ(stepEarly.status, ttd::TTDCardMoveResult::Status::AtStart) << stepEarly.message;
    EXPECT_EQ(stepEarly.from, startCount + 100);
    unchanged("stepped before the history");

    const ttd::TTDCardMoveResult late = b.ttd->SeekToCardInstruction(b.cpu, endCount + 1);
    EXPECT_EQ(late.status, ttd::TTDCardMoveResult::Status::AtEnd) << late.message;
    unchanged("past the history");

    // The history's first and last card instruction are reached (the checkpoints themselves)
    const ttd::TTDCardMoveResult atStart = b.ttd->SeekToCardInstruction(b.cpu, startCount);
    EXPECT_TRUE(atStart.Ok()) << atStart.message;
    EXPECT_EQ(b.Count(), startCount);
    const ttd::TTDCardMoveResult atEnd = b.ttd->SeekToCardInstruction(b.cpu, endCount);
    EXPECT_TRUE(atEnd.Ok()) << atEnd.message;
    EXPECT_EQ(b.Count(), endCount);

    // No card with a CPU (the lightweight player has none) and the main CPU are refused too
    EXPECT_EQ(b.ttd->SeekToCardInstruction(DebugCpuId::Main, 5).status, ttd::TTDCardMoveResult::Status::NoCard);
    EXPECT_EQ(b.ttd->SeekToCardInstruction(DebugCpuId::MultiSoundGS, 5).status,
              ttd::TTDCardMoveResult::Status::NoCard);
}

/// A card blob without the count (a recording made before them) leaves the count unknown: card moves refuse
TEST(TTD_CardPositions_Edges_Test, ABlobWithoutTheCountLeavesItUnknown)
{
    for (const Card kind : {Card::GS, Card::NeoGS})
    {
        Bench b;
        ASSERT_TRUE(Build(b, kind));
        GeneralSoundCard* card = b.card();
        std::vector<uint8_t> blob(card->TTDStateSize());
        card->TTDSaveState(blob.data());
        const size_t flag = kind == Card::GS ? SoundChip_GeneralSound::TTD_COUNT_FLAG_OFFSET : SoundChip_NeoGS::TTD_COUNT_FLAG_OFFSET;
        const size_t at = kind == Card::GS ? SoundChip_GeneralSound::TTD_COUNT_OFFSET : SoundChip_NeoGS::TTD_COUNT_OFFSET;
        ASSERT_EQ(blob[flag], 1) << CardName(kind);
        uint64_t count = 0;
        ASSERT_TRUE(card->cardInstructionCount(count));
        uint64_t stored = 0;
        std::memcpy(&stored, &blob[at], sizeof(stored));   // little-endian hosts
        EXPECT_EQ(stored, count) << CardName(kind);

        // The count is not part of the state hash: two machines at the same state with other counts hash alike
        std::vector<uint8_t> moved = blob;
        moved[at] ^= 0x5A;
        const uint64_t hash = card->TTDHashState();
        card->TTDLoadState(moved.data());
        EXPECT_EQ(card->TTDHashState(), hash) << CardName(kind);
        EXPECT_NE(card->cardInstructionCount(count) ? count : 0, stored) << "the count was loaded";

        std::fill(blob.begin() + static_cast<std::ptrdiff_t>(at), blob.begin() + static_cast<std::ptrdiff_t>(flag) + 1, 0);
        card->TTDLoadState(blob.data());
        EXPECT_FALSE(card->cardInstructionCount(count)) << CardName(kind);
        Record(b, 2);
        ASSERT_TRUE(b.ttd->SeekTo({b.ttd->GetEarliestRecordedFrame() + 1, 0}));
        EXPECT_EQ(b.ttd->SeekToCardInstruction(b.cpu, 10).status, ttd::TTDCardMoveResult::Status::NoCount)
            << CardName(kind);
        EXPECT_FALSE(b.ttd->CardPositionNow(b.cpu).known);
    }
}

/// The main CPU at 7 MHz: the T-states into its instruction are its own (twice the 3.5 MHz ones)
TEST(TTD_CardPositions_Turbo_Test, SevenMegahertzMainCpu)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    EmulatorState& state = b.context->emulatorState;
    state.next_z80_frequency_multiplier = 1;
    state.hw_turbo_ratio = 2;
    state.hw_turbo_ratio_applied = 2;
    state.current_z80_frequency_multiplier = 2;
    b.z80()->RecomputeFrameTiming();
    b.emulator->RunNFrames(1);
    ASSERT_EQ(state.current_z80_frequency_multiplier, 2);

    const uint64_t perFrame = PerFrame(b);
    const uint64_t target = b.Count() + perFrame * 2 + perFrame / 5;
    const GSCardTarget::ScanCapture oracle = Record(b, 4, target);
    ASSERT_TRUE(oracle.captured);

    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, target + 5).Ok());
    const ttd::TTDCardMoveResult back = b.ttd->StepCardInstructions(b.cpu, -5);
    ASSERT_TRUE(back.Ok()) << back.message;
    ExpectAtCapture(b, oracle, "7 MHz");
    EXPECT_LE(back.position.tOffset, 30u);

    // The offsets of an LDIR step: some above the 3.5 MHz one's half (the main CPU's T-states, not 3.5 MHz ones)
    uint32_t widest = 0;
    for (uint64_t n = target; n < target + 40; n += 4)
    {
        const ttd::TTDCardMoveResult r = b.ttd->SeekToCardInstruction(b.cpu, n);
        ASSERT_TRUE(r.Ok()) << r.message;
        widest = std::max(widest, r.position.tOffset);
    }
    EXPECT_GT(widest, 10u);
}

/// Reverse continue to the card's previous breakpoint hit; none: a clear answer and nothing moved
TEST(TTD_CardPositions_Breakpoints_Test, ReverseContinueFindsThePreviousCardHit)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::GS));
    const uint64_t perFrame = PerFrame(b);
    Record(b, 4);
    const uint64_t target = b.Count() - perFrame * 2;

    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, target).Ok());
    EXPECT_EQ(b.ttd->ReverseContinueCard(b.cpu).status, ttd::TTDCardMoveResult::Status::NoBreakpoints);

    // A PC the card's firmware loop runs: the one it stands at
    const uint16_t loopPc = b.ttd->CardPositionNow(b.cpu).pc;
    BreakpointManager* breakpoints = b.context->pDebugManager->GetBreakpointsManager(b.cpu);
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = loopPc;
    std::string error;
    const uint16_t id = breakpoints->AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;
    const uint32_t hitsBefore = breakpoints->GetBreakpointById(id)->hitCount;

    const ttd::TTDCardMoveResult hit = b.ttd->ReverseContinueCard(b.cpu);
    ASSERT_TRUE(hit.Ok()) << hit.message;
    EXPECT_LT(hit.position.instruction, target);
    EXPECT_EQ(hit.position.pc, loopPc);
    EXPECT_EQ(b.ttd->CardPositionNow(b.cpu).pc, loopPc);
    EXPECT_EQ(hit.breakpointId, id);
    EXPECT_EQ(hit.hitAddress, loopPc);
    EXPECT_GE(hit.searchedFrames, 1u);
    EXPECT_EQ(breakpoints->GetBreakpointById(id)->hitCount, hitsBefore) << "the search counts no hits";

    // The previous one: from one instruction after the hit the same hit; from the hit an earlier one
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, hit.position.instruction + 1).Ok());
    const ttd::TTDCardMoveResult again = b.ttd->ReverseContinueCard(b.cpu);
    ASSERT_TRUE(again.Ok()) << again.message;
    EXPECT_EQ(again.position.instruction, hit.position.instruction);
    const ttd::TTDCardMoveResult earlier = b.ttd->ReverseContinueCard(b.cpu);
    ASSERT_TRUE(earlier.Ok()) << earlier.message;
    EXPECT_LT(earlier.position.instruction, hit.position.instruction);

    // Between the earlier hit and the hit nothing ran the PC: every instruction in between is elsewhere
    const uint64_t gap = hit.position.instruction - earlier.position.instruction;
    if (gap < 64)
        for (uint64_t n = earlier.position.instruction + 1; n < hit.position.instruction; n++)
        {
            ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, n).Ok());
            EXPECT_NE(b.ttd->CardPositionNow(b.cpu).pc, loopPc) << "card instruction " << n;
        }

    // A PC it never runs (inside the reset JP): no hit, nothing moved, the search named
    breakpoints->RemoveBreakpointByID(id);
    spec.address = 0x0001;
    const uint16_t never = breakpoints->AddBreakpoint(spec, error);
    ASSERT_NE(never, BRK_INVALID) << error;
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, target).Ok());
    const GSCardTarget::CardState before = b.CardNow();
    const ttd::TTDCardMoveResult none = b.ttd->ReverseContinueCard(b.cpu);
    EXPECT_EQ(none.status, ttd::TTDCardMoveResult::Status::NoHit) << none.message;
    EXPECT_FALSE(none.message.empty());
    EXPECT_FALSE(none.truncated);
    EXPECT_GE(none.searchedFrames, 2u);
    ExpectSameCard(b.CardNow(), before, "no hit");
    EXPECT_EQ(b.ttd->CardPositionNow(b.cpu).instruction, target);

    const ttd::TTDCardMoveResult bounded = b.ttd->ReverseContinueCard(b.cpu, 1);
    EXPECT_EQ(bounded.status, ttd::TTDCardMoveResult::Status::NoHit);
    EXPECT_TRUE(bounded.truncated);
    EXPECT_EQ(bounded.searchedFrames, 1u);
}

/// On a branch, after a branch switch, in a saved and loaded session, and with segments evicted to disk
TEST(TTD_CardPositions_Sessions_Test, BranchesSwitchesAndALoadedSession)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    const uint64_t perFrame = PerFrame(b);
    const uint64_t trunkTarget = b.Count() + perFrame * 2 + 4321;
    const GSCardTarget::ScanCapture trunkOracle = Record(b, 5, trunkTarget);
    ASSERT_TRUE(trunkOracle.captured);
    const uint64_t trunkEnd = b.ttd->SessionEndPosition().frame;

    // A branch from frame +2: the card's count goes on from there
    const uint64_t fork = b.ttd->GetEarliestRecordedFrame() + 2;
    ASSERT_TRUE(b.ttd->SeekTo({fork, 0}));
    ASSERT_TRUE(b.ttd->ResumeRecordingFrom({fork, 0}));
    b.emulator->RunNFrames(1);
    const uint64_t branchTarget = b.Count() + perFrame / 2;
    GSCardTarget::ScanCapture branchOracle;
    {
        const GSCardTarget::ScanScope scan(*b.target, branchTarget);
        b.emulator->RunNFrames(3);
        branchOracle = b.target->Capture();
    }
    b.ttd->StopRecording();
    ASSERT_TRUE(branchOracle.captured);
    const uint32_t branch = b.ttd->GetSessionInfo().activeBranch;
    ASSERT_NE(branch, ttd::kTTDTrunk);

    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, branchTarget).Ok());
    ExpectAtCapture(b, branchOracle, "on the branch");

    // On the trunk after a switch (its frames past the fork)
    std::string error;
    ASSERT_TRUE(b.ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{trunkEnd - 1, 0}, error)) << error;
    const ttd::TTDCardMoveResult onTrunk = b.ttd->SeekToCardInstruction(b.cpu, trunkTarget);
    ASSERT_TRUE(onTrunk.Ok()) << onTrunk.message;
    ExpectAtCapture(b, trunkOracle, "on the trunk after the switch");

    // Saved and loaded (the counts go with the card blobs): the same card positions
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("card-positions.ttd");
    {
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(b.ttd->SerializeSession(out, error)) << error;
    }
    Bench loaded;
    ASSERT_TRUE(Build(loaded, Card::NeoGS));
    ASSERT_TRUE(loaded.ttd->DeserializeSessionFile(path, error)) << error;
    std::string switchError;
    loaded.ttd->SwitchBranch(ttd::kTTDTrunk, std::nullopt, switchError);
    const ttd::TTDCardMoveResult afterLoad = loaded.ttd->SeekToCardInstruction(loaded.cpu, trunkTarget);
    ASSERT_TRUE(afterLoad.Ok()) << afterLoad.message;
    ExpectAtCapture(loaded, trunkOracle, "in the loaded session");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

/// A history limit with a recording folder moves segments to disk: a card position in an evicted segment reads it
/// back and gives the forward run's card
TEST(TTD_CardPositions_Sessions_Test, ACardPositionInASegmentEvictedToDisk)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::GS));
    const uint64_t perFrame = PerFrame(b);
    const uint64_t target = b.Count() + perFrame * 2 + 123;
    b.ttd->SetShadowRecordingRoot(TestPathHelper::GetUniqueTestScratchPath("card-positions-tiers"));
    b.ttd->SetHistoryLimit(8, 0);
    const GSCardTarget::ScanCapture oracle = Record(b, 24, target);
    ASSERT_TRUE(oracle.captured);
    const ttd::TTDSessionInfo info = b.ttd->GetSessionInfo();
    ASSERT_GT(info.evictedToDiskCheckpoints, 0u) << "the test needs segments on disk";

    const ttd::TTDCardMoveResult r = b.ttd->SeekToCardInstruction(b.cpu, target);
    ASSERT_TRUE(r.Ok()) << r.message;
    ExpectAtCapture(b, oracle, "an evicted segment");
}

// ---------------------------------------------------------------------------------------------------------------------
// The card's step over back (ReverseStepOverCard, the debuggers' Shift+F8 on a card's tab)
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
/// One card boundary the recording's own run saw (GSCardTarget::ScanTrace): the card has run `count` instructions
struct CardBoundary
{
    uint16_t pc = 0;
    uint16_t sp = 0;
    bool isReturn = false;   ///< the instruction at pc is a return (RET, RET cc, RETI, RETN)
};

/// a is at the stack level of b or above it (a stack that grew less), modulo 64K
bool AtOrAbove(uint16_t a, uint16_t b)
{
    return static_cast<int16_t>(static_cast<uint16_t>(a - b)) >= 0;
}

/// Records @p frames frames with every card boundary in [from, to) traced
std::map<uint64_t, CardBoundary> RecordTraced(Bench& b, int frames, uint64_t from, uint64_t to)
{
    std::map<uint64_t, CardBoundary> trace;
    EXPECT_TRUE(b.ttd->StartRecording());
    {
        const GSCardTarget::ScanScope scan(*b.target, UINT64_MAX);
        GSCardTarget* target = b.target;
        b.target->ScanTrace([&trace, target, from, to](GeneralSoundCard* card, uint64_t count, uint16_t pc) {
            if (count < from || count >= to)
                return;
            Z80CpuRegisters regs{};
            card->cardRegisters(regs);
            trace[count] = CardBoundary{pc, regs.sp, target->IsReturnAt(card, pc)};
        });
        b.emulator->RunNFrames(frames);
    }
    b.ttd->StopRecording();
    return trace;
}

/// What a step over back from boundary n gives by the trace: n - 1, or, when the instruction at n - 1 is a return that
/// lifted the stack to n's level, the newest boundary before it at that level (nullopt: not in the trace)
std::optional<uint64_t> ExpectedOver(const std::map<uint64_t, CardBoundary>& trace, uint64_t n, bool& over)
{
    over = false;
    const auto at = trace.find(n);
    const auto ret = trace.find(n - 1);
    if (at == trace.end() || ret == trace.end())
        return std::nullopt;
    if (!ret->second.isReturn || AtOrAbove(ret->second.sp, at->second.sp))
        return n - 1;
    over = true;
    for (auto it = std::make_reverse_iterator(ret); it != trace.rend(); ++it)
        if (AtOrAbove(it->second.sp, at->second.sp))
            return it->first;
    return std::nullopt;
}

class TTD_CardReverseStepOver_Test : public ::testing::TestWithParam<Card>
{
};
}  // namespace

/// The oracle: the recording's own trace of every card boundary (PC, SP) names where each step over back lands; for a
/// short routine also single card steps back (one instruction back at a time until the stack is at the level again).
/// The landing is the card position a direct seek gives, byte for byte, with the main CPU at that boundary. A plain
/// instruction before is one step back
TEST_P(TTD_CardReverseStepOver_Test, LandsWhereTheTraceAndSingleStepsBackSay)
{
    Bench b;
    ASSERT_TRUE(Build(b, GetParam()));
    ASSERT_TRUE(LoadCardRoutines(b));
    const uint64_t perFrame = PerFrame(b);
    const uint64_t from = b.Count() + perFrame + perFrame / 4;
    const uint64_t to = from + perFrame / 2;
    const std::map<uint64_t, CardBoundary> trace = RecordTraced(b, 4, from, to);
    ASSERT_GT(trace.size(), 1000u);
    ASSERT_EQ(trace.begin()->first, from);
    ASSERT_EQ(trace.rbegin()->first, to - 1) << "a boundary per card instruction";
    ASSERT_EQ(trace.size(), to - from) << "a boundary per card instruction";

    // The boundaries after a lifting return: the shortest routine, the longest one, and one after a plain instruction
    std::optional<uint64_t> shortest, longest, plain;
    uint64_t shortestLength = UINT64_MAX, longestLength = 0;
    size_t returns = 0;
    for (uint64_t n = from + 1; n < to; n++)
    {
        bool over = false;
        const std::optional<uint64_t> k = ExpectedOver(trace, n, over);
        if (!k)
            continue;
        if (!over)
        {
            if (!plain && n > from + perFrame / 8)
                plain = n;
            continue;
        }
        returns++;
        const uint64_t length = n - 1 - *k;
        if (length < shortestLength)
            shortest = n, shortestLength = length;
        if (length > longestLength)
            longest = n, longestLength = length;
    }
    ASSERT_TRUE(shortest && longest && plain) << "the card returns from its routines";
    ASSERT_GT(returns, 0u);

    for (const uint64_t n : {*plain, *shortest, *longest})
    {
        bool over = false;
        const uint64_t expected = *ExpectedOver(trace, n, over);
        const std::string what = std::string(CardName(GetParam())) + " from card instruction " + std::to_string(n);
        // The landing as a direct seek gives it
        ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, expected).Ok()) << what;
        const GSCardTarget::CardState direct = b.CardNow();
        const ttd::TTDTimePoint directMain = b.ttd->CurrentPosition();

        ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, n).Ok()) << what;
        const ttd::TTDCardMoveResult r = b.ttd->ReverseStepOverCard(b.cpu);
        ASSERT_TRUE(r.Ok()) << what << ": " << r.message;
        EXPECT_EQ(r.from, n) << what;
        EXPECT_EQ(r.target, expected) << what;
        EXPECT_EQ(r.position.instruction, expected) << what;
        EXPECT_EQ(r.overCall, over) << what;
        EXPECT_FALSE(r.callOutOfReach) << what;
        EXPECT_EQ(r.skipped, over ? n - 1 - expected : 0u) << what;
        EXPECT_EQ(r.position.pc, trace.at(expected).pc) << what;
        ExpectSameCard(b.CardNow(), direct, what);
        EXPECT_EQ(b.ttd->CurrentPosition().frame, directMain.frame) << what;
        EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, directMain.tInFrame) << what;
        EXPECT_TRUE(b.ttd->AtCardPosition()) << what;
    }

    // Single steps back for the shortest routine: the return, then back until the stack is at the level again
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, *shortest).Ok());
    const uint16_t level = trace.at(*shortest).sp;
    ASSERT_TRUE(b.ttd->StepCardInstructions(b.cpu, -1).Ok());
    Z80CpuRegisters regs = b.RegistersOf(b.CardNow());
    ASSERT_TRUE(b.target->IsReturnAt(b.card(), regs.pc)) << "the instruction before is the return";
    ASSERT_FALSE(AtOrAbove(regs.sp, level));
    uint64_t steps = 1;
    while (!AtOrAbove(regs.sp, level) && steps <= shortestLength + 1)
    {
        ASSERT_TRUE(b.ttd->StepCardInstructions(b.cpu, -1).Ok());
        regs = b.RegistersOf(b.CardNow());
        steps++;
    }
    EXPECT_EQ(b.ttd->CardPositionNow(b.cpu).instruction, *shortest - steps);
    bool over = false;
    EXPECT_EQ(*shortest - steps, *ExpectedOver(trace, *shortest, over)) << "single steps back and the trace agree";
}

INSTANTIATE_TEST_SUITE_P(Cards, TTD_CardReverseStepOver_Test, ::testing::Values(Card::GS, Card::NeoGS, Card::MultiSound),
                         [](const ::testing::TestParamInfo<Card>& info) { return std::string(CardName(info.param)); });

/// From a main position the level is the card's where it stands caught up to the main CPU's moment; a refusal (no
/// history, no card, at the card's first instruction of the history) leaves the machine where it stood
TEST(TTD_CardReverseStepOver_Edges_Test, FromAMainPositionAndTheRefusals)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    ASSERT_TRUE(LoadCardRoutines(b));
    EXPECT_EQ(b.ttd->ReverseStepOverCard(b.cpu).status, ttd::TTDCardMoveResult::Status::NoHistory);
    EXPECT_EQ(b.ttd->ReverseStepOverCard(DebugCpuId::MultiSoundGS).status, ttd::TTDCardMoveResult::Status::NoCard);
    const uint64_t perFrame = PerFrame(b);
    const uint64_t from = b.Count();
    const std::map<uint64_t, CardBoundary> trace = RecordTraced(b, 3, from, from + perFrame * 3);

    // A main position: the card's count there is the caught-up one; the step over back starts from it
    const uint64_t first = b.ttd->GetEarliestRecordedFrame();
    ASSERT_TRUE(b.ttd->SeekTo({first + 1, 1000}));
    ASSERT_FALSE(b.ttd->AtCardPosition());
    const ttd::TTDCardMoveResult r = b.ttd->ReverseStepOverCard(b.cpu);
    ASSERT_TRUE(r.Ok()) << r.message;
    bool over = false;
    const std::optional<uint64_t> expected = ExpectedOver(trace, r.from, over);
    ASSERT_TRUE(expected) << "card instruction " << r.from << " in the trace";
    EXPECT_EQ(r.position.instruction, *expected);
    EXPECT_EQ(r.overCall, over);
    EXPECT_TRUE(b.ttd->AtCardPosition());

    // The history's first card instruction: nothing before it, nothing moved
    ASSERT_TRUE(b.ttd->SeekTo({first, 0}));
    const uint64_t startCount = b.ttd->CardPositionNow(b.cpu).instruction;
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, startCount).Ok());
    const GSCardTarget::CardState before = b.CardNow();
    const ttd::TTDCardMoveResult early = b.ttd->ReverseStepOverCard(b.cpu);
    EXPECT_EQ(early.status, ttd::TTDCardMoveResult::Status::AtStart) << early.message;
    EXPECT_EQ(early.from, startCount);
    ExpectSameCard(b.CardNow(), before, "at the history's start");
    EXPECT_EQ(b.ttd->CardPositionNow(b.cpu).instruction, startCount);
}

// ---------------------------------------------------------------------------------------------------------------------
// The card's step over / step out through the past (StepCardForward, the debuggers' F8 / Ctrl+F8 on a card's tab)
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
/// Where a live card step (GSCardTarget::ArmStep / StepBoundary from a standing card) taken at boundary n ends, by
/// the recording's trace: Over with `overTarget` at the first later boundary at it with SP at or above n's (none: the
/// next boundary); Out at the first boundary after a return ran with SP at or above n's. nullopt: not before `end`
std::optional<uint64_t> ExpectedForward(const std::map<uint64_t, CardBoundary>& trace, uint64_t n, bool over,
                                        std::optional<uint16_t> overTarget, uint64_t end)
{
    const auto start = trace.find(n);
    if (start == trace.end())
        return std::nullopt;
    const uint16_t sp = start->second.sp;
    bool returning = start->second.isReturn;
    uint16_t returnSp = sp;
    for (auto it = std::next(start); it != trace.end() && it->first < end; ++it)
    {
        const CardBoundary& k = it->second;
        if (over)
        {
            if (!overTarget || (k.pc == *overTarget && k.sp >= sp))
                return it->first;
            continue;
        }
        if (returning && k.sp > returnSp)
            return it->first;
        returning = k.sp >= sp && k.isReturn;
        returnSp = k.sp;
    }
    return std::nullopt;
}

class TTD_CardStepForward_Test : public ::testing::TestWithParam<Card>
{
};
}  // namespace

/// The oracle: the recording's own trace of every card boundary (PC, SP) names where a live card step over / step out
/// from each boundary would end; the step through the history lands there, byte for byte the card a direct seek gives.
/// Over a CALL the card passes the routine (and a step over back from there returns to the CALL); a breakpoint inside
/// it ends the step there first; out of the routine the card stands after its return. At the history's end the step
/// goes no further (at_end: the debuggers step on live)
TEST_P(TTD_CardStepForward_Test, LandsWhereALiveStepWouldAndStopsAtTheHistorysEnd)
{
    Bench b;
    ASSERT_TRUE(Build(b, GetParam()));
    ASSERT_TRUE(LoadCardRoutines(b));
    const uint64_t perFrame = PerFrame(b);
    const uint64_t from = b.Count() + perFrame;
    const std::map<uint64_t, CardBoundary> trace = RecordTraced(b, 4, from, UINT64_MAX);
    const ttd::TTDTimePoint end = b.ttd->SessionEndPosition();
    ASSERT_TRUE(b.ttd->SeekTo({end.frame, 0}));
    const uint64_t endCount = b.ttd->CardPositionNow(b.cpu).instruction;
    ASSERT_GT(endCount, from + perFrame * 2);
    const std::string name = CardName(GetParam());

    // A CALL the card enters (its SP two lower at the next boundary, the next PC not after it), early in the window
    std::optional<uint64_t> call, plain;
    uint16_t callTarget = 0;
    for (uint64_t n = from + 1; n + 1 < from + perFrame && (!call || !plain); n++)
    {
        const CardBoundary& at = trace.at(n);
        const CardBoundary& next = trace.at(n + 1);
        uint16_t target = 0;
        const bool passes = b.target->StepOverTarget(b.card(), at.pc, target);
        if (!call && passes && static_cast<uint16_t>(at.sp - 2) == next.sp && next.pc != target &&
            ExpectedForward(trace, n, true, target, endCount))
            call = n, callTarget = target;
        if (!plain && !passes && !at.isReturn && n > from + 16)
            plain = n;
    }
    ASSERT_TRUE(call && plain) << name << ": the card calls its routines";

    auto check = [&](uint64_t n, bool over, uint64_t expected, const std::string& what) {
        ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, expected).Ok()) << what;
        const GSCardTarget::CardState direct = b.CardNow();
        const ttd::TTDTimePoint directMain = b.ttd->CurrentPosition();
        ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, n).Ok()) << what;
        const ttd::TTDCardMoveResult r = b.ttd->StepCardForward(b.cpu, over);
        ASSERT_TRUE(r.Ok()) << what << ": " << r.message;
        EXPECT_EQ(r.from, n) << what;
        EXPECT_EQ(r.position.instruction, expected) << what;
        EXPECT_EQ(r.position.pc, trace.at(expected).pc) << what;
        EXPECT_FALSE(r.historyEnd) << what;
        EXPECT_FALSE(r.stoppedByBreakpoint) << what;
        ExpectSameCard(b.CardNow(), direct, what);
        EXPECT_EQ(b.ttd->CurrentPosition().frame, directMain.frame) << what;
        EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, directMain.tInFrame) << what;
    };

    // Step over the CALL: past the whole routine
    const uint64_t overEnd = *ExpectedForward(trace, *call, true, callTarget, endCount);
    EXPECT_GT(overEnd, *call + 1) << "the routine ran in between";
    check(*call, true, overEnd, name + " over the CALL at " + std::to_string(*call));
    // Back over it again: a step over back from there names the CALL when the routine's return came just before
    bool backOver = false;
    if (const std::optional<uint64_t> back = ExpectedOver(trace, overEnd, backOver); back)
    {
        const ttd::TTDCardMoveResult r = b.ttd->ReverseStepOverCard(b.cpu);
        ASSERT_TRUE(r.Ok()) << r.message;
        EXPECT_EQ(r.position.instruction, *back);
        if (backOver)
            EXPECT_EQ(*back, *call) << "over and back over the same routine";
    }
    // Step over a plain instruction: one instruction
    check(*plain, true, *plain + 1, name + " over a plain instruction");
    // Step out from inside the routine: after its return
    const std::optional<uint64_t> outEnd = ExpectedForward(trace, *call + 1, false, std::nullopt, endCount);
    ASSERT_TRUE(outEnd) << name << ": the routine returns in the history";
    EXPECT_EQ(*outEnd, overEnd) << "out of the routine lands where over it does";
    check(*call + 1, false, *outEnd, name + " out of the routine");

    // A breakpoint at the routine's first instruction ends the step over there, as live
    BreakpointManager* breakpoints = b.context->pDebugManager->GetBreakpointsManager(b.cpu);
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = trace.at(*call + 1).pc;
    std::string error;
    const uint16_t id = breakpoints->AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;
    const uint32_t hitsBefore = breakpoints->GetBreakpointById(id)->hitCount;
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, *call).Ok());
    const ttd::TTDCardMoveResult hit = b.ttd->StepCardForward(b.cpu, true);
    ASSERT_TRUE(hit.Ok()) << hit.message;
    EXPECT_TRUE(hit.stoppedByBreakpoint);
    EXPECT_EQ(hit.breakpointId, id);
    EXPECT_EQ(hit.position.instruction, *call + 1);
    EXPECT_EQ(breakpoints->GetBreakpointById(id)->hitCount, hitsBefore) << "the search counts no hits";
    breakpoints->RemoveBreakpointByID(id);

    // Near the history's end: a step that does not end before it goes to its end; there nothing moves
    std::optional<uint64_t> late;
    for (uint64_t n = endCount - 1; n > endCount - 4000 && !late; n--)
        if (trace.count(n) && !ExpectedForward(trace, n, false, std::nullopt, endCount))
            late = n;
    if (late)
    {
        ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, *late).Ok());
        const ttd::TTDCardMoveResult r = b.ttd->StepCardForward(b.cpu, false);
        ASSERT_TRUE(r.Ok()) << r.message;
        EXPECT_EQ(r.position.instruction, endCount) << name << ": step out from " << *late;
        EXPECT_TRUE(r.historyEnd || r.position.instruction == endCount);
    }
    else
        ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, endCount).Ok());
    const GSCardTarget::CardState atEnd = b.CardNow();
    for (const bool over : {true, false})
    {
        const ttd::TTDCardMoveResult r = b.ttd->StepCardForward(b.cpu, over);
        EXPECT_EQ(r.status, ttd::TTDCardMoveResult::Status::AtEnd) << r.message;
        EXPECT_EQ(r.from, endCount);
        ExpectSameCard(b.CardNow(), atEnd, "at the history's end");
        EXPECT_EQ(b.ttd->CardPositionNow(b.cpu).instruction, endCount);
    }
}

INSTANTIATE_TEST_SUITE_P(Cards, TTD_CardStepForward_Test, ::testing::Values(Card::GS, Card::NeoGS, Card::MultiSound),
                         [](const ::testing::TestParamInfo<Card>& info) { return std::string(CardName(info.param)); });

/// From a main position the step starts where the card stands caught up to the main CPU's moment; no history, no
/// card: refused, nothing moved
TEST(TTD_CardStepForward_Edges_Test, FromAMainPositionAndTheRefusals)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::GS));
    ASSERT_TRUE(LoadCardRoutines(b));
    EXPECT_EQ(b.ttd->StepCardForward(b.cpu, true).status, ttd::TTDCardMoveResult::Status::NoHistory);
    EXPECT_EQ(b.ttd->StepCardForward(DebugCpuId::MultiSoundGS, true).status, ttd::TTDCardMoveResult::Status::NoCard);
    const uint64_t perFrame = PerFrame(b);
    const uint64_t from = b.Count();
    const std::map<uint64_t, CardBoundary> trace = RecordTraced(b, 3, from, from + perFrame * 3);

    const uint64_t first = b.ttd->GetEarliestRecordedFrame();
    ASSERT_TRUE(b.ttd->SeekTo({first + 1, 1000}));
    ASSERT_FALSE(b.ttd->AtCardPosition());
    const ttd::TTDCardMoveResult r = b.ttd->StepCardForward(b.cpu, true);
    ASSERT_TRUE(r.Ok()) << r.message;
    ASSERT_TRUE(trace.count(r.from)) << "card instruction " << r.from << " in the trace";
    uint16_t target = 0;
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, r.from).Ok());
    const bool passes = b.target->StepOverTarget(b.card(), trace.at(r.from).pc, target);
    const std::optional<uint64_t> expected =
        ExpectedForward(trace, r.from, true, passes ? std::optional<uint16_t>(target) : std::nullopt, UINT64_MAX);
    ASSERT_TRUE(expected);
    EXPECT_EQ(r.position.instruction, *expected);
}

// ---------------------------------------------------------------------------------------------------------------------
// The card's run to the cursor through the history (RunCardToCursor, the debuggers' F4 on a card's tab in the past)
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
/// The first boundary after n in the trace with the card's PC at pc (nullopt: none before end)
std::optional<uint64_t> ExpectedCursor(const std::map<uint64_t, CardBoundary>& trace, uint64_t n, uint16_t pc, uint64_t end)
{
    for (auto it = trace.upper_bound(n); it != trace.end() && it->first < end; ++it)
        if (it->second.pc == pc)
            return it->first;
    return std::nullopt;
}

class TTD_CardRunToCursor_Test : public ::testing::TestWithParam<Card>
{
};
}  // namespace

/// The oracle: the recording's own trace names the card's next boundary at the cursor; the run lands there, byte for
/// byte the card a direct seek gives, the main CPU at that boundary. A card breakpoint on the way ends it first; a
/// cursor the card never reaches answers no_hit and moves nothing (max_frames bounds the search: truncated); at the
/// history's end nothing moves (at_end: the debuggers run on live)
TEST_P(TTD_CardRunToCursor_Test, LandsAtTheCardsNextInstructionThereOrMovesNothing)
{
    Bench b;
    ASSERT_TRUE(Build(b, GetParam()));
    ASSERT_TRUE(LoadCardRoutines(b));
    const uint64_t perFrame = PerFrame(b);
    const uint64_t from = b.Count() + perFrame;
    const std::map<uint64_t, CardBoundary> trace = RecordTraced(b, 4, from, UINT64_MAX);
    const ttd::TTDTimePoint end = b.ttd->SessionEndPosition();
    ASSERT_TRUE(b.ttd->SeekTo({end.frame, 0}));
    const uint64_t endCount = b.ttd->CardPositionNow(b.cpu).instruction;
    ASSERT_GT(endCount, from + perFrame * 2);
    const std::string name = CardName(GetParam());

    // From the outer loop's CALL (PC #4000): to the innermost routine, and to the outer loop's NOP after the calls
    std::optional<uint64_t> start;
    for (uint64_t n = from + 1; n < from + perFrame && !start; n++)
        if (trace.count(n) && trace.at(n).pc == 0x4000)
            start = n;
    ASSERT_TRUE(start) << name << ": the card runs its outer loop";
    for (const uint16_t cursor : {uint16_t(0x4020), uint16_t(0x4003), uint16_t(0x4000)})
    {
        const std::string what = name + " to #" + std::to_string(cursor);
        const std::optional<uint64_t> expected = ExpectedCursor(trace, *start, cursor, endCount);
        ASSERT_TRUE(expected) << what;
        ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, *expected).Ok()) << what;
        const GSCardTarget::CardState direct = b.CardNow();
        const ttd::TTDTimePoint directMain = b.ttd->CurrentPosition();
        ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, *start).Ok()) << what;
        const ttd::TTDCardMoveResult r = b.ttd->RunCardToCursor(b.cpu, cursor);
        ASSERT_TRUE(r.Ok()) << what << ": " << r.message;
        EXPECT_EQ(r.from, *start) << what;
        EXPECT_EQ(r.position.instruction, *expected) << what;
        EXPECT_EQ(r.position.pc, cursor) << what;
        EXPECT_FALSE(r.stoppedByBreakpoint) << what;
        ExpectSameCard(b.CardNow(), direct, what);
        EXPECT_EQ(b.ttd->CurrentPosition().frame, directMain.frame) << what;
        EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, directMain.tInFrame) << what;
    }

    // A breakpoint on the way (the middle routine's first instruction) ends it there first, counting no hits
    BreakpointManager* breakpoints = b.context->pDebugManager->GetBreakpointsManager(b.cpu);
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = 0x4010;
    std::string error;
    const uint16_t id = breakpoints->AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;
    const uint32_t hitsBefore = breakpoints->GetBreakpointById(id)->hitCount;
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, *start).Ok());
    const ttd::TTDCardMoveResult hit = b.ttd->RunCardToCursor(b.cpu, 0x4020);
    ASSERT_TRUE(hit.Ok()) << hit.message;
    EXPECT_TRUE(hit.stoppedByBreakpoint);
    EXPECT_EQ(hit.breakpointId, id);
    EXPECT_EQ(hit.position.instruction, *ExpectedCursor(trace, *start, 0x4010, endCount));
    EXPECT_EQ(breakpoints->GetBreakpointById(id)->hitCount, hitsBefore) << "the search counts no hits";
    breakpoints->RemoveBreakpointByID(id);

    // Never reached: no_hit, the machine where it stood; one frame searched: truncated
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, *start).Ok());
    const GSCardTarget::CardState before = b.CardNow();
    const ttd::TTDTimePoint beforeMain = b.ttd->CurrentPosition();
    for (const uint64_t maxFrames : {uint64_t(1000), uint64_t(1)})
    {
        const ttd::TTDCardMoveResult miss = b.ttd->RunCardToCursor(b.cpu, 0x5000, maxFrames);
        EXPECT_EQ(miss.status, ttd::TTDCardMoveResult::Status::NoHit) << miss.message;
        EXPECT_FALSE(miss.Ok());
        EXPECT_EQ(miss.truncated, maxFrames == 1) << miss.message;
        EXPECT_NE(miss.message.find("#5000"), std::string::npos) << miss.message;
        ExpectSameCard(b.CardNow(), before, "not reached");
        EXPECT_EQ(b.ttd->CurrentPosition().frame, beforeMain.frame);
        EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, beforeMain.tInFrame);
        EXPECT_EQ(b.ttd->CardPositionNow(b.cpu).instruction, *start);
    }

    // At the history's end: nothing moves
    ASSERT_TRUE(b.ttd->SeekToCardInstruction(b.cpu, endCount).Ok());
    const GSCardTarget::CardState atEnd = b.CardNow();
    const ttd::TTDCardMoveResult r = b.ttd->RunCardToCursor(b.cpu, 0x4020);
    EXPECT_EQ(r.status, ttd::TTDCardMoveResult::Status::AtEnd) << r.message;
    ExpectSameCard(b.CardNow(), atEnd, "at the history's end");
    EXPECT_EQ(b.ttd->CardPositionNow(b.cpu).instruction, endCount);
}

INSTANTIATE_TEST_SUITE_P(Cards, TTD_CardRunToCursor_Test, ::testing::Values(Card::GS, Card::NeoGS, Card::MultiSound),
                         [](const ::testing::TestParamInfo<Card>& info) { return std::string(CardName(info.param)); });
