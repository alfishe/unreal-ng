#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/engine/ttdbytes.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdsessionfacts.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// Branches in the controller (docs/inprogress/2026-11-08-ttd-branches, test-strategy.md T-C): a resume in the
/// past forks a branch that keeps the old future, both stay reachable, running forward from an ancestor's part
/// follows the active lineage across the fork.
///
/// The program folds the keyboard port into RAM #6000-#7FFF on every pass: a key held in one history and not in
/// the other makes the two histories' memory differ from that frame on.
///
/// Runtime justification: each test records 30-50 frames of a small program and seeks a few times (~0.1 s).
class TimeTravelController_Branches_Test : public ::testing::Test
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

    uint64_t RamAt(uint64_t frame)
    {
        EXPECT_TRUE(_ttd->SeekTo({frame, 0})) << "frame " << frame;
        return Ram();
    }

    void HoldKey(int frames)
    {
        auto* keys = _context->pDebugManager->GetKeyboardManager();
        keys->PressKey(ZXKEY_A);
        _emulator->RunNFrames(frames);
        keys->ReleaseKey(ZXKEY_A);
    }

    /// The trunk: 30 frames without a key
    void RecordTrunk()
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _start = _context->emulatorState.frame_counter;
        _emulator->RunNFrames(30);
        _ttd->StopRecording();
    }

    /// A branch from @p at: a key held for a frame, @p frames in all
    void RecordBranchFrom(uint64_t at, int frames)
    {
        ASSERT_TRUE(_ttd->SeekTo({at, 0}));
        ASSERT_TRUE(_ttd->ResumeRecordingFrom({at, 0}));
        HoldKey(1);
        _emulator->RunNFrames(frames - 1);
        _ttd->StopRecording();
    }

    std::string Save(bool lineageOnly = false)
    {
        std::ostringstream out;
        std::string err;
        EXPECT_TRUE(_ttd->SerializeSession(out, err, lineageOnly)) << err;
        return out.str();
    }

    /// The frames of the active lineage, from the trunk's start to the active branch's end: their RAM
    std::map<uint64_t, uint64_t> LineageFrames()
    {
        std::map<uint64_t, uint64_t> all;
        const uint32_t active = _ttd->ActiveBranch();
        std::string error;
        for (uint64_t f = _start; f <= _ttd->Branches().Get(active)->end.frame; ++f)
        {
            EXPECT_TRUE(_ttd->SwitchBranch(active, ttd::TTDTimePoint{f, 0}, error)) << f << " " << error;
            all[f] = Ram();
        }
        return all;
    }

    /// The branch table's create record (storage.md §3.2)
    struct CreateRecord
    {
        uint32_t local = 0;
        uint32_t parent = 0;
        uint64_t frame = 0;
        uint32_t tInFrame = 0;
        std::vector<uint8_t> meta;   ///< the controller's record: version, GUID (16 bytes), ...
    };
    static bool ParseCreate(const std::vector<uint8_t>& bytes, CreateRecord& c)
    {
        ttd::TTDByteReader r(bytes);
        uint8_t kind = 0;
        uint64_t size = 0;
        return r.U8(kind) && kind == 1 && r.VarintAs(c.local) && r.VarintAs(c.parent) && r.Varint(c.frame) &&
               r.VarintAs(c.tInFrame) && r.Varint(size) && size <= r.Left() &&
               r.Bytes(c.meta, static_cast<size_t>(size));
    }
    static std::vector<uint8_t> EmitCreate(const CreateRecord& c)
    {
        ttd::TTDByteWriter w;
        w.U8(1);
        w.Varint(c.local);
        w.Varint(c.parent);
        w.Varint(c.frame);
        w.Varint(c.tInFrame);
        w.Varint(c.meta.size());
        w.Bytes(c.meta.data(), c.meta.size());
        return w.bytes;
    }

    /// The file with every branch-table create record passed through @p edit, the rest copied as it is
    static std::string EditBranchTable(const std::string& file, const std::function<void(CreateRecord&)>& edit)
    {
        const ttd::TTDMemorySource source(std::vector<uint8_t>(file.begin(), file.end()));
        ttd::TTDContainerReader reader;
        std::string error;
        EXPECT_TRUE(reader.Open(source, error, [](uint16_t id) { return ttd::TTDSessionFile::KnownStream(id); }))
            << error;
        ttd::TTDMemorySink sink;
        ttd::TTDContainerWriter writer;
        EXPECT_TRUE(writer.Begin(sink, reader.Header(), &error)) << error;
        for (const ttd::TTDPartRef& part : reader.Parts())
        {
            for (const ttd::TTDRecordRef& r : part.records)
            {
                std::vector<uint8_t> bytes;
                EXPECT_TRUE(reader.ReadRecord(r, bytes));
                if (r.streamId == ttd::sessionstream::kBranchTable)
                {
                    CreateRecord c;
                    EXPECT_TRUE(ParseCreate(bytes, c));
                    edit(c);
                    bytes = EmitCreate(c);
                }
                writer.AddRecord(r.streamId, bytes);
            }
            writer.EndPart({part.firstFrame, part.frameCount, part.branch, part.dependencies, part.extra});
        }
        EXPECT_TRUE(writer.Finalize(reader.IndexExtra()));
        return std::string(sink.bytes.begin(), sink.bytes.end());
    }

    /// The reader without branch support for the scope (storage.md §5, the old reader)
    struct OldReader
    {
        OldReader() { ttd::TTDSessionFile::SetBranchReadingForTesting(false); }
        ~OldReader() { ttd::TTDSessionFile::SetBranchReadingForTesting(true); }
        OldReader(const OldReader&) = delete;
        OldReader& operator=(const OldReader&) = delete;
    };

    static bool HasBranchStreams(const std::vector<uint16_t>& streams)
    {
        return std::any_of(streams.begin(), streams.end(), [](uint16_t id) {
            return id >= ttd::sessionstream::kBranchTable || id == ttd::holderstream::kBranchState;
        });
    }

    bool Load(const std::string& file, std::string& err)
    {
        std::istringstream in(file);
        return _ttd->DeserializeSession(in, err);
    }

    /// Every recorded frame of every live branch: its RAM, reached by a switch to the branch at the frame
    std::map<uint32_t, std::map<uint64_t, uint64_t>> EveryFrame()
    {
        std::map<uint32_t, std::map<uint64_t, uint64_t>> all;
        const uint32_t active = _ttd->ActiveBranch();
        std::string error;
        for (uint32_t id = 0; id < _ttd->Branches().Ids(); ++id)
        {
            const ttd::TTDBranchInfo* b = _ttd->Branches().Get(id);
            if (!b)
                continue;
            const uint64_t from = id == ttd::kTTDTrunk ? _start : b->fork.frame;
            for (uint64_t f = from; f <= b->end.frame; ++f)
            {
                EXPECT_TRUE(_ttd->SwitchBranch(id, ttd::TTDTimePoint{f, 0}, error)) << id << " " << f << " " << error;
                all[id][f] = Ram();
            }
        }
        EXPECT_TRUE(_ttd->SwitchBranch(active, std::nullopt, error)) << error;
        return all;
    }

    /// The streams a session file declares
    static std::vector<uint16_t> Streams(const std::string& file)
    {
        const ttd::TTDMemorySource source(std::vector<uint8_t>(file.begin(), file.end()));
        ttd::TTDContainerReader reader;
        std::string error;
        std::vector<uint16_t> ids;
        if (!reader.Open(source, error, [](uint16_t id) { return ttd::TTDSessionFile::KnownStream(id); }))
            return ids;
        for (const ttd::TTDStreamDesc& d : reader.Header().streams)
            ids.push_back(d.id);
        return ids;
    }
};

/// T-C1: the old future stays on the trunk, the new one on the branch; each is reached by switching to it
TEST_F(TimeTravelController_Branches_Test, AResumeInThePastKeepsTheOldFuture)
{
    RecordTrunk();
    const uint64_t trunkAt20 = RamAt(_start + 20);

    RecordBranchFrom(_start + 10, 20);
    ASSERT_EQ(_ttd->Branches().Count(), 2u);
    EXPECT_EQ(_ttd->ActiveBranch(), 1u);
    EXPECT_EQ(_ttd->GetEarliestRecordedFrame(), _start);
    const ttd::TTDBranchInfo* branch = _ttd->Branches().Get(1);
    ASSERT_NE(branch, nullptr);
    EXPECT_EQ(branch->parent, ttd::kTTDTrunk);
    EXPECT_EQ(branch->fork.frame, _start + 10);
    EXPECT_EQ(branch->meta.reason, "resume");

    const uint64_t branchAt20 = RamAt(_start + 20);
    EXPECT_NE(branchAt20, trunkAt20) << "the key held on the branch changed nothing";

    std::string error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{_start + 20, 0}, error)) << error;
    EXPECT_EQ(_ttd->ActiveBranch(), ttd::kTTDTrunk);
    EXPECT_EQ(Ram(), trunkAt20) << "the trunk's future after the fork is lost";

    ASSERT_TRUE(_ttd->SwitchBranch(1, ttd::TTDTimePoint{_start + 20, 0}, error)) << error;
    EXPECT_EQ(Ram(), branchAt20);
    // Before the fork both lineages hold the trunk's frames
    EXPECT_EQ(_ttd->ViewedBranch(), 1u);
    RamAt(_start + 5);
    EXPECT_EQ(_ttd->ViewedBranch(), ttd::kTTDTrunk);
}

/// T-E8 (§4.6): a resume in the past followed by a seek elsewhere before anything was recorded leaves nothing; a
/// branch that recorded a frame stays
TEST_F(TimeTravelController_Branches_Test, AnEmptyBranchLeavesNoTrace)
{
    RecordTrunk();
    const uint64_t trunkAt20 = RamAt(_start + 20);
    const size_t trunkCheckpoints = _ttd->GetCheckpointCount();

    // A seek elsewhere
    ASSERT_TRUE(_ttd->SeekTo({_start + 10, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 10, 0}));
    ASSERT_EQ(_ttd->Branches().Count(), 2u);
    ASSERT_TRUE(_ttd->SeekTo({_start + 20, 0}));
    EXPECT_EQ(_ttd->Branches().Count(), 1u) << "the empty branch went";
    EXPECT_EQ(_ttd->ActiveBranch(), ttd::kTTDTrunk);
    EXPECT_EQ(Ram(), trunkAt20) << "the trunk's future is the active one again";
    EXPECT_EQ(_ttd->GetCheckpointCount(), trunkCheckpoints);
    EXPECT_EQ(_ttd->Branches().Get(1), nullptr);

    // A switch away
    ASSERT_TRUE(_ttd->SeekTo({_start + 12, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 12, 0}));
    ASSERT_EQ(_ttd->Branches().Count(), 2u);
    const uint32_t second = _ttd->ActiveBranch();
    EXPECT_NE(second, 1u) << "ids are never reused";
    std::string error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{_start + 25, 0}, error)) << error;
    EXPECT_EQ(_ttd->Branches().Count(), 1u);

    // Staying at the fork keeps it; a frame recorded keeps it too
    ASSERT_TRUE(_ttd->SeekTo({_start + 14, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 14, 0}));
    ASSERT_TRUE(_ttd->SeekTo({_start + 14, 0}));
    EXPECT_EQ(_ttd->Branches().Count(), 2u) << "a seek to the fork moment leaves the branch";
    _ttd->ResumeRecordingLive();
    _emulator->RunNFrames(1);
    ASSERT_TRUE(_ttd->SeekTo({_start + 5, 0}));
    EXPECT_EQ(_ttd->Branches().Count(), 2u) << "a branch with a frame of its own stays";
}

/// T-C2: both branches go on recording at their ends; a resume at the end forks nothing
TEST_F(TimeTravelController_Branches_Test, BothBranchesContinueAtTheirEnds)
{
    RecordTrunk();
    const uint64_t trunkEnd = _ttd->GetSessionInfo().currentEndFrame;
    RecordBranchFrom(_start + 10, 10);
    const uint64_t branchEnd = _ttd->GetSessionInfo().currentEndFrame;

    std::string error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{trunkEnd, 0}, error)) << error;
    EXPECT_EQ(_ttd->GetSessionInfo().currentEndFrame, trunkEnd);
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({trunkEnd, 0}));
    _emulator->RunNFrames(5);
    _ttd->StopRecording();
    EXPECT_EQ(_ttd->Branches().Count(), 2u) << "a resume at the end forked";
    EXPECT_EQ(_ttd->ActiveBranch(), ttd::kTTDTrunk);
    EXPECT_GE(_ttd->GetSessionInfo().currentEndFrame, trunkEnd + 5);

    ASSERT_TRUE(_ttd->SwitchBranch(1, ttd::TTDTimePoint{branchEnd, 0}, error)) << error;
    EXPECT_EQ(_ttd->GetSessionInfo().currentEndFrame, branchEnd);
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({branchEnd, 0}));
    _emulator->RunNFrames(5);
    _ttd->StopRecording();
    EXPECT_EQ(_ttd->Branches().Count(), 2u);
    EXPECT_GE(_ttd->GetSessionInfo().currentEndFrame, branchEnd + 5);
}

/// T-C3: running forward from the trunk's part of the lineage goes on with the branch's records at its fork. A run
/// stops a few instructions past a frame boundary, wherever it lands: the moment is compared exactly, against a
/// seek to the same moment (replayed from the owner's checkpoint) on the branch and on the trunk
TEST_F(TimeTravelController_Branches_Test, RunningForwardFollowsTheLineageAcrossTheFork)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 20);

    ASSERT_TRUE(_ttd->SeekTo({_start + 5, 0}));
    EXPECT_EQ(_ttd->ViewedBranch(), ttd::kTTDTrunk);
    _emulator->RunNFrames(10);
    const ttd::TTDTimePoint at = _ttd->CurrentPosition();
    ASSERT_GT(at.frame, _start + 10) << "the run did not pass the fork";
    EXPECT_EQ(_ttd->ViewedBranch(), 1u);
    const uint64_t ran = Ram();

    ASSERT_TRUE(_ttd->SeekTo(at));
    EXPECT_EQ(Ram(), ran) << "the run left the branch's records";
    std::string error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, at, error)) << error;
    EXPECT_NE(Ram(), ran) << "the run stayed on the trunk's old future";
}

/// T-C4: a branch off the active lineage is deleted with its history; the active lineage is protected
TEST_F(TimeTravelController_Branches_Test, DeletingABranchOffTheActiveLineage)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 10);   // branch 1
    std::string error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{_start + 20, 0}, error)) << error;
    RecordBranchFrom(_start + 20, 5);    // branch 2, from the trunk
    ASSERT_EQ(_ttd->Branches().Count(), 3u);
    ASSERT_EQ(_ttd->ActiveBranch(), 2u);

    std::vector<uint32_t> removed;
    EXPECT_FALSE(_ttd->DeleteBranch(2, false, removed, error)) << "the active branch was deleted";
    EXPECT_FALSE(_ttd->DeleteBranch(ttd::kTTDTrunk, false, removed, error));
    ASSERT_TRUE(_ttd->DeleteBranch(1, false, removed, error)) << error;
    EXPECT_EQ(removed, std::vector<uint32_t>{1});
    EXPECT_EQ(_ttd->Branches().Count(), 2u);
    EXPECT_FALSE(_ttd->SwitchBranch(1, std::nullopt, error));

    // The lineage still reads both parts
    RamAt(_start + 5);
    EXPECT_EQ(_ttd->ViewedBranch(), ttd::kTTDTrunk);
    RamAt(_start + 22);
    EXPECT_EQ(_ttd->ViewedBranch(), 2u);
}

/// A new session drops every branch
TEST_F(TimeTravelController_Branches_Test, ANewSessionHasTheTrunkAlone)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 5);
    ASSERT_EQ(_ttd->Branches().Count(), 2u);
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3);
    _ttd->StopRecording();
    EXPECT_EQ(_ttd->Branches().Count(), 1u);
    EXPECT_EQ(_ttd->ActiveBranch(), ttd::kTTDTrunk);
    EXPECT_EQ(_ttd->ViewedBranch(), ttd::kTTDTrunk);
}


/// T-C3 / T-C4: a tool's edit at a point of the recorded past forks a branch there and is recorded on it; running
/// from the fork across the next boundary reproduces it (the edit changes where the program writes)
TEST_F(TimeTravelController_Branches_Test, AnEditInThePastForksAndIsReplayed)
{
    RecordTrunk();
    ASSERT_TRUE(_ttd->SeekTo({_start + 10, 0}));
    ASSERT_TRUE(_emulator->SetRegisterFromTool("test register", "HL", 0x7000));
    ASSERT_EQ(_ttd->Branches().Count(), 2u);
    EXPECT_EQ(_ttd->ActiveBranch(), 1u);
    EXPECT_EQ(_ttd->Branches().Get(1)->meta.reason, "edit");
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording);
    _emulator->RunNFrames(10);
    _ttd->StopRecording();

    // From the fork's checkpoint the replay applies the edit, then runs into the next frame
    ASSERT_TRUE(_ttd->SeekTo({_start + 10, 0}));
    _emulator->RunNFrames(2);
    const ttd::TTDTimePoint at = _ttd->CurrentPosition();
    ASSERT_GT(at.frame, _start + 10);
    const uint64_t ran = Ram();
    ASSERT_TRUE(_ttd->SeekTo(at));   // from a checkpoint captured live after the edit
    EXPECT_EQ(Ram(), ran) << "the replay did not reproduce the edit";
    std::string error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, at, error)) << error;
    EXPECT_NE(Ram(), ran) << "the edit reached the trunk";
}

/// T-C5: the paused end is the active branch's: switched to another branch, running to its end goes on recording
/// there (no new branch)
TEST_F(TimeTravelController_Branches_Test, RunningToTheActiveBranchsEndContinuesIt)
{
    RecordTrunk();
    const uint64_t trunkEnd = _ttd->GetSessionInfo().currentEndFrame;
    ASSERT_TRUE(_ttd->SeekTo({_start + 10, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 10, 0}));
    HoldKey(1);
    _emulator->RunNFrames(4);   // recording on branch 1
    ASSERT_TRUE(_ttd->SeekTo({_start + 12, 0}));   // the recording pauses for browsing
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);

    std::string error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{trunkEnd - 3, 0}, error)) << error;
    _emulator->RunNFrames(6);   // across the trunk's end
    EXPECT_EQ(_ttd->GetState(), ttd::TTDSessionState::Recording) << "the trunk's end did not continue it";
    EXPECT_EQ(_ttd->Branches().Count(), 2u);
    EXPECT_EQ(_ttd->ActiveBranch(), ttd::kTTDTrunk);
    _ttd->StopRecording();
    EXPECT_GT(_ttd->GetSessionInfo().currentEndFrame, trunkEnd);
}

/// T-C6: bookmarks per branch. Each takes the branch that holds its moment; the list shows the active lineage's, all
/// of them on request; a seek to one on another branch switches to it; deleting a branch drops its bookmarks
TEST_F(TimeTravelController_Branches_Test, BookmarksBelongToTheirBranch)
{
    RecordTrunk();
    std::string error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 5, 0}, "shared", &error)) << error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 20, 0}, "trunk-late", &error)) << error;
    const uint64_t trunkLate = RamAt(_start + 20);
    RecordBranchFrom(_start + 10, 15);
    ASSERT_EQ(_ttd->ActiveBranch(), 1u);
    ASSERT_TRUE(_ttd->AddBookmark({_start + 20, 0}, "branch-late", &error)) << error;
    const uint64_t branchLate = RamAt(_start + 20);
    ASSERT_NE(trunkLate, branchLate);

    auto labels = [](const std::vector<ttd::TTDBookmark>& list) {
        std::vector<std::string> out;
        for (const ttd::TTDBookmark& b : list)
            out.push_back(b.label);
        return out;
    };
    EXPECT_EQ(labels(_ttd->GetBookmarks()), (std::vector<std::string>{"shared", "branch-late"}));
    EXPECT_EQ(_ttd->GetAllBookmarks().size(), 3u);
    ttd::TTDBookmark b;
    ASSERT_TRUE(_ttd->FindBookmark("shared", b));
    EXPECT_EQ(b.branch, ttd::kTTDTrunk) << "before the fork the trunk holds it";
    ASSERT_TRUE(_ttd->FindBookmark("branch-late", b));
    EXPECT_EQ(b.branch, 1u);

    // To the trunk's bookmark: the trunk becomes active, the machine at its moment there
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_ttd->SeekToBookmark("trunk-late", &r, &error)) << error;
    EXPECT_TRUE(r.reached);
    EXPECT_EQ(_ttd->ActiveBranch(), ttd::kTTDTrunk);
    EXPECT_EQ(Ram(), trunkLate);
    EXPECT_EQ(labels(_ttd->GetBookmarks()), (std::vector<std::string>{"shared", "trunk-late"}));
    // And back
    ASSERT_TRUE(_ttd->SeekToBookmark("branch-late", &r, &error)) << error;
    EXPECT_EQ(_ttd->ActiveBranch(), 1u);
    EXPECT_EQ(Ram(), branchLate);

    // Deleting the branch (off the lineage again) drops its bookmark only
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, std::nullopt, error)) << error;
    std::vector<uint32_t> removed;
    ASSERT_TRUE(_ttd->DeleteBranch(1, false, removed, error)) << error;
    EXPECT_EQ(labels(_ttd->GetAllBookmarks()), (std::vector<std::string>{"shared", "trunk-late"}));
}

/// T-C7: the lineage queries (find-last, reverse-step, reverse-continue) cross the fork into the parent's history and
/// stop at the session start. From the branch they give what they gave on the linear session before it forked: the
/// history before the fork is that session's
TEST_F(TimeTravelController_Branches_Test, LineageQueriesCrossTheFork)
{
    RecordTrunk();
    // The loop writes an address about every 7 frames: the one it writes next at frame 9 is not written again
    // before frame 12
    ASSERT_TRUE(_ttd->SeekTo({_start + 9, 0}));
    ttd::TTDSearchQuery query;
    query.addrFrom = query.addrTo = _context->pCore->GetZ80()->hl;
    query.access = ttd::TTDAccessType::Write;

    struct Seen
    {
        std::optional<ttd::TTDSearchResult> last;
        ttd::TTDTimePoint stepped;
        uint16_t steppedPc = 0;
        uint64_t steppedRam = 0;
        ttd::TTDReverseContinueResult first;
        ttd::TTDReverseContinueResult never;
        std::vector<ttd::TTDTimePoint> writes;    // FindAccesses: every write to the address before frame 12
        std::vector<ttd::TTDTimePoint> changes;   // find-event: the value's changes up to frame 11
        std::vector<std::pair<ttd::TTDTimePoint, uint16_t>> cause;   // cause-chain links of the byte at frame 12
        uint64_t explained = 0;                   // explain-frame: instructions of frame 9
        uint64_t explainedCodeT = 0;
        ttd::TTDReplayCheckResult check;          // replay-check of frames 2..11
        uint32_t frameLength = 0;                 // FrameLength of trunk frame 3
    };
    auto ask = [&]() {
        Seen s;
        EXPECT_TRUE(_ttd->SeekTo({_start + 12, 0}));
        s.last = _ttd->FindLastAccess(query);
        EXPECT_TRUE(_ttd->SeekTo({_start + 10, 0}));
        EXPECT_TRUE(_ttd->ReverseStepInstructions(5));
        s.stepped = _ttd->CurrentPosition();
        s.steppedPc = _context->pCore->GetZ80()->pc;
        s.steppedRam = Ram();
        EXPECT_TRUE(_ttd->SeekTo({_start + 12, 0}));
        s.first = _ttd->ReverseContinue(std::vector<uint16_t>{0x8000});   // the program's first instruction
        EXPECT_TRUE(_ttd->SeekTo({_start + 12, 0}));
        s.never = _ttd->ReverseContinue(std::vector<uint16_t>{0x9000});   // never runs
        s.frameLength = _ttd->FrameLength(_start + 3);

        EXPECT_TRUE(_ttd->SeekTo({_start + 12, 0}));
        ttd::TTDFindOptions all;
        all.all = true;
        for (const ttd::TTDFoundAccess& a : _ttd->FindAccesses(query, all).hits)
            s.writes.push_back(a.access.time);

        ttd::TTDFindEventQuery changed;
        changed.address = query.addrFrom;
        changed.fromFrame = _start;
        changed.toFrame = _start + 11;
        changed.limit = 20;
        const ttd::TTDFindEventResult found = _ttd->QueryFindEvent(changed);
        EXPECT_TRUE(found.error.empty()) << found.error;
        for (const ttd::TTDFindEventHit& h : found.hits)
            s.changes.push_back(h.time);

        ttd::TTDCauseQuery why;
        why.address = query.addrFrom;
        why.hasTime = true;
        why.at = {_start + 12, 0};
        const ttd::TTDCauseResult chain = _ttd->QueryCauseChain(why);
        EXPECT_TRUE(chain.error.empty()) << chain.error;
        for (const ttd::TTDCauseLink& l : chain.links)
            s.cause.emplace_back(l.time, l.pc);

        ttd::TTDExplainFrameQuery explain;
        explain.hasFrame = true;
        explain.frame = _start + 9;
        const ttd::TTDExplainFrameResult frame = _ttd->QueryExplainFrame(explain);
        EXPECT_TRUE(frame.error.empty()) << frame.error;
        s.explained = frame.instructions;
        s.explainedCodeT = frame.codeT;

        ttd::TTDReplayCheckQuery check;
        check.hasFrom = check.hasTo = true;
        check.fromFrame = _start + 2;
        check.toFrame = _start + 11;
        s.check = _ttd->QueryReplayCheck(check);
        EXPECT_TRUE(s.check.error.empty()) << s.check.error;
        return s;
    };
    const Seen linear = ask();
    ASSERT_TRUE(linear.last.has_value());
    ASSERT_LT(linear.last->time.frame, _start + 10);

    RecordBranchFrom(_start + 10, 15);
    ASSERT_EQ(_ttd->ActiveBranch(), 1u);
    const Seen branch = ask();
    ASSERT_TRUE(branch.last.has_value()) << "find-last did not cross the fork";
    EXPECT_EQ(branch.last->time, linear.last->time);
    EXPECT_EQ(branch.last->value, linear.last->value);
    EXPECT_EQ(branch.stepped, linear.stepped) << "reverse-step from the fork";
    EXPECT_EQ(branch.steppedPc, linear.steppedPc);
    EXPECT_EQ(branch.steppedRam, linear.steppedRam);
    EXPECT_TRUE(branch.first.matched);
    EXPECT_EQ(branch.first.arrivedAt, linear.first.arrivedAt) << "reverse-continue to the session's first instruction";
    EXPECT_FALSE(branch.never.matched);
    EXPECT_EQ(branch.never.arrivedAt, linear.never.arrivedAt) << "a miss stops at the session start";
    EXPECT_TRUE(branch.never.window.searched);
    EXPECT_EQ(branch.never.window.from, linear.never.window.from) << "a miss reports the whole lineage as searched";
    EXPECT_EQ(branch.frameLength, linear.frameLength) << "the length of a parent frame before the fork";
    EXPECT_FALSE(linear.writes.empty());
    EXPECT_EQ(branch.writes, linear.writes) << "find-all crosses the fork";
    EXPECT_FALSE(linear.changes.empty());
    EXPECT_EQ(branch.changes, linear.changes) << "find-event crosses the fork";
    EXPECT_FALSE(linear.cause.empty());
    EXPECT_EQ(branch.cause, linear.cause) << "cause-chain crosses the fork";
    EXPECT_GT(linear.explained, 0u);
    EXPECT_EQ(branch.explained, linear.explained) << "explain-frame of a trunk frame";
    EXPECT_EQ(branch.explainedCodeT, linear.explainedCodeT);
    EXPECT_GT(linear.check.checked, 0u);
    EXPECT_EQ(branch.check.checked, linear.check.checked) << "replay-check crosses the fork";
    EXPECT_EQ(branch.check.diverged, 0u);
    EXPECT_EQ(_ttd->ActiveBranch(), 1u) << "a query does not change the active branch";
}

/// T-F1: a session saved with its branches loads with the same tree - parents, forks, names, tags, notes, pins, the
/// active branch, the bookmarks' branches - and every frame of every branch as recorded
TEST_F(TimeTravelController_Branches_Test, SaveAndLoadKeepEveryBranch)
{
    RecordTrunk();
    ttd::TTDBranchMeta named;
    named.name = "experiment";
    named.tags = {"keep"};
    named.note = "the key held";
    named.createdBy = "test";
    _ttd->SetNextBranchMeta(named);
    RecordBranchFrom(_start + 10, 15);   // 1, from the trunk
    RecordBranchFrom(_start + 18, 8);    // 2, from 1
    ASSERT_EQ(_ttd->Branches().Count(), 3u);
    ASSERT_EQ(_ttd->Branches().Get(2)->parent, 1u);
    std::string error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 5, 0}, "early", &error)) << error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 20, 0}, "late", &error)) << error;   // on branch 2
    ASSERT_TRUE(_ttd->SwitchBranch(1, ttd::TTDTimePoint{_start + 20, 0}, error)) << error;
    const ttd::TTDBranchInfo before1 = *_ttd->Branches().Get(1);
    const ttd::TTDBranchInfo before2 = *_ttd->Branches().Get(2);
    const auto frames = EveryFrame();
    ASSERT_EQ(frames.size(), 3u);

    const std::string file = Save();
    ASSERT_TRUE(Load(file, error)) << error;

    ASSERT_EQ(_ttd->Branches().Count(), 3u);
    EXPECT_EQ(_ttd->ActiveBranch(), 1u) << "the active branch as saved";
    const ttd::TTDBranchInfo* b1 = _ttd->Branches().Get(1);
    const ttd::TTDBranchInfo* b2 = _ttd->Branches().Get(2);
    ASSERT_TRUE(b1 && b2);
    EXPECT_EQ(b1->meta.name, "experiment");
    EXPECT_EQ(b1->meta.tags, (std::vector<std::string>{"keep"}));
    EXPECT_EQ(b1->meta.note, "the key held");
    EXPECT_EQ(b1->meta.createdBy, "test");
    EXPECT_EQ(b1->meta.createdHostMicros, before1.meta.createdHostMicros);
    EXPECT_EQ(b1->guid, before1.guid);
    EXPECT_EQ(b1->parent, ttd::kTTDTrunk);
    EXPECT_EQ(b1->fork, before1.fork);
    EXPECT_EQ(b1->meta.reason, "resume");
    EXPECT_EQ(b2->parent, 1u);
    EXPECT_EQ(b2->fork, before2.fork);
    EXPECT_EQ(b2->end, before2.end);
    EXPECT_EQ(b2->guid, before2.guid);
    ttd::TTDBookmark mark;
    ASSERT_TRUE(_ttd->FindBookmark("late", mark));
    EXPECT_EQ(mark.branch, 2u);
    ASSERT_TRUE(_ttd->FindBookmark("early", mark));
    EXPECT_EQ(mark.branch, ttd::kTTDTrunk);

    EXPECT_EQ(EveryFrame(), frames) << "a frame of some branch differs after the load";

    // The loaded session goes on: a resume in its past forks again
    ASSERT_TRUE(_ttd->SwitchBranch(2, ttd::TTDTimePoint{_start + 22, 0}, error)) << error;
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 22, 0}));
    _emulator->RunNFrames(3);
    _ttd->StopRecording();
    EXPECT_EQ(_ttd->Branches().Count(), 4u);
    EXPECT_EQ(_ttd->Branches().Get(_ttd->ActiveBranch())->parent, 2u);
}

/// T-F1: deleted branches are not in the file; the file numbers the rest densely, the tree and the frames stay
TEST_F(TimeTravelController_Branches_Test, SaveLeavesDeletedBranchesOut)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 12);   // 1
    std::string error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{_start + 20, 0}, error)) << error;
    RecordBranchFrom(_start + 15, 10);   // 2
    std::vector<uint32_t> removed;
    ASSERT_TRUE(_ttd->DeleteBranch(1, false, removed, error)) << error;
    ASSERT_EQ(_ttd->ActiveBranch(), 2u);
    const ttd::TTDBranchInfo kept = *_ttd->Branches().Get(2);
    auto frames = EveryFrame();

    ASSERT_TRUE(Load(Save(), error)) << error;
    ASSERT_EQ(_ttd->Branches().Count(), 2u);
    EXPECT_EQ(_ttd->ActiveBranch(), 1u) << "branch 2 is the file's branch 1";
    const ttd::TTDBranchInfo* b = _ttd->Branches().Get(1);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->guid, kept.guid);
    EXPECT_EQ(b->meta.name, kept.meta.name);
    EXPECT_EQ(b->fork.frame, kept.fork.frame);
    frames[1] = frames[2];
    frames.erase(2);
    EXPECT_EQ(EveryFrame(), frames);
}

/// T-F2 (the part a unit test can check): a session without branches declares no branch stream, and loads as one
TEST_F(TimeTravelController_Branches_Test, ASessionWithoutBranchesHasNoBranchStreams)
{
    RecordTrunk();
    const std::string file = Save();
    for (const uint16_t id : Streams(file))
        EXPECT_FALSE(id >= 0x0200 && id <= 0x02FF) << "stream " << id;
    std::string error;
    ASSERT_TRUE(Load(file, error)) << error;
    EXPECT_EQ(_ttd->Branches().Count(), 1u);

    // With a branch the twins and the table are declared
    RecordBranchFrom(_start + 10, 5);
    const std::vector<uint16_t> branched = Streams(Save());
    EXPECT_NE(std::find(branched.begin(), branched.end(), ttd::sessionstream::kBranchTable), branched.end());
    EXPECT_NE(std::find(branched.begin(), branched.end(), ttd::sessionstream::BranchTwin(ttd::sessionstream::kPieces)),
              branched.end());
}

/// T-F3: saving the active lineage gives a linear file: no branch stream, the lineage's frames and bookmarks, read
/// the same by the reader without branch support
TEST_F(TimeTravelController_Branches_Test, SaveLineageGivesALinearFile)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 15);   // 1, from the trunk
    RecordBranchFrom(_start + 18, 8);    // 2, from 1
    std::string error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 5, 0}, "trunk-early", &error)) << error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 15, 0}, "on-1", &error)) << error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 22, 0}, "on-2", &error)) << error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{_start + 26, 0}, error)) << error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 12, 0}, "trunk-late", &error)) << error;   // after branch 1's fork
    RecordBranchFrom(_start + 26, 4);    // 3, from the trunk: not on 2's lineage
    ASSERT_TRUE(_ttd->AddBookmark({_start + 28, 0}, "on-3", &error)) << error;
    ASSERT_EQ(_ttd->Branches().Count(), 4u);
    ASSERT_TRUE(_ttd->SwitchBranch(2, std::nullopt, error)) << error;
    const ttd::TTDPosition end = _ttd->Branches().Get(2)->end;
    const auto frames = LineageFrames();

    const std::string file = Save(/*lineageOnly=*/true);
    EXPECT_FALSE(HasBranchStreams(Streams(file)));

    auto check = [&](const char* reader) {
        SCOPED_TRACE(reader);
        ASSERT_EQ(_ttd->Branches().Count(), 1u);
        EXPECT_EQ(_ttd->Branches().Trunk().end.frame, end.frame);
        EXPECT_EQ(LineageFrames(), frames);
        std::vector<std::string> labels;
        for (const ttd::TTDBookmark& b : _ttd->GetAllBookmarks())
        {
            labels.push_back(b.label);
            EXPECT_EQ(b.branch, ttd::kTTDTrunk) << b.label;
        }
        std::sort(labels.begin(), labels.end());
        EXPECT_EQ(labels, (std::vector<std::string>{"on-1", "on-2", "trunk-early"}));
    };
    ASSERT_TRUE(Load(file, error)) << error;
    check("this reader");
    {
        const OldReader old;
        ASSERT_TRUE(Load(file, error)) << error;
        check("the reader without branches");
    }
}

/// T-F4: a damaged branch table leaves the damaged branch (and its descendants) out, with the trunk and the other
/// branches as saved
TEST_F(TimeTravelController_Branches_Test, ADamagedBranchTableLeavesTheBranchOut)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 15);   // 1, from the trunk
    RecordBranchFrom(_start + 18, 8);    // 2, from 1
    const ttd::TTDGuid guid1 = _ttd->Branches().Get(1)->guid;
    const uint64_t end1 = _ttd->Branches().Get(1)->end.frame;
    const auto frames = EveryFrame();
    const std::string file = Save();

    struct Damage
    {
        const char* what;
        std::function<void(CreateRecord&)> edit;
        size_t branches;   ///< left in the tree
    };
    const Damage damages[] = {
        {"dangling parent", [](CreateRecord& c) { c.parent = c.local == 1 ? 9 : c.parent; }, 1},
        {"cycle", [](CreateRecord& c) { c.parent = c.local == 1 ? 2 : c.parent; }, 1},
        {"own parent", [](CreateRecord& c) { c.parent = c.local == 2 ? 2 : c.parent; }, 2},
        {"local id out of range", [](CreateRecord& c) { c.local = c.local == 2 ? 40 : c.local; }, 2},
        {"fork past the parent's end",
         [end1](CreateRecord& c) {
             if (c.local == 2)
                 c.frame = end1 + 100;
         },
         2},
        {"fork past the trunk's end",
         [this](CreateRecord& c) {
             if (c.local == 1)
                 c.frame = _start + 1000;
         },
         1},
        {"duplicate GUID",
         [guid1](CreateRecord& c) {
             if (c.local == 2 && c.meta.size() > 17)
                 std::copy(guid1.begin(), guid1.end(), c.meta.begin() + 1);
         },
         3},
    };
    for (const Damage& d : damages)
    {
        SCOPED_TRACE(d.what);
        std::string error;
        ASSERT_TRUE(Load(EditBranchTable(file, d.edit), error)) << error;
        ASSERT_EQ(_ttd->Branches().Count(), d.branches);
        const auto loaded = EveryFrame();
        EXPECT_EQ(loaded.at(ttd::kTTDTrunk), frames.at(ttd::kTTDTrunk));
        if (d.branches > 1)
            EXPECT_EQ(loaded.at(1), frames.at(1));
        if (d.branches > 2)
        {
            EXPECT_EQ(loaded.at(2), frames.at(2));
            EXPECT_EQ(_ttd->Branches().Get(1)->guid, guid1);
            EXPECT_NE(_ttd->Branches().Get(2)->guid, guid1) << "the second holder of the GUID gets a new one";
        }
    }
}

/// T-F5: the reader without branch support loads a branched file's trunk, the same as a save of the trunk alone
TEST_F(TimeTravelController_Branches_Test, AnOldReaderLoadsTheTrunkOfABranchedFile)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 15);   // 1
    RecordBranchFrom(_start + 18, 8);    // 2, from 1
    std::string error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 22, 0}, "on-2", &error)) << error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{_start + 20, 0}, error)) << error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 25, 0}, "on-trunk", &error)) << error;
    const auto trunk = LineageFrames();
    const std::string trunkOnly = Save(/*lineageOnly=*/true);
    EXPECT_FALSE(HasBranchStreams(Streams(trunkOnly)));
    ASSERT_TRUE(_ttd->SwitchBranch(2, std::nullopt, error)) << error;
    const std::string branched = Save();

    auto check = [&](const char* file) {
        SCOPED_TRACE(file);
        ASSERT_EQ(_ttd->Branches().Count(), 1u);
        EXPECT_EQ(LineageFrames(), trunk);
        const std::vector<ttd::TTDBookmark> marks = _ttd->GetAllBookmarks();
        ASSERT_EQ(marks.size(), 1u) << "a branch's bookmark is the branch's";
        EXPECT_EQ(marks.front().label, "on-trunk");
    };
    const OldReader old;
    ASSERT_TRUE(Load(branched, error)) << error;
    check("branched");
    ASSERT_TRUE(Load(trunkOnly, error)) << error;
    check("trunk alone");
}
