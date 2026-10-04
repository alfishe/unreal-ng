// The TTD v2 session container (debugger/ttd/engine/ttdcontainer.h): records
// round-trip, a file without its trailer opens by scanning, a torn tail is
// never accepted, damage makes holes (a part and the parts that depend on
// it) instead of refusing the file, unknown streams are refused or skipped by
// kind, and the same writes give the same bytes.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/ttd/engine/ttdcontainer.h"

using namespace ttd;

namespace
{
std::vector<uint8_t> Bytes(size_t n, uint8_t seed, bool compressible)
{
    std::vector<uint8_t> v(n);
    uint32_t x = seed * 2654435761u + 1;
    for (size_t i = 0; i < n; ++i)
    {
        x = x * 1103515245u + 12345u;
        v[i] = compressible ? static_cast<uint8_t>(i / 64 + seed) : static_cast<uint8_t>(x >> 16);
    }
    return v;
}

TTDContainerHeader Header()
{
    TTDContainerHeader h;
    for (uint8_t i = 0; i < 16; ++i)
        h.uuid[i] = static_cast<uint8_t>(i * 7 + 1);
    h.createdMicros = 1'759'500'000'000'000ull;
    h.streams = {{1, 1, TTDStreamKind::Required, "pieces"}, {7, 1, TTDStreamKind::Ancillary, "write-journal"}};
    h.sessionTables = {1, 2, 3, 4, 5};
    return h;
}

/// Three parts: 0 (frames 0-49), 1 (50-99, depends on 0), 2 (100-149, depends on nothing)
struct Written
{
    std::vector<std::vector<uint8_t>> payloads;   ///< in record order
    std::vector<uint8_t> file;
    size_t partOneEnd = 0;                         ///< file size after part 1's part end
};

Written Write(bool finalize, bool tail = false)
{
    Written w;
    TTDMemorySink sink;
    TTDContainerWriter writer;
    EXPECT_TRUE(writer.Begin(sink, Header()));
    auto add = [&](uint16_t stream, std::vector<uint8_t> data, bool compress) {
        EXPECT_TRUE(writer.AddRecord(stream, data, compress));
        w.payloads.push_back(std::move(data));
    };
    add(1, Bytes(5000, 1, true), true);
    add(7, Bytes(300, 2, false), true);
    EXPECT_TRUE(writer.EndPart({0, 50, 0, {}, {9, 9}}));
    add(1, Bytes(4000, 3, false), true);
    add(1, {}, true);
    EXPECT_TRUE(writer.EndPart({50, 50, 0, {0}, {}}));
    w.partOneEnd = sink.bytes.size();
    add(7, Bytes(100, 4, true), false);
    EXPECT_TRUE(writer.EndPart({100, 50, 0, {}, {}}));
    if (tail)
        add(1, Bytes(2000, 5, true), true);   // a part with no end: a crash mid-part
    if (finalize)
        EXPECT_TRUE(writer.Finalize({42, 43}));
    w.file = std::move(sink.bytes);
    return w;
}

void ExpectAllRecordsRead(TTDContainerReader& reader, const Written& w)
{
    size_t k = 0;
    for (const TTDPartRef& part : reader.Parts())
        for (const TTDRecordRef& record : part.records)
        {
            std::vector<uint8_t> out;
            std::string error;
            ASSERT_TRUE(reader.ReadRecord(record, out, &error)) << error;
            ASSERT_LT(k, w.payloads.size());
            EXPECT_EQ(out, w.payloads[k]) << "record " << k;
            ++k;
        }
}
}  // namespace

TEST(TTDContainer_Test, RoundTrip)
{
    const Written w = Write(true);
    TTDMemorySource source(w.file);
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    EXPECT_TRUE(reader.Finalized());
    EXPECT_TRUE(reader.Notes().empty());
    EXPECT_EQ(reader.Header().uuid, Header().uuid);
    EXPECT_EQ(reader.Header().createdMicros, Header().createdMicros);
    EXPECT_EQ(reader.Header().sessionTables, Header().sessionTables);
    ASSERT_EQ(reader.Header().streams.size(), 2u);
    EXPECT_EQ(reader.Header().Stream(7)->name, "write-journal");
    EXPECT_EQ(reader.IndexExtra(), (std::vector<uint8_t>{42, 43}));

    ASSERT_EQ(reader.Parts().size(), 3u);
    EXPECT_EQ(reader.Parts()[1].firstFrame, 50u);
    EXPECT_EQ(reader.Parts()[1].frameCount, 50u);
    EXPECT_EQ(reader.Parts()[1].dependencies, (std::vector<uint32_t>{0}));
    EXPECT_EQ(reader.Parts()[0].extra, (std::vector<uint8_t>{9, 9}));
    EXPECT_EQ(reader.Parts()[0].records.size(), 2u);
    EXPECT_NE(reader.Parts()[0].records[0].flags & 1, 0) << "the compressible record is stored compressed";
    EXPECT_EQ(reader.Parts()[0].records[1].flags & 1, 0) << "random bytes: stored as they are";
    ExpectAllRecordsRead(reader, w);
}

/// No trailer (the recording crashed): the complete parts open by scanning;
/// a last part without its end is dropped
TEST(TTDContainer_Test, AFileWithoutItsTrailerOpensByScanning)
{
    const Written w = Write(false, true);
    TTDMemorySource source(w.file);
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    EXPECT_FALSE(reader.Finalized());
    ASSERT_EQ(reader.Parts().size(), 3u);
    EXPECT_TRUE(std::any_of(reader.Notes().begin(), reader.Notes().end(),
                            [](const std::string& n) { return n.find("incomplete last part") != std::string::npos; }));
    ExpectAllRecordsRead(reader, w);
}

/// Cut the file at every byte from part 1's end on: only complete parts are
/// kept, and every kept record reads back intact
TEST(TTDContainer_Test, ATornTailIsNeverAccepted)
{
    const Written w = Write(true);
    for (size_t cut = w.partOneEnd; cut < w.file.size(); ++cut)
    {
        TTDMemorySource source(std::vector<uint8_t>(w.file.begin(), w.file.begin() + static_cast<long>(cut)));
        TTDContainerReader reader;
        std::string error;
        ASSERT_TRUE(reader.Open(source, error)) << "cut " << cut << ": " << error;
        ASSERT_FALSE(reader.Finalized());
        ASSERT_GE(reader.Parts().size(), 2u);
        ASSERT_LE(reader.Parts().size(), 3u);
        for (const TTDPartRef& part : reader.Parts())
        {
            ASSERT_FALSE(part.damaged) << "cut " << cut;
            for (const TTDRecordRef& record : part.records)
            {
                std::vector<uint8_t> out;
                ASSERT_TRUE(reader.ReadRecord(record, out)) << "cut " << cut;
            }
        }
    }
}

/// A flipped byte in a payload: the file opens, the read fails, its part is a
/// hole and so is the part that depends on it; the independent part stays
TEST(TTDContainer_Test, DamageMakesHolesNotRefusals)
{
    Written w = Write(true);
    TTDMemorySource probe(w.file);
    TTDContainerReader first;
    std::string error;
    ASSERT_TRUE(first.Open(probe, error));
    const TTDRecordRef target = first.Parts()[0].records[1];
    w.file[target.offset + kRecordHeaderSize + 10] ^= 0x40;

    TTDMemorySource source(w.file);
    TTDContainerReader reader;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    EXPECT_TRUE(reader.IsReachable(0)) << "payloads are checked when read, not at open";
    std::vector<uint8_t> out;
    EXPECT_FALSE(reader.ReadRecord(reader.Parts()[0].records[1], out, &error));
    EXPECT_NE(error.find("CRC"), std::string::npos) << error;
    EXPECT_FALSE(reader.IsReachable(0));
    EXPECT_FALSE(reader.IsReachable(1)) << "depends on part 0";
    EXPECT_TRUE(reader.IsReachable(2));
}

/// A damaged part-end record (finalized file) or record header (scan): that part
/// is a hole found at open, the rest opens
TEST(TTDContainer_Test, DamagedPartEndOrRecordHeader)
{
    std::string error;
    {
        Written w = Write(true);
        TTDMemorySource probe(w.file);
        TTDContainerReader first;
        ASSERT_TRUE(first.Open(probe, error));
        // Part 1's part-end record sits right before part 2's first record
        const uint64_t partOneEnd = first.Parts()[2].records[0].offset - 1;
        w.file[partOneEnd] ^= 0x01;
        TTDMemorySource source(w.file);
        TTDContainerReader reader;
        ASSERT_TRUE(reader.Open(source, error)) << error;
        EXPECT_TRUE(reader.Finalized());
        EXPECT_TRUE(reader.Parts()[1].damaged);
        EXPECT_FALSE(reader.IsReachable(1));
        EXPECT_TRUE(reader.IsReachable(0));
        EXPECT_TRUE(reader.IsReachable(2));
    }
    {
        Written w = Write(false);
        TTDMemorySource probe(w.file);
        TTDContainerReader first;
        ASSERT_TRUE(first.Open(probe, error));
        w.file[first.Parts()[0].records[0].offset + 5] ^= 0x10;   // inside the stream id
        TTDMemorySource source(w.file);
        TTDContainerReader reader;
        ASSERT_TRUE(reader.Open(source, error)) << error;
        ASSERT_EQ(reader.Parts().size(), 3u);
        EXPECT_TRUE(reader.Parts()[0].damaged);
        EXPECT_TRUE(reader.IsReachable(2));
        EXPECT_FALSE(reader.IsReachable(1));
    }
}

TEST(TTDContainer_Test, HeaderDamageAndUnknownStreams)
{
    std::string error;
    {
        Written w = Write(true);
        w.file[20] ^= 0x01;   // inside the UUID
        TTDMemorySource source(w.file);
        TTDContainerReader reader;
        EXPECT_FALSE(reader.Open(source, error));
        EXPECT_NE(error.find("header"), std::string::npos) << error;
    }
    const Written w = Write(true);
    TTDMemorySource source(w.file);
    {
        TTDContainerReader reader;
        EXPECT_FALSE(reader.Open(source, error, [](uint16_t id) { return id != 1; }));
        EXPECT_NE(error.find("'pieces'"), std::string::npos) << error;
    }
    {
        TTDContainerReader reader;
        ASSERT_TRUE(reader.Open(source, error, [](uint16_t id) { return id != 7; })) << error;
        ASSERT_EQ(reader.Notes().size(), 1u);
        EXPECT_NE(reader.Notes()[0].find("write-journal"), std::string::npos);
    }
    {
        std::vector<uint8_t> v1 = w.file;
        v1[4] = 1;   // schema 1: v1's own format
        TTDMemorySource old(v1);
        TTDContainerReader reader;
        EXPECT_FALSE(reader.Open(old, error));
        EXPECT_EQ(error, "unsupported schema v1");
    }
}

/// The same writes give the same bytes (QR-3: no time or pointer leaks into the file)
TEST(TTDContainer_Test, TheSameWritesGiveTheSameBytes)
{
    EXPECT_EQ(Write(true).file, Write(true).file);
}

/// Through the platform file layer, at a path with non-ASCII characters; a
/// file that exists is not overwritten
TEST(TTDContainer_Test, RealFile)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("session-\xc3\xa9t\xc3\xa9.ttd");
    std::error_code ec;
    std::filesystem::remove(FileHelper::ToFsPath(path), ec);
    const Written w = Write(true);
    {
        TTDFileSink sink(path);
        ASSERT_TRUE(sink.Valid()) << sink.Error();
        ASSERT_TRUE(sink.Write(w.file.data(), w.file.size()));
        ASSERT_TRUE(sink.Sync());
        EXPECT_EQ(sink.Size(), w.file.size());
    }
    TTDFileSink again(path);
    EXPECT_FALSE(again.Valid());

    TTDFileSource source(path);
    ASSERT_TRUE(source.Valid()) << source.Error();
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    EXPECT_TRUE(reader.Finalized());
    ExpectAllRecordsRead(reader, w);
}
