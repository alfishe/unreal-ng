#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/ttdcardbench.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

/// Time travel while the machine stands inside the sound card's run (TTD branches debuggers.md §7): a General Sound
/// breakpoint that fires in the card's catch-up at the frame end parks the machine's thread there, its frame counter
/// already moved and the frame's boundary work not done. A recording started there begins at the next frame boundary
/// (before the fix its baseline was taken there and the first boundary refused the frame: capture-failed). A move in
/// time there first leaves it: the machine runs on to the next main CPU instruction start and pauses (D-3, the moves
/// after a live card step at the history's end).
///
/// The main CPU runs a loop that never touches the card's ports, so the card catches up only at the frame end.
/// Runtime: the machine at real speed for about 5 frames (~0.1 s)
namespace
{
ttd::TTDReply Verb(EmulatorContext* context, const std::string& verb, const std::map<std::string, std::string>& options = {})
{
    return ttd::TTDControl(context).Execute({verb, options});
}
}  // namespace

TEST(TTDControl_CardPark_Test, ARecordingAskedInsideTheCardsFrameEndRunStartsAtTheNextBoundary)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS));
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    emulator->RunNFrames(2);

    // DI; loop: JR loop - no port access, the card runs only in the frame end's catch-up
    Z80* z80 = context->pCore->GetZ80();
    z80->DirectWrite(0x8000, 0xF3);
    z80->DirectWrite(0x8001, 0x18);
    z80->DirectWrite(0x8002, 0xFE);
    z80->pc = 0x8000;

    // The card's firmware loops (no SD card): its PC comes round again within the next catch-up
    Z80CpuRegisters regs{};
    ASSERT_TRUE(context->pSoundManager->getGeneralSound()->cardRegisters(regs));
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = regs.pc;
    std::string error;
    BreakpointManager* breakpoints = context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS);
    const uint16_t id = breakpoints->AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;

    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return emulator->IsParkedInsideCardRun(); }, std::chrono::milliseconds(5000)))
        << "the card's breakpoint did not stop the machine";

    // The start waits for the frame boundary: the session is still idle, no checkpoint taken here
    const ttd::TTDReply start = Verb(context, "start");
    ASSERT_TRUE(start.Ok()) << start.message;
    EXPECT_TRUE(start.body.find("start_pending")->b);
    EXPECT_TRUE(start.body.find("started")->b);
    EXPECT_EQ(start.body.find("state")->s, "idle");
    const ttd::TTDReply status = Verb(context, "status");
    EXPECT_TRUE(status.body.find("start_pending")->b);
    EXPECT_EQ(status.body.find("checkpoint_count")->i, 0);

    // Leaving it: the recording begins at the boundary and goes on (not capture-failed at its first frame)
    breakpoints->RemoveBreakpointByID(id);
    emulator->Resume();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    ASSERT_TRUE(TestWait::For([&] { return ttd->ReadSessionInfo().checkpointCount >= 3 || !ttd->ReadSessionInfo().lastStopReason.empty(); },
                              std::chrono::milliseconds(5000)));
    const ttd::TTDSessionInfo info = ttd->ReadSessionInfo();
    EXPECT_EQ(info.lastStopReason, "");
    EXPECT_GE(info.checkpointCount, 3u);
    EXPECT_FALSE(info.startPending);
    EXPECT_TRUE(ttd->IsRecording());

    emulator->Stop();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A stop before the boundary cancels the start that waits for it
TEST(TTDControl_CardPark_Test, AStopCancelsTheStartThatWaitsForTheBoundary)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS));
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->RunNFrames(2);

    Z80CpuRegisters regs{};
    ASSERT_TRUE(context->pSoundManager->getGeneralSound()->cardRegisters(regs));
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = regs.pc;
    std::string error;
    BreakpointManager* breakpoints = context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS);
    const uint16_t id = breakpoints->AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;

    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return emulator->IsParkedInsideCardRun(); }, std::chrono::milliseconds(5000)));
    ASSERT_TRUE(Verb(context, "start").Ok());
    ASSERT_TRUE(Verb(context, "stop").Ok());
    EXPECT_FALSE(Verb(context, "status").body.find("start_pending")->b);

    breakpoints->RemoveBreakpointByID(id);
    const uint64_t from = context->emulatorState.frame_counter;
    emulator->Resume();
    ASSERT_TRUE(TestWait::For([&] { return context->emulatorState.frame_counter >= from + 2; },
                              std::chrono::milliseconds(5000)));
    EXPECT_FALSE(context->pTimeTravelController->IsRecording()) << "nothing records unless asked";

    emulator->Stop();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// D-3: a live card step at the history's end (F7 on the card's tab: RunControl::CardStep) parks the machine inside
/// the card's run; a move in time from there (a main frame step back, a card instruction step back) leaves it first
/// and goes on, no 409. A card step back from past the history's end lands at the history's newest card instruction
/// (history_end), as the main CPU's step back from a live position lands on its last recorded one.
/// Runtime: 3 recorded frames, the machine resumed twice up to its next main instruction (~0.1 s)
TEST(TTDControl_CardPark_Test, AMoveAfterALiveCardStepLeavesTheCardsRunFirst)
{
    ttdtest::Bench b;
    ASSERT_TRUE(ttdtest::Build(b, ttdtest::Card::GS));
    ASSERT_TRUE(ttdtest::LoadCardRoutines(b));
    ttdtest::Record(b, 3);
    const ttd::TTDTimePoint end = b.ttd->SessionEndPosition();
    std::shared_ptr<Emulator> shared = EmulatorManager::GetInstance()->GetEmulator(b.emulator->GetId());
    ASSERT_NE(shared, nullptr);
    b.emulator->StartAsync();
    b.emulator->Pause();
    ASSERT_TRUE(TestWait::For([&] { return b.emulator->IsEmulationParked(); }, std::chrono::milliseconds(5000)));

    execution::RunControl control(shared);
    const execution::Requester requester{execution::Origin::WebApi, {}};
    auto liveStep = [&]() {
        const execution::RunReply step = control.CardStep(requester, b.cpu, "in", 1);
        ASSERT_TRUE(step.Ok()) << step.message;
        ASSERT_TRUE(b.emulator->IsParkedInsideCardRun()) << "a card step parks inside the card's run";
    };

    // A main frame step back
    liveStep();
    const ttd::TTDReply back = Verb(b.context, "step-back");
    ASSERT_TRUE(back.Ok()) << back.message;
    EXPECT_FALSE(b.emulator->IsParkedInsideCardRun());
    EXPECT_TRUE(b.emulator->IsPaused());
    EXPECT_LT(b.ttd->CurrentPosition().frame, end.frame + 1) << "in the history";

    // Back to the end, a live card step again, a card instruction step back: the history's newest card instruction
    ASSERT_TRUE(Verb(b.context, "seek", {{"frame", std::to_string(end.frame)}, {"tinframe", std::to_string(end.tInFrame)}}).Ok());
    liveStep();
    uint64_t live = 0;
    ASSERT_TRUE(b.card()->cardInstructionCount(live));
    const ttd::TTDReply cardBack = Verb(b.context, "step-instruction", {{"cpu", "gs"}, {"dir", "back"}});
    ASSERT_TRUE(cardBack.Ok()) << cardBack.message;
    EXPECT_EQ(cardBack.body.find("status")->s, "ok") << cardBack.body.find("message")->s;
    ASSERT_NE(cardBack.body.find("history_end"), nullptr);
    EXPECT_TRUE(cardBack.body.find("history_end")->b);
    EXPECT_LT(static_cast<uint64_t>(cardBack.body.find("target")->i), live);
    EXPECT_FALSE(b.emulator->IsParkedInsideCardRun());

    b.emulator->Stop();
}
