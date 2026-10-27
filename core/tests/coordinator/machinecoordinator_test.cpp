/// @file machinecoordinator_test.cpp
/// @brief The machine state coordinator alone, every port a fake (architecture §10.1): the lifecycle of Run
/// (refuse, prepare, commit point, commit, abort), parking, WouldRefuse, wiring, the log, nesting and threads.
/// Each test compares the sequence of calls across all fakes.

#include <gtest/gtest.h>

#include <algorithm>
#include <any>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "coordinatorfakes.h"
#include "emulator/coordinator/machinecoordinator.h"

using namespace coordinator;
using namespace coordinator::fakes;
using Calls = std::vector<std::string>;

class MachineCoordinator_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        coordinator.SetMachine(&thread, &clock);
        coordinator.SetSession(&session);
        for (int k = 0; k < static_cast<int>(OpKind::Count); ++k)
            ASSERT_TRUE(coordinator.Own(static_cast<OpKind>(k), &owner));
    }

    static MachineOp Op(OpKind kind, std::string reason = {})
    {
        MachineOp op;
        op.kind = kind;
        op.source = OpSource::Test;
        op.summary = "test";
        op.sessionReason = std::move(reason);
        return op;
    }

    CallLog calls;
    FakeMachineThread thread{&calls};
    FakeClock clock;
    FakeSession session{&calls};
    FakeOwner owner{&calls};
    MachineCoordinator coordinator;
};

TEST_F(MachineCoordinator_Test, AResetParksEndsTheSessionAtTheCommitPointAndReportsTheReset)
{
    const OpResult r = coordinator.Run(Op(OpKind::Reset, "reset"));
    EXPECT_TRUE(r.Ok());
    EXPECT_EQ(r.sessionEffect, SessionEffect::EndKeepHistory);
    EXPECT_EQ(std::any_cast<std::string>(r.output), "result");
    EXPECT_EQ(calls.Take(), (Calls{"park:reset", "owner:prepare", "owner:commit", "session:end-keep-history:reset",
                                   "owner:change", "session:machine-reset", "unpark"}));
}

TEST_F(MachineCoordinator_Test, TheSessionReasonDefaultsToTheKindsName)
{
    coordinator.Run(Op(OpKind::SnapshotLoad));
    const Calls c = calls.Take();
    EXPECT_NE(std::find(c.begin(), c.end(), "session:end-keep-history:snapshot-load"), c.end());
    EXPECT_EQ(std::find(c.begin(), c.end(), "session:machine-reset"), c.end()) << "a snapshot load is not a reset";
}

TEST_F(MachineCoordinator_Test, ARefusalTouchesNothing)
{
    session.mode = MachineMode::Recording;
    MachineOp op = Op(OpKind::NetworkChange);
    const OpResult r = coordinator.Run(op);
    EXPECT_EQ(r.status, OpStatus::Refused);
    EXPECT_EQ(r.reason, "a TTD recording is running: the network settings are fixed until it stops");
    EXPECT_EQ(r.sessionEffect, SessionEffect::None);
    EXPECT_TRUE(calls.Take().empty());

    const auto log = coordinator.Log().Entries();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0].status, OpStatus::Refused);
    EXPECT_EQ(log[0].mode, MachineMode::Recording);
    EXPECT_EQ(log[0].detail, r.reason);
    EXPECT_EQ(log[0].seq, r.logSeq);
}

/// M3 (Q4): a media change while a recording runs is an event of the recording: the session sees nothing at the
/// commit, nothing is parked, and the owner is told to land it at the next frame boundary
TEST_F(MachineCoordinator_Test, AMediaChangeWhileRecordingLandsAtTheNextFrameBoundary)
{
    session.mode = MachineMode::Recording;
    EXPECT_TRUE(coordinator.Run(Op(OpKind::MediaInsert, "media-change")).Ok());
    EXPECT_TRUE(owner.lastAtFrameBoundary);
    EXPECT_EQ(calls.Take(), (Calls{"owner:prepare", "owner:commit", "owner:change"}));
    EXPECT_EQ(coordinator.Log().Entries().back().effect, SessionEffect::None);

    session.mode = MachineMode::Live;
    EXPECT_TRUE(coordinator.Run(Op(OpKind::MediaInsert, "media-change")).Ok());
    EXPECT_FALSE(owner.lastAtFrameBoundary) << "live: at once where the owner can";
}

TEST_F(MachineCoordinator_Test, EndingTheRecordingInsteadOfARefusalHoldsTheMachine)
{
    session.mode = MachineMode::Recording;
    MachineOp op = Op(OpKind::MediaEject, "media-change");
    op.endRecording = true;
    EXPECT_TRUE(coordinator.Run(op).Ok());
    EXPECT_EQ(calls.Take(), (Calls{"park:media-eject", "owner:prepare", "owner:commit",
                                   "session:stop-then-media-changed:media-change", "owner:change", "unpark"}));
}

TEST_F(MachineCoordinator_Test, AMediaChangeWithoutASessionParksNothing)
{
    EXPECT_TRUE(coordinator.Run(Op(OpKind::MediaInsert, "disk-load")).Ok());
    EXPECT_EQ(calls.Take(), (Calls{"owner:prepare", "owner:commit", "session:media-changed:disk-load", "owner:change"}));
}

TEST_F(MachineCoordinator_Test, AMediaChangeInTheBlackBoxParksTheMachine)
{
    session.mode = MachineMode::BlackBox;
    EXPECT_TRUE(coordinator.Run(Op(OpKind::MediaInsert)).Ok());
    EXPECT_EQ(calls.Take(), (Calls{"park:media-insert", "owner:prepare", "owner:commit",
                                   "session:media-changed:media-insert", "owner:change", "unpark"}));
}

TEST_F(MachineCoordinator_Test, AModeThatChangesBeforeTheCoordinatorIsTakenStillGetsItsPark)
{
    // The first read (no lock) sees Live: no park needed; under the lock the black box runs: park, then decide again
    session.modes = {MachineMode::Live, MachineMode::BlackBox, MachineMode::BlackBox};
    EXPECT_TRUE(coordinator.Run(Op(OpKind::MediaInsert)).Ok());
    const Calls c = calls.Take();
    ASSERT_FALSE(c.empty());
    EXPECT_EQ(c.front(), "park:media-insert");
    EXPECT_EQ(c.back(), "unpark");
    EXPECT_EQ(coordinator.Log().Entries().back().mode, MachineMode::BlackBox);
}

TEST_F(MachineCoordinator_Test, AFailedPrepareLeavesNoTrace)
{
    owner.prepareSucceeds = false;
    const OpResult r = coordinator.Run(Op(OpKind::Reset));
    EXPECT_EQ(r.status, OpStatus::Failed);
    EXPECT_EQ(r.reason, "bad file");
    EXPECT_EQ(r.sessionEffect, SessionEffect::None);
    EXPECT_EQ(calls.Take(), (Calls{"park:reset", "owner:prepare", "owner:abort", "unpark"}));
    EXPECT_EQ(coordinator.Log().Entries().back().status, OpStatus::Failed);
}

TEST_F(MachineCoordinator_Test, APrepareThatThrowsLeavesNoTrace)
{
    owner.throwInPrepare = true;
    const OpResult r = coordinator.Run(Op(OpKind::SnapshotLoad));
    EXPECT_EQ(r.status, OpStatus::Failed);
    EXPECT_EQ(r.reason, "prepare threw");
    EXPECT_EQ(calls.Take(), (Calls{"park:snapshot-load", "owner:prepare", "owner:abort", "unpark"}));
}

TEST_F(MachineCoordinator_Test, ACommitThatFailsBeforeItsCommitPointKeepsTheSession)
{
    owner.failBeforeCommitPoint = true;
    const OpResult r = coordinator.Run(Op(OpKind::Reset));
    EXPECT_EQ(r.status, OpStatus::Failed);
    EXPECT_EQ(r.sessionEffect, SessionEffect::None);
    EXPECT_EQ(calls.Take(), (Calls{"park:reset", "owner:prepare", "owner:commit", "owner:abort", "unpark"}));
}

TEST_F(MachineCoordinator_Test, ACommitThatFailsAfterItsCommitPointSaysTheSessionEnded)
{
    owner.failAfterCommitPoint = true;
    const OpResult r = coordinator.Run(Op(OpKind::Reset, "reset"));
    EXPECT_EQ(r.status, OpStatus::Failed);
    EXPECT_EQ(r.reason, "commit failed late");
    EXPECT_EQ(r.sessionEffect, SessionEffect::EndKeepHistory);
    EXPECT_EQ(calls.Take(), (Calls{"park:reset", "owner:prepare", "owner:commit", "session:end-keep-history:reset",
                                   "owner:change", "owner:abort", "unpark"}));
    EXPECT_EQ(coordinator.Log().Entries().back().effect, SessionEffect::EndKeepHistory);
}

TEST_F(MachineCoordinator_Test, ACommitThatThrowsReleasesThePark)
{
    owner.throwAfterCommitPoint = true;
    const OpResult r = coordinator.Run(Op(OpKind::Reset));
    EXPECT_EQ(r.status, OpStatus::Failed);
    EXPECT_EQ(r.reason, "commit threw");
    EXPECT_EQ(thread.parks.load(), 1);
    EXPECT_EQ(thread.unparks.load(), 1);
}

TEST_F(MachineCoordinator_Test, AnOwnerWithoutACommitPointStillGetsTheEffectAndANote)
{
    owner.marksCommitPoint = false;
    const OpResult r = coordinator.Run(Op(OpKind::Reset, "reset"));
    EXPECT_TRUE(r.Ok());
    EXPECT_EQ(calls.Take(), (Calls{"park:reset", "owner:prepare", "owner:commit", "owner:change",
                                   "session:end-keep-history:reset", "session:machine-reset", "unpark"}));
    EXPECT_NE(coordinator.Log().Entries().back().detail.find("did not mark its commit point"), std::string::npos);
}

TEST_F(MachineCoordinator_Test, AnOwnerThatChangedNothingLeavesTheSessionAlone)
{
    owner.changesNothing = true;
    const OpResult r = coordinator.Run(Op(OpKind::Autostart));
    EXPECT_TRUE(r.Ok());
    EXPECT_EQ(r.sessionEffect, SessionEffect::None);
    EXPECT_EQ(calls.Take(), (Calls{"park:autostart", "owner:prepare", "owner:commit", "unpark"}));
    EXPECT_EQ(coordinator.Log().Entries().back().detail, "no change");
}

TEST_F(MachineCoordinator_Test, AFailureKeepsTheOwnersOutput)
{
    owner.failAfterCommitPoint = true;
    EXPECT_EQ(std::any_cast<std::string>(coordinator.Run(Op(OpKind::Reset)).output), "result");
}

TEST_F(MachineCoordinator_Test, AnOwnersRefusalTouchesNothing)
{
    owner.refusal = "no slot 'fdd9'";
    const OpResult r = coordinator.Run(Op(OpKind::Reset));
    EXPECT_EQ(r.status, OpStatus::Refused);
    EXPECT_EQ(r.reason, "no slot 'fdd9'");
    EXPECT_TRUE(calls.Take().empty()) << "not even a park";
}

TEST_F(MachineCoordinator_Test, WouldRefuseAnswersAsRunDoesInEveryCellWithoutSideEffects)
{
    for (int k = 0; k < static_cast<int>(OpKind::Count); ++k)
        for (MachineMode mode : {MachineMode::Live, MachineMode::Recording, MachineMode::BlackBox,
                                 MachineMode::Replaying, MachineMode::Detached})
            for (bool end : {false, true})
            {
                session.mode = mode;
                MachineOp op = Op(static_cast<OpKind>(k));
                op.endRecording = end;
                const uint64_t logged = coordinator.Log().LastSeq();
                const OpResult would = coordinator.WouldRefuse(op);
                EXPECT_TRUE(calls.Take().empty());
                EXPECT_EQ(coordinator.Log().LastSeq(), logged);
                const OpResult ran = coordinator.Run(op);
                calls.Take();
                const std::string where = std::string(OpKindName(op.kind)) + " in " + MachineModeName(mode);
                EXPECT_EQ(would.status, ran.status) << where;
                EXPECT_EQ(would.reason, ran.reason) << where;
                EXPECT_EQ(would.sessionEffect, ran.sessionEffect) << where;
            }
}

TEST_F(MachineCoordinator_Test, WouldRefuseNamesTheOwnersRefusal)
{
    owner.refusal = "the drive is busy";
    const OpResult r = coordinator.WouldRefuse(Op(OpKind::MediaEject));
    EXPECT_EQ(r.status, OpStatus::Refused);
    EXPECT_EQ(r.reason, "the drive is busy");
}

TEST_F(MachineCoordinator_Test, OneOwnerPerKindAndADisownedOwnerIsNeverCalled)
{
    FakeOwner other(&calls, "other");
    EXPECT_FALSE(coordinator.Own(OpKind::Reset, &other));
    EXPECT_EQ(coordinator.Log().Entries().back().detail, "a second owner for reset");
    EXPECT_TRUE(coordinator.Own(OpKind::Reset, &owner)) << "the same owner again is no conflict";

    coordinator.Disown(&owner);
    const OpResult r = coordinator.Run(Op(OpKind::Reset));
    EXPECT_EQ(r.status, OpStatus::Refused);
    EXPECT_EQ(r.reason, "nothing on this machine does 'reset'");
    EXPECT_EQ(coordinator.WouldRefuse(Op(OpKind::MediaInsert)).reason, "nothing on this machine does 'media-insert'");
    for (const std::string& c : calls.Take())
        EXPECT_EQ(c.rfind("owner:", 0), std::string::npos) << c;

    EXPECT_TRUE(coordinator.Own(OpKind::Reset, &other));
    EXPECT_TRUE(coordinator.Run(Op(OpKind::Reset)).Ok());
}

TEST_F(MachineCoordinator_Test, WithoutASessionTheModeIsLiveAndNothingIsEnded)
{
    coordinator.SetSession(nullptr);
    EXPECT_EQ(coordinator.Mode(), MachineMode::Live);
    const OpResult r = coordinator.Run(Op(OpKind::Reset));
    EXPECT_TRUE(r.Ok());
    EXPECT_EQ(r.sessionEffect, SessionEffect::None);
    EXPECT_EQ(calls.Take(), (Calls{"park:reset", "owner:prepare", "owner:commit", "owner:change", "unpark"}));
}

TEST_F(MachineCoordinator_Test, OnTheMachineThreadNothingIsParked)
{
    thread.onMachineThread = true;
    EXPECT_TRUE(coordinator.Run(Op(OpKind::Reset, "reset")).Ok());
    EXPECT_EQ(thread.parks.load(), 0);
    EXPECT_EQ(calls.Take(), (Calls{"owner:prepare", "owner:commit", "session:end-keep-history:reset", "owner:change",
                                   "session:machine-reset"}));
}

TEST_F(MachineCoordinator_Test, AParkThatTimesOutIsNotReleasedAndTheOperationGoesOn)
{
    thread.parkSucceeds = false;
    EXPECT_TRUE(coordinator.Run(Op(OpKind::Reset)).Ok());
    EXPECT_EQ(thread.parks.load(), 1);
    EXPECT_EQ(thread.unparks.load(), 0);
}

TEST_F(MachineCoordinator_Test, ANestedOperationRunsInlineAndIsLoggedFirst)
{
    // A disk's discard re-inserts it (MediaManager::Discard): an owner running another operation from its Commit
    bool nested = false;
    owner.onCommit = [&](const MachineOp& op) {
        if (op.kind == OpKind::MediaDiscard && !nested)
        {
            nested = true;
            EXPECT_TRUE(coordinator.Run(Op(OpKind::MediaInsert)).Ok());
        }
    };
    session.mode = MachineMode::BlackBox;
    EXPECT_TRUE(coordinator.Run(Op(OpKind::MediaDiscard)).Ok());
    const auto log = coordinator.Log().Entries();
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0].kind, OpKind::MediaInsert);
    EXPECT_EQ(log[1].kind, OpKind::MediaDiscard);
    EXPECT_EQ(thread.parks.load(), thread.unparks.load());
}

TEST_F(MachineCoordinator_Test, TheLogKeepsTimeSourceAndSummary)
{
    MachineOp op = Op(OpKind::MediaInsert);
    op.source = OpSource::WebApi;
    op.summary = "fdd0 game.trd";
    coordinator.Run(op);
    const auto e = coordinator.Log().Entries().back();
    EXPECT_EQ(e.source, OpSource::WebApi);
    EXPECT_EQ(e.summary, "fdd0 game.trd");
    EXPECT_EQ(e.status, OpStatus::Done);
    EXPECT_EQ(e.effect, SessionEffect::MediaChanged);
    EXPECT_GT(e.time.frame, 0u);
    EXPECT_EQ(e.time.tInFrame, 7u);
}

TEST_F(MachineCoordinator_Test, AnOperationWithoutASourceTakesTheOneItsThreadAskedIn)
{
    MachineOp op = Op(OpKind::MediaInsert);
    op.source = OpSource::Unknown;
    {
        SourceScope scope(OpSource::Lua);
        coordinator.Run(op);
        op.source = OpSource::Cli;   // a named source wins over the thread's
        coordinator.Run(op);
        op.source = OpSource::Unknown;
    }
    EXPECT_EQ(CurrentSource(), OpSource::Unknown) << "the scope gave the previous source back";
    coordinator.Run(op);
    std::thread other([&] {
        SetThreadSource(OpSource::WebApi);
        coordinator.Run(op);
    });
    other.join();
    EXPECT_EQ(CurrentSource(), OpSource::Unknown) << "another thread's source is its own";

    const auto log = coordinator.Log().Entries();
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[0].source, OpSource::Lua);
    EXPECT_EQ(log[1].source, OpSource::Cli);
    EXPECT_EQ(log[2].source, OpSource::Unknown);
    EXPECT_EQ(log[3].source, OpSource::WebApi);
}

TEST_F(MachineCoordinator_Test, AskLogsARefusalWithItsSourceAndNothingElse)
{
    SourceScope scope(OpSource::Qt);
    MachineOp op = Op(OpKind::NetworkChange);
    op.source = OpSource::Unknown;
    op.summary = "network settings";

    // A yes: nothing logged, nothing parked, the owner only asked
    EXPECT_TRUE(coordinator.Ask(op).Ok());
    EXPECT_TRUE(coordinator.Log().Entries().empty());
    EXPECT_TRUE(calls.Take().empty());

    // The rules refuse: logged like Run's refusal, the sentence Run would give
    session.mode = MachineMode::Recording;
    const OpResult would = coordinator.WouldRefuse(op);
    const OpResult asked = coordinator.Ask(op);
    EXPECT_EQ(asked.status, OpStatus::Refused);
    EXPECT_EQ(asked.reason, would.reason);
    EXPECT_GT(asked.logSeq, 0u);

    // The owner refuses
    session.mode = MachineMode::Live;
    owner.refusal = "no deck";
    EXPECT_EQ(coordinator.Ask(op).reason, "no deck");

    EXPECT_TRUE(calls.Take().empty()) << "Ask parks nothing and runs no owner step";
    const auto log = coordinator.Log().Entries();
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0].status, OpStatus::Refused);
    EXPECT_EQ(log[0].source, OpSource::Qt);
    EXPECT_EQ(log[0].mode, MachineMode::Recording);
    EXPECT_EQ(log[0].summary, "network settings");
    EXPECT_EQ(log[0].detail, would.reason);
    EXPECT_EQ(log[0].effect, SessionEffect::None);
    EXPECT_EQ(log[1].detail, "no deck");
}

TEST_F(MachineCoordinator_Test, OperationsFromManyThreadsRunOneAtATime)
{
    // No call log here: the fakes count; each thread runs parked and unparked operations
    FakeMachineThread quiet;
    FakeSession blackBox;
    blackBox.mode = MachineMode::BlackBox;
    FakeOwner counted;
    MachineCoordinator c;
    c.SetMachine(&quiet, &clock);
    c.SetSession(&blackBox);
    for (int k = 0; k < static_cast<int>(OpKind::Count); ++k)
        ASSERT_TRUE(c.Own(static_cast<OpKind>(k), &counted));

    constexpr int kThreads = 6;
    constexpr int kOps = 50;
    std::vector<std::thread> threads;
    std::atomic<int> done{0};
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kOps; ++i)
            {
                MachineOp op;
                op.kind = (i + t) % 2 ? OpKind::Reset : OpKind::MediaInsert;
                if (c.Run(op).Ok())
                    ++done;
                c.WouldRefuse(op);
                c.Mode();
            }
        });
    for (std::thread& th : threads)
        th.join();

    EXPECT_EQ(done.load(), kThreads * kOps);
    EXPECT_EQ(counted.overlappingCommits.load(), 0);
    EXPECT_EQ(quiet.parks.load(), kThreads * kOps);
    EXPECT_EQ(quiet.unparks.load(), quiet.parks.load());
    const auto log = c.Log().Entries();
    ASSERT_EQ(log.size(), static_cast<size_t>(kThreads * kOps));
    for (size_t i = 1; i < log.size(); ++i)
        EXPECT_EQ(log[i].seq, log[i - 1].seq + 1);
}

/// M4 (architecture §5): the session reports, the coordinator reads the mode again and tells its listeners once per
/// transition, in registration order; nothing when nothing changed; an unregistered listener hears nothing more
TEST_F(MachineCoordinator_Test, AModeTransitionIsBroadcastOnceInRegistrationOrder)
{
    FakeListener first(&calls, "first");
    FakeListener second(&calls, "second");
    coordinator.Listen(&first);
    coordinator.Listen(&second);
    coordinator.Listen(&first);  // once is enough

    coordinator.SessionChanged();
    EXPECT_EQ(calls.Take(), Calls{}) << "live, as published at the start";

    session.mode = MachineMode::Recording;
    coordinator.SessionChanged();
    coordinator.SessionChanged();
    EXPECT_EQ(calls.Take(), (Calls{"first:live->recording", "second:live->recording"}));
    EXPECT_EQ(coordinator.Published().mode, MachineMode::Recording);

    session.replaying = true;  // a paused recording browsed: still the user's recording, now replaying
    coordinator.SessionChanged();
    EXPECT_EQ(calls.Take(), (Calls{"first:recording->recording:replaying", "second:recording->recording:replaying"}));

    coordinator.Unlisten(&first);
    session.mode = MachineMode::Live;
    session.replaying = false;
    coordinator.SessionChanged();
    EXPECT_EQ(calls.Take(), Calls{"second:recording->live"});
    EXPECT_FALSE(coordinator.Published().replaying);
}

// M6: a reset ends an input playback (RZX) first - before the park, never inside it; a nested reset does not again,
// and an operation that keeps the playback never calls it
TEST_F(MachineCoordinator_Test, APlaybackEndsBeforeTheParkOfTheOutermostOperationOnly)
{
    FakePlayback playback(&calls);
    coordinator.SetPlayback(&playback);
    EXPECT_TRUE(coordinator.Run(Op(OpKind::Reset, "reset")).Ok());
    Calls got = calls.Take();
    ASSERT_GE(got.size(), 2u);
    EXPECT_EQ(got[0], "playback:end:reset");
    EXPECT_EQ(got[1], "park:reset");

    // A snapshot load whose owner runs a reset inside: the playback ends once, for the outer one
    MachineOp outer = Op(OpKind::SnapshotLoad, "snapshot-load");
    FakeOwner nesting(&calls, "nesting");
    nesting.onCommit = [&](const MachineOp&) { coordinator.Run(Op(OpKind::Reset, "reset")); };
    coordinator.Disown(&owner);
    ASSERT_TRUE(coordinator.Own(OpKind::SnapshotLoad, &nesting));
    ASSERT_TRUE(coordinator.Own(OpKind::Reset, &owner));
    ASSERT_TRUE(coordinator.Own(OpKind::MediaInsert, &owner));
    EXPECT_TRUE(coordinator.Run(std::move(outer)).Ok());
    got = calls.Take();
    EXPECT_EQ(std::count(got.begin(), got.end(), "playback:end:snapshot-load"), 1);
    EXPECT_EQ(std::count(got.begin(), got.end(), "playback:end:reset"), 0);

    EXPECT_TRUE(coordinator.Run(Op(OpKind::MediaInsert, "media")).Ok());
    got = calls.Take();
    EXPECT_EQ(std::count_if(got.begin(), got.end(), [](const std::string& c) { return c.rfind("playback:", 0) == 0; }),
              0);
    coordinator.SetPlayback(nullptr);
}

// M6: TTD's verbs keep their own lock and park: the rules and the log, the commit on the caller's thread, no park,
// no session effect; a failure is logged with its reason, a kind with a park or a session effect is a contract error
TEST_F(MachineCoordinator_Test, AnOperationOnItsOwnLockIsLoggedWithoutAParkOrTheSession)
{
    const OpResult done = coordinator.RunOwnLocked(Op(OpKind::TimelineSeek), [&](OpStage& stage) {
        calls.Add("ttd:seek");
        stage.detail = "frame 5";
        return true;
    });
    EXPECT_TRUE(done.Ok());
    EXPECT_EQ(calls.Take(), (Calls{"ttd:seek"}));
    const auto entries = coordinator.Log().Entries(done.logSeq - 1);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].kind, OpKind::TimelineSeek);
    EXPECT_EQ(entries[0].status, OpStatus::Done);
    EXPECT_EQ(entries[0].detail, "frame 5");

    const OpResult failed = coordinator.RunOwnLocked(Op(OpKind::TtdLoad), [](OpStage& stage) {
        stage.error = "no such file";
        return false;
    });
    EXPECT_EQ(failed.status, OpStatus::Failed);
    EXPECT_EQ(failed.reason, "no such file");

    bool ran = false;
    const OpResult wrong = coordinator.RunOwnLocked(Op(OpKind::Reset), [&](OpStage&) { return ran = true; });
    EXPECT_EQ(wrong.status, OpStatus::Refused);
    EXPECT_FALSE(ran);
    EXPECT_TRUE(calls.Take().empty());
}
