// The container's chunked streams and its write failure reasons (F3, docs/inprogress/2026-11-12-re-api-efficiency
// analysis.md §F3): a payload over the record limit is written as records of the stream's chunked twin and read
// back whole, bit for bit, from a finished file and by scanning; a verbatim copy of the records gives the same
// bytes; a reader that does not join chunks sees only the twin (never a piece of the payload under the stream's
// id); a cut chunked stream is damage, not a short payload; a payload over the limit without a declared twin is
// refused with the sizes; a refused write is "disk full" only when the sink says so. The record limit is set small
// (TTDContainerWriter::SetRecordLimit), so every case takes about a millisecond.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "debugger/ttd/engine/ttdcontainer.h"

using namespace ttd;

namespace
{
constexpr uint32_t kLimit = 1000;   ///< the test's record limit

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

TTDContainerHeader Header(bool declareTwin = true)
{
    TTDContainerHeader h;
    h.uuid[0] = 0x5A;
    h.createdMicros = 1'759'500'000'000'000ull;
    h.streams = {{1, 1, TTDStreamKind::Required, "pieces"}, {7, 1, TTDStreamKind::Ancillary, "write-journal"}};
    if (declareTwin)
        h.streams.push_back({ChunkedTwin(7), 1, TTDStreamKind::Ancillary, "chunked-write-journal"});
    return h;
}

struct Written
{
    std::vector<uint8_t> file;
    std::vector<std::vector<uint8_t>> payloads;   ///< in order: per part the stream 1, 7, 1 payloads
};

/// Two parts, each: a small stream-1 payload, a stream-7 payload of @p bigSize (chunked over kLimit), another
/// small stream-1 payload
Written Write(size_t bigSize, bool finalize, bool compressible)
{
    Written w;
    TTDMemorySink sink;
    TTDContainerWriter writer;
    writer.SetRecordLimit(kLimit);
    EXPECT_TRUE(writer.Begin(sink, Header()));
    for (uint8_t part = 0; part < 2; ++part)
    {
        w.payloads.push_back(Bytes(100, static_cast<uint8_t>(part * 3 + 1), true));
        w.payloads.push_back(Bytes(bigSize, static_cast<uint8_t>(part * 3 + 2), compressible));
        w.payloads.push_back(Bytes(kLimit, static_cast<uint8_t>(part * 3 + 3), false));   // exactly the limit: one
        EXPECT_TRUE(writer.AddRecord(1, w.payloads[w.payloads.size() - 3]));
        EXPECT_TRUE(writer.AddRecord(7, w.payloads[w.payloads.size() - 2]));
        EXPECT_TRUE(writer.AddRecord(1, w.payloads.back()));
        EXPECT_TRUE(writer.EndPart({part * 50ull, 50, 0, {}, {}})) << writer.Error().text;
    }
    if (finalize)
        EXPECT_TRUE(writer.Finalize());
    w.file = std::move(sink.bytes);
    return w;
}

void ExpectPayloads(TTDContainerReader& reader, const Written& w, uint32_t chunks)
{
    size_t k = 0;
    ASSERT_EQ(reader.Parts().size(), 2u);
    for (const TTDPartRef& part : reader.Parts())
    {
        ASSERT_FALSE(part.damaged) << part.damage;
        ASSERT_EQ(part.records.size(), 3u) << "the chunked stream is one payload";
        EXPECT_EQ(part.physical.size(), 2u + chunks);
        EXPECT_EQ(part.records[1].streamId, 7u);
        EXPECT_EQ(part.records[1].chunks, chunks);
        EXPECT_EQ(part.records[2].chunks, 1u) << "a payload of exactly the limit is one record";
        for (const TTDRecordRef& record : part.records)
        {
            std::vector<uint8_t> out;
            std::string error;
            ASSERT_TRUE(reader.ReadRecord(record, out, &error)) << error;
            EXPECT_TRUE(out == w.payloads[k]) << "payload " << k << " is not bit-identical";
            ++k;
        }
    }
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

TEST(TTDContainerChunks_Test, AStreamOverTheRecordLimitReadsBackWhole)
{
    for (const bool compressible : {false, true})
    {
        SCOPED_TRACE(compressible ? "compressible" : "random");
        const Written w = Write(3500, true, compressible);
        TTDMemorySource source(w.file);
        TTDContainerReader reader;
        std::string error;
        ASSERT_TRUE(reader.Open(source, error)) << error;
        EXPECT_TRUE(reader.Finalized());
        EXPECT_TRUE(reader.Notes().empty());
        ExpectPayloads(reader, w, 4);   // 1000 + 1000 + 1000 + 500
        EXPECT_EQ(reader.Parts()[0].records[1].chunkedRaw, 3500u);
        for (const TTDRecordRef& r : reader.Parts()[0].physical)
            EXPECT_LE(r.rawSize, kLimit) << "no record holds more than the limit";
    }
}

/// The recovery path (no trailer: the file is scanned) joins the chunks the same way
TEST(TTDContainerChunks_Test, AScannedFileJoinsTheChunksToo)
{
    const Written w = Write(2001, false, false);
    TTDMemorySource source(w.file);
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    EXPECT_FALSE(reader.Finalized());
    ExpectPayloads(reader, w, 3);
}

/// A copy record by record as stored (the recording join, the Python tools) gives the same bytes
TEST(TTDContainerChunks_Test, AVerbatimCopyIsByteIdentical)
{
    const Written w = Write(3500, true, true);
    TTDMemorySource source(w.file);
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    TTDMemorySink copy;
    TTDContainerWriter writer;
    ASSERT_TRUE(writer.Begin(copy, reader.Header()));
    for (const TTDPartRef& part : reader.Parts())
    {
        for (const TTDRecordRef& r : part.physical)
        {
            std::vector<uint8_t> stored;
            ASSERT_TRUE(reader.ReadStored(r, stored, &error)) << error;
            ASSERT_TRUE(writer.AddStoredRecord(r.streamId, r.flags, stored.data(), stored.size(), r.rawSize));
        }
        ASSERT_TRUE(writer.EndPart({part.firstFrame, part.frameCount, part.branch, part.dependencies, part.extra}));
    }
    ASSERT_TRUE(writer.Finalize());
    EXPECT_TRUE(copy.bytes == w.file);
    // A joined payload is not a stored record
    std::vector<uint8_t> stored;
    EXPECT_FALSE(reader.ReadStored(reader.Parts()[0].records[1], stored, &error));
}

/// A reader that does not know chunks (an older build) sees the twin's records under the twin's id, which it skips
/// as an unknown ancillary stream: never a piece of the payload as stream 7's whole
TEST(TTDContainerChunks_Test, AReaderWithoutChunksSeesOnlyTheTwin)
{
    const Written w = Write(3500, true, false);
    TTDMemorySource source(w.file);
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(source, error, [](uint16_t id) { return !IsChunkedTwin(id); })) << error;
    ASSERT_EQ(reader.Notes().size(), 1u);
    EXPECT_NE(reader.Notes()[0].find("chunked-write-journal"), std::string::npos) << reader.Notes()[0];
    for (const TTDRecordRef& r : reader.Parts()[0].physical)
        EXPECT_NE(r.streamId, 7u) << "no record is stored under the stream's own id";
}

/// A chunked stream whose last record still says "continued" (a writer that stopped in the middle) is damage
TEST(TTDContainerChunks_Test, ACutChunkedStreamIsDamage)
{
    TTDMemorySink sink;
    TTDContainerWriter writer;
    ASSERT_TRUE(writer.Begin(sink, Header()));
    const std::vector<uint8_t> chunk = Bytes(500, 9, false);
    constexpr uint16_t kContinued = 4;
    ASSERT_TRUE(writer.AddStoredRecord(ChunkedTwin(7), kContinued, chunk.data(), chunk.size(), chunk.size()));
    ASSERT_TRUE(writer.AddRecord(1, Bytes(50, 1, true)));
    ASSERT_TRUE(writer.EndPart({0, 1, 0, {}, {}}));
    ASSERT_TRUE(writer.Finalize());
    TTDMemorySource source(sink.bytes);
    TTDContainerReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(source, error)) << error;
    ASSERT_EQ(reader.Parts().size(), 1u);
    EXPECT_TRUE(reader.Parts()[0].damaged);
    EXPECT_NE(reader.Parts()[0].damage.find("is cut"), std::string::npos) << reader.Parts()[0].damage;
    EXPECT_FALSE(reader.IsReachable(0));
}

/// Over the limit without a declared twin: refused, the reason names the stream and the sizes
TEST(TTDContainerChunks_Test, AnUndeclaredStreamOverTheLimitIsRefusedWithTheSizes)
{
    TTDMemorySink sink;
    TTDContainerWriter writer;
    writer.SetRecordLimit(kLimit);
    ASSERT_TRUE(writer.Begin(sink, Header(false)));
    EXPECT_FALSE(writer.AddRecord(7, Bytes(1500, 1, false)));
    EXPECT_TRUE(writer.Failed());
    EXPECT_EQ(writer.Error().kind, TTDWriteFailure::TooLarge);
    const std::string& text = writer.Error().text;
    EXPECT_NE(text.find("'write-journal'"), std::string::npos) << text;
    EXPECT_NE(text.find("1500"), std::string::npos) << text;
    EXPECT_NE(text.find("1000"), std::string::npos) << text;
    EXPECT_EQ(text.find("disk"), std::string::npos) << "not a disk problem: " << text;
    // The first reason stays
    EXPECT_FALSE(writer.EndPart({0, 1, 0, {}, {}}));
    EXPECT_EQ(writer.Error().kind, TTDWriteFailure::TooLarge);
}

/// A refused write: the sink's reason in the text; NoSpace only when the sink says the disk is full
TEST(TTDContainerChunks_Test, ARefusedWriteNamesTheSystemsReason)
{
    struct Case
    {
        const char* reason;
        bool noSpace;
        TTDWriteFailure kind;
    };
    for (const Case& c : {Case{"write: Input/output error", false, TTDWriteFailure::Io},
                          Case{"write: No space left on device", true, TTDWriteFailure::NoSpace}})
    {
        SCOPED_TRACE(c.reason);
        RefusingSink sink(400, c.reason, c.noSpace);
        TTDContainerWriter writer;
        ASSERT_TRUE(writer.Begin(sink, Header()));
        EXPECT_FALSE(writer.AddRecord(1, Bytes(600, 2, false)));
        EXPECT_EQ(writer.Error().kind, c.kind);
        EXPECT_NE(writer.Error().text.find(c.reason), std::string::npos) << writer.Error().text;
        EXPECT_NE(writer.Error().text.find("'pieces'"), std::string::npos) << writer.Error().text;
    }
    // The header itself
    RefusingSink tiny(10, "write: Broken pipe", false);
    TTDContainerWriter writer;
    std::string error;
    EXPECT_FALSE(writer.Begin(tiny, Header(), &error));
    EXPECT_EQ(writer.Error().kind, TTDWriteFailure::Io);
    EXPECT_NE(error.find("header"), std::string::npos) << error;
    EXPECT_NE(error.find("Broken pipe"), std::string::npos) << error;
}
