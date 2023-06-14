#include <gtest/gtest.h>
#include <array>
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "debugger/analyzers/basicextractor.h"

class BasicExtractorTest : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
};

TEST_F(BasicExtractorTest, extractBasic)
{
    const std::vector<std::vector<uint8_t>> testVector =
    {
        { 0x8C, 0x8E, 0x80, 0x9C, 0x14, 0xA1},  // 10 LET $a=20
    };
}
