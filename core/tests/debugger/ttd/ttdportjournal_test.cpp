#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "debugger/ttd/ttdportjournal.h"

/// TTDPortJournal on its own: recording, playback with verification,
/// truncation and the file section (ttd-port-read-journal.md). The machine
/// integration is in timetravelcontroller_portjournal_test.cpp, the search in
/// ttdportsearch_test.cpp.
namespace
{
using ttd::TTDPortJournal;
using ttd::TTDPortRecord;
using Mode = TTDPortJournal::Mode;
using Direction = TTDPortJournal::Direction;

constexpr uint32_t kBlock = TTDPortJournal::kBlockRecords;

/// A deterministic record stream: record `i`, spread over frames of 70000 T
TTDPortRecord RecordOf(uint64_t i)
{
    TTDPortRecord r;
    r.frame = 100 + i / 1500;
    r.tInFrame = static_cast<uint32_t>((i % 1500) * 46 + (i % 7));
    r.port = static_cast<uint16_t>(0x00FE | ((i * 7) & 0xFF00));
    r.pc = static_cast<uint16_t>(0x8000 + (i % 3) * 4);
    r.value = static_cast<uint8_t>((i * 31 + (i >> 9)) & 0xFF);
    return r;
}

bool Same(const TTDPortRecord& a, const TTDPortRecord& b)
{
    return a.SameAccess(b) && a.value == b.value;
}

void Record(TTDPortJournal& j, uint64_t from, uint64_t count)
{
    j.StartRecording();
    for (uint64_t i = from; i < from + count; i++)
    {
        const TTDPortRecord r = RecordOf(i);
        if (j.GetDirection() == Direction::Read)
        {
            EXPECT_EQ(j.OnRead(r.port, r.value, r.frame, r.tInFrame, r.pc), r.value) << "recording passes the value through";
        }
        else
            j.OnWrite(r.port, r.value, r.frame, r.tInFrame, r.pc);
    }
    j.Stop();
}

void ExpectRecords(const TTDPortJournal& j, uint64_t count)
{
    ASSERT_EQ(j.Size(), count);
    for (uint64_t i = 0; i < count; i++)
    {
        TTDPortRecord r;
        ASSERT_TRUE(j.Get(i, r)) << "record " << i;
        ASSERT_TRUE(Same(r, RecordOf(i))) << "record " << i;
    }
}

std::string Save(const TTDPortJournal& j, const std::vector<uint64_t>& cursors)
{
    std::ostringstream out;
    std::string err;
    EXPECT_TRUE(j.Serialize(out, cursors, err)) << err;
    return out.str();
}

template <typename T>
void Put(std::string& file, size_t offset, T value)
{
    std::memcpy(&file[offset], &value, sizeof(value));
}
}  // namespace

TEST(TTDPortJournal, RecordsEveryAccessWithItsTimeAndPcAcrossBlocks)
{
    for (const Direction d : {Direction::Read, Direction::Write})
    {
        TTDPortJournal j(d);
        Record(j, 0, 3 * kBlock + 17);
        EXPECT_EQ(j.SealedBlockCount(), 3u);
        ExpectRecords(j, 3 * kBlock + 17);
        TTDPortRecord r;
        EXPECT_FALSE(j.Get(3 * kBlock + 17, r)) << "past the end";
    }
}

TEST(TTDPortJournal, OffPassesAccessesThroughWithoutRecording)
{
    TTDPortJournal j;
    EXPECT_EQ(j.GetMode(), Mode::Off);
    EXPECT_EQ(j.OnRead(0xFE, 0x5A, 1, 2, 3), 0x5A);
    EXPECT_EQ(j.Size(), 0u);
}

/// The point of the IN journal: on replay the CPU gets the recorded value, not
/// the live one; a differing value is a mismatch (the device answered
/// otherwise), a differing time, PC or port a divergence (execution left the
/// recording)
TEST(TTDPortJournal, ReplayedReadsGetTheRecordedValuesAndCountWhatDiffered)
{
    TTDPortJournal j(Direction::Read);
    Record(j, 0, 10);

    j.StartPlayback(2);
    ASSERT_EQ(j.GetMode(), Mode::Play);
    TTDPortRecord r = RecordOf(2);
    EXPECT_EQ(j.OnRead(r.port, r.value, r.frame, r.tInFrame, r.pc), r.value);
    r = RecordOf(3);
    EXPECT_EQ(j.OnRead(r.port, static_cast<uint8_t>(r.value ^ 0xFF), r.frame, r.tInFrame, r.pc), r.value)
        << "recorded, not live";
    r = RecordOf(4);
    EXPECT_EQ(j.OnRead(r.port, r.value, r.frame, r.tInFrame + 1, r.pc), r.value) << "late by one T-state";
    r = RecordOf(5);
    EXPECT_EQ(j.OnRead(r.port, r.value, r.frame, r.tInFrame, 0x1234), r.value) << "another instruction";
    EXPECT_EQ(j.ValueMismatches(), 1u);
    EXPECT_EQ(j.Divergences(), 2u);
    ASSERT_TRUE(j.HasMismatch());
    EXPECT_EQ(j.FirstMismatch().index, 3u);
    EXPECT_EQ(j.FirstMismatch().live.value, static_cast<uint8_t>(RecordOf(3).value ^ 0xFF));
    ASSERT_TRUE(j.HasDivergence());
    EXPECT_EQ(j.FirstDivergence().index, 4u);
    EXPECT_EQ(j.Cursor(), 6u);
    EXPECT_EQ(j.Size(), 10u) << "playback never appends";
}

/// OUTs are facts: a replayed OUT of another value is a divergence
TEST(TTDPortJournal, ReplayedWritesAreCheckedAgainstTheRecord)
{
    TTDPortJournal j(Direction::Write);
    Record(j, 0, 4);
    j.StartPlayback(0);
    TTDPortRecord r = RecordOf(0);
    j.OnWrite(r.port, r.value, r.frame, r.tInFrame, r.pc);
    r = RecordOf(1);
    j.OnWrite(r.port, static_cast<uint8_t>(r.value + 1), r.frame, r.tInFrame, r.pc);
    EXPECT_EQ(j.Divergences(), 1u);
    EXPECT_EQ(j.ValueMismatches(), 0u);
    EXPECT_EQ(j.FirstDivergence().index, 1u);
}

TEST(TTDPortJournal, PlaybackEndsWithTheRecordedHistory)
{
    TTDPortJournal j;
    Record(j, 0, 3);
    j.StartPlayback(1);
    for (uint64_t i = 1; i < 3; i++)
    {
        const TTDPortRecord r = RecordOf(i);
        j.OnRead(r.port, 0, r.frame, r.tInFrame, r.pc);
    }
    EXPECT_EQ(j.GetMode(), Mode::Off) << "the last record replayed ends playback";
    EXPECT_EQ(j.OnRead(0xFE, 0x42, 1, 2, 3), 0x42) << "past the history the machine reads live devices";

    j.StartPlayback(3);
    EXPECT_EQ(j.GetMode(), Mode::Off) << "a cursor at the end has nothing to replay";
}

TEST(TTDPortJournal, PlaybackReadsSealedBlocks)
{
    TTDPortJournal j;
    Record(j, 0, 2 * kBlock + 5);
    j.StartPlayback(kBlock - 2);
    for (uint64_t i = kBlock - 2; i < 2 * kBlock + 5; i++)
    {
        const TTDPortRecord r = RecordOf(i);
        ASSERT_EQ(j.OnRead(r.port, 0, r.frame, r.tInFrame, r.pc), r.value) << "record " << i;
    }
    EXPECT_EQ(j.Divergences(), 0u) << "the same accesses in the same order at the same times";
}

TEST(TTDPortJournal, LowerBoundFindsTheFirstRecordAtOrAfterATime)
{
    TTDPortJournal j;
    Record(j, 0, 2 * kBlock + 100);
    for (const uint64_t i : {uint64_t(0), uint64_t(1), uint64_t(kBlock + 3), uint64_t(2 * kBlock + 99)})
        EXPECT_EQ(j.LowerBound(RecordOf(i).Time()), i);
    EXPECT_EQ(j.LowerBound({0, 0}), 0u);
    EXPECT_EQ(j.LowerBound({1'000'000, 0}), j.Size());
}

TEST(TTDPortJournal, TruncateInsideTheOpenBlockKeepsThePrefix)
{
    TTDPortJournal j;
    Record(j, 0, kBlock + 100);
    j.TruncateTo(kBlock + 40);
    ExpectRecords(j, kBlock + 40);
    Record(j, kBlock + 40, 60);
    ExpectRecords(j, kBlock + 100);
}

/// A resume from the past cuts inside a sealed block: the block becomes the
/// open block again and new records continue it
TEST(TTDPortJournal, TruncateInsideASealedBlockReopensIt)
{
    TTDPortJournal j;
    Record(j, 0, 3 * kBlock + 10);
    j.TruncateTo(kBlock + 123);
    EXPECT_EQ(j.SealedBlockCount(), 1u);
    ExpectRecords(j, kBlock + 123);
    Record(j, kBlock + 123, 2 * kBlock);
    EXPECT_EQ(j.SealedBlockCount(), 3u);
    ExpectRecords(j, 3 * kBlock + 123);
}

TEST(TTDPortJournal, TruncateAtABlockBoundaryAndToZero)
{
    TTDPortJournal j;
    Record(j, 0, 2 * kBlock + 10);
    j.TruncateTo(kBlock);
    EXPECT_EQ(j.SealedBlockCount(), 1u);
    ExpectRecords(j, kBlock);
    j.TruncateTo(0);
    EXPECT_EQ(j.Size(), 0u);
    Record(j, 0, 5);
    ExpectRecords(j, 5);
}

TEST(TTDPortJournal, SectionRoundTripsRecordsAndCursors)
{
    for (const uint64_t count : {uint64_t(0), uint64_t(1), uint64_t(kBlock), uint64_t(2 * kBlock + 777)})
    {
        SCOPED_TRACE(count);
        TTDPortJournal j;
        Record(j, 0, count);
        const std::vector<uint64_t> cursors = {0, count / 3, count / 2, count};
        const std::string file = Save(j, cursors);

        TTDPortJournal loaded;
        std::vector<uint64_t> loadedCursors;
        std::string err;
        std::istringstream in(file);
        ASSERT_TRUE(loaded.Deserialize(in, 4, loadedCursors, err)) << err;
        EXPECT_EQ(loadedCursors, cursors);
        ExpectRecords(loaded, count);
        EXPECT_EQ(Save(loaded, cursors), file) << "a loaded journal saves back byte for byte";

        // A loaded journal records on (the open block came back raw)
        Record(loaded, count, 50);
        ExpectRecords(loaded, count + 50);
    }
}

/// A polling loop - the same port and instruction, the same T-state step, a
/// value that changes now and then (a tape loader) - costs almost nothing on
/// disk, time and PC included
TEST(TTDPortJournal, APollingLoopCompressesToAlmostNothing)
{
    TTDPortJournal j;
    j.StartRecording();
    constexpr uint64_t kReads = 200000;
    constexpr uint32_t kLoopT = 47;
    for (uint64_t i = 0; i < kReads; i++)
    {
        const uint64_t t = 1000 + i * kLoopT;
        j.OnRead(0x7FFE, (i / 700) & 1 ? 0xBF : 0xFF, t / 69888, static_cast<uint32_t>(t % 69888), 0x05ED);
    }
    j.Stop();
    const size_t raw = kReads * TTDPortJournal::kRawRecordBytes;
    EXPECT_LT(j.SerializedBytes(), raw / 100) << "raw " << raw << " B, stored " << j.SerializedBytes() << " B";
}

/// The open block's size is compressed once per change: a cut and as many other records again give the size of a
/// journal recorded that way, not the one before the cut
TEST(TTDPortJournal, SerializedBytesFollowsACutOpenBlock)
{
    // 100 records of one polling loop: they compress far better than RecordOf's
    auto poll = [](TTDPortJournal& journal) {
        journal.StartRecording();
        for (uint32_t i = 0; i < 100; i++)
            journal.OnRead(0x7FFE, 0xFF, 900, 1000 + i * 47, 0x05ED);
        journal.Stop();
    };
    TTDPortJournal j;
    Record(j, 0, kBlock + 100);
    const size_t before = j.SerializedBytes();
    EXPECT_EQ(j.SerializedBytes(), before);
    j.TruncateTo(kBlock);
    poll(j);

    TTDPortJournal fresh;
    Record(fresh, 0, kBlock);
    poll(fresh);
    EXPECT_EQ(j.Size(), kBlock + 100u);
    EXPECT_EQ(j.SerializedBytes(), fresh.SerializedBytes());
    EXPECT_LT(j.SerializedBytes(), before);
}

// ---------------------------------------------------------------------------
// A damaged section fails the load and leaves nothing half-loaded
// ---------------------------------------------------------------------------

class TTDPortJournalDamage : public ::testing::Test
{
protected:
    std::string _file;
    std::vector<uint64_t> _cursors = {0, 10, kBlock + 3};
    // Offsets: header u64 count, u32 block_records, u32 block_count (16 bytes),
    // then block 0: u32 records, u64 base_frame, u32 crc, u32 compressed_size, payload
    static constexpr size_t kBlockCountAt = 12;
    static constexpr size_t kBlock0At = 16;
    static constexpr size_t kBlockHeader = 20;

    void SetUp() override
    {
        TTDPortJournal j;
        Record(j, 0, kBlock + 50);
        _file = Save(j, _cursors);
    }

    void ExpectRefused(const std::string& damaged, const std::string& messagePart, uint32_t checkpoints = 3)
    {
        TTDPortJournal j;
        Record(j, 0, 7);  // must be left empty, not half-loaded
        std::vector<uint64_t> cursors;
        std::string err;
        std::istringstream in(damaged);
        EXPECT_FALSE(j.Deserialize(in, checkpoints, cursors, err));
        EXPECT_NE(err.find(messagePart), std::string::npos) << "error was: " << err;
        EXPECT_EQ(j.Size(), 0u);
        EXPECT_TRUE(cursors.empty());
    }

    size_t CursorListAt() const { return _file.size() - 4 - _cursors.size() * 8; }
};

TEST_F(TTDPortJournalDamage, IntactSectionLoads)
{
    TTDPortJournal j;
    std::vector<uint64_t> cursors;
    std::string err;
    std::istringstream in(_file);
    EXPECT_TRUE(j.Deserialize(in, 3, cursors, err)) << err;
}

TEST_F(TTDPortJournalDamage, TruncationFails)
{
    ExpectRefused(_file.substr(0, 10), "truncated header");
    ExpectRefused(_file.substr(0, kBlock0At + kBlockHeader + 8), "truncated block 0 payload");
    ExpectRefused(_file.substr(0, _file.size() - 3), "truncated cursor");
}

TEST_F(TTDPortJournalDamage, AFlippedPayloadByteFails)
{
    std::string damaged = _file;
    damaged[kBlock0At + kBlockHeader + 20] ^= 0x10;
    ExpectRefused(damaged, "block 0");
}

TEST_F(TTDPortJournalDamage, AWrongChecksumFails)
{
    std::string damaged = _file;
    Put<uint32_t>(damaged, kBlock0At + 12, 0xDEADBEEF);
    ExpectRefused(damaged, "fails its CRC");
}

TEST_F(TTDPortJournalDamage, InconsistentCountsFail)
{
    std::string damaged = _file;
    Put<uint64_t>(damaged, 0, kBlock + 51);
    ExpectRefused(damaged, "records");

    damaged = _file;
    Put<uint32_t>(damaged, kBlockCountAt, 7);
    ExpectRefused(damaged, "blocks cannot hold");

    damaged = _file;
    Put<uint32_t>(damaged, 8, 1024);
    ExpectRefused(damaged, "unsupported block size");

    damaged = _file;
    Put<uint32_t>(damaged, kBlock0At, kBlock - 1);  // a non-last block short of full
    ExpectRefused(damaged, "claims");
}

/// The second block moved before the first in time
TEST_F(TTDPortJournalDamage, RecordsOutOfTimeOrderFail)
{
    std::string damaged = _file;
    const size_t block0Size = *reinterpret_cast<const uint32_t*>(&_file[kBlock0At + 16]);
    const size_t block1At = kBlock0At + kBlockHeader + block0Size;
    Put<uint64_t>(damaged, block1At + 4, 1);  // base_frame of block 1
    ExpectRefused(damaged, "earlier than the one before it");
}

TEST_F(TTDPortJournalDamage, CursorsMustMatchTheCheckpointsAndStayInOrder)
{
    ExpectRefused(_file, "cursors for 4 checkpoints", 4);

    std::string damaged = _file;
    Put<uint64_t>(damaged, CursorListAt() + 4, 11);  // the first cursor past the second (10)
    ExpectRefused(damaged, "out of order");

    damaged = _file;
    Put<uint64_t>(damaged, CursorListAt() + 4 + 16, kBlock + 51);
    ExpectRefused(damaged, "past the end");
}

namespace
{
/// A polling loop: frames of 3,000 accesses to two ports (#FE and #7FFD-like), the ULA's bit 4 flipping now and then
TTDPortRecord PollingRecordOf(uint64_t i)
{
    TTDPortRecord r;
    r.frame = 500 + i / 3000;
    r.tInFrame = static_cast<uint32_t>((i % 3000) * 23);
    r.port = (i % 5 == 0) ? 0x7FFD : 0x00FE;
    r.pc = 0x8004;
    r.value = static_cast<uint8_t>(((i / 7) & 1) ? 0x10 : 0x07);
    return r;
}

struct Totals
{
    std::vector<uint64_t> count = std::vector<uint64_t>(65536, 0);
    uint64_t toggles = 0;   // bit 4 of #FE, from 0
    uint8_t fe = 0;
    bool operator==(const Totals& o) const { return count == o.count && toggles == o.toggles && fe == o.fe; }
};

void AddByRecords(const TTDPortJournal& j, uint64_t from, uint64_t to, Totals& t)
{
    TTDPortRecord r;
    for (uint64_t k = from; k < to; ++k)
        if (j.Get(k, r))
        {
            ++t.count[r.port];
            if (r.port == 0x00FE)
            {
                t.toggles += ((r.value ^ t.fe) & 0x10) != 0;
                t.fe = r.value;
            }
        }
}

void AddByTally(const TTDPortJournal& j, uint64_t from, uint64_t to, Totals& t)
{
    TTDPortJournal::ReadCache cache;
    ASSERT_TRUE(j.Tally(from, to, cache, [&](const TTDPortJournal::PortTally* p, size_t n) {
        for (size_t k = 0; k < n; ++k)
        {
            t.count[p[k].port] += p[k].count;
            if (p[k].port == 0x00FE)
            {
                t.toggles += (((p[k].first ^ t.fe) & 0x10) != 0) + p[k].bit4Toggles;
                t.fe = p[k].last;
            }
        }
        return true;
    }));
}
}  // namespace

/// Tally gives what visiting every record gives, frame by frame and over ranges that cut frames and blocks; a polling
/// journal's frames come from the block summaries without a decode, also after a save and load (the timeline's port
/// facts, P1.S10 seek tails). A journal of few accesses a frame to many ports keeps no summary and is decoded
TEST(TTDPortJournal, TallyMatchesTheRecordsWithoutDecodingAPollingJournal)
{
    TTDPortJournal j(Direction::Write);
    j.StartRecording();
    const uint64_t total = 3 * kBlock + 1234;
    for (uint64_t i = 0; i < total; ++i)
    {
        const TTDPortRecord r = PollingRecordOf(i);
        j.OnWrite(r.port, r.value, r.frame, r.tInFrame, r.pc);
    }
    j.Stop();
    // Frame starts, as checkpoint cursors are
    std::vector<uint64_t> starts;
    for (uint64_t i = 0; i < total; i += 3000)
        starts.push_back(i);
    starts.push_back(total);

    auto check = [&](const TTDPortJournal& journal, bool expectNoDecode) {
        const uint64_t decodedBefore = journal.BlocksDecoded();
        Totals byTally;
        for (size_t f = 0; f + 1 < starts.size(); ++f)
            AddByTally(journal, starts[f], starts[f + 1], byTally);
        if (expectNoDecode)
            EXPECT_EQ(journal.BlocksDecoded(), decodedBefore) << "frame-aligned ranges of summarised blocks";
        Totals byRecords;
        AddByRecords(journal, 0, total, byRecords);
        EXPECT_TRUE(byTally == byRecords);
        EXPECT_GT(byRecords.toggles, 100u);
        // Ranges that cut frames and blocks
        for (const auto& [from, to] : std::vector<std::pair<uint64_t, uint64_t>>{
                 {17, 9001}, {kBlock - 5, kBlock + 7}, {2 * kBlock - 3000, 3 * kBlock + 100}, {0, total}})
        {
            Totals a, b;
            AddByTally(journal, from, to, a);
            AddByRecords(journal, from, to, b);
            EXPECT_TRUE(a == b) << from << ".." << to;
        }
    };
    check(j, true);

    std::istringstream in(Save(j, {0}));
    TTDPortJournal loaded(Direction::Write);
    std::vector<uint64_t> cursors;
    std::string err;
    ASSERT_TRUE(loaded.Deserialize(in, 1, cursors, err)) << err;
    check(loaded, true);

    // Many ports a frame: no summary, the same answers by decoding
    TTDPortJournal sparse(Direction::Read);
    Record(sparse, 0, 2 * kBlock + 50);
    TTDPortJournal::ReadCache cache;
    std::vector<uint64_t> a(65536, 0), b(65536, 0);
    ASSERT_TRUE(sparse.Tally(10, 2 * kBlock + 40, cache, [&](const TTDPortJournal::PortTally* p, size_t n) {
        for (size_t k = 0; k < n; ++k)
            a[p[k].port] += p[k].count;
        return true;
    }));
    for (uint64_t k = 10; k < 2 * kBlock + 40; ++k)
        ++b[RecordOf(k).port];
    EXPECT_EQ(a, b);
}
