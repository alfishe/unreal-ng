#include "pch.h"

#include "dumphelper_test.h"

#include "common/dumphelper.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <exception>

/// region <SetUp / TearDown>

void DumpHelper_Test::SetUp()
{

}

void DumpHelper_Test::TearDown()
{

}

/// endregion </SetUp / TearDown>

// Helper accessor for DumpHelper::width for testing
class DumpHelperTestAccessor : public DumpHelper {
public:
    static void SetWidth(int w) { width = w; }
    static int GetWidth() { return width; }
};

TEST_F(DumpHelper_Test, HexDumpBuffer)
{
    uint8_t testBuffer[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF };

    std::string result1 = DumpHelper::HexDumpBuffer(testBuffer, 4);
    std::string reference1 = "01 02 04 08";
    EXPECT_EQ(reference1, result1);

    std::string result2 = DumpHelper::HexDumpBuffer(testBuffer, 4, ", ", "0x");
    std::string reference2 = "0x01, 0x02, 0x04, 0x08";
    EXPECT_EQ(reference2, result2);

    std::string result3 = DumpHelper::HexDumpBuffer(testBuffer, 9, " ", "$");
    std::string reference3 = "$01 $02 $04 $08 $10 $20 $40 $80 $FF";
    EXPECT_EQ(reference3, result3);
}

TEST_F(DumpHelper_Test, HexDumpBuffer_EmptyBuffer)
{
    uint8_t testBuffer[] = {};
    std::string result = DumpHelper::HexDumpBuffer(testBuffer, 0);
    EXPECT_EQ(result, "");
}

TEST_F(DumpHelper_Test, HexDumpBuffer_SingleByte)
{
    uint8_t testBuffer[] = {0xAB};
    std::string result = DumpHelper::HexDumpBuffer(testBuffer, 1);
    EXPECT_EQ(result, "AB");

    result = DumpHelper::HexDumpBuffer(testBuffer, 1, ", ", "0x");
    EXPECT_EQ(result, "0xAB");
}

TEST_F(DumpHelper_Test, HexDumpBuffer_CustomDelimiter)
{
    uint8_t testBuffer[] = {0x01, 0x02, 0x03};
    std::string result = DumpHelper::HexDumpBuffer(testBuffer, 3, "-", "");
    EXPECT_EQ(result, "01-02-03");
}

TEST_F(DumpHelper_Test, HexDumpBuffer_CustomPrefix)
{
    uint8_t testBuffer[] = {0x0A, 0x0B, 0x0C};
    std::string result = DumpHelper::HexDumpBuffer(testBuffer, 3, " ", "#");
    EXPECT_EQ(result, "#0A #0B #0C");
}

TEST_F(DumpHelper_Test, HexDumpBuffer_MultiLine)
{
    // Set width to 4 for this test
    int oldWidth = DumpHelperTestAccessor::GetWidth();
    DumpHelperTestAccessor::SetWidth(4);
    uint8_t testBuffer[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    std::string result = DumpHelper::HexDumpBuffer(testBuffer, 5);
    // Should wrap after 4 bytes
    EXPECT_EQ(result, "10 20 30 40\n50");
    DumpHelperTestAccessor::SetWidth(oldWidth);
}

TEST_F(DumpHelper_Test, HexDumpBuffer_LongDelimiterAndPrefix)
{
    uint8_t testBuffer[] = {0x01, 0x02};
    std::string result = DumpHelper::HexDumpBuffer(testBuffer, 2, "--", "PRE:");
    EXPECT_EQ(result, "PRE:01--PRE:02");
}

TEST_F(DumpHelper_Test, HexDumpBuffer_UppercaseOutput)
{
    uint8_t testBuffer[] = {0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f};
    std::string result = DumpHelper::HexDumpBuffer(testBuffer, 6);
    EXPECT_EQ(result, "0A 1B 2C 3D 4E 5F");
    // Ensure no lowercase letters are present
    for (char c : result)
    {
        ASSERT_FALSE(c >= 'a' && c <= 'f');
    }
}

TEST_F(DumpHelper_Test, HexDumpBuffer_UppercaseWithPrefix)
{
    uint8_t testBuffer[] = {0xab, 0xcd};
    std::string result = DumpHelper::HexDumpBuffer(testBuffer, 2, " ", "0x");
    EXPECT_EQ(result, "0xAB 0xCD");
    // Ensure no lowercase letters are present
    for (char c : result)
    {
        ASSERT_FALSE(c >= 'a' && c <= 'f');
    }
}

TEST_F(DumpHelper_Test, HexDumpBuffer_DoesNotProduceLowercase)
{
    uint8_t testBuffer[] = {0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    std::string result = DumpHelper::HexDumpBuffer(testBuffer, 6);
    // Check that lowercase hex digits are not present
    for (char c : result)
    {
        ASSERT_TRUE(c < 'a' || c > 'f');
    }
}