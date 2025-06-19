#include "pch.h"

#include "disassembler_test.h"

#include <vector>
#include "common/dumphelper.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void Disassembler_Test::SetUp()
{
    _context = new EmulatorContext();
    _disasm = new Z80DisassemblerCUT(_context);
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
#ifdef _DEBUG
            std::cout << e.what();
#endif // _DEBUG
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

            FAIL() << message << std::endl;
        }
        else
        {
#ifdef _DEBUG
            if (referenceOperandNumber == ERROR_OPERANDS)
            {
                std::cout << "    => OK - it was negative scenario test" << std::endl;
            }
#endif // _DEBUG
        }

        i++;
    }
}

TEST_F(Disassembler_Test, formatOperandString)
{
    static char message[256];
    static constexpr const char* ERROR_OPERANDS = "<FAIL>";

    // Pre-parsed mnemonics to use during Z80Disassembler::FormatOperand() method testing. Contain template errors to catch.
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

    // Test values to pass as operands
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

    // Expected reference results
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

        DecodedInstruction decoded;

        try
        {
            // Probe method under test and get result
            result = _disasm->formatOperandString(decoded, mnemonic, values);
        }
        catch (std::logic_error e)
        {
#ifdef _DEBUG
            std::cout << e.what();
#endif // _DEBUG

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

            FAIL() << message << std::endl;
        }
        else
        {
#ifdef _DEBUG
            if (referenceResult == ERROR_OPERANDS)
            {
                std::cout << "    => OK - it was negative scenario test" << std::endl;
            }
#endif // _DEBUG
        }

        i++;
    }
}

TEST_F(Disassembler_Test, disassembleSingleCommand)
{
    static char message[256];
    static constexpr const char* ERROR_OPERANDS = "<FAIL>";

    std::vector<std::vector<uint8_t>> testData =
    {
        { 0x00 },                     // nop
        { 0x01, 0xEF, 0xBE },         // ld bc,#BEEF
        { 0xCB, 0x2F },               // sra a
        { 0xFD, 0x36, 0xBA, 0x13 },   // ld (iy+#BA),#13
        { 0x38, 0x35 },               // jr c,#35
    };

    std::vector<std::string> referenceValues =
    {
        "nop",
        "ld bc,#BEEF",
        "sra a",
        "ld (iy+#BA),#13",
        "jr c,#35",
    };

    int i = 0;
    for (auto& cmd : testData)
    {
        std::string hexCommand = DumpHelper::HexDumpBuffer(cmd.data(), cmd.size());
        std::string referenceResult = referenceValues[i];

        // Probe method under test and get result
        std::string result = _disasm->disassembleSingleCommand(cmd, 0);

        if (result != referenceResult)
        {
            snprintf(message, sizeof message, "Iteration %d. Data '%s'. Expected '%s', found '%s'", i,
                     hexCommand.c_str(), referenceResult.c_str(), result.c_str());

            EXPECT_EQ(referenceResult, result) << message << std::endl;
        }
        else
        {
#ifdef _DEBUG
            std::cout << std::left << std::setw(16) << hexCommand << std::setw(0) << result << std::endl;
#endif // _DEBUG
        }

        i++;
    }
}

/// Test how disassembler sets support flags in DecodedInstruction structure
TEST_F(Disassembler_Test, commandType)
{
    struct TestCase
    {
        std::vector<uint8_t> bytes;    // Input bytes
        bool hasJump;                  // Expected flags
        bool hasRelativeJump;
        bool hasDisplacement;
        bool hasReturn;
        bool hasByteOperand;
        bool hasWordOperand;
        bool hasCondition;
        bool hasVariableCycles;
    };

    std::vector<TestCase> testCases =
    {
        // NOP - no special flags
        { {0x00}, false, false, false, false, false, false, false, false },
        
        // LD BC,nn - has word operand
        { {0x01, 0x34, 0x12}, false, false, false, false, false, true, false, false },
        
        // JR NZ,d - conditional relative jump with variable cycles
        { {0x20, 0x05}, false, true, false, false, true, false, true, false },
        
        // LD (IX+d),n - has displacement and byte operand
        { {0xDD, 0x36, 0x05, 0x42}, false, false, true, false, true, false, false, false },
        
        // CALL nn - unconditional jump
        { {0xCD, 0x34, 0x12}, true, false, false, false, false, true, false, false },
        
        // RET NZ - conditional return
        { {0xC0}, false, false, false, true, false, false, true, false },
        
        // RST 0 - reset instruction (special kind of jump)
        { {0xC7}, true, false, false, false, false, false, false, false },

        // Extended instructions (ED prefix)
        // LDIR - block transfer instruction with variable cycles
        { {0xED, 0xB0}, false, false, false, false, false, false, false, true },
        
        // SBC HL,BC - extended arithmetic
        { {0xED, 0x42}, false, false, false, false, false, false, false, false },
        
        // Bit operations (CB prefix)
        // BIT 7,H - test bit instruction
        { {0xCB, 0x7C}, false, false, false, false, false, false, false, false },
        
        // RLC (IX+d) - rotated bit operation with displacement
        { {0xDD, 0xCB, 0x05, 0x06}, false, false, true, false, false, false, false, false },
        
        // Complex addressing modes
        // LD A,(BC) - indirect addressing
        { {0x0A}, false, false, false, false, false, false, false, false },
        
        // LD (nn),HL - direct addressing with word operand
        { {0x22, 0x34, 0x12}, false, false, false, false, false, true, false, false },
        
        // Edge cases for displacement
        // LD (IX-128),A - minimum displacement
        { {0xDD, 0x77, 0x80}, false, false, true, false, false, false, false, false },
        
        // LD (IY+127),A - maximum displacement
        { {0xFD, 0x77, 0x7F}, false, false, true, false, false, false, false, false }
    };

    for (size_t i = 0; i < testCases.size(); i++)
    {
        const auto& test = testCases[i];
        DecodedInstruction decoded = _disasm->decodeInstruction(test.bytes, 0);

        // Convert the bytes to a hex string and get mnemonic for error messages
        std::string hexBytes;
        for (uint8_t byte : test.bytes)
            hexBytes += StringHelper::Format("%02X ", byte);
        
        std::string mnemonic = _disasm->disassembleSingleCommand(test.bytes, 0);
        std::string errorPrefix = StringHelper::Format("Test case %zu [%s] '%s': ", i, hexBytes.c_str(), mnemonic.c_str());

        EXPECT_EQ(decoded.hasJump, test.hasJump)
            << errorPrefix << "hasJump mismatch. Expected: " << test.hasJump << ", Got: " << decoded.hasJump;
        EXPECT_EQ(decoded.hasRelativeJump, test.hasRelativeJump)
            << errorPrefix << "hasRelativeJump mismatch. Expected: " << test.hasRelativeJump << ", Got: " << decoded.hasRelativeJump;
        EXPECT_EQ(decoded.hasDisplacement, test.hasDisplacement)
            << errorPrefix << "hasDisplacement mismatch. Expected: " << test.hasDisplacement << ", Got: " << decoded.hasDisplacement;
        EXPECT_EQ(decoded.hasReturn, test.hasReturn)
            << errorPrefix << "hasReturn mismatch. Expected: " << test.hasReturn << ", Got: " << decoded.hasReturn;
        EXPECT_EQ(decoded.hasByteOperand, test.hasByteOperand)
            << errorPrefix << "hasByteOperand mismatch. Expected: " << test.hasByteOperand << ", Got: " << decoded.hasByteOperand;
        EXPECT_EQ(decoded.hasWordOperand, test.hasWordOperand)
            << errorPrefix << "hasWordOperand mismatch. Expected: " << test.hasWordOperand << ", Got: " << decoded.hasWordOperand;
        EXPECT_EQ(decoded.hasCondition, test.hasCondition)
            << errorPrefix << "hasCondition mismatch. Expected: " << test.hasCondition << ", Got: " << decoded.hasCondition;
        EXPECT_EQ(decoded.hasVariableCycles, test.hasVariableCycles)
            << errorPrefix << "hasVariableCycles mismatch. Expected: " << test.hasVariableCycles << ", Got: " << decoded.hasVariableCycles;
    }
}