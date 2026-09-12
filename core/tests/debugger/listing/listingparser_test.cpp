/// @file listingparser_test.cpp
/// @brief Unit tests for the sjasmplus .lst ListingParser (M7h / M9).
///
/// Writes small .lst fixtures to the runtime directory and verifies the
/// row grammar: line-number + address + hex-pair byte tokens + source text,
/// continuation-row merging (explicit line number and anonymous address-only
/// rows), address→line mapping, statistics, and file error handling.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "debugger/listing/listingparser.h"
#include "emulator/emulatorcontext.h"

namespace
{

/// Basic listing: 7 source lines, 6 emitting code (line 4 is a bare label)
const char* const kBasicListing = "1 8000: C3 05 80 jp start\n"
                                  "2 8003: 00        nop\n"
                                  "3 8004: AF        xor a\n"
                                  "4 start:\n"
                                  "5 8005: D3 FE     out (0xFE),a\n"
                                  "6 8007: 3D        dec a\n"
                                  "7 8008: 18 FB     jr start\n";

const char* const kMergedListing = "5 8005: 01 02 03 04\n"
                                   "5 8009: 05 06\n";

const char* const kAnonymousContinuation = "1 8000: 01 02\n"
                                           "   8002: 03\n";

class ListingParser_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _parser = new ListingParser(_context);
    }

    void TearDown() override
    {
        delete _parser;
        delete _context;
        for (const std::string& path : _tempFiles)
        {
            std::remove(path.c_str());
        }
    }

    /// Writes fixture text to a file in the runtime directory and returns the path
    std::string WriteListing(const std::string& name, const char* content)
    {
        std::string path = name;
        std::ofstream out(path, std::ios::binary);
        out << content;
        out.close();
        _tempFiles.push_back(path);
        return path;
    }

    EmulatorContext* _context = nullptr;
    ListingParser* _parser = nullptr;
    std::vector<std::string> _tempFiles;
};

} // namespace

TEST_F(ListingParser_Test, ParsesBasicListing_StatisticsAndLookup)
{
    std::string path = WriteListing("test_listing_basic.lst", kBasicListing);
    ASSERT_TRUE(_parser->LoadListing(path));

    EXPECT_TRUE(_parser->IsLoaded());
    EXPECT_EQ(_parser->GetSourcePath(), path);
    EXPECT_EQ(_parser->GetLineCount(), 7u);
    EXPECT_EQ(_parser->GetCodeLineCount(), 6u);
    EXPECT_EQ(_parser->GetTotalBytes(), 10u); // 3+1+1+2+1+2
    EXPECT_EQ(_parser->GetMinAddress(), 0x8000);
    EXPECT_EQ(_parser->GetMaxAddress(), 0x8009); // 0x8008 + 2 bytes - 1
}

TEST_F(ListingParser_Test, FindLineByNumber_ReturnsSourceAndBytes)
{
    ASSERT_TRUE(_parser->LoadListing(WriteListing("test_listing_lookup.lst", kBasicListing)));

    const ListingLine* line = _parser->FindLineByNumber(5);
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(line->hasCode);
    EXPECT_EQ(line->addressStart, 0x8005);
    EXPECT_EQ(line->bytes.size(), 2u);
    EXPECT_EQ(line->bytes[0], 0xD3);
    EXPECT_EQ(line->bytes[1], 0xFE);
    EXPECT_EQ(line->source, "out (0xFE),a");

    // Bare label line has no code
    const ListingLine* label = _parser->FindLineByNumber(4);
    ASSERT_NE(label, nullptr);
    EXPECT_FALSE(label->hasCode);
    EXPECT_EQ(label->source, "start:");

    EXPECT_EQ(_parser->FindLineByNumber(99), nullptr);
}

TEST_F(ListingParser_Test, FindLineByAddress_MapsEveryEmittedByte)
{
    ASSERT_TRUE(_parser->LoadListing(WriteListing("test_listing_addr.lst", kBasicListing)));

    // 0x8005-0x8006 belong to line 5 (out (0xFE),a)
    EXPECT_EQ(_parser->FindLineByAddress(0x8005)->lineNumber, 5);
    EXPECT_EQ(_parser->FindLineByAddress(0x8006)->lineNumber, 5);
    // 0x8000-0x8002 belong to line 1 (jp start)
    EXPECT_EQ(_parser->FindLineByAddress(0x8000)->lineNumber, 1);
    EXPECT_EQ(_parser->FindLineByAddress(0x8002)->lineNumber, 1);

    // Address outside the listing → null
    EXPECT_EQ(_parser->FindLineByAddress(0x9000), nullptr);
}

TEST_F(ListingParser_Test, SameLineNumberRows_MergeIntoOneLine)
{
    ASSERT_TRUE(_parser->LoadListing(WriteListing("test_listing_merge.lst", kMergedListing)));

    EXPECT_EQ(_parser->GetLineCount(), 1u);
    const ListingLine* line = _parser->FindLineByNumber(5);
    ASSERT_NE(line, nullptr);
    ASSERT_EQ(line->bytes.size(), 6u);
    EXPECT_EQ(line->addressStart, 0x8005);
    EXPECT_EQ(line->addressEnd, 0x800A); // 0x8009 + 2 - 1
    EXPECT_EQ(line->bytes[4], 0x05);
    EXPECT_EQ(line->bytes[5], 0x06);
}

TEST_F(ListingParser_Test, AnonymousAddressRow_AppendsToPreviousLine)
{
    ASSERT_TRUE(_parser->LoadListing(WriteListing("test_listing_anon.lst", kAnonymousContinuation)));

    const ListingLine* line = _parser->FindLineByNumber(1);
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->bytes.size(), 3u);
    EXPECT_EQ(line->addressEnd, 0x8002);
}

TEST_F(ListingParser_Test, MissingFile_ReturnsFalse)
{
    EXPECT_FALSE(_parser->LoadListing("test_listing_does_not_exist.lst"));
    EXPECT_FALSE(_parser->IsLoaded());
}

TEST_F(ListingParser_Test, Clear_ResetsState)
{
    ASSERT_TRUE(_parser->LoadListing(WriteListing("test_listing_clear.lst", kBasicListing)));
    ASSERT_TRUE(_parser->IsLoaded());

    _parser->Clear();
    EXPECT_FALSE(_parser->IsLoaded());
    EXPECT_EQ(_parser->GetLineCount(), 0u);
    EXPECT_EQ(_parser->FindLineByNumber(1), nullptr);
}
