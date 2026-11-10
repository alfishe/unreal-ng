/// @file ttdrecordingwriter_test.cpp
/// @brief A recording as segment files (debugger/ttd/engine/ttdrecordingwriter.h):
/// a ring records 100 frames with segments of 10 while the writer puts each
/// segment in its own file; the folder holds the whole recording, every file
/// loads alone, the default load reads the last window, the files join into
/// one .ttd that loads like the whole recording.

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/ttdsyntheticsession.h"
#include "common/filehelper.h"
#include "debugger/ttd/engine/ttdrecordingwriter.h"
#include "debugger/ttd/ttdrecordingfolders.h"

using namespace ttd;
using namespace ttdtest;

namespace
{
struct Recorded
{
    Session ring{Ring(25, 10)};
    Session all{Growable(10)};
    std::unique_ptr<TTDRecordingFolder> folder;
    std::vector<std::string> files;

    Recorded()
    {
        const std::string root = TestPathHelper::GetUniqueTestScratchPath("ttd-recordings-writer");
        FileHelper::DeleteFolder(root);
        std::string error;
        folder = TTDRecordingFolder::Create(root, "synthetic", 1'759'581'012, error);
        EXPECT_NE(folder, nullptr) << error;
        TTDSessionSaveParams params;
        params.checkpointsPerPart = 4;
        TTDRecordingWriter writer([this](uint32_t n) { return folder->SegmentPath(n); }, params);
        ring.Frame();
        all.Frame();
        EXPECT_TRUE(writer.Begin(ring.engine, error)) << error;
        for (int i = 1; i < 100; ++i)
        {
            ring.Frame();
            all.Frame();
            EXPECT_TRUE(writer.Collect(ring.engine)) << writer.Error();
        }
        EXPECT_TRUE(writer.Finish(ring.engine)) << writer.Error();
        files = folder->Segments();
    }
};

void ExpectRange(const TimeTravelEngine& loaded, const TimeTravelEngine& reference, size_t offset)
{
    for (size_t j = 0; j < loaded.CheckpointCount(); ++j)
    {
        ExpectSameCheckpoint(loaded, j, reference, offset + j);
        TTDPortRecord a, b;
        ASSERT_TRUE(loaded.BusReads().Get(loaded.Checkpoint(j)->busReadCursor, a));
        ASSERT_TRUE(reference.BusReads().Get(reference.Checkpoint(offset + j)->busReadCursor, b));
        ASSERT_TRUE(a.SameAccess(b) && a.value == b.value) << "bus position at checkpoint " << j;
    }
}
}  // namespace

TEST(TTDRecordingWriter_Test, OneFilePerSegmentHoldingTheWholeRecording)
{
    Recorded r;
    ASSERT_EQ(r.files.size(), 10u) << "100 frames in segments of 10";
    EXPECT_GT(r.ring.engine.FirstCheckpoint(), 0u) << "memory dropped segments, the folder kept them";

    // Every file alone: ten frames from a baseline
    for (size_t f = 0; f < r.files.size(); ++f)
    {
        SCOPED_TRACE(r.files[f]);
        TimeTravelEngine one;
        TTDFileSource source(r.files[f]);
        std::string error;
        ASSERT_TRUE(TTDSessionFile::Load(one, source, error)) << error;
        ASSERT_EQ(one.CheckpointCount(), 10u);
        EXPECT_TRUE(one.Checkpoint(0)->baseline);
        ExpectRange(one, r.all.engine, f * 10);
    }

    // All of them, one after another
    TimeTravelEngine whole;
    std::string error;
    TTDSessionLoadReport report;
    ASSERT_TRUE(LoadRecording(whole, r.files, 0, error, &report)) << error;
    EXPECT_TRUE(report.complete) << report.stoppedAt;
    ASSERT_EQ(whole.CheckpointCount(), 100u);
    ExpectRange(whole, r.all.engine, 0);
    EXPECT_EQ(whole.Events().Count(), r.all.engine.Events().Count());
}

/// The default load: whole files from the end until they cover the window
TEST(TTDRecordingWriter_Test, LoadingTheLastWindow)
{
    Recorded r;
    TimeTravelEngine window;
    std::string error;
    ASSERT_TRUE(LoadRecording(window, r.files, 25, error)) << error;
    ASSERT_EQ(window.CheckpointCount(), 30u) << "three files of ten cover 25 frames";
    ExpectRange(window, r.all.engine, 70);
}

/// Save as: the files joined into one .ttd, records copied as stored
TEST(TTDRecordingWriter_Test, TheFilesJoinIntoOne)
{
    Recorded r;
    const std::string target = FileHelper::PathCombine(r.folder->Path(), "../joined-\xc3\xa9.ttd");
    std::string error;
    ASSERT_TRUE(r.folder->SaveAs(target, true, error)) << error;
    TimeTravelEngine joined;
    TTDFileSource source(target);
    ASSERT_TRUE(source.Valid()) << source.Error();
    TTDSessionLoadReport report;
    ASSERT_TRUE(TTDSessionFile::Load(joined, source, error, &report)) << error;
    EXPECT_TRUE(report.complete) << report.stoppedAt;
    ASSERT_EQ(joined.CheckpointCount(), 100u);
    ExpectRange(joined, r.all.engine, 0);

    uint64_t sum = 0;
    for (const std::string& f : r.files)
        sum += FileHelper::GetFileSize(f);
    EXPECT_LT(FileHelper::GetFileSize(target), sum) << "one header and index instead of ten";
    EXPECT_FALSE(JoinSessionFiles(r.files, target, false, error)) << "an existing file stays unless asked";
}

namespace
{
std::vector<uint8_t> ReadBytes(const std::string& path)
{
    std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}  // namespace

/// A recording resumed from the past (finding F-1 of the branches design,
/// docs/inprogress/2026-11-08-ttd-branches/README.md §4): 60 frames in files
/// of 10, cut inside frame 33, 30 new frames. The files before the cut's
/// segment stay as they were written; the cut's segment is written again from
/// its baseline; the folder loads as a session that recorded only the kept
/// past and the new frames
TEST(TTDRecordingWriter_Test, ResumingFromThePastKeepsTheFilesBeforeTheCut)
{
    const std::string root = TestPathHelper::GetUniqueTestScratchPath("ttd-recordings-rewind");
    FileHelper::DeleteFolder(root);
    std::string error;
    std::unique_ptr<TTDRecordingFolder> folder = TTDRecordingFolder::Create(root, "synthetic", 1'759'581'012, error);
    ASSERT_NE(folder, nullptr) << error;
    TTDSessionSaveParams params;
    params.checkpointsPerPart = 4;
    TTDRecordingWriter writer([&folder](uint32_t n) { return folder->SegmentPath(n); }, params);

    Session cut(Growable(10));
    Session ref(Growable(10));
    cut.Frame();
    ASSERT_TRUE(writer.Begin(cut.engine, error)) << error;
    for (int i = 1; i < 60; ++i)
        ASSERT_TRUE((cut.Frame(), writer.Collect(cut.engine))) << writer.Error();
    for (int i = 0; i < 34; ++i)
        ref.Frame();
    ASSERT_EQ(folder->Segments().size(), 6u);
    std::vector<std::vector<uint8_t>> before;
    for (uint32_t n = 0; n < 3; ++n)
        before.push_back(ReadBytes(folder->SegmentPath(n)));

    writer.StopWriting();
    ASSERT_TRUE(cut.engine.TruncateAfter(33, {0, 33, 1500}, error)) << error;
    ASSERT_TRUE(writer.RewindToLastSegment(cut.engine, error)) << error;
    EXPECT_EQ(folder->Segments().size(), 4u) << "files 0-2 kept, the cut's segment (3) open again, 4-5 gone";

    cut.memory = ref.memory;
    cut.frame = ref.frame;
    for (int i = 0; i < 30; ++i)
    {
        cut.Frame();
        ref.Frame();
        ASSERT_TRUE(writer.Collect(cut.engine)) << writer.Error();
    }
    ASSERT_TRUE(writer.Finish(cut.engine)) << writer.Error();

    for (uint32_t n = 0; n < 3; ++n)
        EXPECT_EQ(ReadBytes(folder->SegmentPath(n)), before[n]) << "file " << n << " is not written again";
    const std::vector<std::string> files = folder->Segments();
    ASSERT_EQ(files.size(), 7u) << "frames 0-63 in files of 10";
    TimeTravelEngine whole;
    TTDSessionLoadReport report;
    ASSERT_TRUE(LoadRecording(whole, files, 0, error, &report)) << error;
    EXPECT_TRUE(report.complete) << report.stoppedAt;
    ASSERT_EQ(whole.CheckpointCount(), ref.engine.CheckpointCount());
    ExpectRange(whole, ref.engine, 0);
    EXPECT_EQ(whole.Events().Count(), ref.engine.Events().Count());
}

/// The writer behind the engine when the cut comes (captures it has not
/// collected yet, a later segment it never opened): its open file has no
/// index, so it is written again from its own baseline, then the cut's segment
TEST(TTDRecordingWriter_Test, ResumingFromThePastWithTheWriterBehind)
{
    const std::string root = TestPathHelper::GetUniqueTestScratchPath("ttd-recordings-rewind-behind");
    FileHelper::DeleteFolder(root);
    std::string error;
    std::unique_ptr<TTDRecordingFolder> folder = TTDRecordingFolder::Create(root, "synthetic", 1'759'581'012, error);
    ASSERT_NE(folder, nullptr) << error;
    TTDSessionSaveParams params;
    params.checkpointsPerPart = 4;
    TTDRecordingWriter writer([&folder](uint32_t n) { return folder->SegmentPath(n); }, params);

    Session cut(Growable(10));
    Session ref(Growable(10));
    cut.Frame();
    ASSERT_TRUE(writer.Begin(cut.engine, error)) << error;
    for (int i = 1; i < 15; ++i)
        ASSERT_TRUE((cut.Frame(), writer.Collect(cut.engine))) << writer.Error();
    for (int i = 15; i < 30; ++i)
        cut.Frame();   // the writer's open file is segment 1; segment 2 (20-29) only in memory
    for (int i = 0; i < 26; ++i)
        ref.Frame();
    const std::vector<uint8_t> first = ReadBytes(folder->SegmentPath(0));

    writer.StopWriting();
    ASSERT_TRUE(cut.engine.TruncateAfter(25, {0, 25, 1500}, error)) << error;   // frame 25's bus read (T 1000) kept
    ASSERT_TRUE(writer.RewindToLastSegment(cut.engine, error)) << error;
    cut.memory = ref.memory;
    cut.frame = ref.frame;
    for (int i = 0; i < 10; ++i)
    {
        cut.Frame();
        ref.Frame();
        ASSERT_TRUE(writer.Collect(cut.engine)) << writer.Error();
    }
    ASSERT_TRUE(writer.Finish(cut.engine)) << writer.Error();

    EXPECT_EQ(ReadBytes(folder->SegmentPath(0)), first) << "file 0 is not written again";
    const std::vector<std::string> files = folder->Segments();
    ASSERT_EQ(files.size(), 4u) << "frames 0-35 in files of 10";
    TimeTravelEngine whole;
    TTDSessionLoadReport report;
    ASSERT_TRUE(LoadRecording(whole, files, 0, error, &report)) << error;
    EXPECT_TRUE(report.complete) << report.stoppedAt;
    ASSERT_EQ(whole.CheckpointCount(), ref.engine.CheckpointCount());
    ExpectRange(whole, ref.engine, 0);
}
