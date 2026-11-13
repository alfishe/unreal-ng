#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// The branch verbs (docs/inprogress/2026-11-08-ttd-branches/surfaces.md §5.1) and the branch list published with
/// the session info, which the Qt TTD widget reads: branches, branch-switch, branch-update, branch-delete,
/// dump lineage=true, the status fields.
///
/// The program folds the keyboard port into RAM #6000-#7FFF on every pass: a key held in one history and not in the
/// other makes the two histories' memory differ from that frame on.
///
/// Runtime justification: each test records 30 + 15 frames of a small program and switches a few times (~0.1 s).
class TTDControl_Branches_Test : public ::testing::Test
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

    /// The trunk: 30 frames without a key
    void RecordTrunk()
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _start = _context->emulatorState.frame_counter;
        _emulator->RunNFrames(30);
        _ttd->StopRecording();
    }

    /// A resume at start + 10 with a key held, 15 frames in all: recording goes on as a new branch,
    /// the trunk keeps its old future
    void ResumeWithAKeyAt10()
    {
        ASSERT_TRUE(_ttd->SeekTo({_start + 10, 0}));
        ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 10, 0}));
        auto* keys = _context->pDebugManager->GetKeyboardManager();
        keys->PressKey(ZXKEY_A);
        _emulator->RunNFrames(1);
        keys->ReleaseKey(ZXKEY_A);
        _emulator->RunNFrames(14);
        _ttd->StopRecording();
    }

    /// Branch 1's name: the fork's time from the trunk's start ("+00:00:00.204" for 10 Pentagon frames)
    std::string Fork1() const
    {
        const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
        const ttd::TTDBranchInfo* b = info.branches ? info.branches->Find(1) : nullptr;
        return b ? b->meta.name : std::string();
    }

    void RecordTrunkAndBranch()
    {
        RecordTrunk();
        ResumeWithAKeyAt10();
    }
};

TEST_F(TTDControl_Branches_Test, WithoutBranchesNothingIsPublishedOrReported)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(5);
    _ttd->StopRecording();

    const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
    EXPECT_EQ(info.branches, nullptr) << "a session with the trunk alone publishes no branch list";
    const ttd::TTDReply status = Run("status");
    ASSERT_TRUE(status.Ok()) << status.message;
    EXPECT_EQ(status.body.find("branch"), nullptr);
    EXPECT_EQ(status.body.find("branch_count"), nullptr);

    const ttd::TTDReply list = Run("branches");
    ASSERT_TRUE(list.Ok()) << list.message;
    EXPECT_EQ(list.body.find("branches")->size(), 0u);
    EXPECT_EQ(list.body.find("count")->i, 1);
}

TEST_F(TTDControl_Branches_Test, AForkIsPublishedAndListed)
{
    RecordTrunkAndBranch();

    const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
    ASSERT_NE(info.branches, nullptr);
    ASSERT_EQ(info.branches->branches.size(), 2u);
    EXPECT_EQ(info.activeBranch, 1u) << "the resume records on a new branch; the trunk keeps its old future";
    const ttd::TTDBranchInfo& fork = info.branches->branches[1];
    EXPECT_EQ(fork.parent, ttd::kTTDTrunk);
    EXPECT_EQ(fork.fork.frame, _start + 10);
    EXPECT_EQ(fork.meta.reason, "resume");
    // Named by the fork's time from the trunk's start: 10 Pentagon frames of 71680 T at 3.5 MHz = 204.8 ms
    EXPECT_EQ(fork.meta.name.substr(0, 12), "+00:00:00.20") << fork.meta.name;

    const ttd::TTDReply status = Run("status");
    ASSERT_TRUE(status.Ok()) << status.message;
    ASSERT_NE(status.body.find("branch"), nullptr);
    EXPECT_EQ(status.body.find("branch")->find("name")->s, Fork1());
    EXPECT_EQ(status.body.find("branch_count")->i, 2);

    const ttd::TTDReply list = Run("branches");
    ASSERT_TRUE(list.Ok()) << list.message;
    const StateNode& rows = *list.body.find("branches");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_FALSE(rows.items[0].find("active")->b);
    EXPECT_TRUE(rows.items[0].find("on_lineage")->b) << "the trunk holds frames 0..10 of the line";
    EXPECT_TRUE(rows.items[1].find("active")->b);
    EXPECT_EQ(rows.items[1].find("fork")->find("frame")->i, static_cast<int64_t>(_start + 10));
    EXPECT_EQ(rows.items[1].find("length")->i, 15) << "the new future runs from frame 10 to 25";

    // On the trunk the branch is off the line
    ASSERT_TRUE(Run("branch-switch", {{"branch", "trunk"}}).Ok());
    const ttd::TTDReply lineage = Run("branches", {{"scope", "lineage"}});
    ASSERT_TRUE(lineage.Ok()) << lineage.message;
    ASSERT_EQ(lineage.body.find("branches")->size(), 1u);
    EXPECT_EQ(lineage.body.find("branches")->items[0].find("name")->s, "trunk");
    EXPECT_FALSE(Run("branches", {{"scope", "nowhere"}}).Ok());
}

TEST_F(TTDControl_Branches_Test, SwitchingByNameRestoresThatHistory)
{
    RecordTrunk();
    ASSERT_TRUE(_ttd->SeekTo({_start + 20, 0}));
    const uint64_t oldFuture = Ram();
    ResumeWithAKeyAt10();
    ASSERT_TRUE(_ttd->SeekTo({_start + 20, 0}));
    const uint64_t newFuture = Ram();
    ASSERT_NE(oldFuture, newFuture) << "the key changes the memory from the fork on";

    const ttd::TTDReply sw = Run("branch-switch", {{"branch", "trunk"}, {"frame", std::to_string(_start + 20)}});
    ASSERT_TRUE(sw.Ok()) << sw.message;
    EXPECT_TRUE(sw.body.find("branch")->find("active")->b);
    EXPECT_EQ(Ram(), oldFuture) << "the switch stands on the future the trunk kept";
    const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
    ASSERT_NE(info.branches, nullptr);
    EXPECT_EQ(info.activeBranch, ttd::kTTDTrunk);
    EXPECT_EQ(info.viewedBranch, ttd::kTTDTrunk);
    EXPECT_EQ(info.branches->lineage.size(), 1u);

    // By id back to the new future; an unknown branch is not found
    const ttd::TTDReply back = Run("branch-switch", {{"branch", "1"}, {"frame", std::to_string(_start + 20)}});
    ASSERT_TRUE(back.Ok()) << back.message;
    EXPECT_EQ(Ram(), newFuture);
    EXPECT_EQ(Run("branch-switch", {{"branch", "nowhere"}}).error, ttd::TTDControlError::NotFound);
    EXPECT_EQ(Run("branch-switch", {}).error, ttd::TTDControlError::BadRequest);
}

TEST_F(TTDControl_Branches_Test, UpdateRenamesTagsAndPins)
{
    RecordTrunkAndBranch();
    const uint64_t revision = _ttd->ReadSessionInfo().branches->revision;

    const ttd::TTDReply up = Run("branch-update", {{"branch", Fork1()},
                                                   {"name", "no key"},
                                                   {"note", "the old future"},
                                                   {"tags", "b,a"},
                                                   {"pinned", "true"}});
    ASSERT_TRUE(up.Ok()) << up.message;
    const StateNode& b = *up.body.find("branch");
    EXPECT_EQ(b.find("name")->s, "no key");
    EXPECT_EQ(b.find("note")->s, "the old future");
    ASSERT_EQ(b.find("tags")->size(), 2u);
    EXPECT_EQ(b.find("tags")->items[0].s, "a") << "tags are kept sorted";
    EXPECT_TRUE(b.find("pinned")->b);

    const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
    ASSERT_NE(info.branches, nullptr);
    EXPECT_NE(info.branches->revision, revision) << "an edit publishes a new list";
    EXPECT_EQ(info.branches->Find(1)->meta.name, "no key");

    ASSERT_TRUE(Run("branch-update", {{"branch", "no key"}, {"add_tags", "c"}, {"remove_tags", "a"}}).Ok());
    EXPECT_EQ(_ttd->ReadSessionInfo().branches->Find(1)->meta.tags, (std::vector<std::string>{"b", "c"}));

    // The trunk's name is taken; an empty name and an empty edit are refused
    EXPECT_EQ(Run("branch-update", {{"branch", "no key"}, {"name", "trunk"}}).error, ttd::TTDControlError::Conflict);
    EXPECT_EQ(Run("branch-update", {{"branch", "no key"}, {"name", ""}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-update", {{"branch", "no key"}}).error, ttd::TTDControlError::BadRequest);
}

TEST_F(TTDControl_Branches_Test, DeleteKeepsTheActiveLineageAndPinnedBranchesNeedForce)
{
    RecordTrunkAndBranch();

    // The line the timeline ends with stays: neither the trunk nor the active branch goes
    EXPECT_EQ(Run("branch-delete", {{"branch", "trunk"}}).error, ttd::TTDControlError::Conflict);
    EXPECT_EQ(Run("branch-delete", {{"branch", Fork1()}}).error, ttd::TTDControlError::Conflict);
    ASSERT_TRUE(Run("branch-switch", {{"branch", "trunk"}}).Ok());
    ASSERT_TRUE(Run("branch-update", {{"branch", Fork1()}, {"pinned", "true"}}).Ok());
    const ttd::TTDReply refused = Run("branch-delete", {{"branch", Fork1()}});
    EXPECT_EQ(refused.error, ttd::TTDControlError::Conflict);
    EXPECT_NE(refused.message.find("pinned"), std::string::npos) << refused.message;

    const std::string fork = Fork1();
    const ttd::TTDReply del = Run("branch-delete", {{"branch", fork}, {"force", "true"}});
    ASSERT_TRUE(del.Ok()) << del.message;
    ASSERT_EQ(del.body.find("removed_names")->size(), 1u);
    EXPECT_EQ(del.body.find("removed_names")->items[0].s, fork);
    EXPECT_EQ(_ttd->ReadSessionInfo().branches, nullptr) << "the trunk alone again: nothing to show";
}

TEST_F(TTDControl_Branches_Test, DumpLineageWritesTheActiveLineageAlone)
{
    RecordTrunkAndBranch();
    const std::string all = TestPathHelper::GetUniqueTestScratchPath("branches-all.ttd");
    const std::string lineage = TestPathHelper::GetUniqueTestScratchPath("branches-lineage.ttd");

    const ttd::TTDReply a = Run("dump", {{"path", all}});
    ASSERT_TRUE(a.Ok()) << a.message;
    const ttd::TTDReply l = Run("dump", {{"path", lineage}, {"lineage", "true"}});
    ASSERT_TRUE(l.Ok()) << l.message;
    EXPECT_LT(l.body.find("bytes")->i, a.body.find("bytes")->i) << "the future the trunk kept is left out";
    EXPECT_EQ(Run("dump", {{"path", lineage}, {"lineage", "maybe"}}).error, ttd::TTDControlError::BadRequest);

    // The lineage file loads as one line
    const ttd::TTDReply load = Run("load", {{"path", lineage}});
    ASSERT_TRUE(load.Ok()) << load.message;
    EXPECT_EQ(_ttd->ReadSessionInfo().branches, nullptr);
    std::error_code ec;
    std::filesystem::remove(FileHelper::ToFsPath(all), ec);
    std::filesystem::remove(FileHelper::ToFsPath(lineage), ec);
}

TEST_F(TTDControl_Branches_Test, ResumeNamesTheNewBranchAndTheKeptFuture)
{
    RecordTrunk();
    ASSERT_TRUE(_ttd->SeekTo({_start + 10, 0}));
    const ttd::TTDReply resume = Run("resume", {{"frame", std::to_string(_start + 10)}});
    ASSERT_TRUE(resume.Ok()) << resume.message;
    _ttd->StopRecording();
    ASSERT_NE(resume.body.find("branch"), nullptr);
    EXPECT_EQ(resume.body.find("branch")->find("name")->s, Fork1());
    ASSERT_NE(resume.body.find("kept"), nullptr);
    EXPECT_EQ(resume.body.find("kept")->find("name")->s, "trunk");
    EXPECT_NE(resume.body.find("text")->s.find("kept on 'trunk'"), std::string::npos) << resume.body.find("text")->s;
}

/// The TTD panel's Continue Rec at the end of a stopped recording while the machine runs ahead on its own thread:
/// where the machine stands is past the history, so the resume there is refused; after a seek to the recording's
/// end (which parks the machine, as the panel does) it answers and the recording goes on
TEST_F(TTDControl_Branches_Test, ContinueAtTheEndWhileTheMachineRunsAnswers)
{
    RecordTrunk();
    const uint64_t end = _context->emulatorState.frame_counter;
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _context->emulatorState.frame_counter >= end + 5; },
                              std::chrono::milliseconds(5000)));
    const ttd::TTDReply past = Run("resume", {{"media", "ask"}});
    EXPECT_FALSE(past.Ok() && past.body.find("resumed")->b) << "no gap: past the end the session cannot go on";
    const std::string last = std::to_string(_ttd->ReadSessionInfo().currentEndFrame);
    const ttd::TTDReply seek = Run("seek", {{"frame", last}});
    ASSERT_TRUE(seek.body.find("reached")->b) << "the seek parks the machine at the end";
    auto answer = std::async(std::launch::async, [&] { return Run("resume", {{"media", "ask"}}); });
    ASSERT_EQ(answer.wait_for(std::chrono::seconds(3)), std::future_status::ready) << "the resume hangs";
    const ttd::TTDReply r = answer.get();
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(r.body.find("resumed")->b);
    EXPECT_TRUE(_ttd->IsRecording());
    _emulator->Pause();
}

TEST_F(TTDControl_Branches_Test, TheListNamesTheBranchesLeavingTheMomentsFrame)
{
    RecordTrunkAndBranch();

    // At the fork, on the branch: the trunk's old future leaves here (20 frames past the fork)
    ASSERT_TRUE(_ttd->SeekTo({_start + 10, 0}));
    ttd::TTDReply list = Run("branches");
    ASSERT_TRUE(list.Ok()) << list.message;
    const StateNode* forks = list.body.find("forks_here");
    ASSERT_NE(forks, nullptr);
    ASSERT_EQ(forks->size(), 1u);
    EXPECT_EQ(forks->items[0].find("id")->i, 0);
    EXPECT_EQ(forks->items[0].find("name")->s, "trunk");
    EXPECT_EQ(forks->items[0].find("frames")->i, 20);
    EXPECT_NE(list.body.find("text")->s.find("Fork here: trunk (0), 20 frames"), std::string::npos)
        << list.body.find("text")->s;

    // Elsewhere nothing leaves
    ASSERT_TRUE(_ttd->SeekTo({_start + 5, 0}));
    list = Run("branches");
    ASSERT_TRUE(list.Ok()) << list.message;
    EXPECT_EQ(list.body.find("forks_here")->size(), 0u);
}
