#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/engine/ttdrecordingwriter.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdrecordingfolders.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace fs = std::filesystem;

/// Branches in the recording folder (docs/inprogress/2026-11-08-ttd-branches/storage.md §4, test-strategy.md
/// T-F6..T-F11): a file per line and segment, branches.tdx with the branch events and the closed segments, crash
/// recovery from the complete parts, delete records before the files go, a clean close.
///
/// The program folds the keyboard port into RAM #6000-#7FFF on every pass: a key held in one history and not in
/// the other makes the histories' memory differ from that frame on.
///
/// Runtime justification: each test records 30-60 frames of a small program into a folder and loads it back
/// (~0.2 s); the record-boundary cuts of T-F7 run on a sample of offsets, all of them behind TestTier::Long().
class TimeTravelController_BranchFolder_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    uint64_t _start = 0;
    std::string _root;

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
        _root = TestPathHelper::GetUniqueTestScratchPath("ttd-branch-folder");
        _ttd->SetShadowRecordingRoot(_root);

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

    void HoldKey(int frames)
    {
        auto* keys = _context->pDebugManager->GetKeyboardManager();
        keys->PressKey(ZXKEY_A);
        _emulator->RunNFrames(frames);
        keys->ReleaseKey(ZXKEY_A);
    }

    void RecordTrunk(int frames = 30)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _start = _context->emulatorState.frame_counter;
        _emulator->RunNFrames(frames);
        _ttd->StopRecording();
    }

    /// A branch from @p at: a key held for a frame, @p frames in all
    void RecordBranchFrom(uint64_t at, int frames)
    {
        ASSERT_TRUE(_ttd->SeekTo({at, 0})) << "frame " << at << " on branch " << _ttd->ActiveBranch() << " (start "
                                           << _start << ", end " << _ttd->Branches().Get(_ttd->ActiveBranch())->end.frame
                                           << ")";
        ASSERT_TRUE(_ttd->ResumeRecordingFrom({at, 0}));
        HoldKey(1);
        _emulator->RunNFrames(frames - 1);
        _ttd->StopRecording();
    }

    /// Trunk 30 frames, branch 1 from +10 (15 frames), branch 2 from branch 1 at +18 (8 frames), then the trunk
    /// again from its end (a second trunk file after the branches')
    void RecordTree()
    {
        RecordTrunk();
        RecordBranchFrom(_start + 10, 15);
        RecordBranchFrom(_start + 18, 8);
        std::string error;
        ASSERT_TRUE(SwitchToEnd(ttd::kTTDTrunk, error)) << error;
        const uint64_t trunkEnd = LastCheckpoint()->time.frame;
        ASSERT_TRUE(ContinueAtEnd()) << "the trunk records on at its end";
        _emulator->RunNFrames(5);
        _ttd->StopRecording();
        ASSERT_EQ(_ttd->Branches().Count(), 3u);
        ASSERT_GT(LastCheckpoint()->time.frame, trunkEnd);
    }

    std::string Folder() const { return _ttd->ShadowRecordingFolder(); }

    /// The active lineage's last checkpoint (the recording continues there without a fork)
    const ttd::TTDCheckpoint* LastCheckpoint() const
    {
        return _ttd->GetCheckpointCount() ? _ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 1) : nullptr;
    }

    bool ContinueAtEnd()
    {
        const ttd::TTDCheckpoint* last = LastCheckpoint();
        return last && _ttd->SeekTo(last->time) && _ttd->ResumeRecordingFrom(last->time);
    }

    bool SwitchToEnd(uint32_t branch, std::string& error)
    {
        const ttd::TTDBranchInfo* info = _ttd->Branches().Get(branch);
        if (!info)
            return false;
        // The active branch's end in the table is noted when it is left: its last checkpoint is the live one
        ttd::TTDTimePoint end{info->end.frame, static_cast<uint32_t>(info->end.tInFrame)};
        if (branch == _ttd->ActiveBranch() && LastCheckpoint())
            end = LastCheckpoint()->time;
        return _ttd->SwitchBranch(branch, end, error);
    }

    static std::vector<std::string> FilesIn(const std::string& folder)
    {
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(FileHelper::ToFsPath(folder)))
            names.push_back(FileHelper::FromFsPath(entry.path().filename()));
        std::sort(names.begin(), names.end());
        return names;
    }

    static std::vector<uint8_t> ReadBytes(const std::string& path)
    {
        std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    static void WriteBytes(const std::string& path, const std::vector<uint8_t>& bytes, size_t size)
    {
        std::ofstream out(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(size));
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

    /// The folder's files joined and loaded into the controller (its own folder goes with the load)
    bool LoadFolder(const std::string& folder, std::string& error)
    {
        const auto opened = ttd::TTDRecordingFolder::Open(folder);
        if (!opened)
        {
            error = "not a recording folder";
            return false;
        }
        const std::string joined = TestPathHelper::GetUniqueTestScratchPath("ttd-branch-folder-joined") + ".ttd";
        if (!opened->SaveAs(joined, true, error))
            return false;
        const std::vector<uint8_t> bytes = ReadBytes(joined);
        std::istringstream in(std::string(bytes.begin(), bytes.end()));
        return _ttd->DeserializeSession(in, error);
    }

    /// A copy of the folder (a crash's state is made on the copy)
    static std::string CopyFolder(const std::string& folder, const std::string& tag)
    {
        const std::string copy = TestPathHelper::GetUniqueTestScratchPath(tag);
        fs::create_directories(FileHelper::ToFsPath(copy));
        for (const auto& entry : fs::directory_iterator(FileHelper::ToFsPath(folder)))
            fs::copy_file(entry.path(), FileHelper::ToFsPath(copy) / entry.path().filename(),
                          fs::copy_options::overwrite_existing);
        return copy;
    }
};

/// The folder holds a file per line: the trunk's as today, each branch's named after its line; the joined files
/// load every branch with every frame
TEST_F(TimeTravelController_BranchFolder_Test, EveryBranchIsOnDiskAndLoads)
{
    RecordTree();
    const std::string folder = Folder();
    ASSERT_FALSE(folder.empty());
    const std::vector<std::string> names = FilesIn(folder);
    EXPECT_EQ(names, (std::vector<std::string>{"branches.tdx", "owner.pid", "segment-0000.ttd",
                                               "segment-0001-b1.ttd", "segment-0002-b2.ttd", "segment-0003.ttd"}));
    const ttd::TTDBranchInfo before1 = *_ttd->Branches().Get(1);
    const auto frames = EveryFrame();
    ASSERT_EQ(frames.size(), 3u);

    std::string error;
    ASSERT_TRUE(LoadFolder(folder, error)) << error;
    ASSERT_EQ(_ttd->Branches().Count(), 3u);
    const ttd::TTDBranchInfo* b1 = _ttd->Branches().Get(1);
    ASSERT_NE(b1, nullptr);
    EXPECT_EQ(b1->guid, before1.guid);
    EXPECT_EQ(b1->meta.name, before1.meta.name);
    EXPECT_EQ(b1->fork, before1.fork);
    EXPECT_EQ(_ttd->Branches().Get(2)->parent, 1u);
    EXPECT_EQ(EveryFrame(), frames) << "a frame of some branch differs after the folder's load";
}

/// T-F6: branches.tdx deleted is rebuilt from the files, record for record
TEST_F(TimeTravelController_BranchFolder_Test, TheFolderIndexIsRebuiltFromTheFiles)
{
    RecordTree();
    const auto folder = ttd::TTDRecordingFolder::Open(Folder());
    ASSERT_NE(folder, nullptr);
    ttd::TTDBranchIndex written;
    std::string error;
    ASSERT_TRUE(ttd::TTDRecordingFolder::ReadBranchIndex(folder->BranchIndexPath(), written, error)) << error;
    EXPECT_EQ(written.creates.size(), 2u);
    EXPECT_EQ(written.segments.size(), 4u) << "every closed file";
    EXPECT_TRUE(written.deleted.empty());

    const ttd::TTDBranchIndex fromFiles = folder->IndexFromFiles();
    EXPECT_EQ(fromFiles.creates, written.creates);
    EXPECT_EQ(fromFiles.segments, written.segments);

    fs::remove(FileHelper::ToFsPath(folder->BranchIndexPath()));
    ASSERT_TRUE(folder->RebuildBranchIndex(&error)) << error;
    ttd::TTDBranchIndex rebuilt;
    ASSERT_TRUE(ttd::TTDRecordingFolder::ReadBranchIndex(folder->BranchIndexPath(), rebuilt, error)) << error;
    EXPECT_EQ(rebuilt.creates, written.creates);
    EXPECT_EQ(rebuilt.segments, written.segments);
    EXPECT_EQ(rebuilt.records, written.records);
}

/// T-F7: a crash cuts the open file anywhere; the recovery keeps every complete part of every branch. Each cut at
/// a record boundary of the branch file (a sample in the default tier, every one in the long tier) loads the trunk
/// whole and the branch up to its last complete part, more of it the longer the file
TEST_F(TimeTravelController_BranchFolder_Test, ACrashKeepsTheCompletePartsOfEveryBranch)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 15);
    const std::string folder = Folder();
    const std::string branchFile = FileHelper::PathCombine(folder, "segment-0001-b1.ttd");
    const std::vector<uint8_t> whole = ReadBytes(branchFile);
    ASSERT_FALSE(whole.empty());
    std::vector<uint64_t> cuts;
    {
        ttd::TTDFileSource source(branchFile);
        ttd::TTDContainerReader reader;
        std::string error;
        ASSERT_TRUE(reader.Open(source, error, ttd::TTDSessionFile::KnownStream)) << error;
        for (const ttd::TTDPartRef& part : reader.Parts())
            for (const ttd::TTDRecordRef& record : part.records)
                cuts.push_back(record.offset);
    }
    ASSERT_GT(cuts.size(), 4u);
    cuts.push_back(whole.size() - 1);   // inside the trailer
    if (!TestTier::Long())
    {
        std::vector<uint64_t> sample;
        for (size_t k = 0; k < cuts.size(); k += std::max<size_t>(cuts.size() / 6, 1))
            sample.push_back(cuts[k]);
        sample.push_back(cuts.back());
        cuts = sample;
    }

    const std::vector<std::string> trunkFiles{FileHelper::PathCombine(folder, "segment-0000.ttd")};
    size_t trunkCheckpoints = 0;
    {
        ttd::TimeTravelEngine trunk;
        std::string error;
        ASSERT_TRUE(ttd::LoadRecording(trunk, trunkFiles, 0, error)) << error;
        trunkCheckpoints = trunk.CheckpointCount();
    }
    size_t previous = 0;
    for (const uint64_t cut : cuts)
    {
        const std::string copy = CopyFolder(folder, "ttd-branch-folder-cut");
        const std::string cutFile = FileHelper::PathCombine(copy, "segment-0001-b1.ttd");
        WriteBytes(cutFile, whole, static_cast<size_t>(cut));
        const auto opened = ttd::TTDRecordingFolder::Open(copy);
        ASSERT_NE(opened, nullptr);
        ttd::TimeTravelEngine loaded;
        std::string error;
        ttd::TTDSessionLoadReport report;
        ASSERT_TRUE(ttd::LoadRecording(loaded, opened->Segments(), 0, error, &report)) << "cut " << cut << ": " << error;
        ASSERT_TRUE(loaded.ViewLine(ttd::kTTDTrunk));
        EXPECT_EQ(loaded.CheckpointCount(), trunkCheckpoints) << "cut " << cut << ": the trunk is whole";
        size_t branchCheckpoints = 0;
        if (loaded.HasLine(1) && loaded.ViewLine(1))
            branchCheckpoints = loaded.CheckpointCount() - loaded.FirstCheckpoint();
        EXPECT_GE(branchCheckpoints, previous) << "cut " << cut << ": a longer file keeps at least as much";
        previous = branchCheckpoints;
    }
    EXPECT_GT(previous, 1u) << "the uncut parts of the branch load";
}

/// T-F8: a resume from the past, at any depth, never deletes or rewrites a file
TEST_F(TimeTravelController_BranchFolder_Test, AResumeNeverDeletesAFile)
{
    RecordTrunk();
    const std::string folder = Folder();
    std::map<std::string, std::vector<uint8_t>> seen;
    auto check = [&](const char* step) {
        for (const auto& [name, bytes] : seen)
            EXPECT_EQ(ReadBytes(FileHelper::PathCombine(folder, name)), bytes) << step << ": " << name;
        for (const std::string& name : FilesIn(folder))
            if (name.rfind("segment-", 0) == 0)
                seen.emplace(name, ReadBytes(FileHelper::PathCombine(folder, name)));
    };
    check("trunk");
    RecordBranchFrom(_start + 10, 15);
    check("first resume");
    RecordBranchFrom(_start + 18, 8);
    check("a resume on the branch");
    RecordBranchFrom(_start + 5, 6);
    check("a resume on the trunk again");
    EXPECT_EQ(seen.size(), 4u);
}

/// T-F9: a deleted branch gets its delete record before its files go; a crash between leaves files that the
/// folder no longer lists and the next open deletes
TEST_F(TimeTravelController_BranchFolder_Test, ADeletedBranchsFilesGoAfterItsRecord)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 15);   // 1
    std::string error;
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{_start + 20, 0}, error)) << error;
    RecordBranchFrom(_start + 20, 6);   // 2, from the trunk
    const std::string folder = Folder();
    const std::string file1 = FileHelper::PathCombine(folder, "segment-0001-b1.ttd");
    ASSERT_TRUE(fs::exists(FileHelper::ToFsPath(file1)));
    const std::vector<uint8_t> bytes1 = ReadBytes(file1);

    std::vector<uint32_t> removed;
    ASSERT_TRUE(_ttd->DeleteBranch(1, false, removed, error)) << error;
    EXPECT_FALSE(fs::exists(FileHelper::ToFsPath(file1))) << "the branch's files go";
    const auto opened = ttd::TTDRecordingFolder::Open(folder);
    ASSERT_NE(opened, nullptr);
    ttd::TTDBranchIndex index;
    ASSERT_TRUE(ttd::TTDRecordingFolder::ReadBranchIndex(opened->BranchIndexPath(), index, error)) << error;
    EXPECT_EQ(index.deleted, (std::set<uint32_t>{1}));

    // The crash between: the record is written, the file is still there
    WriteBytes(file1, bytes1, bytes1.size());
    for (const std::string& path : opened->Segments())
        EXPECT_EQ(path.find("-b1."), std::string::npos) << "a deleted branch's file is not the recording's";
    EXPECT_NE(ttd::TTDRecordingFolder::Open(folder), nullptr);
    EXPECT_FALSE(fs::exists(FileHelper::ToFsPath(file1))) << "the next open deletes it";

    _ttd->StopRecording();
    ASSERT_TRUE(LoadFolder(folder, error)) << error;
    EXPECT_EQ(_ttd->Branches().Count(), 2u);
    EXPECT_EQ(_ttd->Branches().Get(1), nullptr);
    ASSERT_NE(_ttd->Branches().Get(2), nullptr);
    EXPECT_EQ(_ttd->Branches().Get(2)->parent, ttd::kTTDTrunk);
}

/// Q-11: a loaded session that forks writes the new branch into a folder of its own; branches.tdx names the loaded
/// file, and the folder's join puts that file first: every branch loads with every frame. The loaded file changed or
/// gone: the join says so
TEST_F(TimeTravelController_BranchFolder_Test, ALoadedSessionsFolderJoinsTheLoadedFile)
{
    RecordTree();
    const std::string saved = TestPathHelper::GetUniqueTestScratchPath("ttd-branch-folder-source") + ".ttd";
    {
        std::ofstream out(FileHelper::ToFsPath(saved), std::ios::binary);
        std::string error;
        ASSERT_TRUE(_ttd->SerializeSession(out, error)) << error;
    }
    std::string error;
    ASSERT_TRUE(_ttd->DeserializeSessionFile(saved, error)) << error;
    _ttd->SetSessionSourcePath(saved);
    ASSERT_EQ(_ttd->Branches().Count(), 3u);
    ASSERT_TRUE(SwitchToEnd(ttd::kTTDTrunk, error)) << error;
    RecordBranchFrom(_start + 5, 8);   // a fourth branch, after the load
    ASSERT_EQ(_ttd->Branches().Count(), 4u);
    const std::string folder = Folder();
    ASSERT_FALSE(folder.empty()) << "the new branch is written to a folder";
    ttd::TTDBranchIndex index;
    ASSERT_TRUE(ttd::TTDRecordingFolder::ReadBranchIndex(FileHelper::PathCombine(folder, ttd::kBranchIndexFile), index,
                                                         error))
        << error;
    ASSERT_TRUE(index.source.has_value());
    EXPECT_EQ(index.source->path, saved);
    const auto frames = EveryFrame();

    const std::string copy = CopyFolder(folder, "ttd-branch-folder-q11");
    ASSERT_TRUE(LoadFolder(copy, error)) << error;
    EXPECT_EQ(_ttd->Branches().Count(), 4u);
    EXPECT_EQ(EveryFrame(), frames) << "a frame of some branch differs after the folder's load";

    // The loaded file changed, then gone
    const auto opened = ttd::TTDRecordingFolder::Open(copy);
    ASSERT_NE(opened, nullptr);
    const std::string joined = TestPathHelper::GetUniqueTestScratchPath("ttd-branch-folder-q11-joined") + ".ttd";
    std::vector<uint8_t> bytes = ReadBytes(saved);
    bytes.push_back(0);
    WriteBytes(saved, bytes, bytes.size());
    EXPECT_FALSE(opened->SaveAs(joined, true, error));
    EXPECT_NE(error.find("changed"), std::string::npos) << error;
    fs::remove(FileHelper::ToFsPath(saved));
    EXPECT_FALSE(opened->SaveAs(joined, true, error));
    EXPECT_NE(error.find("missing"), std::string::npos) << error;
}

/// T-F11: a clean close deletes the unsaved folder, with its branches; a saved file stays
TEST_F(TimeTravelController_BranchFolder_Test, ACleanCloseDeletesTheFolderAndKeepsTheSavedFile)
{
    RecordTrunk();
    RecordBranchFrom(_start + 10, 15);
    const std::string folder = Folder();
    ASSERT_TRUE(fs::exists(FileHelper::ToFsPath(FileHelper::PathCombine(folder, "segment-0001-b1.ttd"))));
    const std::string saved = TestPathHelper::GetUniqueTestScratchPath("ttd-branch-folder-saved") + ".ttd";
    {
        std::ofstream out(FileHelper::ToFsPath(saved), std::ios::binary);
        std::string error;
        ASSERT_TRUE(_ttd->SerializeSession(out, error)) << error;
    }
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
    EXPECT_FALSE(fs::exists(FileHelper::ToFsPath(folder))) << "the unsaved recording goes with the instance";
    EXPECT_TRUE(fs::exists(FileHelper::ToFsPath(saved)));
    EXPECT_GT(fs::file_size(FileHelper::ToFsPath(saved)), 0u);
}

/// T-F10 (S-7): under the warning's free space the files go on with a warning; under the stop limit they are
/// finished whole and the recording pauses; a new recording is refused while the disk is full and writes files again
/// when there is space
TEST_F(TimeTravelController_BranchFolder_Test, AFullDiskPausesTheRecordingWithAWarningBefore)
{
    uint64_t free = 100ull << 30;
    _ttd->SetFreeSpaceProbeForTesting([&free](const std::string&) { return free; });
    ASSERT_TRUE(_ttd->StartRecording());
    _start = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(30);
    EXPECT_EQ(_ttd->ShadowDiskSpace(), ttd::TTDDiskSpace::Ok);
    free = 512ull << 20;
    _emulator->RunNFrames(30);
    EXPECT_EQ(_ttd->ShadowDiskSpace(), ttd::TTDDiskSpace::Low);
    EXPECT_TRUE(_ttd->IsRecording()) << "a warning only";
    free = 64ull << 20;
    _emulator->RunNFrames(30);
    EXPECT_EQ(_ttd->ShadowDiskSpace(), ttd::TTDDiskSpace::Full);
    EXPECT_FALSE(_ttd->IsRecording()) << "the recording pauses at the limit";
    EXPECT_EQ(_ttd->GetSessionInfo().lastStopReason, "disk-full");
    const std::string folder = Folder();
    const std::vector<std::string> files = FilesIn(folder);

    // Everything recorded is in the files, finished whole
    {
        const auto opened = ttd::TTDRecordingFolder::Open(folder);
        ASSERT_NE(opened, nullptr);
        ttd::TimeTravelEngine loaded;
        std::string error;
        ttd::TTDSessionLoadReport report;
        ASSERT_TRUE(ttd::LoadRecording(loaded, opened->Segments(), 0, error, &report)) << error;
        EXPECT_TRUE(report.complete) << "the files were finished whole: " << report.stoppedAt;
        EXPECT_EQ(loaded.Checkpoint(loaded.CheckpointCount() - 1)->position.frame, LastCheckpoint()->time.frame)
            << "the files hold the recording to its pause";
    }

    // Recording again while the disk is full: refused, no file
    EXPECT_FALSE(ContinueAtEnd()) << "the recording does not go on on a full disk";
    EXPECT_FALSE(_ttd->IsRecording());
    EXPECT_EQ(FilesIn(folder), files) << "no file is written while the disk is full";

    // Space again: the recording goes on into a new file
    free = 100ull << 30;
    ASSERT_TRUE(ContinueAtEnd());
    _emulator->RunNFrames(10);
    EXPECT_TRUE(_ttd->IsRecording());
    _ttd->StopRecording();
    EXPECT_GT(FilesIn(folder).size(), files.size()) << "the files go on when there is space";
}

/// F-3: a folder cut by a crash after any sequence of forks, switches, deletions and recording loads: every file but
/// the one written last is whole, that one is cut at a random byte, branches.tdx at a random record. The load keeps
/// the trunk's files before the cut and never fails on the cut. Default tier: 4 sequences; long tier: 60
TEST_F(TimeTravelController_BranchFolder_Test, AFolderCutByACrashLoads)
{
    const int rounds = TestTier::Long() ? 60 : 4;
    uint32_t seed = 0x9E3779B9u;
    auto next = [&seed]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    for (int round = 0; round < rounds; ++round)
    {
        SCOPED_TRACE("round " + std::to_string(round));
        RecordTrunk(20);
        for (int step = 0; step < 4; ++step)
        {
            std::string error;
            const uint32_t active = _ttd->ActiveBranch();
            const ttd::TTDBranchInfo* info = _ttd->Branches().Get(active);
            const uint64_t from = active == ttd::kTTDTrunk ? _start : info->fork.frame;
            const uint64_t end = LastCheckpoint() ? LastCheckpoint()->time.frame : from;
            const uint64_t span = end > from ? end - from : 0;
            switch (next() % 4)
            {
            case 0:
            case 1:
                if (span > 2)
                    RecordBranchFrom(from + 1 + next() % (span - 1), 3 + static_cast<int>(next() % 6));
                break;
            case 2:
            {
                std::vector<uint32_t> ids;
                for (uint32_t id = 0; id < _ttd->Branches().Ids(); ++id)
                    if (_ttd->Branches().Get(id))
                        ids.push_back(id);
                EXPECT_TRUE(SwitchToEnd(ids[next() % ids.size()], error)) << error;
                ASSERT_TRUE(ContinueAtEnd());
                _emulator->RunNFrames(2);
                _ttd->StopRecording();
                break;
            }
            default:
            {
                std::vector<uint32_t> removed;
                for (uint32_t id = _ttd->Branches().Ids(); id-- > 1;)
                    if (_ttd->Branches().Get(id) && _ttd->DeleteBranch(id, false, removed, error))
                        break;
                break;
            }
            }
        }
        const std::string folder = Folder();
        ASSERT_FALSE(folder.empty());
        const std::string copy = CopyFolder(folder, "ttd-branch-folder-fuzz");
        // The newest file is the one a crash cuts
        const auto opened = ttd::TTDRecordingFolder::Open(copy);
        ASSERT_NE(opened, nullptr);
        const std::vector<std::string> segments = opened->Segments();
        ASSERT_FALSE(segments.empty());
        const std::vector<uint8_t> last = ReadBytes(segments.back());
        WriteBytes(segments.back(), last, next() % (last.size() + 1));
        if (fs::exists(FileHelper::ToFsPath(opened->BranchIndexPath())))
        {
            const std::vector<uint8_t> index = ReadBytes(opened->BranchIndexPath());
            WriteBytes(opened->BranchIndexPath(), index, index.size() - next() % (index.size() / 4 + 1));
        }
        ttd::TimeTravelEngine loaded;
        std::string error;
        // The first file whole loads; one file cut before its first checkpoint holds nothing
        const bool loads = ttd::LoadRecording(loaded, ttd::TTDRecordingFolder::Open(copy)->Segments(), 0, error);
        if (segments.size() > 1)
            EXPECT_TRUE(loads) << error;
        std::string joinError;
        const std::string joined = TestPathHelper::GetUniqueTestScratchPath("ttd-branch-folder-fuzz-joined") + ".ttd";
        if (ttd::TTDRecordingFolder::Open(copy)->SaveAs(joined, true, joinError))
        {
            std::vector<uint8_t> bytes = ReadBytes(joined);
            std::istringstream in(std::string(bytes.begin(), bytes.end()));
            ttd::TimeTravelEngine engine;
            ttd::TTDSessionLoadReport report;
            ttd::TTDFileSource source(joined);
            EXPECT_TRUE(ttd::TTDSessionFile::Load(engine, source, error, &report)) << error;
        }
        // The next round: a new session, a new folder
        _ttd->InvalidateSession("next round");
    }
}
