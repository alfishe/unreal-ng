#include "pch.h"

#include "bithelper_test.h"

#include "common/bithelper.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <exception>

using namespace std;

/// region <SetUp / TearDown>

void BitHelper_Test::SetUp()
{

}

void BitHelper_Test::TearDown()
{

}

/// endregion </SetUp / TearDown>

TEST_F(BitHelper_Test, GetFirstSetBitPosition8)
{
    uint8_t inputValue = 0;
    uint8_t resultValue = 0;

    for (uint8_t i = 0; i < 8; i++)
    {
        inputValue = 1 << i;
        resultValue = BitHelper::GetFirstSetBitPosition(inputValue);

        EXPECT_EQ(resultValue, i);
    }
}

TEST_F(BitHelper_Test, GetFirstSetBitPosition16)
{
    uint16_t inputValue = 0;
    uint8_t resultValue = 0;

    for (uint8_t i = 0; i < 16; i++)
    {
        inputValue = 1 << i;
        resultValue = BitHelper::GetFirstSetBitPosition(inputValue);

        EXPECT_EQ(resultValue, i);
    }
}

TEST_F(BitHelper_Test, GetLastSetBitPosition8)
{
    uint8_t inputValue = 0;
    uint8_t resultValue = 0;

    for (int i = 7; i >= 0; i--)
    {
        inputValue = 1 << i;
        resultValue = BitHelper::GetLastSetBitPosition(inputValue);

        EXPECT_EQ(resultValue, i);
    }
}

TEST_F(BitHelper_Test, GetLastSetBitPosition16)
{
    uint16_t inputValue = 0;
    uint8_t resultValue = 0;

    for (int i = 15; i >= 0; i--)
    {
        inputValue = 1 << i;
        resultValue = BitHelper::GetLastSetBitPosition(inputValue);

        EXPECT_EQ(resultValue, i);
    }
}

TEST_F(BitHelper_Test, CountSetBits8)
{
    uint8_t resultValue = 0;

    // Single bit set
    for (uint16_t i = 0x0001; i < 0x0100; i <<= 1)
    {
        uint8_t value = static_cast<uint8_t>(i);
        resultValue = BitHelper::CountSetBits(value);

        EXPECT_EQ(resultValue, 1);
    }

    // All except single bit
    for (uint16_t i = 0x0001; i < 0x0100; i <<= 1)
    {
        uint8_t value = static_cast<uint8_t>(~i);
        resultValue = BitHelper::CountSetBits(value);

        EXPECT_EQ(resultValue, 7);
    }
}

TEST_F(BitHelper_Test, CountSetBits16)
{
    uint8_t resultValue = 0;

    // Single bit set
    for (uint32_t i = 0x0000'0001; i < 0x0001'0000; i <<= 1)
    {
        uint16_t value = static_cast<uint16_t>(i);
        resultValue = BitHelper::CountSetBits(value);

        EXPECT_EQ(resultValue, 1);
    }

    // All except single bit
    for (uint32_t i = 0x0000'0001; i < 0x0001'0000; i <<= 1)
    {
        uint16_t value = static_cast<uint16_t>(~i);
        resultValue = BitHelper::CountSetBits(value);

        EXPECT_EQ(resultValue, 15);
    }
}