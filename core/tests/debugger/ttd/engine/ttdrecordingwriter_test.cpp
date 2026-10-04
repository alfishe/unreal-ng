/// @file ttdrecordingwriter_test.cpp
/// @brief A recording as segment files (debugger/ttd/engine/ttdrecordingwriter.h):
/// a ring records 100 frames with segments of 10 while the writer puts each
/// segment in its own file; the folder holds the whole recording, every file
/// loads alone, the default load reads the last window, the files join into
/// one .ttd that loads like the whole recording.

#include <gtest/gtest.h>

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
