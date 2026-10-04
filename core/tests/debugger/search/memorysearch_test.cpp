// MemorySearch (memorysearch.h): the pattern syntax (hex, ?? and nibble wildcards, masks), the three spaces (the
// CPU view, one page, every RAM page), the range, alignment, max and the context around a match. A bare 128K
// memory: RAM 5 at #4000, RAM 2 at #8000, RAM 0 at #C000 after a reset.

#include <gtest/gtest.h>

#include <cstring>

#include "common/modulelogger.h"
#include "debugger/search/memorysearch.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "3rdparty/message-center/messagecenter.h"

class MemorySearch_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->config.ramsize = 128;
        _memory = new Memory(_context);
        _context->pMemory = _memory;
        _memory->Reset();
        for (int p = 0; p < 8; p++)
            std::memset(_memory->RAMPageAddress(static_cast<uint16_t>(p)), 0, PAGE_SIZE);
    }
    void TearDown() override
    {
        delete _memory;
        _context->pMemory = nullptr;
        delete _context;
        MessageCenter::DisposeDefaultMessageCenter();
    }

    void Put(int page, uint16_t offset, std::initializer_list<uint8_t> bytes)
    {
        uint8_t* p = _memory->RAMPageAddress(static_cast<uint16_t>(page)) + offset;
        for (uint8_t b : bytes)
            *p++ = b;
    }

    MemorySearchResult Find(const std::string& pattern, const std::string& space = "", const std::string& mask = "",
                            uint32_t start = 0, uint32_t end = 0xFFFFFFFF, unsigned max = 64, unsigned alignment = 1)
    {
        MemorySearchRequest request;
        std::string error;
        EXPECT_TRUE(MemorySearch::BuildRequest(pattern, nullptr, mask, space, start, end, max, alignment, request, error))
            << error;
        _request = request;
        return MemorySearch::Search(_context, request);
    }

    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    MemorySearchRequest _request;
};

TEST_F(MemorySearch_Test, PatternSyntax)
{
    std::vector<uint8_t> pattern, mask;
    std::string error;
    ASSERT_TRUE(MemorySearch::ParsePattern("cd1600", pattern, mask, error));
    EXPECT_EQ(pattern, (std::vector<uint8_t>{0xCD, 0x16, 0x00}));
    EXPECT_TRUE(mask.empty()) << "an exact pattern needs no mask";
    ASSERT_TRUE(MemorySearch::ParsePattern("0xCD, ?? A?", pattern, mask, error));
    EXPECT_EQ(pattern, (std::vector<uint8_t>{0xCD, 0x00, 0xA0}));
    EXPECT_EQ(mask, (std::vector<uint8_t>{0xFF, 0x00, 0xF0}));
    EXPECT_FALSE(MemorySearch::ParsePattern("ABC", pattern, mask, error)) << "half a byte";
    EXPECT_FALSE(MemorySearch::ParsePattern("?? ??", pattern, mask, error)) << "nothing to match";
    EXPECT_FALSE(MemorySearch::ParsePattern("XY", pattern, mask, error));
    EXPECT_EQ(MemorySearch::NumberPattern(0xAF3C), "AF3C");
    EXPECT_EQ(MemorySearch::NumberPattern(0xF), "0F");
}

TEST_F(MemorySearch_Test, CpuViewWithContext)
{
    Put(2, 0x0100, {0xCD, 0x16, 0x00});   // #8100 through RAM 2
    const MemorySearchResult r = Find("CD 16 00");
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.matches.size(), 1u);
    EXPECT_EQ(r.matches[0].address, 0x8100u);
    EXPECT_EQ(r.matches[0].page, -1);
    EXPECT_EQ(r.matches[0].contextStart, 0x80FCu) << "4 bytes before";
    ASSERT_EQ(r.matches[0].bytes.size(), 11u) << "4 before, 3, 4 after";
    EXPECT_EQ(r.matches[0].bytes[4], 0xCD);
    EXPECT_EQ(MemorySearch::SpaceName(_request), "cpu");
}

TEST_F(MemorySearch_Test, WildcardsAndMasks)
{
    Put(2, 0x0000, {0x21, 0x34, 0x45});   // LD HL,#4534 at #8000
    Put(2, 0x0010, {0x21, 0x34, 0x95});   // LD HL,#9534
    EXPECT_EQ(Find("21 ?? ??").matches.size(), 2u);
    const MemorySearchResult r = Find("21 00 40", "", "FF 00 F0");
    ASSERT_EQ(r.matches.size(), 1u) << "#40xx..#4Fxx only";
    EXPECT_EQ(r.matches[0].address, 0x8000u);
    EXPECT_EQ(Find("21 34 4?").matches.size(), 1u) << "nibble wildcard";
}

TEST_F(MemorySearch_Test, OnePageFindsWhatTheCpuDoesNotSee)
{
    Put(3, 0x2000, {0xDE, 0xAD, 0xBE, 0xEF});   // RAM 3: not paged in
    EXPECT_TRUE(Find("DE AD BE EF").matches.empty()) << "not in the CPU view";
    const MemorySearchResult r = Find("DE AD BE EF", "ram3");
    ASSERT_EQ(r.matches.size(), 1u);
    EXPECT_EQ(r.matches[0].page, 3);
    EXPECT_EQ(r.matches[0].pageType, BANK_RAM);
    EXPECT_EQ(r.matches[0].address, 0x2000u) << "the offset in the page";
    EXPECT_EQ(MemorySearch::SpaceName(_request), "ram3");
}

TEST_F(MemorySearch_Test, AllRamReportsPageAndOffset)
{
    Put(1, 0x0010, {0xAA, 0x55});
    Put(6, 0x3FF0, {0xAA, 0x55});
    Put(6, 0x3FFF, {0xAA});   // RAM 6's last byte and RAM 7's first: not neighbors, no match
    Put(7, 0x0000, {0x55});
    const MemorySearchResult r = Find("AA 55", "ram");
    ASSERT_EQ(r.matches.size(), 2u) << "a match stays inside one page";
    EXPECT_EQ(r.matches[0].page, 1);
    EXPECT_EQ(r.matches[0].address, 0x0010u);
    EXPECT_EQ(r.matches[1].page, 6);
    EXPECT_EQ(r.matches[1].address, 0x3FF0u);
    EXPECT_EQ(r.matches[1].bytes.size(), 4u + 2u + 4u);
    const MemorySearchResult edge = Find("AA", "ram", "", 6 * 0x4000 + 0x3FFF, 6 * 0x4000 + 0x3FFF);
    ASSERT_EQ(edge.matches.size(), 1u);
    EXPECT_EQ(edge.matches[0].bytes.size(), 5u) << "the context ends with the page";
}

TEST_F(MemorySearch_Test, RangeAlignmentAndMax)
{
    for (uint16_t o = 0; o < 10; o++)
        Put(2, static_cast<uint16_t>(0x0200 + o * 3), {0x77});
    EXPECT_EQ(Find("77", "", "", 0x8200, 0x8208).matches.size(), 3u) << "inclusive end";
    EXPECT_EQ(Find("77", "", "", 0x8200, 0x8300, 64, 2).matches.size(), 5u) << "even addresses only";
    const MemorySearchResult r = Find("77", "", "", 0, 0xFFFFFFFF, 4);
    EXPECT_EQ(r.matches.size(), 4u);
    EXPECT_TRUE(r.truncated);
}

TEST_F(MemorySearch_Test, RefusedRequestsSayWhy)
{
    MemorySearchRequest request;
    std::string error;
    EXPECT_FALSE(MemorySearch::BuildRequest("CD 16", nullptr, "FF", "", 0, 0xFFFF, 64, 1, request, error));
    EXPECT_EQ(error, "the mask must be as long as the pattern");
    EXPECT_FALSE(MemorySearch::BuildRequest("CD", nullptr, "", "flash9", 0, 0xFFFF, 64, 1, request, error));
    EXPECT_FALSE(MemorySearch::BuildRequest("CD", nullptr, "", "", 10, 5, 64, 1, request, error));
    ASSERT_TRUE(MemorySearch::BuildRequest("CD", nullptr, "", "ram9", 0, 0xFFFF, 64, 1, request, error));
    EXPECT_EQ(MemorySearch::Search(_context, request).error, "this machine has no page ram9") << "a 128K has RAM 0-7";
    ASSERT_TRUE(MemorySearch::BuildRequest("CD", nullptr, "", "", 0, 0xFFFF, 64, 3, request, error));
    EXPECT_EQ(MemorySearch::Search(_context, request).error, "alignment must be 1 or 2");
}

TEST_F(MemorySearch_Test, StateForTheScriptingSurfaces)
{
    Put(4, 0x0001, {0x12, 0x34});
    MemorySearchRequest request;
    std::string error;
    ASSERT_TRUE(MemorySearch::BuildRequest("12 34", nullptr, "", "ram", 0, 0xFFFFFFFF, 64, 1, request, error));
    const StateNode node = MemorySearch::ToState(request, MemorySearch::Search(_context, request));
    ASSERT_NE(node.find("matches"), nullptr);
    ASSERT_EQ(node.find("matches")->items.size(), 1u);
    const StateNode& m = node.find("matches")->items[0];
    ASSERT_NE(m.find("page"), nullptr);
    EXPECT_EQ(m.find("page")->find("kind")->s, "ram");
    EXPECT_EQ(m.find("page")->find("page")->i, 4);
    EXPECT_EQ(m.find("offset")->i, 1);
    EXPECT_EQ(m.find("context_start")->i, 0) << "clamped at the page's start";
    EXPECT_EQ(node.find("space")->s, "ram");
}
