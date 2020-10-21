#include "pch.h"

#include "disassembler_test.h"

#include <vector>
#include <common/stringhelper.h>

/// region <SetUp / TearDown>

void Disassembler_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _disasm = new Z80DisassemblerCUT();
}

void Disassembler_Test::TearDown()
{
    if (_disasm != nullptr)
    {
        delete _disasm;
        _disasm = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(Disassembler_Test, parseOperands)
{
    static char message[256];
    static constexpr uint8_t ERROR_OPERANDS = 255;

    std::vector<std::string> testMnemonics =
    {
        "ld a,:1",
        "ld bc,:4",         // Invalid. Exception expected
        "ld b,:0",          // Invalid. Exception expected
        "ld (iy+:1),:1",
        "ld (ix+:4),:1",    // Invalid. Exception expected
        "ld (iy+:1),:4",    // Invalid. Exception expected
        "ld (iy+:0),:4"     // Invalid. Exception expected
    };

    std::vector<uint8_t> referenceOperands =
    {
        1,
        ERROR_OPERANDS,    // Exception marker. Means - test mnemonics should trigger logic_error exception
        ERROR_OPERANDS,    // Exception marker. Means - test mnemonics should trigger logic_error exception
        2,
        ERROR_OPERANDS,    // Exception marker. Means - test mnemonics should trigger logic_error exception
        ERROR_OPERANDS,    // Exception marker. Means - test mnemonics should trigger logic_error exception
        ERROR_OPERANDS,    // Exception marker. Means - test mnemonics should trigger logic_error exception
    };

    /// region <Test data sanity check>

    if (testMnemonics.size() != referenceOperands.size())
    {
        std::string message = StringHelper::Format("Number of elements in testMnemonics(%d) should be equal to referenceOperands(%d)", testMnemonics.size(), referenceOperands.size());
        FAIL() << message << std::endl;
    }

    /// endregion </Test data sanity check>

    int i = 0;
    for (auto& mnemonic : testMnemonics)
    {
        uint8_t referenceOperandNumber = referenceOperands[i];

        vector<uint8_t> result;
        uint8_t resultValue = 0;

        try
        {
            result = _disasm->parseOperands(mnemonic);
            resultValue = result.size();
        }
        catch (std::logic_error e)
        {
            std::cout << e.what() << std::endl;
            resultValue = ERROR_OPERANDS;
        }


        if (resultValue != referenceOperandNumber)
        {
            if (referenceOperandNumber == ERROR_OPERANDS)
            {
                snprintf(message, sizeof message, "Iteration %d. Mnemonic '%s'. Expected exception due to incorrect number of operands, found %d", i,
                         mnemonic.c_str(), resultValue);
            }
            else
            {
                snprintf(message, sizeof message, "Iteration %d. Mnemonic '%s'. Expected %d operands, found %d", i,
                         mnemonic.c_str(), referenceOperandNumber, resultValue);
            }

            EXPECT_EQ(referenceOperandNumber, resultValue) << message << std::endl;
        }

        i++;
    }
}

TEST_F(Disassembler_Test, formatOperandString)
{
    static char message[256];
    static constexpr const char* ERROR_OPERANDS = "<FAIL>";

    std::vector<std::string> testMnemonics =
    {
            "ld a,:1",
            "ld bc,:4",         // Invalid. Exception expected
            "ld b,:0",          // Invalid. Exception expected
            "ld (iy+:1),:1",
            "ld (ix+:4),:1",    // Invalid. Exception expected
            "ld (iy+:1),:4",    // Invalid. Exception expected
            "ld (iy+:0),:4",    // Invalid. Exception expected
            "ld de,:2",
    };

    std::vector<std::vector<uint16_t>> testValues =
    {
        { 0xBEEF },
        { 0x0000 },
        { 0x0000 },
        { 0xDEAD, 0xBEEF },
        { 0x0000 },
        { 0x0000 },
        { 0x0000 },
        { 0xEDD0 },
    };

    std::vector<std::string> referenceResults =
    {
        "ld a,#EF",
        ERROR_OPERANDS,
        ERROR_OPERANDS,
        "ld (iy+#AD),#EF",
        ERROR_OPERANDS,
        ERROR_OPERANDS,
        ERROR_OPERANDS,
        "ld de,#EDD0"
    };

    /// region <Test data sanity check>

    if (testMnemonics.size() != testValues.size())
    {
        std::string message = StringHelper::Format("Number of elements in testMnemonics(%d) should be equal to testValues(%d)", testMnemonics.size(), testValues.size());
        FAIL() << message << std::endl;
    }

    if (testMnemonics.size() != referenceResults.size())
    {
        std::string message = StringHelper::Format("Number of elements in testMnemonics(%d) should be equal to referenceResults(%d)", testMnemonics.size(), referenceResults.size());
        FAIL() << message << std::endl;
    }

    /// endregion </Test data sanity check>

    int i = 0;
    for (auto& mnemonic : testMnemonics)
    {
        std::string result;

        vector<uint16_t> values = testValues[i];
        std::string referenceResult = referenceResults[i];

        try
        {
            result = _disasm->formatOperandString(mnemonic, values);
        }
        catch (std::logic_error e)
        {
            std::cout << e.what() << std::endl;
            result = ERROR_OPERANDS;
        }

        if (result != referenceResult)
        {
            if (referenceResult == ERROR_OPERANDS)
            {
                snprintf(message, sizeof message, "Iteration %d. Mnemonic '%s'. Expected exception due to incorrect number of operands, found %s", i,
                         mnemonic.c_str(), result.c_str());
            }
            else
            {
                snprintf(message, sizeof message, "Iteration %d. Mnemonic '%s'. Expected result '%s', got '%s'", i,
                         mnemonic.c_str(), referenceResult.c_str(), result.c_str());
            }

            EXPECT_EQ(referenceResult, result) << message << std::endl;
        }


        i++;
    }
}