/// @file ttdsessionfile_chunks_test.cpp
/// @brief A session whose whole-session streams pass the record limit (F3, docs/inprogress/2026-11-12-re-api-
/// efficiency/analysis.md §F3). SimCity's 139 382-frame dump failed because the write journal, written whole with
/// the session's last part, passed 1 GiB raw (96 503 frames gave 797 MB). Here the container's record limit is set
/// small (TTDSessionSaveParams::recordLimit), so a session of 30 frames has a write journal and a holder stream of
/// several records each: the load gives back the same write records and the same holder bytes; the reason of a
/// failed save is the sink's (a full disk only when it says so); a reader without chunk support reads such a file
/// as one without those streams; the committed chunked fixture (testdata/ttd/v2/chunked/, also read by the Python
/// tools' tests) still loads. Each case takes a few milliseconds.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/ttdsyntheticsession.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdcompression.h"

namespace
{
namespace fs = std::filesystem;
using namespace ttd;

constexpr uint32_t kLimit = 4096;   ///< the test's record limit: above every per-part record of a required stream here
constexpr uint16_t kHolder = sessionstream::kHolderFirstFree;

/// 30 frames, 6000 write records over them, a holder stream of 10 000 bytes
struct Chunky
{
    ttdtest::Session s{ttdtest::Growable(0)};
    std::vector<uint8_t> holder;

    Chunky()
    {
        for (int i = 0; i < 30; ++i)
            s.Frame();
        uint32_t x = 12345;
        for (uint32_t k = 0; k < 6000; ++k)
        {
            x = x * 1103515245u + 12345u;
            TTDWriteRecord r{};
            r.globalT = uint64_t(k) * 300 + (x >> 28);
            r.addr = 0x4000 + ((x >> 8) & 0x3FFF);
            r.m1pc = static_cast<uint16_t>(0x8000 + (x >> 20));
            r.value = static_cast<uint8_t>(x >> 13);
            r.physPage = static_cast<uint8_t>(5 + (k & 1));
            s.engine.Writes().Append(r);
        }
        holder.resize(10000);
        for (size_t i = 0; i < holder.size(); ++i)
        {
            x = x * 1103515245u + 12345u;
            holder[i] = static_cast<uint8_t>(x >> 16);
        }
    }

    TTDSessionSaveParams Params(uint32_t limit) const
    {
        TTDSessionSaveParams p;
        p.checkpointsPerPart = 8;
        p.createdMicros = 1'759'500'000'000'000ull;
        p.uuid[0] = 0xC4;
        p.recordLimit = limit;
        p.holderStreams = {{kHolder, "holder-test", holder}};
        return p;
    }
};

std::vector<TTDWriteRecord> WritesOf(const TimeTravelEngine& e)
{
    std::vector<TTDWriteRecord> out;
    e.Writes().ForEach([&out](const TTDWriteRecord& r) { out.push_back(r); });
    return out;
}

bool SameWrites(const std::vector<TTDWriteRecord>& a, const std::vector<TTDWriteRecord>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0);
}

/// The stream's records as the last part stores them (physical), and the joined payload
struct StreamInFile
{
    uint32_t chunks = 0;
    uint32_t twinRecords = 0;
    std::vector<uint8_t> payload;
};

StreamInFile Stream(const std::vector<uint8_t>& file, uint16_t id)
{
    StreamInFile out;
    TTDMemorySource source(file);
    TTDContainerReader reader;
    std::string error;
    EXPECT_TRUE(reader.Open(source, error, TTDSessionFile::KnownStream)) << error;
    for (const TTDPartRef& part : reader.Parts())
    {
        for (const TTDRecordRef& r : part.physical)
            out.twinRecords += r.streamId == ChunkedTwin(id);
        for (const TTDRecordRef& r : part.records)
            if (r.streamId == id)
            {
                out.chunks = r.chunks;
                EXPECT_TRUE(reader.ReadRecord(r, out.payload, &error)) << error;
            }
    }
    return out;
}

fs::path FixtureDir()
{
    return TestPathHelper::FindProjectRoot() / "testdata" / "ttd" / "v2" / "chunked";
}

std::vector<uint8_t> ReadFile(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

/// Fails every write once @p limit bytes went through, with the reason a real file gives
class RefusingSink : public TTDMemorySink
{
public:
    RefusingSink(size_t limit, std::string reason, bool noSpace) : _limit(limit), _reason(std::move(reason)), _noSpace(noSpace) {}
    bool Write(const uint8_t* data, size_t size) override
    {
        if (bytes.size() + size > _limit)
            return false;
        return TTDMemorySink::Write(data, size);
    }
    std::string LastError() const override { return _reason; }
    bool LastErrorNoSpace() const override { return _noSpace; }

private:
    size_t _limit;
    std::string _reason;
    bool _noSpace;
};
}  // namespace

/// The write journal and a holder stream over the limit: several records each, read back bit for bit
TEST(TTDSessionFileChunks_Test, WholeSessionStreamsOverTheLimitRoundTrip)
{
    Chunky c;
    TTDMemorySink sink;
    std::string error;
    TTDWriteFailure failure = TTDWriteFailure::Layout;
    ASSERT_TRUE(TTDSessionFile::Save(c.s.engine, sink, error, c.Params(kLimit), &failure)) << error;
    EXPECT_EQ(failure, TTDWriteFailure::None);

    const StreamInFile journal = Stream(sink.bytes, sessionstream::kWriteJournal);
    const StreamInFile holder = Stream(sink.bytes, kHolder);
    EXPECT_GT(journal.chunks, 2u) << "the write journal passes the limit: chunked";
    EXPECT_EQ(journal.twinRecords, journal.chunks);
    EXPECT_EQ(holder.chunks, (10000 + kLimit - 1) / kLimit);
    EXPECT_TRUE(holder.payload == c.holder);

    TimeTravelEngine loaded;
    TTDMemorySource source(sink.bytes);
    TTDSessionLoadReport report;
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error, &report)) << error;
    EXPECT_TRUE(report.complete) << report.stoppedAt;
    EXPECT_EQ(loaded.CheckpointCount(), c.s.engine.CheckpointCount());
    EXPECT_TRUE(SameWrites(WritesOf(loaded), WritesOf(c.s.engine))) << "the write journal is not bit-identical";
    ASSERT_EQ(report.holderStreams.count(kHolder), 1u);
    EXPECT_TRUE(report.holderStreams[kHolder] == c.holder);

    // The default limit (1 GiB) writes the same session with one record per stream and loads the same
    TTDMemorySink plain;
    ASSERT_TRUE(TTDSessionFile::Save(c.s.engine, plain, error, c.Params(0))) << error;
    EXPECT_EQ(Stream(plain.bytes, sessionstream::kWriteJournal).chunks, 1u);
    EXPECT_EQ(Stream(plain.bytes, sessionstream::kWriteJournal).payload, journal.payload);
}

/// A reader without chunk support (an older build) skips the chunked twins as unknown ancillary streams: it loads
/// the checkpoints, without the write journal and the holder stream, and never reads a piece as the whole
TEST(TTDSessionFileChunks_Test, AReaderWithoutChunksLoadsTheSessionWithoutThoseStreams)
{
    Chunky c;
    TTDMemorySink sink;
    std::string error;
    ASSERT_TRUE(TTDSessionFile::Save(c.s.engine, sink, error, c.Params(kLimit))) << error;
    TTDMemorySource source(sink.bytes);
    TTDContainerReader reader;
    ASSERT_TRUE(reader.Open(source, error, [](uint16_t id) { return !IsChunkedTwin(id) && TTDSessionFile::KnownStream(id); }))
        << error;
    size_t skipped = 0;
    for (const std::string& note : reader.Notes())
        skipped += note.find("'chunked-") != std::string::npos;
    EXPECT_GE(skipped, 2u) << "the twins of the write journal and of the holder stream";
    for (const TTDPartRef& part : reader.Parts())
        for (const TTDRecordRef& r : part.physical)
        {
            EXPECT_NE(r.streamId, sessionstream::kWriteJournal) << "a piece stored under the stream's own id";
            EXPECT_NE(r.streamId, kHolder);
        }
}

/// A refused write: the save's reason is the sink's, "disk full" kind only when the sink says so
TEST(TTDSessionFileChunks_Test, AFailedSaveNamesTheRealReason)
{
    Chunky c;
    std::string error;
    {
        RefusingSink sink(5000, "write: Input/output error", false);
        TTDWriteFailure failure = TTDWriteFailure::None;
        EXPECT_FALSE(TTDSessionFile::Save(c.s.engine, sink, error, c.Params(0), &failure));
        EXPECT_EQ(failure, TTDWriteFailure::Io);
        EXPECT_NE(error.find("Input/output error"), std::string::npos) << error;
        EXPECT_EQ(error.find("disk full"), std::string::npos) << error;
    }
    {
        RefusingSink sink(5000, "write: No space left on device", true);
        TTDWriteFailure failure = TTDWriteFailure::None;
        EXPECT_FALSE(TTDSessionFile::Save(c.s.engine, sink, error, c.Params(0), &failure));
        EXPECT_EQ(failure, TTDWriteFailure::NoSpace);
        EXPECT_NE(error.find("No space left on device"), std::string::npos) << error;
    }
    {
        TTDMemorySink sink;
        TimeTravelEngine empty;
        TTDWriteFailure failure = TTDWriteFailure::None;
        EXPECT_FALSE(TTDSessionFile::Save(empty, sink, error, {}, &failure));
        EXPECT_EQ(failure, TTDWriteFailure::Layout);
        EXPECT_EQ(error, "no session to save");
    }
}

/// Writes testdata/ttd/v2/chunked/ (run by hand after a format change, then commit: *.ttd is git-ignored, add it
/// with -f): the Chunky session with a record limit of 4096, and chunked.json, what a reader must find in it
TEST(TTDSessionFileChunks_Test, DISABLED_WriteChunkedFixture)
{
    Chunky c;
    TTDMemorySink sink;
    std::string error;
    ASSERT_TRUE(TTDSessionFile::Save(c.s.engine, sink, error, c.Params(kLimit))) << error;
    fs::create_directories(FixtureDir());
    std::ofstream(FixtureDir() / "session-chunked.ttd", std::ios::binary)
        .write(reinterpret_cast<const char*>(sink.bytes.data()), static_cast<std::streamsize>(sink.bytes.size()));
    const StreamInFile journal = Stream(sink.bytes, sessionstream::kWriteJournal);
    const StreamInFile holder = Stream(sink.bytes, kHolder);
    std::ofstream json(FixtureDir() / "chunked.json");
    json << "{\n"
         << "  \"file\": \"session-chunked.ttd\",\n"
         << "  \"record_limit\": " << kLimit << ",\n"
         << "  \"checkpoints\": " << c.s.engine.CheckpointCount() << ",\n"
         << "  \"write_records\": " << c.s.engine.Writes().Size() << ",\n"
         << "  \"write_journal_chunks\": " << journal.chunks << ",\n"
         << "  \"write_journal_bytes\": " << journal.payload.size() << ",\n"
         << "  \"write_journal_crc32c\": " << codec::Crc32C(journal.payload.data(), journal.payload.size()) << ",\n"
         << "  \"holder_stream\": " << kHolder << ",\n"
         << "  \"holder_chunks\": " << holder.chunks << ",\n"
         << "  \"holder_bytes\": " << holder.payload.size() << ",\n"
         << "  \"holder_crc32c\": " << codec::Crc32C(holder.payload.data(), holder.payload.size()) << "\n"
         << "}\n";
}

/// The committed chunked file loads: the same write records and holder bytes as the session it was written from
TEST(TTDSessionFileChunks_Test, TheCommittedChunkedFixtureLoads)
{
    const std::vector<uint8_t> file = ReadFile(FixtureDir() / "session-chunked.ttd");
    ASSERT_FALSE(file.empty()) << "testdata/ttd/v2/chunked/session-chunked.ttd";
    Chunky c;
    TimeTravelEngine loaded;
    TTDMemorySource source(file);
    TTDSessionLoadReport report;
    std::string error;
    ASSERT_TRUE(TTDSessionFile::Load(loaded, source, error, &report)) << error;
    EXPECT_TRUE(report.complete) << report.stoppedAt;
    EXPECT_EQ(loaded.CheckpointCount(), c.s.engine.CheckpointCount());
    EXPECT_TRUE(SameWrites(WritesOf(loaded), WritesOf(c.s.engine)));
    EXPECT_TRUE(report.holderStreams[kHolder] == c.holder);
    EXPECT_GT(Stream(file, sessionstream::kWriteJournal).chunks, 2u);
}
