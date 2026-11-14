#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdsessiontypes.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// The branch verbs of TTD branches P5 (docs/inprogress/2026-11-08-ttd-branches/surfaces.md §5.1): branch-create
/// (a fork at a moment, at the active branch's end too; with run_frames the machine runs it headless, fed by an input
/// script, and the branch records it: Q-6), branch-info, branch-prune, and the branch events the ttd.branches topic
/// reads (PublishedBranches).
///
/// The program folds the keyboard port into RAM #6000-#7FFF on every pass: a key held in one history and not in the
/// other makes the two histories' memory differ from that frame on.
///
/// Runtime justification: each test records 30 frames of a small program and runs a few short branches (~0.1 s).
class TTDControl_BranchCreate_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    uint64_t _start = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();

        static const uint8_t program[] = {
            0xF3,              // 8000 DI
            0x21, 0x00, 0x60,  // 8001 LD HL,#6000
            0xDB, 0xFE,        // 8004 loop: IN A,(#FE)
            0x86,              // 8006 ADD A,(HL)
            0x77,              // 8007 LD (HL),A
            0x23,              // 8008 INC HL
            0x7C,              // 8009 LD A,H  -- wrap #7FFF -> #6000
            0xE6, 0x1F,        // 800A AND #1F
            0xF6, 0x60,        // 800C OR #60
            0x67,              // 800E LD H,A
            0x18, 0xF3,        // 800F JR loop
        };
        _emulator->RunNFrames(5);
        Z80* z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
        z80->pc = 0x8000;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint64_t Ram() const
    {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t a = 0x6000; a < 0x8000; ++a)
            h = (h ^ _context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(a))) * 1099511628211ull;
        return h;
    }

    ttd::TTDReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        ttd::TTDControl control(_context);
        return control.Execute({verb, std::move(options)});
    }

    /// The trunk: 30 frames without a key, the recording stopped at its end
    void RecordTrunk()
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _start = _context->emulatorState.frame_counter;
        _emulator->RunNFrames(30);
        _ttd->StopRecording();
    }

    const ttd::TTDBranchInfo* Find(const ttd::TTDSessionInfo& info, const std::string& name) const
    {
        if (!info.branches)
            return nullptr;
        for (const ttd::TTDBranchInfo& b : info.branches->branches)
            if (b.meta.name == name)
                return &b;
        return nullptr;
    }

    /// A branch from frame start + @p at, run @p frames with @p input, back where the caller stood
    ttd::TTDReply Probe(const std::string& name, uint64_t at, uint64_t frames, const std::string& input,
                        const std::string& run)
    {
        std::map<std::string, std::string> options = {{"name", name},
                                                      {"frame", std::to_string(_start + at)},
                                                      {"run_frames", std::to_string(frames)},
                                                      {"run", run},
                                                      {"created_by", "analyzer"},
                                                      {"go_back", "true"}};
        if (!input.empty())
            options["input"] = input;
        return Run("branch-create", options);
    }
};

/// A branch asked for at the active branch's end forks there (a resume there only goes on), and stays when the
/// machine leaves it before it recorded anything: somebody asked for it by name (architecture.md §4.6)
TEST_F(TTDControl_BranchCreate_Test, AtTheEndItForksABranchThatStaysEmpty)
{
    RecordTrunk();
    const ttd::TTDReply created = Run("branch-create", {{"name", "plan b"}, {"created_by", "test"},
                                                        {"tags", "a,b"}, {"reason", "what if"}});
    ASSERT_TRUE(created.Ok()) << created.message;
    EXPECT_TRUE(created.body.find("created")->b);
    const StateNode& branch = *created.body.find("branch");
    EXPECT_EQ(branch.find("name")->s, "plan b");
    EXPECT_EQ(branch.find("reason")->s, "create: what if");
    EXPECT_EQ(branch.find("created_by")->s, "test");
    EXPECT_EQ(branch.find("tags")->items.size(), 2u);
    EXPECT_EQ(created.body.find("parent")->find("id")->i, 0);
    EXPECT_EQ(created.body.find("ran_frames")->i, 0);

    // Back on the trunk: the empty branch stays (a resume's empty branch would go)
    ASSERT_TRUE(Run("branch-switch", {{"branch", "0"}}).Ok());
    const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
    ASSERT_NE(Find(info, "plan b"), nullptr) << "an asked-for branch stays before it records";
    EXPECT_EQ(info.activeBranch, ttd::kTTDTrunk);
}

/// With run_frames the machine runs the new branch headless, fed by the input script; the branch records it
/// (replayed the same way), the reads come from where the run ended, go_back=true puts the caller back, and the
/// trunk keeps its own future
TEST_F(TTDControl_BranchCreate_Test, ARunRecordsTheScriptedInputAndReadsTheOutcome)
{
    RecordTrunk();
    ASSERT_TRUE(_ttd->SeekTo({_start + 15, 0}));
    const ttd::TTDTimePoint caller = _ttd->CurrentPosition();

    const ttd::TTDReply created = Run("branch-create", {{"name", "keyed"},
                                                        {"frame", std::to_string(_start + 10)},
                                                        {"run_frames", "5"},
                                                        {"input", "0:a:2"},
                                                        {"read", "#6000:4,#6010"},
                                                        {"go_back", "true"}});
    ASSERT_TRUE(created.Ok()) << created.message;
    EXPECT_EQ(created.body.find("ran_frames")->i, 5);
    EXPECT_FALSE(created.body.find("aborted")->b);
    EXPECT_EQ(created.body.find("end")->find("frame")->i, static_cast<int64_t>(_start + 15));
    const StateNode& reads = *created.body.find("reads");
    ASSERT_EQ(reads.items.size(), 2u);
    EXPECT_EQ(reads.items[0].find("address")->i, 0x6000);
    EXPECT_EQ(reads.items[0].find("bytes")->items.size(), 4u);
    EXPECT_EQ(reads.items[0].find("hex")->s.size(), 8u);
    EXPECT_EQ(reads.items[1].find("bytes")->items.size(), 1u);
    ASSERT_NE(created.body.find("returned_to"), nullptr);
    EXPECT_EQ(_ttd->CurrentPosition(), caller) << "go_back=true: back where the caller stood";
    EXPECT_EQ(_ttd->ReadSessionInfo().activeBranch, ttd::kTTDTrunk);

    // The branch holds the keyed history, replayed alike (the press lies inside the fork frame: a moment after it
    // in that frame is reached by replaying the recorded input); the trunk keeps its own
    const std::string inFork = std::to_string(_start + 10);
    ASSERT_TRUE(Run("branch-switch", {{"branch", "keyed"}, {"frame", inFork}, {"tinframe", "60000"}}).Ok());
    const uint64_t keyed = Ram();
    ASSERT_TRUE(Run("seek", {{"frame", std::to_string(_start + 13)}}).Ok());
    ASSERT_TRUE(Run("seek", {{"frame", inFork}, {"tinframe", "60000"}}).Ok());
    EXPECT_EQ(Ram(), keyed) << "the recorded input replays";
    ASSERT_TRUE(Run("branch-switch", {{"branch", "0"}, {"frame", inFork}, {"tinframe", "60000"}}).Ok());
    EXPECT_NE(Ram(), keyed) << "the key the script held changed the branch's memory, not the trunk's";
    const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
    ASSERT_NE(info.branches, nullptr);
    EXPECT_EQ(info.branches->Find(0)->end.frame, _start + 30) << "the trunk keeps its future";
}

TEST_F(TTDControl_BranchCreate_Test, ABadRequestChangesNothing)
{
    EXPECT_EQ(Run("branch-create").error, ttd::TTDControlError::Conflict) << "no history yet";
    RecordTrunk();
    EXPECT_EQ(Run("branch-create", {{"input", "0:a"}}).error, ttd::TTDControlError::BadRequest)
        << "input without run_frames";
    EXPECT_EQ(Run("branch-create", {{"run_frames", "2"}, {"input", "0:nosuchkey"}}).error,
              ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-create", {{"run_frames", "2"}, {"input", "x:a"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-create", {{"read", "#10000"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-create", {{"run_frames", "10001"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-create", {{"tinframe", "5"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-create", {{"branch", "nosuch"}}).error, ttd::TTDControlError::NotFound);
    EXPECT_EQ(_ttd->ReadSessionInfo().branches, nullptr) << "nothing forked";
}

/// branch-info: the branch with its children, its ancestors and its files; the trunk alone answers too
TEST_F(TTDControl_BranchCreate_Test, InfoNamesChildrenAndAncestors)
{
    RecordTrunk();
    const ttd::TTDReply alone = Run("branch-info", {{"branch", "trunk"}});
    ASSERT_TRUE(alone.Ok()) << alone.message;
    EXPECT_EQ(alone.body.find("branch")->find("id")->i, 0);

    ASSERT_TRUE(Probe("first", 10, 3, "", "").Ok());
    ASSERT_TRUE(Run("branch-create", {{"branch", "first"}, {"frame", std::to_string(_start + 12)}, {"name", "second"},
                                      {"run_frames", "2"}, {"go_back", "true"}})
                    .Ok());
    const ttd::TTDReply first = Run("branch-info", {{"branch", "first"}});
    ASSERT_TRUE(first.Ok()) << first.message;
    ASSERT_EQ(first.body.find("children")->items.size(), 1u);
    EXPECT_EQ(first.body.find("children")->items[0].find("name")->s, "second");
    EXPECT_EQ(first.body.find("ancestors")->items.size(), 1u) << "the trunk";
    const ttd::TTDReply second = Run("branch-info", {{"branch", "second"}});
    ASSERT_TRUE(second.Ok()) << second.message;
    ASSERT_EQ(second.body.find("ancestors")->items.size(), 2u);
    EXPECT_EQ(second.body.find("ancestors")->items[1].find("name")->s, "first");
    EXPECT_NE(second.body.find("files"), nullptr);
    EXPECT_NE(second.body.find("text")->s.find("'first'"), std::string::npos) << second.body.find("text")->s;
    EXPECT_EQ(Run("branch-info", {{"branch", "nosuch"}}).error, ttd::TTDControlError::NotFound);
}

/// branch-prune: a dry run by default; the filter picks (an analyzer's run id here); the active lineage and pinned
/// branches stay and are named
TEST_F(TTDControl_BranchCreate_Test, PruneDeletesARunsBranchesAndKeepsTheProtectedOnes)
{
    RecordTrunk();
    ASSERT_TRUE(Probe("r1-a", 5, 2, "0:space", "r1").Ok());
    ASSERT_TRUE(Probe("r1-b", 8, 2, "", "r1").Ok());
    ASSERT_TRUE(Probe("r1-pinned", 9, 2, "", "r1").Ok());
    ASSERT_TRUE(Run("branch-update", {{"branch", "r1-pinned"}, {"pinned", "true"}}).Ok());
    ASSERT_TRUE(Probe("r2", 12, 2, "", "r2").Ok());

    EXPECT_EQ(Run("branch-prune").error, ttd::TTDControlError::BadRequest) << "a filter is required";
    const ttd::TTDReply dry = Run("branch-prune", {{"run", "r1"}});
    ASSERT_TRUE(dry.Ok()) << dry.message;
    EXPECT_TRUE(dry.body.find("dry_run")->b);
    EXPECT_EQ(dry.body.find("would_delete")->items.size(), 2u);
    ASSERT_EQ(dry.body.find("kept")->items.size(), 1u);
    EXPECT_EQ(dry.body.find("kept")->items[0].find("reason")->s, "pinned");
    EXPECT_EQ(_ttd->ReadSessionInfo().branches->branches.size(), 5u) << "a dry run deletes nothing";

    const ttd::TTDReply done = Run("branch-prune", {{"run", "r1"}, {"dry_run", "false"}});
    ASSERT_TRUE(done.Ok()) << done.message;
    EXPECT_EQ(done.body.find("deleted")->items.size(), 2u);
    ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
    EXPECT_EQ(Find(info, "r1-a"), nullptr);
    EXPECT_EQ(Find(info, "r1-b"), nullptr);
    EXPECT_NE(Find(info, "r1-pinned"), nullptr);
    EXPECT_NE(Find(info, "r2"), nullptr);

    // Standing on r2: its lineage stays
    ASSERT_TRUE(Run("branch-switch", {{"branch", "r2"}}).Ok());
    const ttd::TTDReply onLineage = Run("branch-prune", {{"run", "r2"}, {"dry_run", "false"}});
    ASSERT_TRUE(onLineage.Ok()) << onLineage.message;
    EXPECT_TRUE(onLineage.body.find("deleted")->items.empty());
    ASSERT_EQ(onLineage.body.find("kept")->items.size(), 1u);
    EXPECT_EQ(onLineage.body.find("kept")->items[0].find("reason")->s, "on the active lineage");
}

/// The ttd.branches topic reads the published branch events: created, updated, switched, deleted, numbered
TEST_F(TTDControl_BranchCreate_Test, BranchEventsArePublishedInOrder)
{
    RecordTrunk();
    EXPECT_EQ(_ttd->PublishedBranches().event.seq, 0u);
    std::vector<std::string> events;
    uint64_t seq = 0;
    auto take = [&]() {
        const ttd::TTDBranchesPublished published = _ttd->PublishedBranches();
        if (published.event.seq != seq)
        {
            EXPECT_GT(published.event.seq, seq);
            seq = published.event.seq;
            events.push_back(published.event.event + ":" + published.event.name);
        }
    };
    ASSERT_TRUE(Run("branch-create", {{"name", "x"}, {"frame", std::to_string(_start + 10)}}).Ok());
    take();
    ASSERT_TRUE(Run("branch-update", {{"branch", "x"}, {"note", "n"}}).Ok());
    take();
    ASSERT_TRUE(Run("branch-switch", {{"branch", "0"}}).Ok());
    take();
    ASSERT_TRUE(Run("branch-delete", {{"branch", "x"}}).Ok());
    take();
    EXPECT_EQ(events, (std::vector<std::string>{"created:x", "updated:x", "switched:trunk", "deleted:x"}));
    EXPECT_EQ(_ttd->PublishedBranches().event.removed, 1u);
}


/// Pokes and registers land on the new branch as tool edits (recorded, so the branch replays them), before the run;
/// the input script takes joystick buttons and release_all; bad pokes and registers refuse before anything forks
TEST_F(TTDControl_BranchCreate_Test, PokesRegistersAndJoystickDriveTheBranch)
{
    RecordTrunk();
    EXPECT_EQ(Run("branch-create", {{"poke", "#6000"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-create", {{"poke", "#6000=256"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-create", {{"regs", "nosuch=1"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-create", {{"regs", "a=300"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-create", {{"run_frames", "2"}, {"input", "0:joy.nosuch"}}).error,
              ttd::TTDControlError::BadRequest);
    EXPECT_EQ(_ttd->ReadSessionInfo().branches, nullptr) << "nothing forked";

    // #7F00 lies past what the loop has folded by then: the poke survives the run
    const ttd::TTDReply created = Run("branch-create", {{"name", "edited"},
                                                        {"frame", std::to_string(_start + 10)},
                                                        {"poke", "#7F00=#5A,#7F01=7"},
                                                        {"regs", "bc=#1234"},
                                                        {"run_frames", "3"},
                                                        {"input", "0:joy.fire:down,1:release_all,2:a"},
                                                        {"read", "#7F00:2"},
                                                        {"go_back", "true"}});
    ASSERT_TRUE(created.Ok()) << created.message;
    EXPECT_EQ(created.body.find("poked")->i, 2);
    EXPECT_EQ(created.body.find("registers_set")->i, 1);
    const StateNode& bytes = *created.body.find("reads")->items[0].find("bytes");
    ASSERT_EQ(bytes.items.size(), 2u);
    EXPECT_EQ(bytes.items[0].i, 0x5A);
    EXPECT_EQ(bytes.items[1].i, 7);
    EXPECT_EQ(_ttd->ReadSessionInfo().activeBranch, ttd::kTTDTrunk);
    EXPECT_NE(_context->pMemory->DirectReadFromZ80Memory(0x7F00), 0x5A) << "the trunk is not poked";

    // The branch replays the edit: a moment on it after the fork has the bytes
    ASSERT_TRUE(Run("branch-switch", {{"branch", "edited"}, {"frame", std::to_string(_start + 12)}}).Ok());
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x7F00), 0x5A);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x7F01), 7);
}

/// Asked while the session records, branch-create forks at the asked moment (the recording closes the trunk at its
/// last committed frame), the run lands on the new branch, and go_back returns to the trunk's end: the frame in
/// progress was never recorded
TEST_F(TTDControl_BranchCreate_Test, WhileRecordingItForksAtTheAskedMoment)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _start = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(20);
    const ttd::TTDReply created = Run("branch-create", {{"name", "mid"},
                                                        {"frame", std::to_string(_start + 8)},
                                                        {"run_frames", "2"},
                                                        {"go_back", "true"}});
    ASSERT_TRUE(created.Ok()) << created.message;
    EXPECT_EQ(created.body.find("ran_frames")->i, 2);
    EXPECT_EQ(created.body.find("end")->find("frame")->i, static_cast<int64_t>(_start + 10));
    const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
    const ttd::TTDBranchInfo* mid = Find(info, "mid");
    ASSERT_NE(mid, nullptr);
    EXPECT_EQ(mid->fork.frame, _start + 8);
    EXPECT_EQ(info.activeBranch, ttd::kTTDTrunk) << created.body.find("text")->s;
    ASSERT_NE(created.body.find("returned_to"), nullptr);
    EXPECT_EQ(_ttd->CurrentPosition().frame, info.branches->Find(ttd::kTTDTrunk)->end.frame);
}
