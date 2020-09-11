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
