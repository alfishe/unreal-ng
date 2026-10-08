// Label tables in RAM (testdata/symbols/live): pages dumped from unreal-ng after ALASM 5.09 / 4.44 and XAS 7.447 /
// 4.18 assembled the sources next to them; each scanner finds its table where the research found it and reads the
// labels the assembler made (the *.expected.txt lists, macro and undefined entries marked)

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <sstream>

#include "testdata.h"
#include "unrealasm/symbols/live.h"

using namespace unrealasm;
using namespace unrealasm::symbols;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
/// "NAME #HHHH" lines of the defined labels (entries marked macro / nodef / wrong are not symbols)
std::vector<std::pair<std::string, uint32_t>> Expected(const std::string& file, bool inReadOrder)
{
    std::vector<std::pair<std::string, uint32_t>> out;
    std::istringstream in(ReadTestText("symbols/live/" + file));
    std::string name, value, mark;
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream fields(line);
        mark.clear();
        fields >> name >> value >> mark;
        if (!mark.empty())
            continue;
        out.push_back({name, static_cast<uint32_t>(std::stoul(value.substr(1), nullptr, 16))});
    }
    if (!inReadOrder)
        std::reverse(out.begin(), out.end());   // the ALASM lists are written newest first, as the table holds them
    return out;
}

struct Case
{
    const char* dump;
    uint16_t page;
    const char* expected;
    const char* version;
    uint32_t offset;
    bool listOrder;   // the expected list is in the order Read gives (XAS); ALASM's is reversed
};

constexpr Case kCases[] = {
    {"alasm509-lta-ram3.bin", 3, "alasm509-lta.expected.txt", "5.0x", 0x3D8A, false},
    {"alasm509-ltb-ram3.bin", 3, "alasm509-ltb.expected.txt", "5.0x", 0x3DC3, false},
    {"alasm444-lta-ram3.bin", 3, "alasm444-lta.expected.txt", "4.4x", 0x3F0A, false},
    {"alasm38c-ltc-ram3.bin", 3, "alasm-ltc.expected.txt", "4.4x", 0x3F2E, false},
    {"alasm45-ltc-ram6.bin", 6, "alasm-ltc.expected.txt", "4.4x", 0x3F2E, false},
    {"xas7447-constrct-ram6.bin", 6, "xas7447-constrct.expected.txt", "7.x", 0x1FFF, true},
    {"xas7447-xprobe-ram6.bin", 6, "xas7447-xprobe.expected.txt", "7.x", 0x1FFF, true},
    {"xas418-cons418-ram6.bin", 6, "xas418-cons418.expected.txt", "4.x", 0x0B16, true},
    {"xas418-xprobe-ram6.bin", 6, "xas418-xprobe.expected.txt", "4.x", 0x0B16, true},
};
}  // namespace

TEST(Live_Test, EachTableIsFoundWhereTheAssemblerKeepsItAndReadsBack)
{
    for (const Case& c : kCases)
    {
        const std::vector<uint8_t> page = ReadTestData(std::string("symbols/live/") + c.dump);
        ASSERT_EQ(page.size(), 16384u) << c.dump;
        const MemoryView memory{{{c.page, page}}};
        const std::vector<LiveCandidate> found = FindLabelTables(memory);
        ASSERT_FALSE(found.empty()) << c.dump;
        const LiveCandidate& best = found[0];
        EXPECT_EQ(best.version, c.version) << c.dump;
        EXPECT_EQ(best.page, c.page) << c.dump;
        EXPECT_EQ(best.offset, c.offset) << c.dump;
        const LiveReadResult r = ReadLabelTable(memory, best);
        ASSERT_TRUE(r.ok) << c.dump;
        const auto expected = Expected(c.expected, c.listOrder);
        ASSERT_EQ(r.set.symbols.size(), expected.size()) << c.dump;
        for (size_t k = 0; k < expected.size(); ++k)
        {
            EXPECT_EQ(r.set.symbols[k].name, expected[k].first) << c.dump;
            EXPECT_EQ(r.set.symbols[k].location.offset, expected[k].second) << c.dump << " " << expected[k].first;
            EXPECT_EQ(r.set.symbols[k].location.space.kind, SpaceKind::CpuView);
        }
        EXPECT_EQ(r.set.origin.kind, "live");
    }
}

TEST(Live_Test, MacroAndUndefinedEntriesAreReportedNotRead)
{
    const std::vector<uint8_t> page = ReadTestData("symbols/live/alasm509-lta-ram3.bin");
    const MemoryView memory{{{3, page}}};
    const LiveReadResult r = ReadLabelTable(memory, FindLabelTables(memory).at(0));
    bool macro = false, undefined = false;
    for (const Diagnostic& d : r.diagnostics)
    {
        macro = macro || d.message.find("macro") != std::string::npos;
        undefined = undefined || d.message.find("not defined") != std::string::npos;
    }
    EXPECT_TRUE(macro);
    EXPECT_TRUE(undefined);
    for (const Symbol& s : r.set.symbols)
        EXPECT_TRUE(s.name != "MAC1" && s.name != "UNDEF") << s.name;
}

TEST(Live_Test, SeveralPagesAtOnceAndPagesWithoutATable)
{
    const std::vector<uint8_t> alasm = ReadTestData("symbols/live/alasm509-ltb-ram3.bin");
    const std::vector<uint8_t> xas = ReadTestData("symbols/live/xas7447-constrct-ram6.bin");
    std::vector<uint8_t> zeros(16384, 0);
    std::vector<uint8_t> noise(16384);
    std::mt19937 random(7);
    for (uint8_t& b : noise)
        b = static_cast<uint8_t>(random());
    const MemoryView memory{{{0, zeros}, {1, noise}, {3, alasm}, {6, xas}}};
    const std::vector<LiveCandidate> found = FindLabelTables(memory);
    bool alasmFound = false, xasFound = false;
    for (const LiveCandidate& c : found)
    {
        EXPECT_NE(c.page, 0) << "a table in a page of zeros";
        if (c.page == 1)
        {
            EXPECT_LT(c.score, 50) << c.scanner << " in noise at " << c.offset;
        }
        alasmFound = alasmFound || (c.scanner == "alasm-table" && c.page == 3 && c.offset == 0x3DC3);
        xasFound = xasFound || (c.scanner == "xas-table" && c.page == 6 && c.version == "7.x");
    }
    EXPECT_TRUE(alasmFound);
    EXPECT_TRUE(xasFound);
    EXPECT_TRUE(FindLabelTables(MemoryView{{{0, zeros}}}).empty());
}

TEST(Live_Test, AlasmTableOverTwoPagesOfAPentagon512)
{
    // 1800 labels: ALASM 5.09 filled #FDFF-#C0FB of its first symbol page (RAM 11, INFO's #43) and went on from the top
    // of the second (RAM 27, #C3); on 128K both are page 3 and the second part overwrites the first
    const std::vector<uint8_t> upper = ReadTestData("symbols/live/alasm509-big-ram11.bin");
    const std::vector<uint8_t> lower = ReadTestData("symbols/live/alasm509-big-ram27.bin");
    const MemoryView memory{{{11, upper}, {27, lower}}};
    const std::vector<LiveCandidate> found = FindLabelTables(memory);
    ASSERT_FALSE(found.empty());
    const LiveCandidate& best = found[0];
    EXPECT_EQ(best.page, 11);
    EXPECT_EQ(best.lowerPage, 27);
    EXPECT_EQ(best.split, 0x00FBu);
    EXPECT_EQ(best.count, 1800u);
    const LiveReadResult r = ReadLabelTable(memory, best);
    ASSERT_TRUE(r.ok);
    const auto expected = Expected("alasm509-big.expected.txt", false);
    ASSERT_EQ(r.set.symbols.size(), expected.size());
    for (size_t k = 0; k < expected.size(); ++k)
    {
        EXPECT_EQ(r.set.symbols[k].name, expected[k].first);
        EXPECT_EQ(r.set.symbols[k].location.offset, expected[k].second) << expected[k].first;
    }
    EXPECT_EQ(r.set.symbols.front().provenance.raw.rfind("ram11:", 0), 0u);   // L0000, the oldest, at the top of RAM 11
    EXPECT_EQ(r.set.symbols.back().provenance.raw.rfind("ram27:", 0), 0u);
    // One page alone gives its part only
    const MemoryView alone{{{11, upper}}};
    EXPECT_EQ(FindLabelTables(alone).at(0).count, 1562u);
}
