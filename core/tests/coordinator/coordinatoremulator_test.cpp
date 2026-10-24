/// @file coordinatoremulator_test.cpp
/// @brief The machine state coordinator wired into a real emulator (architecture §10.3): reset, snapshot load and the
/// media operations go through it - one logged operation each, today's rules, the TTD session effect at the owner's
/// commit point (no owner skips it: the log carries no contract note).
///
/// Boots a machine and records a few frames: slower than the 50 ms guideline, one acceptance check per operation.

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/coordinator/machinecoordinator.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/floppydriveslot.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"

using coordinator::MachineMode;
using coordinator::OperationLogEntry;
using coordinator::OpKind;
using coordinator::OpStatus;
using coordinator::SessionEffect;

class CoordinatorEmulator_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    uint64_t _since = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        EmulatorContext* context = _emulator->GetContext();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        _ttd = context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
    }

    void TearDown() override
    {
        if (_engineHooks && _emulator)
        {
            EmulatorContext* context = _emulator->GetContext();
            context->pTimeTravelManager->StopRecording();
            context->pTimeTravelHooks = _engineHooks;
        }
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// TTD v1 as the backend: it keeps no media change events, so it still refuses them while recording (goes with v1)
    void UseV1()
    {
        EmulatorContext* context = _emulator->GetContext();
        ASSERT_NE(context->pTimeTravelManager, nullptr);
        _engineHooks = context->pTimeTravelHooks;
        context->pTimeTravelHooks = context->pTimeTravelManager;
    }
    ttd::ITimeTravelHooks* _engineHooks = nullptr;

    /// Start counting log entries from here
    void Mark() { _since = _emulator->GetOperationLog().LastSeq(); }
    std::vector<OperationLogEntry> Logged() const { return _emulator->GetOperationLog().Entries(_since); }

    static std::string Data(const std::string& relative)
    {
        return (TestPathHelper::FindProjectRoot() / "testdata" / relative).string();
    }
    static std::string Garbage(const std::string& name)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        std::ofstream(path, std::ios::binary) << "this is not a medium of any kind";
        return path;
    }
};

TEST_F(CoordinatorEmulator_Test, AResetIsOneOperationThatEndsTheSessionAndKeepsTheHistory)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    EXPECT_EQ(_emulator->MachineOps().Mode(), MachineMode::Recording);

    Mark();
    _emulator->Reset();
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::Reset);
    EXPECT_EQ(log[0].status, OpStatus::Done);
    EXPECT_EQ(log[0].mode, MachineMode::Recording);
    EXPECT_EQ(log[0].effect, SessionEffect::EndKeepHistory);
    EXPECT_EQ(log[0].detail, "") << "the reset marks its commit point";
    EXPECT_FALSE(_ttd->IsRecording());
    EXPECT_TRUE(_ttd->HasHistory()) << "D42: the history of the same machine stays browsable";
    EXPECT_EQ(_ttd->GetSessionInfo().lastStopReason, "reset");
    EXPECT_EQ(_emulator->MachineOps().Mode(), MachineMode::Live);
}

TEST_F(CoordinatorEmulator_Test, TtdV1RefusesATapeWhileRecordingAndTheLogHasItBeforeTheFileIsOpened)
{
    UseV1();
    ASSERT_TRUE(_emulator->GetContext()->pTimeTravelManager->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    // Every surface asks the same question and gets the same sentence
    coordinator::MachineOp ask;
    ask.kind = OpKind::MediaInsert;
    ask.media = coordinator::MediaClass::Tape;
    const coordinator::OpResult would = _emulator->MachineOps().WouldRefuse(ask);
    ASSERT_EQ(would.status, OpStatus::Refused);

    Mark();
    std::string error;
    EXPECT_FALSE(_emulator->LoadTape(Garbage("refused.tzx"), &error));
    EXPECT_EQ(error, would.reason) << "a garbage file would fail to parse: the refusal came first";
    EXPECT_TRUE(_emulator->GetContext()->pTimeTravelManager->IsRecording());
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::MediaInsert);
    EXPECT_EQ(log[0].status, OpStatus::Refused);
    EXPECT_EQ(log[0].detail, would.reason);
}

TEST_F(CoordinatorEmulator_Test, ASurfaceThatAsksFirstGetsTheSameRefusalAndTheLogNamesIt)
{
    UseV1();   // the engine refuses no media change while recording (M3): v1 still does
    ASSERT_TRUE(_emulator->GetContext()->pTimeTravelManager->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    // A surface asks before its own preparation (an upload, a dialog): the sentence LoadTape / LoadDisk /
    // CreateBlankDisk give, logged under the asking thread's source
    coordinator::SourceScope scope(coordinator::OpSource::WebApi);
    Mark();
    const std::string tape = _emulator->AskMediaChange(Emulator::MediaChange::LoadTape, 0, "game.tzx");
    const std::string disk = _emulator->AskMediaChange(Emulator::MediaChange::LoadDisk, 1, "game.trd");
    const std::string blank = _emulator->AskMediaChange(Emulator::MediaChange::CreateDisk, 0);
    std::string error;
    EXPECT_FALSE(_emulator->LoadTape(Garbage("asked.tzx"), &error));
    EXPECT_EQ(tape, error);
    EXPECT_NE(disk.find("insert a disk"), std::string::npos) << disk;
    EXPECT_NE(blank.find("create a disk"), std::string::npos) << blank;

    const auto log = Logged();
    ASSERT_EQ(log.size(), 4u) << "every refusal is in the log, the ones a surface asked for too";
    for (const OperationLogEntry& e : log)
    {
        EXPECT_EQ(e.status, OpStatus::Refused);
        EXPECT_EQ(e.source, coordinator::OpSource::WebApi);
        EXPECT_EQ(e.mode, MachineMode::Recording);
    }
    EXPECT_EQ(log[0].summary, "tape game.tzx");
    EXPECT_EQ(log[1].summary, FloppyDriveSlot::IdFor(1) + " game.trd");
    EXPECT_EQ(log[2].kind, OpKind::DiskCreate);
    EXPECT_TRUE(_emulator->GetContext()->pTimeTravelManager->IsRecording());

    // Live: nothing to refuse, nothing logged
    _emulator->GetContext()->pTimeTravelManager->StopRecording();
    Mark();
    EXPECT_EQ(_emulator->AskMediaChange(Emulator::MediaChange::LoadTape, 0, "game.tzx"), "");
    EXPECT_TRUE(Logged().empty());
}

TEST_F(CoordinatorEmulator_Test, TheBlackBoxEndsAtATapeLoadAndTheLogSaysSo)
{
    _ttd->SetBlackBox(true, 1);
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    EXPECT_EQ(_emulator->MachineOps().Mode(), MachineMode::BlackBox);

    Mark();
    std::string error;
    ASSERT_TRUE(_emulator->LoadTape(Data("contention/rak-timing-test/timing.tap"), &error)) << error;
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::MediaInsert);
    EXPECT_EQ(log[0].status, OpStatus::Done);
    EXPECT_EQ(log[0].mode, MachineMode::BlackBox);
    EXPECT_EQ(log[0].effect, SessionEffect::MediaChanged);
    EXPECT_NE(log[0].summary.find("timing.tap"), std::string::npos) << log[0].summary;
    EXPECT_EQ(_ttd->GetSessionInfo().lastStopReason, "tape-load");
}

TEST_F(CoordinatorEmulator_Test, ASnapshotLoadEndsTheSessionAtItsCommitPoint)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    Mark();
    ASSERT_TRUE(_emulator->LoadSnapshot(Data("loaders/sna/7threality.sna")));
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::SnapshotLoad);
    EXPECT_EQ(log[0].status, OpStatus::Done);
    EXPECT_EQ(log[0].effect, SessionEffect::EndKeepHistory);
    EXPECT_EQ(log[0].detail, "") << "the pipeline's beforeCommit is the commit point";
    EXPECT_FALSE(_ttd->IsRecording());
    EXPECT_EQ(_ttd->GetSessionInfo().lastStopReason, "snapshot-load");
}

TEST_F(CoordinatorEmulator_Test, ASnapshotThatFailsKeepsTheRecording)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    Mark();
    EXPECT_FALSE(_emulator->LoadSnapshot(Garbage("broken.sna")));
    EXPECT_TRUE(_ttd->IsRecording());
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::SnapshotLoad);
    EXPECT_EQ(log[0].status, OpStatus::Failed);
    EXPECT_EQ(log[0].effect, SessionEffect::None);
}

TEST_F(CoordinatorEmulator_Test, DiscardingAFloppyIsAnInsertInsideADiscard)
{
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(Data("loaders/trd/EyeAche.trd"), 0, &error)) << error;

    Mark();
    const MediaResult discarded = _emulator->GetContext()->pMediaManager->Discard(FloppyDriveSlot::IdFor(0));
    ASSERT_TRUE(discarded.Ok()) << discarded.message;
    const auto log = Logged();
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0].kind, OpKind::MediaInsert) << "the disk opened again is logged first, inside the discard";
    EXPECT_EQ(log[0].effect, SessionEffect::MediaChanged);
    EXPECT_EQ(log[1].kind, OpKind::MediaDiscard);
    EXPECT_EQ(log[1].status, OpStatus::Done);
    EXPECT_EQ(log[1].effect, SessionEffect::None);
    EXPECT_EQ(log[1].detail, "no change");
}

TEST_F(CoordinatorEmulator_Test, AnEmptySlotsEjectIsNoOperation)
{
    ASSERT_TRUE(_ttd->StartRecording());
    Mark();
    EXPECT_TRUE(_emulator->GetContext()->pMediaManager->Eject(FloppyDriveSlot::IdFor(1)).Ok())
        << "nothing leaves: allowed while recording, as before";
    EXPECT_TRUE(Logged().empty());
    EXPECT_TRUE(_ttd->IsRecording());
}

// D43 (owner 2026-10-10): the network settings and an IDE unit's drive are fixed while a recording the user started
// runs; the black box ends its session as for a medium; an RZX restore is a snapshot load

TEST_F(CoordinatorEmulator_Test, TheNetworkSettingsAreFixedWhileARecordingTheUserStartedRuns)
{
    NetworkManager* network = _emulator->GetContext()->pCore->GetNetworkManager();
    ASSERT_NE(network, nullptr);
    NetworkManager::Change timeout;
    timeout.connectTimeoutMs = 5000;
    NetworkManager::Change listeners;
    listeners.remoteAccess = true;
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    Mark();
    std::string error;
    EXPECT_FALSE(network->RequestChange(timeout, error));
    EXPECT_EQ(error, "a TTD recording is running: the network settings are fixed until it stops");
    EXPECT_TRUE(network->RequestChange(listeners, error)) << "the listeners' address: nothing the machine sees";
    EXPECT_TRUE(_ttd->IsRecording());
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u) << "a host-side setting is no machine operation";
    EXPECT_EQ(log[0].kind, OpKind::NetworkChange);
    EXPECT_EQ(log[0].status, OpStatus::Refused);
    EXPECT_EQ(log[0].mode, MachineMode::Recording);
    EXPECT_EQ(log[0].detail, error);
}

TEST_F(CoordinatorEmulator_Test, TheBlackBoxEndsAtANetworkChange)
{
    NetworkManager* network = _emulator->GetContext()->pCore->GetNetworkManager();
    ASSERT_NE(network, nullptr);
    NetworkManager::Change timeout;
    timeout.connectTimeoutMs = 5000;
    _ttd->SetBlackBox(true, 1);
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    ASSERT_EQ(_emulator->MachineOps().Mode(), MachineMode::BlackBox);

    Mark();
    std::string error;
    ASSERT_TRUE(network->RequestChange(timeout, error)) << error;
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::NetworkChange);
    EXPECT_EQ(log[0].status, OpStatus::Done);
    EXPECT_EQ(log[0].mode, MachineMode::BlackBox);
    EXPECT_EQ(log[0].effect, SessionEffect::MediaChanged);
    EXPECT_EQ(log[0].detail, "") << "the change marks its commit point";
    EXPECT_EQ(_ttd->GetSessionInfo().lastStopReason, "network-change");
}

TEST_F(CoordinatorEmulator_Test, AnIdeUnitsDriveFollowsTheSameRule)
{
    IdeController* ide = _emulator->GetContext()->pIdeController;
    ASSERT_NE(ide, nullptr);
    ASSERT_EQ(ide->KindOf(1), IdeController::UnitKind::Disk);
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    Mark();
    std::string error;
    EXPECT_FALSE(ide->SetUnitKind(1, IdeController::UnitKind::Cdrom, &error));
    EXPECT_EQ(error, "a TTD recording runs: the machine's hardware is fixed until it stops");
    EXPECT_EQ(ide->KindOf(1), IdeController::UnitKind::Disk);
    _ttd->StopRecording();

    _ttd->SetBlackBox(true, 1);
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    ASSERT_EQ(_emulator->MachineOps().Mode(), MachineMode::BlackBox);
    ASSERT_TRUE(ide->SetUnitKind(1, IdeController::UnitKind::Cdrom, &error)) << error;
    EXPECT_EQ(ide->KindOf(1), IdeController::UnitKind::Cdrom);
    EXPECT_EQ(_ttd->GetSessionInfo().lastStopReason, "ide-drive-kind");

    const auto log = Logged();
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0].kind, OpKind::IdeDriveKind);
    EXPECT_EQ(log[0].status, OpStatus::Refused);
    EXPECT_EQ(log[1].status, OpStatus::Done);
    EXPECT_EQ(log[1].mode, MachineMode::BlackBox);
    EXPECT_EQ(log[1].effect, SessionEffect::MediaChanged);
    EXPECT_EQ(log[1].summary, "ide0.slave cdrom");
}

TEST_F(CoordinatorEmulator_Test, AnRzxSeekBackIsASnapshotLoad)
{
    // Its own machine: an RZX recording plays on a 48K
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    _emulator->GetContext()->pMemory->UpdateFeatureCache();
    _ttd = _emulator->GetContext()->pTimeTravelController;
    const rzx::PlayResult played = _emulator->PlayRzx(Data("loaders/rzx/archive/ericfloaters.rzx"));
    ASSERT_TRUE(played.Ok()) << played.message;
    _emulator->RunNFrames(30, /*skipBreakpoints=*/true);
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    ASSERT_EQ(_emulator->MachineOps().Mode(), MachineMode::Recording);

    Mark();
    std::string error;
    ASSERT_TRUE(_emulator->SeekRzx(10, &error)) << error;
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::RzxRestore);
    EXPECT_EQ(log[0].status, OpStatus::Done);
    EXPECT_EQ(log[0].mode, MachineMode::Recording);
    EXPECT_EQ(log[0].effect, SessionEffect::EndKeepHistory);
    EXPECT_EQ(log[0].summary, "seek to frame 10");
    EXPECT_FALSE(_ttd->IsRecording());
    EXPECT_TRUE(_ttd->HasHistory()) << "D42: the history of the same machine stays browsable";
    EXPECT_EQ(_ttd->GetSessionInfo().lastStopReason, "rzx-seek");
}

TEST_F(CoordinatorEmulator_Test, AModelSwitchIsAskedOfTheCoordinatorAndARefusalIsLogged)
{
    // The engine: a change of the machine itself ends the session, never refused (owner rule 2026-10-05)
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    Mark();
    EXPECT_EQ(_emulator->AskModelSwitch("to TSL"), "");
    EXPECT_TRUE(Logged().empty()) << "a yes is not logged: the switch replaces the instance and its log";
    _ttd->StopRecording();

    // TTD v1 refuses it while recording (D26): the emulator's owner refusal, logged with the sentence
    EmulatorContext* context = _emulator->GetContext();
    ASSERT_NE(context->pTimeTravelManager, nullptr);
    ttd::ITimeTravelHooks* engine = context->pTimeTravelHooks;
    context->pTimeTravelHooks = context->pTimeTravelManager;
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    const std::string refusal = _emulator->AskModelSwitch("to TSL");
    context->pTimeTravelManager->StopRecording();
    context->pTimeTravelHooks = engine;

    EXPECT_NE(refusal.find("model"), std::string::npos) << refusal;
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::ModelSwitch);
    EXPECT_EQ(log[0].status, OpStatus::Refused);
    EXPECT_EQ(log[0].summary, "to TSL");
    EXPECT_EQ(log[0].detail, refusal);
}

// M3 (Q4, owner 2026-10-10): a media change while the user's recording runs is an event of the recording - the
// history keeps that the medium changed and what came in, then ports and sectors only

TEST_F(CoordinatorEmulator_Test, ADiskChangedWhileRecordingIsAnEventAtTheFrameBoundaryWhereItLands)
{
    EmulatorContext* context = _emulator->GetContext();
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);

    Mark();
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(Data("loaders/trd/EyeAche.trd"), 0, &error)) << error;
    EXPECT_EQ(context->coreState.diskDrives[0]->getDiskImage(), nullptr) << "never inside a frame";
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    const uint64_t landed = context->emulatorState.frame_counter;
    EXPECT_NE(context->coreState.diskDrives[0]->getDiskImage(), nullptr) << "landed at the boundary";
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    EXPECT_TRUE(_ttd->IsRecording()) << "the recording goes on";
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].status, OpStatus::Done);
    EXPECT_EQ(log[0].mode, MachineMode::Recording);
    EXPECT_EQ(log[0].effect, SessionEffect::None) << "the session sees nothing at the commit";

    size_t changes = 0;
    for (const ttd::TTDEvent& ev : _ttd->GetEngine().Events().Events())
        changes += ev.kind == ttd::TTDEventKind::MediaChange;
    EXPECT_EQ(changes, 1u) << "one event, where it landed (frame " << landed << ")";
}

TEST_F(CoordinatorEmulator_Test, BeforeTheFrameADiskCameInTheHistoryHasNoDiskThere)
{
    EmulatorContext* context = _emulator->GetContext();
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(Data("loaders/trd/atarin.trd"), 0, &error)) << error;
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    DiskImage* first = context->coreState.diskDrives[0]->getDiskImage();
    ASSERT_NE(first, nullptr);

    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    const uint64_t before = context->emulatorState.frame_counter;
    ASSERT_TRUE(_emulator->LoadDisk(Data("loaders/trd/EyeAche.trd"), 0, &error)) << error;
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    const uint64_t landed = context->emulatorState.frame_counter;
    DiskImage* second = context->coreState.diskDrives[0]->getDiskImage();
    ASSERT_NE(second, nullptr);
    ASSERT_NE(second, first);
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    // Back before the change: the disk that came later is not in the drive there - the guest's writes of that time
    // never reach it; the CPU gets the recorded exchange
    ASSERT_TRUE(_ttd->SeekTo({before, 0}));
    EXPECT_EQ(context->coreState.diskDrives[0]->getDiskImage(), nullptr);
    EXPECT_NE(context->pMediaManager->GetMedium("fdd.a"), nullptr) << "the slot keeps its medium";

    // From the frame it came in on, it is there again
    ASSERT_TRUE(_ttd->SeekTo({landed + 2, 0}));
    EXPECT_EQ(context->coreState.diskDrives[0]->getDiskImage(), second);
}

/// M4, current-state §7 item 6: the feature manager is the one writer of the CPU's debug mode. A replay holds it on
/// whatever the 'debugmode' feature says, a feature change during the replay does not drop the hold, and when the
/// replay ends the feature has its say again
TEST_F(CoordinatorEmulator_Test, AReplayHoldsDebugModeAndTheFeatureHasItsSayAfter)
{
    EmulatorContext* context = _emulator->GetContext();
    FeatureManager* features = _emulator->GetFeatureManager();
    Z80* z80 = context->pCore->GetZ80();
    features->setFeature(Features::kDebugMode, false);
    ASSERT_FALSE(z80->isDebugMode);

    // A replay begins (what EnterReplayMode reports); a feature change keeps the hold
    context->ttdReplayActive = true;
    _emulator->MachineOps().SessionChanged();
    EXPECT_TRUE(_emulator->MachineOps().Published().replaying);
    EXPECT_TRUE(z80->isDebugMode) << "held for the replay";
    features->setFeature(Features::kDebugMode, false);
    EXPECT_TRUE(z80->isDebugMode) << "a feature change during the replay keeps the hold";
    context->ttdReplayActive = false;
    _emulator->MachineOps().SessionChanged();
    EXPECT_FALSE(z80->isDebugMode);

    features->setFeature(Features::kDebugMode, true);
    EXPECT_TRUE(z80->isDebugMode);
}

// M6: a change of the machine itself is an operation of its own - the General Sound card here, the ROM a reset reads
// again (nested in the reset) - and ends the session with its history: it belongs to the machine before the change
TEST_F(CoordinatorEmulator_Test, AChangeOfTheMachineItselfEndsTheSessionAndDropsItsHistory)
{
    // Its own machine, with a General Sound card (the runner leaves the sound cards out)
    EmulatorTestHelper::CleanupEmulator(_emulator);
    {
        SoundCardScope cards{TestSound::GeneralSound};
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    }
    ASSERT_NE(_emulator, nullptr);
    _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    _emulator->GetContext()->pMemory->UpdateFeatureCache();
    _ttd = _emulator->GetContext()->pTimeTravelController;
    SoundManager* sound = _emulator->GetContext()->pSoundManager;
    ASSERT_NE(sound, nullptr);
    ASSERT_TRUE(FitGeneralSoundCard(sound, GSTypeKind::Z80));
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);

    Mark();
    ASSERT_TRUE(sound->switchGeneralSoundCard(GSTypeKind::LW));
    auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::GsCardSwitch);
    EXPECT_EQ(log[0].status, OpStatus::Done);
    EXPECT_EQ(log[0].mode, MachineMode::Recording);
    EXPECT_EQ(log[0].effect, SessionEffect::MachineChanged);
    EXPECT_EQ(log[0].detail, "");
    EXPECT_FALSE(_ttd->IsRecording());
    EXPECT_FALSE(_ttd->HasHistory());
    EXPECT_EQ(_ttd->GetSessionInfo().lastDropReason, "gs-card-switch");

    // The ROM reload a reset carries out: inside the reset, logged before it (it finishes first)
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    Mark();
    _emulator->RequestRomReload();
    _emulator->Reset();
    log = Logged();
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0].kind, OpKind::RomLoad);
    EXPECT_EQ(log[0].effect, SessionEffect::MachineChanged);
    EXPECT_EQ(log[0].mode, MachineMode::Live) << "the reset ended the recording before";
    EXPECT_EQ(log[1].kind, OpKind::Reset);
    EXPECT_FALSE(_ttd->HasHistory()) << "the history relied on the old ROM";
    EXPECT_EQ(_ttd->GetSessionInfo().lastDropReason, "rom-reload");
    EXPECT_FALSE(_emulator->RomReloadPending());
}

// A history the user recorded keeps its 1x: a speed change is refused with the reason (frame timing is part of the
// determinism contract, so the control goes, not the history); without the history the change is a logged op again
TEST_F(CoordinatorEmulator_Test, ASpeedChangeIsRefusedWhileAHistoryIsKeptAndLoggedWithout)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    _ttd->StopRecording();
    ASSERT_TRUE(_ttd->HasHistory());

    Mark();
    EXPECT_FALSE(_emulator->SetSpeedMultiplier(2));
    EXPECT_TRUE(_ttd->HasHistory()) << "the history stays";
    EXPECT_EQ(_emulator->SpeedLockReason(), "TTD holds a history recorded at 1x: discard it to change the speed");
    EXPECT_TRUE(_emulator->SetSpeedMultiplier(1)) << "re-selecting the current speed is not a change";
    EXPECT_TRUE(Logged().empty());

    _ttd->InvalidateSession("test");
    ASSERT_FALSE(_ttd->HasHistory());
    EXPECT_TRUE(_emulator->SpeedLockReason().empty());
    Mark();
    ASSERT_TRUE(_emulator->SetSpeedMultiplier(2));
    ASSERT_TRUE(_emulator->SetSpeedMultiplier(1));
    const auto log = Logged();
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0].kind, OpKind::SpeedChange);
    EXPECT_EQ(log[0].summary, "2x");
    EXPECT_EQ(log[0].effect, SessionEffect::TimingChanged);
    EXPECT_EQ(log[1].summary, "1x");
}

// M6: TTD v1 refuses a change of the machine itself while recording (D26), as the emulator's owner refusal, logged;
// the ROM is not touched and the machine is not paused
TEST_F(CoordinatorEmulator_Test, TtdV1RefusesARomLoadWhileRecordingAndTheLogHasIt)
{
    UseV1();
    ASSERT_TRUE(_emulator->GetContext()->pTimeTravelManager->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    Mark();
    EXPECT_FALSE(_emulator->LoadROM("any.rom"));
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::RomLoad);
    EXPECT_EQ(log[0].status, OpStatus::Refused);
    EXPECT_EQ(log[0].summary, "any.rom");
    EXPECT_NE(log[0].detail.find("ROM"), std::string::npos) << log[0].detail;
    EXPECT_TRUE(_emulator->GetContext()->pTimeTravelManager->IsRecording());
}

// M6: a reset leaves the path an RZX playback drives - the coordinator ends the playback first, the caller does not
TEST_F(CoordinatorEmulator_Test, AResetEndsAnRzxPlaybackFirst)
{
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    const rzx::PlayResult played = _emulator->PlayRzx(Data("loaders/rzx/archive/ericfloaters.rzx"));
    ASSERT_TRUE(played.Ok()) << played.message;
    _emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    ASSERT_TRUE(_emulator->IsRzxPlaying());

    Mark();
    _emulator->Reset();
    EXPECT_FALSE(_emulator->IsRzxPlaying());
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::Reset);
    EXPECT_EQ(log[0].status, OpStatus::Done);
}

// M6: a tool's edit is an operation: logged with its source, recorded by a session (the register writes of the
// scripts and the WebAPI were not before); live it is written as it comes
TEST_F(CoordinatorEmulator_Test, AToolsEditIsLoggedAndARegisterWriteIsOneToo)
{
    Mark();
    ASSERT_TRUE(_emulator->SetRegisterFromTool("test register", "BC", 0x1234));
    EXPECT_EQ(_emulator->GetZ80State()->bc, 0x1234);
    EXPECT_FALSE(_emulator->SetRegisterFromTool("test register", "IM", 3)) << "IM 3 is no mode";
    EXPECT_FALSE(_emulator->SetRegisterFromTool("test register", "nonsense", 1));
    auto log = Logged();
    ASSERT_EQ(log.size(), 1u) << "a refused value changes nothing and is no operation";
    EXPECT_EQ(log[0].kind, OpKind::ToolEdit);
    EXPECT_EQ(log[0].summary, "test register");
    EXPECT_EQ(log[0].mode, MachineMode::Live);

    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    const size_t eventsBefore = _ttd->GetExternalEvents().Size();
    Mark();
    ASSERT_TRUE(_emulator->SetRegisterFromTool("test register", "DE", 0x4321));
    log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].mode, MachineMode::Recording);
    EXPECT_EQ(log[0].status, OpStatus::Done);
    EXPECT_GT(_ttd->GetExternalEvents().Size(), eventsBefore) << "the recording keeps the edit";
    EXPECT_TRUE(_ttd->IsRecording());
}

// M6: TTD's verbs are operations of the coordinator on TTD's own lock: each in the log, a failure with its reason
TEST_F(CoordinatorEmulator_Test, TtdVerbsAreLogged)
{
    ttd::TTDControl control(_emulator->GetContext());
    Mark();
    ASSERT_TRUE(control.Execute({"start", {}}).Ok());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    ASSERT_TRUE(control.Execute({"stop", {}}).Ok());
    ASSERT_TRUE(control.Execute({"seek", {{"frame", std::to_string(_ttd->CurrentFrame() - 2)}}}).Ok());
    const ttd::TTDReply bad = control.Execute({"seek", {{"bookmark", "nowhere"}}});
    EXPECT_FALSE(bad.Ok());
    ASSERT_TRUE(control.Execute({"status", {}}).Ok());

    const auto log = Logged();
    ASSERT_EQ(log.size(), 4u) << "status reads, it is no operation";
    EXPECT_EQ(log[0].kind, OpKind::TtdStart);
    EXPECT_EQ(log[0].mode, MachineMode::Live);
    EXPECT_EQ(log[1].kind, OpKind::TtdStop);
    EXPECT_EQ(log[1].mode, MachineMode::Recording);
    EXPECT_EQ(log[2].kind, OpKind::TimelineSeek);
    EXPECT_EQ(log[2].status, OpStatus::Done);
    EXPECT_EQ(log[3].kind, OpKind::TimelineSeek);
    EXPECT_EQ(log[3].status, OpStatus::Failed);
    EXPECT_EQ(log[3].detail, bad.message);
    EXPECT_EQ(log[3].summary, "seek bookmark=nowhere");
}

// M6 (owner 2026-10-11): in the recorded past a tool's edit is refused - nothing would keep it (the next seek sets the
// point back); the surface gets the reason, the memory stays, the log has the refusal
TEST_F(CoordinatorEmulator_Test, AToolsEditInTheRecordedPastIsRefused)
{
    ttd::TTDControl control(_emulator->GetContext());
    ASSERT_TRUE(control.Execute({"start", {}}).Ok());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    ASSERT_TRUE(control.Execute({"stop", {}}).Ok());
    ASSERT_TRUE(control.Execute({"seek", {{"frame", std::to_string(_ttd->CurrentFrame() - 2)}}}).Ok());
    ASSERT_EQ(_emulator->MachineOps().Mode(), MachineMode::Detached);

    Memory* memory = _emulator->GetMemory();
    const uint8_t before = memory->DirectReadFromZ80Memory(0x8000);
    Mark();
    const std::string refused = _emulator->EditMemoryFromTool(
        "test poke", [&]() { memory->DirectWriteToZ80Memory(0x8000, static_cast<uint8_t>(before ^ 0xFF)); });
    EXPECT_NE(refused.find("recorded history"), std::string::npos) << refused;
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x8000), before);
    std::string registerRefusal;
    EXPECT_FALSE(_emulator->SetRegisterFromTool("test register", "BC", 0x1234, &registerRefusal));
    EXPECT_EQ(registerRefusal, refused);
    const auto log = Logged();
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0].kind, OpKind::ToolEdit);
    EXPECT_EQ(log[0].status, OpStatus::Refused);
    EXPECT_EQ(log[0].mode, MachineMode::Detached);
    EXPECT_EQ(log[0].detail, refused);
}
