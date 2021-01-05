#include "pch.h"

#include "dumphelper_test.h"

#include "common/dumphelper.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <exception>

using namespace std;

/// region <SetUp / TearDown>

void DumpHelper_Test::SetUp()
{

}

void DumpHelper_Test::TearDown()
{

}

/// endregion </SetUp / TearDown>

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