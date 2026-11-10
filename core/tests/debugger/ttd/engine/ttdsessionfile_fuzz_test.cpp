/// @file ttdsessionfile_fuzz_test.cpp
/// @brief QR-4: the session file reader against damaged files. Fixtures of
/// testdata/ttd/v2/ are damaged thousands of ways - a flipped bit, a cut, a
/// size field set to 0xFFFFFFFF, garbage - and loaded. The reader never
/// crashes, and never loads a session that differs from the one written: when
/// a load succeeds, every checkpoint it brought in restores exactly as the
/// undamaged file's does (damage is found by the CRCs, or the bytes it hit
/// were not needed, as an index the reader rebuilds by scanning).
///
/// Over four thousand loads, about a second: the reader's acceptance check.
///
/// F-1 (docs/inprogress/2026-11-08-ttd-branches/test-strategy.md §4): the same on files with branches, made here
/// (a save of every branch, and a recording folder's files joined) plus the branch table's create records damaged
/// behind valid CRCs (the T-F4 kinds: a parent, an id, a fork moved). Every branch that loads restores as written;
/// a branch that does not is left out with its descendants. About 3000 loads; the long tier runs 20 times more.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"
#include "_helpers/ttdsyntheticsession.h"
#include "common/filehelper.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/engine/ttdrecordingwriter.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelengine.h"

using namespace ttd;

namespace
{
std::vector<uint8_t> Fixture(const std::string& name)
{
    std::ifstream f(TestPathHelper::FindProjectRoot() / "testdata" / "ttd" / "v2" / name, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/// Every region of every checkpoint @p loaded holds, against @p reference
bool SameAsReference(const TimeTravelEngine& loaded, const TimeTravelEngine& reference)
{
    if (loaded.CheckpointCount() > reference.CheckpointCount() || loaded.Regions().size() != reference.Regions().size())
        return false;
    std::vector<uint8_t> a, b;
    for (size_t i = 0; i < loaded.CheckpointCount(); ++i)
    {
        // Positions and CPUs of all of them, the memory of a sample (the last one included)
        const bool sample = i % 25 == 0 || i + 1 == loaded.CheckpointCount();
        if (!(loaded.Checkpoint(i)->position == reference.Checkpoint(i)->position) ||
            std::memcmp(&loaded.Checkpoint(i)->cpu, &reference.Checkpoint(i)->cpu, sizeof(TTDCpuState)) != 0)
            return false;
        for (uint32_t r = 0; sample && r < loaded.Regions().size(); ++r)
        {
            a.assign(size_t(loaded.Regions()[r].pieces) * kTTDPieceSize, 0);
            b.assign(a.size(), 0);
            const bool okA = loaded.RestoreRegion(i, r, a.data()).Ok();
            const bool okB = reference.RestoreRegion(i, r, b.data()).Ok();
            if (okA && (!okB || a != b))
                return false;   // a damaged version that restores "fine" with other bytes
        }
    }
    return true;
}

void Fuzz(const std::string& name, int rounds, uint32_t seed)
{
    SCOPED_TRACE(name);
    const std::vector<uint8_t> original = Fixture(name);
    ASSERT_GT(original.size(), 1000u);
    TimeTravelEngine reference;
    {
        TTDMemorySource source(original);
        std::string error;
        ASSERT_TRUE(TTDSessionFile::Load(reference, source, error)) << error;
    }
    std::mt19937 rng(seed);
    int loadedWhole = 0, loadedPart = 0, refused = 0;
    for (int round = 0; round < rounds; ++round)
    {
        std::vector<uint8_t> bytes = original;
        const size_t at = std::uniform_int_distribution<size_t>(0, bytes.size() - 1)(rng);
        switch (round % 4)
        {
            case 0:   // a flipped bit
                bytes[at] ^= static_cast<uint8_t>(1u << (rng() % 8));
                break;
            case 1:   // cut short
                bytes.resize(at);
                break;
            case 2:   // a size field gone huge
                for (size_t k = 0; k < 4 && at + k < bytes.size(); ++k)
                    bytes[at + k] = 0xFF;
                break;
            default:   // garbage
                for (size_t k = 0; k < 16 && at + k < bytes.size(); ++k)
                    bytes[at + k] = static_cast<uint8_t>(rng());
                break;
        }
        TimeTravelEngine engine;
        TTDMemorySource source(bytes);
        std::string error;
        TTDSessionLoadReport report;
        if (!TTDSessionFile::Load(engine, source, error, &report))
        {
            ++refused;
            continue;
        }
        (report.complete ? loadedWhole : loadedPart)++;
        ASSERT_TRUE(SameAsReference(engine, reference))
            << "round " << round << ": damage at " << at << " loaded a different session";
    }
    std::printf("[ fuzz     ] %s: %d rounds - %d loaded whole, %d loaded in part, %d refused\n", name.c_str(), rounds,
                loadedWhole, loadedPart, refused);
}

/// The branched session of F-1: 40 trunk frames, branch 1 from frame 20 (15 frames, its memory apart), branch 2
/// from branch 1 at frame 28 (8 frames), branch 3 from the trunk at frame 10 (6 frames)
struct BranchedSession
{
    ttdtest::Session s{ttdtest::Growable(10)};

    BranchedSession()
    {
        std::map<uint32_t, std::map<uint64_t, std::vector<uint8_t>>> memoryAt;   // line, frame -> after the frame
        auto run = [&](uint32_t line, int frames) {
            for (int i = 0; i < frames; ++i)
            {
                s.Frame();
                memoryAt[line][s.frame - 1] = s.memory;
            }
        };
        auto fork = [&](uint32_t from, uint64_t frame, uint8_t mark) {
            std::string error;
            uint32_t line = 0;
            EXPECT_TRUE(s.engine.ViewLine(from));
            EXPECT_TRUE(s.engine.ForkAt(static_cast<size_t>(frame), {0, frame, 1500}, line, error)) << error;
            s.memory = memoryAt[from][frame];
            s.memory[17] ^= mark;   // the branch's history apart from its parent's
            s.frame = frame + 1;
            return line;
        };
        run(0, 40);
        run(fork(0, 20, 0x55), 15);
        run(fork(1, 28, 0x33), 8);
        run(fork(0, 10, 0x0F), 6);
    }
};

/// Every branch of @p loaded against @p reference (positions and CPUs of all checkpoints, the memory of a sample);
/// empty when the same, else what differs
std::string BranchDifference(TimeTravelEngine& loaded, TimeTravelEngine& reference)
{
    for (uint32_t line = 0; line < loaded.LineIds(); ++line)
    {
        if (!loaded.HasLine(line))
            continue;
        const std::string at = "line " + std::to_string(line) + ": ";
        if (!reference.HasLine(line))
            return at + "not in the reference";
        if (loaded.LineParent(line) != reference.LineParent(line))
            return at + "parent " + std::to_string(loaded.LineParent(line)) + ", not " +
                   std::to_string(reference.LineParent(line));
        if (!loaded.ViewLine(line) || !reference.ViewLine(line))
            return at + "not viewable";
        if (loaded.FirstCheckpoint() != reference.FirstCheckpoint() ||
            loaded.CheckpointCount() > reference.CheckpointCount())
            return at + "checkpoints " + std::to_string(loaded.FirstCheckpoint()) + ".." +
                   std::to_string(loaded.CheckpointCount()) + ", not " + std::to_string(reference.FirstCheckpoint()) +
                   ".." + std::to_string(reference.CheckpointCount());
        std::vector<uint8_t> a, b;
        for (size_t i = loaded.FirstCheckpoint(); i < loaded.CheckpointCount(); ++i)
        {
            const std::string cpAt = at + "checkpoint " + std::to_string(i) + ": ";
            if (!(loaded.Checkpoint(i)->position == reference.Checkpoint(i)->position))
                return cpAt + "position";
            if (std::memcmp(&loaded.Checkpoint(i)->cpu, &reference.Checkpoint(i)->cpu, sizeof(TTDCpuState)) != 0)
                return cpAt + "CPU";
            const bool sample = i % 7 == 0 || i == loaded.FirstCheckpoint() || i + 1 == loaded.CheckpointCount();
            for (uint32_t r = 0; sample && r < loaded.Regions().size(); ++r)
            {
                a.assign(size_t(loaded.Regions()[r].pieces) * kTTDPieceSize, 0);
                b.assign(a.size(), 0);
                const bool okA = loaded.RestoreRegion(i, r, a.data()).Ok();
                const bool okB = reference.RestoreRegion(i, r, b.data()).Ok();
                if (okA && (!okB || a != b))
                    return cpAt + "region " + std::to_string(r);
            }
        }
    }
    return {};
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/// The recording folder's way: a file per line (the branch's first one holds its fork whole), joined
std::vector<uint8_t> FolderJoined(TimeTravelEngine& e)
{
    const std::string dir = TestPathHelper::GetUniqueTestScratchPath("ttd-fuzz-branch-folder");
    std::filesystem::create_directories(FileHelper::ToFsPath(dir));
    std::vector<std::string> files;
    for (uint32_t line = 0; line < e.LineIds(); ++line)
    {
        if (!e.HasLine(line) || !e.ViewLine(line))
            continue;
        const std::string path = FileHelper::PathCombine(dir, "segment-" + std::to_string(line) + ".ttd");
        TTDFileSink sink(path);
        TTDSessionWriter writer(false);
        TTDSessionSaveParams params;
        params.checkpointsPerPart = 4;
        params.fileLine = line;
        params.lineMeta[line] = {1, 2, static_cast<uint8_t>(line)};
        std::string error;
        EXPECT_TRUE(writer.Begin(e, sink, params, error)) << error;
        EXPECT_TRUE(writer.Finish(e)) << writer.Error();
        sink.Close();
        files.push_back(path);
    }
    const std::string joined = FileHelper::PathCombine(dir, "joined.ttd");
    std::string error;
    EXPECT_TRUE(JoinSessionFiles(files, joined, true, error)) << error;
    return ReadFile(FileHelper::ToFsPath(joined));
}

/// The file with one create record of the branch table changed (re-encoded: its CRC is valid)
std::vector<uint8_t> WithCreateChanged(const std::vector<uint8_t>& file, std::mt19937& rng)
{
    TTDMemorySource source(file);
    TTDContainerReader reader;
    std::string error;
    if (!reader.Open(source, error, TTDSessionFile::KnownStream))
        return file;
    std::vector<const TTDRecordRef*> creates;
    for (const TTDPartRef& part : reader.Parts())
        for (const TTDRecordRef& r : part.records)
            if (r.streamId == sessionstream::kBranchTable)
                creates.push_back(&r);
    if (creates.empty())
        return file;
    const TTDRecordRef* target = creates[rng() % creates.size()];
    TTDMemorySink sink;
    TTDContainerWriter writer;
    writer.Begin(sink, reader.Header(), &error);
    for (const TTDPartRef& part : reader.Parts())
    {
        for (const TTDRecordRef& r : part.records)
        {
            std::vector<uint8_t> bytes;
            if (&r == target && reader.ReadRecord(r, bytes) && bytes.size() > 1)
            {
                // A field of {kind, local, parent, frame, T, size, meta}: one byte set anew, or the record cut
                if (rng() % 4 == 0)
                    bytes.resize(1 + rng() % (bytes.size() - 1));
                else
                    bytes[1 + rng() % std::min<size_t>(bytes.size() - 1, 6)] = static_cast<uint8_t>(rng() % 8);
                writer.AddRecord(r.streamId, bytes);
                continue;
            }
            std::vector<uint8_t> stored;
            reader.ReadStored(r, stored);
            writer.AddStoredRecord(r.streamId, r.flags, stored.data(), stored.size(), r.rawSize);
        }
        writer.EndPart({part.firstFrame, part.frameCount, part.branch, part.dependencies, part.extra});
    }
    writer.Finalize(reader.IndexExtra());
    return sink.bytes;
}

void FuzzBranched(const char* name, const std::vector<uint8_t>& original, TimeTravelEngine& reference, int rounds,
                  uint32_t seed)
{
    SCOPED_TRACE(name);
    ASSERT_GT(original.size(), 1000u);
    {
        TimeTravelEngine whole;
        TTDMemorySource source(original);
        std::string error;
        ASSERT_TRUE(TTDSessionFile::Load(whole, source, error)) << error;
        ASSERT_EQ(whole.LineIds(), reference.LineIds()) << "the undamaged file has every branch";
        ASSERT_EQ(BranchDifference(whole, reference), "");
    }
    std::mt19937 rng(seed);
    int loaded = 0, refused = 0, branchesLeftOut = 0;
    for (int round = 0; round < rounds; ++round)
    {
        std::vector<uint8_t> bytes = original;
        const size_t at = std::uniform_int_distribution<size_t>(0, bytes.size() - 1)(rng);
        switch (round % 5)
        {
            case 0:
                bytes[at] ^= static_cast<uint8_t>(1u << (rng() % 8));
                break;
            case 1:
                bytes.resize(at);
                break;
            case 2:
                for (size_t k = 0; k < 4 && at + k < bytes.size(); ++k)
                    bytes[at + k] = 0xFF;
                break;
            case 3:
                for (size_t k = 0; k < 16 && at + k < bytes.size(); ++k)
                    bytes[at + k] = static_cast<uint8_t>(rng());
                break;
            default:
                bytes = WithCreateChanged(original, rng);
                break;
        }
        TimeTravelEngine engine;
        TTDMemorySource source(bytes);
        std::string error;
        TTDSessionLoadReport report;
        if (!TTDSessionFile::Load(engine, source, error, &report))
        {
            ++refused;
            continue;
        }
        ++loaded;
        for (uint32_t line = 0; line < reference.LineIds(); ++line)
            branchesLeftOut += reference.HasLine(line) && !engine.HasLine(line);
        // A loaded branch's parent is loaded too
        for (uint32_t line = 1; line < engine.LineIds(); ++line)
            if (engine.HasLine(line))
                ASSERT_TRUE(engine.HasLine(engine.LineParent(line))) << "round " << round << ": branch " << line;
        std::string notes;
        for (const std::string& note : report.notes)
            notes += "\n  " + note;
        ASSERT_EQ(BranchDifference(engine, reference), "")
            << "round " << round << " (mode " << round % 5 << "): damage at " << at << " loaded a different session"
            << notes;
    }
    std::printf("[ fuzz     ] %s: %d rounds - %d loaded, %d refused, %d branches left out\n", name, rounds, loaded,
                refused, branchesLeftOut);
}
}  // namespace

TEST(TTDSessionFileFuzz_Test, DamageNeverLoadsAnotherSession)
{
    Fuzz("synthetic.ttd", 3000, 1);
    Fuzz("synthetic-unfinished.ttd", 1000, 2);
    Fuzz("active-demo-converted.ttd", 120, 3);
}

TEST(TTDSessionFileFuzz_Test, DamageNeverLoadsAnotherBranch)
{
    const int scale = TestTier::Long() ? 20 : 1;
    BranchedSession saved;
    TTDMemorySink sink;
    std::string error;
    TTDSessionSaveParams params;
    params.checkpointsPerPart = 4;
    ASSERT_TRUE(TTDSessionFile::Save(saved.s.engine, sink, error, params)) << error;
    FuzzBranched("branched-save.ttd", sink.bytes, saved.s.engine, 1500 * scale, 4);

    BranchedSession folder;
    const std::vector<uint8_t> joined = FolderJoined(folder.s.engine);
    FuzzBranched("branched-folder.ttd", joined, folder.s.engine, 1500 * scale, 5);
}
