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
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/coordinator/machinecoordinator.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/floppydriveslot.h"
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
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

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

TEST_F(CoordinatorEmulator_Test, ATapeDuringARecordingIsRefusedAndLoggedBeforeItsFileIsOpened)
{
    ASSERT_TRUE(_ttd->StartRecording());
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
    EXPECT_TRUE(_ttd->IsRecording());
    const auto log = Logged();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].kind, OpKind::MediaInsert);
    EXPECT_EQ(log[0].status, OpStatus::Refused);
    EXPECT_EQ(log[0].detail, would.reason);
}

TEST_F(CoordinatorEmulator_Test, ASurfaceThatAsksFirstGetsTheSameRefusalAndTheLogNamesIt)
{
    ASSERT_TRUE(_ttd->StartRecording());
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
    EXPECT_TRUE(_ttd->IsRecording());

    // Live: nothing to refuse, nothing logged
    _ttd->StopRecording();
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
