/// @file ttdsegmentwindow_test.cpp
/// @brief A recording folder's segment file read for one stretch of its checkpoints (TTDSeekIndex,
/// TTDSessionFile::ReadSegmentWindow, TimeTravelEngine::ReadBackSegmentWindow; P1.S10 seek format, 2026-11-23).
///
/// A synthetic session of four pieces (one changing a few bytes a frame) with segments of 40 frames, full tables
/// every 8 checkpoints and files in parts of 5 checkpoints: 120 frames, three segment files in memory sinks. The
/// first two segments are evicted and read back checkpoint by checkpoint through the seek index; every restore
/// gives the bytes it gave before the eviction. Runtime: milliseconds (no machine).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/engine/ttdseekindex.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelengine.h"

using namespace ttd;

namespace
{
constexpr uint32_t kPieces = 4;
constexpr uint32_t kSegmentFrames = 40;
constexpr uint32_t kTableEvery = 8;
constexpr uint32_t kPerPart = 5;

struct Recording
{
    TimeTravelEngine engine;
    std::vector<uint8_t> memory = std::vector<uint8_t>(kPieces * kTTDPieceSize);
    uint64_t frame = 0;
    std::map<size_t, std::vector<uint8_t>> wanted;   ///< per checkpoint of the active line: the region's bytes
    std::map<size_t, std::vector<uint8_t>> files;    ///< per segment (its first checkpoint): its file

    Recording()
    {
        engine.SetHistoryPolicy({TTDHistoryMode::Growable, 0, kSegmentFrames});
        engine.SetSnapshotInterval(kTableEvery);
        TTDRegionDesc ram;
        ram.name = "ram";
        ram.pieces = kPieces;
        ram.bytes = kPieces * kTTDPieceSize;
        std::string error;
        EXPECT_TRUE(engine.BeginSession({ram}, {}, error)) << error;
        for (size_t i = 0; i < memory.size(); ++i)
            memory[i] = static_cast<uint8_t>(i * 13 + i / 512);
    }

    /// One frame: piece 1 changes every frame (a long chain), piece 2 every 9th, piece 3 never, piece 0 every 3rd
    void Frame()
    {
        TTDFrameInput input;
        input.position.frame = frame;
        input.start = frame * 69888;
        auto touch = [&](uint32_t piece, size_t step) {
            uint8_t* p = memory.data() + size_t(piece) * kTTDPieceSize;
            for (size_t i = frame % 7; i < kTTDPieceSize; i += step)
                p[i] = static_cast<uint8_t>(p[i] + frame + piece + 1);
        };
        if (frame == 0)
        {
            for (uint32_t k = 0; k < kPieces; ++k)
                input.changed.push_back({0, k, memory.data() + size_t(k) * kTTDPieceSize});
        }
        else
        {
            touch(1, 401);
            input.changed.push_back({0, 1, memory.data() + kTTDPieceSize});
            if (frame % 9 == 0)
            {
                touch(2, 97);
                input.changed.push_back({0, 2, memory.data() + 2 * kTTDPieceSize});
            }
            if (frame % 3 == 0)
            {
                touch(0, 3);   // a third of the piece: a compressed difference, not ranges
                input.changed.insert(input.changed.begin(), {0, 0, memory.data()});
            }
        }
        std::string error;
        ASSERT_TRUE(engine.CaptureFrame(input, error)) << error;
        wanted[engine.CheckpointCount() - 1] = memory;
        ++frame;
    }

    /// The active line's segment @p segment as a recording folder writes it
    std::vector<uint8_t> WriteSegment(size_t segment, bool seekIndex = true)
    {
        const uint32_t line = engine.ActiveLine();
        TTDMemorySink sink;
        TTDSessionWriter writer(false);
        TTDSessionSaveParams params;
        params.checkpointsPerPart = kPerPart;
        params.fileLine = line;
        params.seekIndex = seekIndex;
        std::string error;
        EXPECT_TRUE(writer.Begin(engine, sink, params, error, {}, engine.Segments()[segment].firstCheckpoint)) << error;
        EXPECT_TRUE(writer.Finish(engine, engine.SegmentEnd(line, segment))) << writer.Error();
        return sink.bytes;
    }

    void ExpectCheckpoint(size_t index)
    {
        std::vector<uint8_t> got(memory.size());
        ASSERT_TRUE(engine.RestoreRegion(index, 0, got.data()).Ok()) << "checkpoint " << index;
        EXPECT_TRUE(got == wanted.at(index)) << "checkpoint " << index;
    }
};

/// 120 frames: three segments, the first two written and evicted
struct Evicted : Recording
{
    Evicted()
    {
        for (int i = 0; i < 120; ++i)
            Frame();
        EXPECT_EQ(engine.Segments().size(), 3u);
        std::string error;
        for (size_t s = 0; s < 2; ++s)
        {
            files[s] = WriteSegment(s);
            EXPECT_TRUE(engine.EvictSegment(0, s, error)) << error;
        }
    }
};
}  // namespace

/// The seek index round trip, and the two checks that make a moved or foreign one unusable
TEST(TTDSegmentWindow_Test, TheSeekIndexNamesItsFile)
{
    TTDSeekIndex index;
    index.line = 3;
    index.parts.resize(2);
    index.parts[0] = {100, 400, 700, 250, 120, 40, 0, 9, 1000, {1000, 1004}};
    index.parts[1] = {900, 1300, 0, 300, 90, 0, 9, 5, 1005, {}};
    const std::vector<uint8_t> bytes = index.Encode(2000);

    TTDSeekIndex back;
    ASSERT_TRUE(TTDSeekIndex::Decode(bytes.data(), bytes.size(), 2000, back));
    EXPECT_EQ(back.line, 3u);
    ASSERT_EQ(back.parts.size(), 2u);
    EXPECT_EQ(back.parts[0].tablesOffset, 700u);
    EXPECT_EQ(back.parts[0].tableFrames, (std::vector<uint64_t>{1000, 1004}));
    EXPECT_EQ(back.parts[1].firstItem, 9u);
    EXPECT_EQ(back.parts[1].items, 5u);
    EXPECT_EQ(back.parts[1].checkpointsStored, 90u);

    EXPECT_FALSE(TTDSeekIndex::Decode(bytes.data(), bytes.size(), 2032, back)) << "the index of a file laid out otherwise";
    EXPECT_FALSE(TTDSeekIndex::Decode(bytes.data(), bytes.size() - 1, 2000, back)) << "cut short";
    EXPECT_FALSE(TTDSeekIndex::Decode(bytes.data(), 0, 2000, back)) << "a file without one";
    const std::vector<uint8_t> other{42, 43};
    EXPECT_FALSE(TTDSeekIndex::Decode(other.data(), other.size(), 2000, back)) << "another layer's index extra";
}

/// Every checkpoint of an evicted segment, read back alone through the seek index: the stretch between two full
/// tables comes in (the checkpoint and the one after it among them), a restore gives the bytes recorded, and the
/// read imports a part of the file's versions, not all of them
TEST(TTDSegmentWindow_Test, AStretchReadBackRestoresWhatWasRecorded)
{
    Evicted r;
    const size_t liveBefore = r.engine.PieceStore().LiveVersions();
    size_t most = 0, least = SIZE_MAX, whole = 0;
    for (size_t segment = 0; segment < 2; ++segment)
    {
        const TTDSegmentInfo& info = r.engine.Segments()[segment];
        const size_t end = r.engine.SegmentEnd(0, segment);
        const TTDMemorySource source(r.files[segment]);
        {
            // What the whole segment brings in
            std::string error;
            ASSERT_TRUE(r.engine.ReadBackSegmentFrom(0, segment, source, error)) << error;
            whole = r.engine.PieceStore().LiveVersions() - liveBefore;
            ASSERT_TRUE(r.engine.EvictSegment(0, segment, error)) << error;
        }
        for (size_t index = info.firstCheckpoint; index < end; ++index)
        {
            SCOPED_TRACE("checkpoint " + std::to_string(index));
            std::string error;
            ASSERT_TRUE(r.engine.ReadBackSegmentWindow(0, segment, source, error, index)) << error;
            EXPECT_TRUE(info.Holds(index));
            EXPECT_TRUE(index + 1 == end || info.Holds(index + 1)) << "the next frame's restore needs no second read";
            EXPECT_FALSE(info.Whole()) << "a stretch, not the segment";
            ASSERT_EQ(info.residentParts.size(), 1u);
            const size_t begin = info.residentParts[0].first;
            EXPECT_TRUE(begin == info.firstCheckpoint || begin % kTableEvery == 0) << "it starts at full tables: " << begin;
            EXPECT_LE(index - begin, kTableEvery - 1) << "the last ones before the checkpoint";
            EXPECT_LE(info.residentParts[0].second - begin, kTableEvery + 1) << "up to the next full tables";
            r.ExpectCheckpoint(index);
            if (index + 1 < end)
                r.ExpectCheckpoint(index + 1);
            most = std::max<size_t>(most, r.engine.LastWindowWork().versionsImported);
            least = std::min<size_t>(least, r.engine.LastWindowWork().versionsImported);
            ASSERT_TRUE(r.engine.EvictSegment(0, segment, error)) << error;
            ASSERT_EQ(r.engine.PieceStore().LiveVersions(), liveBefore) << "the stretch left nothing behind";
        }
    }
    // A segment holds its baseline and 39 frames of changes, about 60 versions; a stretch of 9 checkpoints needs its
    // table's four, its own changes and the versions those build on
    EXPECT_GT(whole, 50u);
    EXPECT_LT(most, whole) << "a stretch imports the versions it needs";
    EXPECT_LT(least * 2, whole);
    EXPECT_GE(least, kPieces);
}

/// Stretches add up: a second restore elsewhere in the segment brings its own stretch in and leaves the first one's
/// versions as they are (a query may hold them), neighbours join, and once every stretch is in memory the segment is
/// whole. The whole segment read in one go (after an eviction) restores the same bytes
TEST(TTDSegmentWindow_Test, StretchesAddUpToTheWholeSegment)
{
    Evicted r;
    const TTDMemorySource source(r.files[1]);
    std::string error;
    ASSERT_TRUE(r.engine.ReadBackSegmentWindow(0, 1, source, error, 67)) << error;
    const TTDSegmentInfo& info = r.engine.Segments()[1];
    using Parts = std::vector<std::pair<size_t, size_t>>;
    EXPECT_EQ(info.residentParts, (Parts{{64, 73}})) << "up to the next checkpoint with full tables, and that one";
    const uint32_t held = r.engine.VersionAt(67, 0, 1);

    ASSERT_TRUE(r.engine.ReadBackSegmentWindow(0, 1, source, error, 43)) << error;
    EXPECT_EQ(info.residentParts, (Parts{{40, 49}, {64, 73}}));
    EXPECT_EQ(r.engine.VersionAt(67, 0, 1), held) << "the first stretch keeps its versions";
    EXPECT_FALSE(info.Holds(50));
    for (const size_t i : {size_t(40), size_t(43), size_t(48), size_t(64), size_t(67), size_t(72)})
        r.ExpectCheckpoint(i);
    EXPECT_FALSE(r.engine.ReadBackSegmentFrom(0, 1, source, error)) << "the whole segment replaces nothing: the holder evicts";
    EXPECT_FALSE(r.engine.ReadBackSegmentFrom(0, 1, source, error, 50)) << "nor does a prefix";
    EXPECT_EQ(info.residentParts, (Parts{{40, 49}, {64, 73}}));
    EXPECT_EQ(r.engine.VersionAt(67, 0, 1), held);

    // The stretch between them shares a checkpoint with each neighbour: one stretch from 40 to 73
    ASSERT_TRUE(r.engine.ReadBackSegmentWindow(0, 1, source, error, 50)) << error;
    ASSERT_TRUE(r.engine.ReadBackSegmentWindow(0, 1, source, error, 60)) << error;
    EXPECT_EQ(info.residentParts, (Parts{{40, 73}}));
    EXPECT_EQ(r.engine.VersionAt(67, 0, 1), held);
    ASSERT_TRUE(r.engine.ReadBackSegmentWindow(0, 1, source, error, 79)) << error;
    EXPECT_TRUE(info.Whole()) << "every stretch is in memory";
    for (size_t i = 40; i < 80; ++i)
        r.ExpectCheckpoint(i);

    const size_t live = r.engine.PieceStore().LiveVersions();
    ASSERT_TRUE(r.engine.EvictSegment(0, 1, error)) << error;
    EXPECT_LT(r.engine.PieceStore().LiveVersions(), live);
    ASSERT_TRUE(r.engine.ReadBackSegmentFrom(0, 1, source, error)) << error;
    EXPECT_TRUE(info.Whole());
    for (size_t i = 40; i < 80; ++i)
        r.ExpectCheckpoint(i);
}

/// A file without the seek index (written before it, unfinished, joined), and one whose records moved under its
/// index: the stretch read refuses, nothing is held, and the read from the file's start still works
TEST(TTDSegmentWindow_Test, AFileWithoutASeekIndexIsReadFromItsStart)
{
    Recording r;
    for (int i = 0; i < 90; ++i)
        r.Frame();
    const std::vector<uint8_t> plain = r.WriteSegment(0, false);
    const std::vector<uint8_t> indexed = r.WriteSegment(0);
    EXPECT_GT(indexed.size(), plain.size()) << "the tables and the index";
    EXPECT_LT(indexed.size() - plain.size(), plain.size() / 2) << "a few bytes a piece and table";
    std::string error;
    ASSERT_TRUE(r.engine.EvictSegment(0, 0, error)) << error;
    const size_t live = r.engine.PieceStore().LiveVersions();

    {
        const TTDMemorySource source(plain);
        EXPECT_FALSE(r.engine.ReadBackSegmentWindow(0, 0, source, error, 20));
        EXPECT_NE(error.find("no seek index"), std::string::npos) << error;
        EXPECT_FALSE(r.engine.Segments()[0].resident);
        EXPECT_EQ(r.engine.PieceStore().LiveVersions(), live);
        ASSERT_TRUE(r.engine.ReadBackSegmentFrom(0, 0, source, error, 20)) << error;
        r.ExpectCheckpoint(20);
        ASSERT_TRUE(r.engine.EvictSegment(0, 0, error)) << error;
    }

    // The same records with one more in front of them, the index extra carried over (a tool's copy)
    TTDMemorySource source(indexed);
    TTDContainerReader reader;
    ASSERT_TRUE(reader.Open(source, error, TTDSessionFile::KnownStream)) << error;
    ASSERT_FALSE(reader.IndexExtra().empty());
    TTDMemorySink moved;
    TTDContainerWriter writer;
    ASSERT_TRUE(writer.Begin(moved, reader.Header(), &error)) << error;
    for (const TTDPartRef& part : reader.Parts())
    {
        if (part.index == 0)
            ASSERT_TRUE(writer.AddRecord(sessionstream::kWriteJournal, std::vector<uint8_t>{0, 0}));
        std::vector<uint8_t> stored;
        for (const TTDRecordRef& record : part.physical)
        {
            ASSERT_TRUE(reader.ReadStored(record, stored, &error)) << error;
            ASSERT_TRUE(writer.AddStoredRecord(record.streamId, record.flags, stored.data(), stored.size(), record.rawSize));
        }
        TTDPartEnd partEnd;
        partEnd.firstFrame = part.firstFrame;
        partEnd.frameCount = part.frameCount;
        partEnd.branch = part.branch;
        partEnd.dependencies = part.dependencies;
        partEnd.extra = part.extra;
        ASSERT_TRUE(writer.EndPart(partEnd));
    }
    ASSERT_TRUE(writer.Finalize(reader.IndexExtra()));
    const TTDMemorySource movedSource(moved.bytes);
    EXPECT_FALSE(r.engine.ReadBackSegmentWindow(0, 0, movedSource, error, 20));
    EXPECT_NE(error.find("no seek index"), std::string::npos) << error;
    EXPECT_EQ(r.engine.PieceStore().LiveVersions(), live);
    ASSERT_TRUE(r.engine.ReadBackSegmentFrom(0, 0, movedSource, error, 20)) << error;
    r.ExpectCheckpoint(20);
}

/// Damage in a record a stretch needs is reported with the record, and nothing of the stretch stays; a stretch that
/// does not need the damaged part still comes in
TEST(TTDSegmentWindow_Test, DamageInAPartTheStretchNeeds)
{
    Evicted r;
    std::vector<uint8_t> file = r.files[1];
    uint64_t pieces = 0;
    {
        // The piece record of the file's last part (checkpoints 75-79)
        TTDMemorySource source(file);
        TTDContainerReader reader;
        std::string error;
        ASSERT_TRUE(reader.Open(source, error, TTDSessionFile::KnownStream)) << error;
        for (const TTDRecordRef& record : reader.Parts().back().physical)
            if (record.streamId == sessionstream::kPieces)
                pieces = record.offset;
        ASSERT_NE(pieces, 0u);
    }
    file[static_cast<size_t>(pieces) + kRecordHeaderSize + 3] ^= 0x40;
    const TTDMemorySource source(file);
    const size_t live = r.engine.PieceStore().LiveVersions();
    std::string error;
    EXPECT_FALSE(r.engine.ReadBackSegmentWindow(0, 1, source, error, 77));
    EXPECT_NE(error.find("CRC"), std::string::npos) << error;
    EXPECT_FALSE(r.engine.Segments()[1].resident);
    EXPECT_EQ(r.engine.PieceStore().LiveVersions(), live);

    ASSERT_TRUE(r.engine.ReadBackSegmentWindow(0, 1, source, error, 45)) << error;
    r.ExpectCheckpoint(45);
}

/// A branch's file (its records in the branch streams, its first checkpoint the fork's copy whole): a stretch of it
/// reads back like the trunk's
TEST(TTDSegmentWindow_Test, AStretchOfABranchFile)
{
    Recording r;
    for (int i = 0; i < 30; ++i)
        r.Frame();
    uint32_t line = 0;
    std::string error;
    ASSERT_TRUE(r.engine.ForkAt(29, {0, 29, 0}, line, error)) << error;
    const std::vector<uint8_t> atFork = r.wanted.at(29);
    r.wanted.clear();
    r.wanted[29] = atFork;
    for (int i = 0; i < 100; ++i)
        r.Frame();
    ASSERT_GE(r.engine.Segments().size(), 3u);
    const std::vector<uint8_t> file = r.WriteSegment(0);
    ASSERT_TRUE(r.engine.EvictSegment(line, 0, error)) << error;

    const TTDMemorySource source(file);
    const size_t first = r.engine.Segments()[0].firstCheckpoint;
    const size_t end = r.engine.SegmentEnd(line, 0);
    ASSERT_GT(end - first, 2 * kTableEvery);
    for (const size_t index : {first, first + 1, first + kTableEvery + 3, end - 1})
    {
        SCOPED_TRACE("checkpoint " + std::to_string(index));
        ASSERT_TRUE(r.engine.ReadBackSegmentWindow(line, 0, source, error, index)) << error;
        r.ExpectCheckpoint(index);
        ASSERT_TRUE(r.engine.EvictSegment(line, 0, error)) << error;
    }
}

/// A baseline keeps the version of a piece that did not change and is stored whole: the segments share it (one
/// compressed copy, the same id for a seek that decoded it in another segment), a changing piece gets its whole
/// version anew, and the shared one outlives the segment that stored it first
TEST(TTDSegmentWindow_Test, ABaselineKeepsAnUnchangedWholeVersion)
{
    Recording r;
    for (int i = 0; i < 90; ++i)
        r.Frame();
    const TimeTravelEngine& e = r.engine;
    ASSERT_TRUE(e.Checkpoint(40)->baseline);
    ASSERT_TRUE(e.Checkpoint(80)->baseline);
    EXPECT_EQ(e.VersionAt(40, 0, 3), e.VersionAt(39, 0, 3)) << "piece 3 never changes";
    EXPECT_EQ(e.VersionAt(80, 0, 3), e.VersionAt(0, 0, 3));
    EXPECT_EQ(e.ChangeCount(40, 0), kPieces) << "a baseline lists every piece";
    for (const uint32_t piece : {0u, 1u, 2u})
    {
        EXPECT_NE(e.VersionAt(40, 0, piece), e.VersionAt(39, 0, piece)) << "piece " << piece;
        EXPECT_FALSE(TTDPieceStore::IsDifference(e.PieceStore().EncodingOf(e.VersionAt(40, 0, piece)))) << "piece " << piece;
    }
    for (const size_t index : {size_t(39), size_t(40), size_t(41), size_t(80), size_t(89)})
        r.ExpectCheckpoint(index);

    ASSERT_TRUE(r.engine.DropOldestHeldSegment());
    r.wanted.erase(r.wanted.begin(), r.wanted.find(40));
    for (const size_t index : {size_t(40), size_t(79), size_t(80)})
        r.ExpectCheckpoint(index);

    // The other side: every piece anew at a baseline
    TimeTravelEngine::SetBaselineReuse(false);
    Recording anew;
    for (int i = 0; i < 45; ++i)
        anew.Frame();
    TimeTravelEngine::SetBaselineReuse(true);
    EXPECT_NE(anew.engine.VersionAt(40, 0, 3), anew.engine.VersionAt(39, 0, 3));
    anew.ExpectCheckpoint(40);
}

namespace
{
std::filesystem::path SeekFixtureDir()
{
    return FileHelper::ToFsPath(TestPathHelper::GetTestDataPath("ttd/v2/seek"));
}

std::vector<uint8_t> ReadFixture(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// A branch forked at checkpoint 29 of 30 trunk frames and 100 frames on it: its first segment's file, the segment
/// evicted (AStretchOfABranchFile, the stored fixture)
struct EvictedBranch : Recording
{
    uint32_t line = 0;
    std::vector<uint8_t> file;

    EvictedBranch()
    {
        for (int i = 0; i < 30; ++i)
            Frame();
        std::string error;
        EXPECT_TRUE(engine.ForkAt(29, {0, 29, 0}, line, error)) << error;
        const std::vector<uint8_t> atFork = wanted.at(29);
        wanted.clear();
        wanted[29] = atFork;
        for (int i = 0; i < 100; ++i)
            Frame();
        EXPECT_GE(engine.Segments().size(), 3u);
        file = WriteSegment(0);
        EXPECT_TRUE(engine.EvictSegment(line, 0, error)) << error;
    }
};
}  // namespace

/// The committed segment files (testdata/ttd/v2/seek/: a trunk segment and a branch's first, with their piece tables
/// and seek index) read back in stretches into the recordings they were written from: the format on disk stays
/// readable, and tools/verification/ttd-analyzer/scripts/check_ksy.py checks ttd.ksy's seek_index and
/// payload_piece_tables against them. UNREAL_TTD_WRITE_SEEK_FIXTURES=1 writes them first (by hand after a format
/// change, then commit: *.ttd is git-ignored, add it with -f)
TEST(TTDSegmentWindow_Test, TheCommittedSegmentFilesReadBackInStretches)
{
    Evicted trunk;
    EvictedBranch branch;
    if (std::getenv("UNREAL_TTD_WRITE_SEEK_FIXTURES"))
    {
        std::filesystem::create_directories(SeekFixtureDir());
        for (const auto& [name, bytes] : {std::pair{"segment-trunk.ttd", &trunk.files[1]}, std::pair{"segment-branch.ttd", &branch.file}})
            std::ofstream(SeekFixtureDir() / name, std::ios::binary)
                .write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    }
    const std::vector<uint8_t> trunkFile = ReadFixture(SeekFixtureDir() / "segment-trunk.ttd");
    const std::vector<uint8_t> branchFile = ReadFixture(SeekFixtureDir() / "segment-branch.ttd");
    ASSERT_FALSE(trunkFile.empty()) << "testdata/ttd/v2/seek/segment-trunk.ttd";
    ASSERT_FALSE(branchFile.empty()) << "testdata/ttd/v2/seek/segment-branch.ttd";

    std::string error;
    {
        const TTDMemorySource source(trunkFile);
        for (const size_t index : {size_t(40), size_t(47), size_t(55), size_t(64), size_t(79)})
        {
            ASSERT_TRUE(trunk.engine.ReadBackSegmentWindow(0, 1, source, error, index)) << "checkpoint " << index << ": " << error;
            trunk.ExpectCheckpoint(index);
        }
        EXPECT_FALSE(trunk.engine.Segments()[1].Whole()) << "stretches, not the file";
    }
    {
        const TTDMemorySource source(branchFile);
        const size_t first = branch.engine.Segments()[0].firstCheckpoint;
        const size_t end = branch.engine.SegmentEnd(branch.line, 0);
        for (const size_t index : {first, first + 12, end - 1})
        {
            ASSERT_TRUE(branch.engine.ReadBackSegmentWindow(branch.line, 0, source, error, index))
                << "checkpoint " << index << ": " << error;
            branch.ExpectCheckpoint(index);
        }
    }
}
