#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "debugger/ttd/ttdportjournal.h"
#include "debugger/ttd/ttdportsearch.h"

/// The "when did the program ..." queries over hand-built port journals
/// (ttdportsearch.h). The same queries on a recorded machine are in
/// timetravelmanager_portjournal_test.cpp.
namespace
{
using ttd::BuildPortEventQuery;
using ttd::SearchPortEvents;
using ttd::TTDPortJournal;
using ttd::TTDPortQuery;
using ttd::TTDPortSearchResult;
using Direction = TTDPortJournal::Direction;

/// Journals filled access by access, time advancing 10 T each
struct Journals
{
    TTDPortJournal reads{Direction::Read};
    TTDPortJournal writes{Direction::Write};
    uint64_t frame = 5;
    uint32_t t = 100;

    Journals()
    {
        reads.StartRecording();
        writes.StartRecording();
    }
    void In(uint16_t port, uint8_t value, uint16_t pc = 0x8000)
    {
        reads.OnRead(port, value, frame, t, pc);
        t += 10;
    }
    void Out(uint16_t port, uint8_t value, uint16_t pc = 0x9000)
    {
        writes.OnWrite(port, value, frame, t, pc);
        t += 10;
    }
    void NextFrame()
    {
        ++frame;
        t = 5;
    }

    TTDPortSearchResult Find(const std::string& event, const std::string& arg = "", size_t limit = 100,
                             bool newestFirst = false)
    {
        TTDPortQuery q;
        std::string err;
        EXPECT_TRUE(BuildPortEventQuery(event, arg, q, err)) << err;
        q.limit = limit;
        q.newestFirst = newestFirst;
        TTDPortSearchResult r = SearchPortEvents(reads, writes, q);
        EXPECT_TRUE(r.ok) << r.error;
        return r;
    }
};

std::vector<uint8_t> Values(const TTDPortSearchResult& r)
{
    std::vector<uint8_t> v;
    for (const auto& h : r.hits)
        v.push_back(h.record.value);
    return v;
}
}  // namespace

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

/// "key a": the first read that shows A down, once per press - a program that
/// keeps polling while A is held reports it once
TEST(TTDPortSearch, KeyReportsEachPressOnceAtTheReadThatSawIt)
{
    Journals j;
    j.In(0xFDFE, 0xFF);           // A's half-row, nothing down
    j.In(0xFDFE, 0xFE);           // A down (bit 0)     <- hit
    j.In(0xFDFE, 0xFE);           // still down
    j.In(0xFEFE, 0xFE);           // CAPS SHIFT's row: not A's
    j.In(0xFDFE, 0xFF, 0x8123);   // released
    j.NextFrame();
    j.In(0xFDFE, 0xFE, 0x8124);   // A down again       <- hit
    const auto r = j.Find("key", "a");
    ASSERT_EQ(r.hits.size(), 2u);
    EXPECT_EQ(r.hits[0].index, 1u);
    EXPECT_EQ(r.hits[0].record.frame, 5u);
    EXPECT_EQ(r.hits[0].record.tInFrame, 110u);
    EXPECT_EQ(r.hits[1].index, 5u);
    EXPECT_EQ(r.hits[1].record.frame, 6u);
    EXPECT_EQ(r.hits[1].record.pc, 0x8124);
}

/// A read of every half-row at once shows bit 0 down for A, Q, 1, 0, P,
/// ENTER, SPACE and CAPS SHIFT alike: it is not "A" - only "some key"
TEST(TTDPortSearch, AReadOfAllRowsIsSomeKeyNotAParticularOne)
{
    Journals j;
    j.In(0x00FE, 0xFF);
    j.In(0x00FE, 0xFE);
    EXPECT_TRUE(j.Find("key", "a").hits.empty());
    EXPECT_EQ(j.Find("key").hits.size(), 1u);
}

TEST(TTDPortSearch, KeyInTheSameBitOfAnotherRowIsNotThatKey)
{
    Journals j;
    j.In(0xFEFE, 0xFE);  // CAPS SHIFT (row #FE, bit 0)
    j.In(0xFBFE, 0xFE);  // Q (row #FB, bit 0)
    EXPECT_TRUE(j.Find("key", "a").hits.empty());
    EXPECT_EQ(j.Find("key", "q").hits.size(), 1u);
    EXPECT_EQ(j.Find("key", "caps").hits.size(), 1u);
}

TEST(TTDPortSearch, AnyKeyAndAnUnknownKeyName)
{
    Journals j;
    j.In(0x7FFE, 0xFF);
    j.In(0x7FFE, 0xFB);  // M
    EXPECT_EQ(j.Find("key").hits.size(), 1u);

    TTDPortQuery q;
    std::string err;
    EXPECT_FALSE(BuildPortEventQuery("key", "left", q, err)) << "an extended key is two matrix keys";
    EXPECT_FALSE(BuildPortEventQuery("key", "nosuchkey", q, err));
    EXPECT_NE(err.find("nosuchkey"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Tape, border, beeper: changes
// ---------------------------------------------------------------------------

TEST(TTDPortSearch, EarReportsEveryChangeTheProgramSaw)
{
    Journals j;
    for (const uint8_t v : {0xFF, 0xFF, 0xBF, 0xBF, 0xBE, 0xFF, 0xFF})  // bit 6 flips twice; key bits do not count
        j.In(0x7FFE, v);
    const auto r = j.Find("ear");
    ASSERT_EQ(r.hits.size(), 2u);
    EXPECT_EQ(r.hits[0].index, 2u);
    EXPECT_EQ(r.hits[1].index, 5u);
}

/// The ULA decodes A0 alone: a program writing the border through OUT (C)
/// with any B writes one port. Found on Dizzy X, which alternates #10FE and
/// #00FE
TEST(TTDPortSearch, BorderChangesAcrossHighBytes)
{
    Journals j;
    j.Out(0x10FE, 0x11);
    j.Out(0x00FE, 0x01);  // beeper off
    j.Out(0x10FE, 0x13);  // beeper on, magenta
    EXPECT_EQ(Values(j.Find("beeper")), std::vector<uint8_t>({0x01, 0x13}));
    EXPECT_EQ(Values(j.Find("border")), std::vector<uint8_t>({0x13}));
}

TEST(TTDPortSearch, BorderAndBeeperChanges)
{
    Journals j;
    j.Out(0x00FE, 0x01);  // blue
    j.Out(0x00FE, 0x11);  // beeper on, still blue
    j.Out(0x00FE, 0x12);  // red
    j.Out(0x7FFD, 0x10);  // not #FE
    j.Out(0x00FE, 0x02);  // beeper off
    EXPECT_EQ(Values(j.Find("border")), std::vector<uint8_t>({0x12}));
    EXPECT_EQ(Values(j.Find("beeper")), std::vector<uint8_t>({0x11, 0x02}));
}

// ---------------------------------------------------------------------------
// AY: the register selection is followed through the OUT journal
// ---------------------------------------------------------------------------

TEST(TTDPortSearch, AyWritesOfOneRegister)
{
    Journals j;
    j.Out(0xFFFD, 7);
    j.Out(0xBFFD, 0x38);  // R7 = #38   <- hit
    j.Out(0xFFFD, 8);
    j.Out(0xBFFD, 0x0F);  // R8
    j.Out(0xFFFD, 0xFE);  // TurboSound chip select: the selection stays
    j.Out(0xBFFD, 0x0E);  // R8 on chip 1
    j.Out(0xFFFD, 7);
    j.Out(0xBFFD, 0x3F);  // R7 = #3F   <- hit
    const auto r = j.Find("ay-write", "7");
    EXPECT_EQ(Values(r), std::vector<uint8_t>({0x38, 0x3F}));
    for (const auto& h : r.hits)
        EXPECT_EQ(h.ayRegister, 7);
    EXPECT_EQ(Values(j.Find("ay-write", "8")), std::vector<uint8_t>({0x0F, 0x0E}));
    EXPECT_EQ(j.Find("ay-write").hits.size(), 4u) << "any register";
    EXPECT_EQ(j.Find("ay-select", "7").hits.size(), 2u);
    EXPECT_EQ(j.Find("ay-select").hits.size(), 4u) << "every #FFFD write, the chip select included";
}

/// A read of #FFFD returns the register selected by the last OUT before it -
/// the IN and OUT journals are merged in time order
TEST(TTDPortSearch, AyReadsOfOneRegisterFollowTheSelectionInTime)
{
    Journals j;
    j.Out(0xFFFD, 14);
    j.In(0xFFFD, 0xAA);  // R14   <- hit
    j.Out(0xFFFD, 3);
    j.In(0xFFFD, 0x05);  // R3
    j.Out(0xFFFD, 14);
    j.In(0xFFFD, 0xAB);  // R14   <- hit
    const auto r = j.Find("ay-read", "14");
    EXPECT_EQ(Values(r), std::vector<uint8_t>({0xAA, 0xAB}));

    TTDPortQuery q;
    std::string err;
    EXPECT_FALSE(BuildPortEventQuery("ay-read", "16", q, err));
    EXPECT_TRUE(BuildPortEventQuery("ay-read", "0x0E", q, err)) << err;
    EXPECT_EQ(q.ayRegister, 14);
}

// ---------------------------------------------------------------------------
// Window, limit, order
// ---------------------------------------------------------------------------

TEST(TTDPortSearch, LimitWindowAndNewestFirst)
{
    Journals j;
    for (int f = 0; f < 5; f++)
    {
        j.Out(0x00FE, static_cast<uint8_t>(f));      // border change every frame (f=0 is the first: no hit)
        j.NextFrame();
    }
    // Border changes at frames 6..9 (values 1..4)
    auto r = j.Find("border", "", 2);
    EXPECT_EQ(Values(r), std::vector<uint8_t>({1, 2}));
    EXPECT_TRUE(r.truncated);

    r = j.Find("border", "", 2, /*newestFirst=*/true);
    EXPECT_EQ(Values(r), std::vector<uint8_t>({4, 3}));
    EXPECT_TRUE(r.truncated);

    TTDPortQuery q;
    std::string err;
    ASSERT_TRUE(BuildPortEventQuery("border", "", q, err));
    q.from = {7, 0};
    q.to = {8, UINT32_MAX};
    r = SearchPortEvents(j.reads, j.writes, q);
    EXPECT_EQ(Values(r), std::vector<uint8_t>({2, 3})) << "the window; the change at 7 still compares with frame 6";
    EXPECT_FALSE(r.truncated);
}

TEST(TTDPortSearch, RawFiltersEveryAccess)
{
    Journals j;
    j.In(0x001F, 0x01);  // Kempston joystick: right
    j.In(0x001F, 0x00);
    j.In(0x00FE, 0xFF);
    TTDPortQuery q;
    std::string err;
    ASSERT_TRUE(BuildPortEventQuery("in", "", q, err));
    q.portMask = 0x00FF;
    q.portValue = 0x001F;
    q.valueMatch = ttd::TTDValueMatch::AnyBitSet;
    q.valueMask = 0x1F;
    const auto r = SearchPortEvents(j.reads, j.writes, q);
    ASSERT_EQ(r.hits.size(), 1u);
    EXPECT_EQ(r.hits[0].record.value, 0x01);
    EXPECT_EQ(r.scanned, 3u);
}

TEST(TTDPortSearch, BadQueriesAreRefused)
{
    Journals j;
    TTDPortQuery q;
    std::string err;
    EXPECT_FALSE(BuildPortEventQuery("nosuchevent", "", q, err));
    EXPECT_FALSE(BuildPortEventQuery("ear", "x", q, err));
    q.limit = 0;
    EXPECT_FALSE(SearchPortEvents(j.reads, j.writes, q).ok);
    q.limit = 1;
    q.from = {9, 0};
    q.to = {8, 0};
    EXPECT_FALSE(SearchPortEvents(j.reads, j.writes, q).ok);
}

// ---------------------------------------------------------------------------
// Text options, the same on every surface
// ---------------------------------------------------------------------------

TEST(TTDPortSearch, TextOptionsOverrideTheEvent)
{
    TTDPortQuery q;
    std::string err;
    ASSERT_TRUE(BuildPortEventQuery("in", "", q, err));
    ASSERT_TRUE(ttd::ApplyPortQueryOption(q, "port", "#1F", err)) << err;
    EXPECT_EQ(q.portMask, 0xFFFF);
    EXPECT_EQ(q.portValue, 0x001F);
    ASSERT_TRUE(ttd::ApplyPortQueryOption(q, "port_mask", "0x00FF", err)) << err;
    EXPECT_EQ(q.portMask, 0x00FF);
    ASSERT_TRUE(ttd::ApplyPortQueryOption(q, "value", "$10", err)) << err;
    EXPECT_EQ(q.value, 0x10);
    EXPECT_EQ(q.valueMatch, ttd::TTDValueMatch::Equals) << "a value alone means equals";
    ASSERT_TRUE(ttd::ApplyPortQueryOption(q, "match", "any-set", err)) << err;
    ASSERT_TRUE(ttd::ApplyPortQueryOption(q, "trigger", "rising", err)) << err;
    ASSERT_TRUE(ttd::ApplyPortQueryOption(q, "from", "120:3500", err)) << err;
    EXPECT_EQ(q.from.frame, 120u);
    EXPECT_EQ(q.from.tInFrame, 3500u);
    ASSERT_TRUE(ttd::ApplyPortQueryOption(q, "to", "130", err)) << err;
    EXPECT_EQ(q.to.frame, 130u);
    ASSERT_TRUE(ttd::ApplyPortQueryOption(q, "limit", "5", err)) << err;
    ASSERT_TRUE(ttd::ApplyPortQueryOption(q, "newest", "true", err)) << err;
    EXPECT_TRUE(q.newestFirst);

    EXPECT_FALSE(ttd::ApplyPortQueryOption(q, "limit", "0", err));
    EXPECT_FALSE(ttd::ApplyPortQueryOption(q, "value", "256", err));
    EXPECT_FALSE(ttd::ApplyPortQueryOption(q, "trigger", "sometimes", err));
    EXPECT_FALSE(ttd::ApplyPortQueryOption(q, "colour", "red", err));
    EXPECT_NE(err.find("colour"), std::string::npos);
}
