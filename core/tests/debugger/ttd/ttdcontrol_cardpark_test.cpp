#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

#include "_helpers/emulatortesthelper.h"
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
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"

/// Time travel while the machine stands inside the sound card's run (TTD branches debuggers.md §7): a General Sound
/// breakpoint that fires in the card's catch-up at the frame end parks the machine's thread there, its frame counter
/// already moved and the frame's boundary work not done. A recording started there begins at the next frame boundary
/// (before the fix its baseline was taken there and the first boundary refused the frame: capture-failed); a move in
/// time there is refused until the machine leaves it.
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

    // A move in time is refused while the machine stands there
    const ttd::TTDReply back = Verb(context, "step-back");
    EXPECT_FALSE(back.Ok());
    EXPECT_EQ(back.error, ttd::TTDControlError::Conflict);
    EXPECT_NE(back.message.find("sound card"), std::string::npos) << back.message;

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
