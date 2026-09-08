#include "pch.h"

#include "disassembler_test.h"

#include <vector>
#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"

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
        { 0xFD, 0x36, 0xBA, 0x13 },   // ld (iy-#46),#13 (0xBA as signed displacement is -#46)
        { 0x38, 0x35 },               // jr c,#0037 (relative jumps show the target: 0 + 2 + 0x35)
    };

    std::vector<std::string> referenceValues =
    {
        "nop",
        "ld bc,#BEEF",
        "sra a",
        "ld (iy-#46),#13",
        "jr c,#0037",
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

TEST_F(Disassembler_Test, decodeJumpAndCallTargets)
{
    // JR e at 0x1000 with offset +10: target = 0x1000 + 2 + 10 = 0x100C
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0x18, 0x0A}, 0x1000);
        EXPECT_EQ(decoded.relJumpOffset, 10);
        EXPECT_EQ(decoded.relJumpAddr, 0x100C);
        EXPECT_EQ(decoded.jumpAddr, 0x100C);
    }

    // JR e with negative offset -2: target wraps back to the instruction start
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0x18, 0xFE}, 0x1000);
        EXPECT_EQ(decoded.relJumpOffset, -2);
        EXPECT_EQ(decoded.relJumpAddr, 0x1000);
    }

    // DD-prefixed JR: the DD prefix is ignored, so the instruction is 3 bytes long
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDD, 0x18, 0x05}, 0x2000);
        EXPECT_EQ(decoded.fullCommandLen, 3);
        EXPECT_EQ(decoded.relJumpAddr, 0x2008);
    }

    // CALL nn resolves its absolute target
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xCD, 0x2C, 0x16}, 0x0000);
        EXPECT_TRUE(decoded.hasJump);
        EXPECT_EQ(decoded.jumpAddr, 0x162C);
    }

    // Conditional CALL also resolves its absolute target
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xC4, 0x00, 0x80}, 0x0000);
        EXPECT_TRUE(decoded.hasJump);
        EXPECT_EQ(decoded.jumpAddr, 0x8000);
    }

    // JP nn resolves its absolute target
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xC3, 0x00, 0x05}, 0x0000);
        EXPECT_EQ(decoded.jumpAddr, 0x0500);
    }

    // RST targets come from the opcode itself (bits 3-5): RST 0x18, RST 0x38
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDF}, 0x1234);
        EXPECT_TRUE(decoded.isRst);
        EXPECT_TRUE(decoded.hasJump);
        EXPECT_EQ(decoded.jumpAddr, 0x0018);
    }
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xFF}, 0x1234);
        EXPECT_EQ(decoded.jumpAddr, 0x0038);
    }
}

TEST_F(Disassembler_Test, decodeDisplacement)
{
    // LD B,(IX+d) carries OF_DISP without OF_MBYTE - displacement must still be populated
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDD, 0x46, 0xFB}, 0x0000);
        EXPECT_TRUE(decoded.hasDisplacement);
        EXPECT_EQ(decoded.displacement, -5);
        EXPECT_EQ(decoded.fullCommandLen, 3);
    }

    // DDCB-prefixed instruction: displacement precedes the opcode
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDD, 0xCB, 0x05, 0x00}, 0x0000);
        EXPECT_TRUE(decoded.hasDisplacement);
        EXPECT_EQ(decoded.displacement, 5);
        EXPECT_EQ(decoded.fullCommandLen, 4);
    }

    // FDCB with negative displacement
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xFD, 0xCB, 0xC0, 0x86}, 0x0000);
        EXPECT_EQ(decoded.displacement, -64);
    }
}

TEST_F(Disassembler_Test, decodeWithRuntimeRegisters)
{
    Z80Registers registers{};
    registers.ix = 0x8000;
    registers.iy = 0x9000;
    registers.hl = 0x1234;

    // LD B,(IX-5): effective address is IX + displacement
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDD, 0x46, 0xFB}, 0x0000, &registers);
        EXPECT_TRUE(decoded.hasRuntime);
        EXPECT_EQ(decoded.displacementAddr, 0x7FFB);
    }

    // LD (IY+127),A: effective address is IY + displacement
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xFD, 0x77, 0x7F}, 0x0000, &registers);
        EXPECT_EQ(decoded.displacementAddr, 0x907F);
    }

    // DDCB effective address uses IX as well
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDD, 0xCB, 0x05, 0x00}, 0x0000, &registers);
        EXPECT_EQ(decoded.displacementAddr, 0x8005);
    }

    // IX wrap-around: 0x0005 - 6 = 0xFFFF
    {
        Z80Registers localRegisters{};
        localRegisters.ix = 0x0005;
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDD, 0x46, 0xFA}, 0x0000, &localRegisters);
        EXPECT_EQ(decoded.displacementAddr, 0xFFFF);
    }

    // JP (HL) resolves through HL at runtime
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xE9}, 0x0000, &registers);
        EXPECT_TRUE(decoded.hasJump);
        EXPECT_TRUE(decoded.hasIndirect);
        EXPECT_EQ(decoded.jumpAddr, 0x1234);
    }

    // DD JP (IX) resolves through IX
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDD, 0xE9}, 0x0000, &registers);
        EXPECT_EQ(decoded.jumpAddr, 0x8000);
    }

    // FD JP (IY) resolves through IY
    {
        DecodedInstruction decoded = _disasm->decodeInstruction({0xFD, 0xE9}, 0x0000, &registers);
        EXPECT_EQ(decoded.jumpAddr, 0x9000);
    }
}

TEST_F(Disassembler_Test, prefixChainsAndTruncatedInput)
{
    // A run of pure prefix bytes (e.g. data area 'DD DD DD DD') must not overrun the buffer
    EXPECT_NO_THROW({
        std::string result = _disasm->disassembleSingleCommand({0xDD, 0xDD, 0xDD, 0xDD}, 0);
        EXPECT_FALSE(result.empty());
    });

    // Mixed prefix chain 'DD FD' behaves as FD-prefixed opcode
    EXPECT_NO_THROW({
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDD, 0xFD, 0x21, 0x00, 0x40}, 0);
        EXPECT_EQ(decoded.prefix, 0x00FD);
        EXPECT_EQ(decoded.fullCommandLen, 5);
    });

    // Truncated DDCB instruction (missing trailing opcode byte) must not overread
    EXPECT_NO_THROW({
        DecodedInstruction decoded = _disasm->decodeInstruction({0xDD, 0xCB, 0x05}, 0);
        EXPECT_EQ(decoded.displacement, 5);
        EXPECT_EQ(decoded.fullCommandLen, 3);
        EXPECT_TRUE(decoded.isValid);
        EXPECT_TRUE(decoded.isTruncated) << "missing DDCB opcode byte is a partial decode";
    });

    // Truncated word operand: instruction stays valid but wordOperand is not fabricated
    EXPECT_NO_THROW({
        DecodedInstruction decoded = _disasm->decodeInstruction({0xCD, 0x34}, 0);
        EXPECT_EQ(decoded.fullCommandLen, 2);
        EXPECT_EQ(decoded.wordOperand, 0);
        EXPECT_EQ(decoded.jumpAddr, 0);
        EXPECT_TRUE(decoded.isValid);
        EXPECT_TRUE(decoded.isTruncated) << "missing operand byte is a partial decode";
    });

    // Truncated instructions render unknown operands instead of leaking the ':N' template
    EXPECT_EQ(_disasm->disassembleSingleCommand({0xCD, 0x34}, 0), "call ??");
    EXPECT_EQ(_disasm->disassembleSingleCommand({0xCD}, 0), "call ??");
    EXPECT_EQ(_disasm->disassembleSingleCommand({0xDD, 0x36, 0x05}, 0), "ld (ix+??),??");

    // Pure prefix run never reaches an opcode - partial as well
    EXPECT_TRUE(_disasm->decodeInstruction({0xDD, 0xDD, 0xDD, 0xDD}, 0).isTruncated);

    // A complete instruction is not truncated
    EXPECT_FALSE(_disasm->decodeInstruction({0xCD, 0x34, 0x12}, 0).isTruncated);
    EXPECT_FALSE(_disasm->decodeInstruction({0xDD, 0xCB, 0x05, 0x00}, 0).isTruncated);
}

TEST_F(Disassembler_Test, labelResolutionInMnemonics)
{
    // The base fixture creates a bare EmulatorContext without a DebugManager,
    // so wire one up manually to enable label resolution
    DebugManager* debugManager = new DebugManager(_context);
    _context->pDebugManager = debugManager;
    LabelManager* labelManager = debugManager->GetLabelManager();
    ASSERT_NE(labelManager, nullptr);

    ASSERT_TRUE(labelManager->AddLabel("TEST_ROUTINE", 0x8010, UINT16_MAX, UINT16_MAX, "code"));
    ASSERT_TRUE(labelManager->AddLabel("TEST_DATA", 0x8020, UINT16_MAX, UINT16_MAX, "data"));

    DecodedInstruction decoded;
    uint8_t len = 0;

    // CALL nn with a label at the target: both label and address must be printed
    std::string mnemonic = _disasm->disassembleSingleCommand({0xCD, 0x10, 0x80}, 0x8000, &len, &decoded);
    EXPECT_EQ(mnemonic, "call TEST_ROUTINE (#8010)");
    EXPECT_EQ(decoded.jumpAddr, 0x8010);

    // JP nn without a label prints the plain address
    mnemonic = _disasm->disassembleSingleCommand({0xC3, 0x34, 0x12}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "jp #1234");

    // JP nn with a label at the target
    mnemonic = _disasm->disassembleSingleCommand({0xC3, 0x10, 0x80}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "jp TEST_ROUTINE (#8010)");

    // LD (nn),HL with a data label at the memory operand (OF_MEMADR path)
    mnemonic = _disasm->disassembleSingleCommand({0x22, 0x20, 0x80}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "ld (TEST_DATA (#8020)),hl");

    // 16-bit immediate (LD rr,nn) is not an address by definition, but a label at exactly that
    // value is shown as well - it is almost certainly a pointer/table base
    mnemonic = _disasm->disassembleSingleCommand({0x21, 0x20, 0x80}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "ld hl,TEST_DATA (#8020)");
    EXPECT_FALSE(decoded.hasJump) << "immediate keeps its semantics - no jump target";

    // ...and stays a plain immediate when no label exists at that value
    mnemonic = _disasm->disassembleSingleCommand({0x01, 0x34, 0x12}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "ld bc,#1234");

    // JR with a label at the relative target (instruction at 0x8000, 2 bytes, offset +14 -> 0x8010)
    mnemonic = _disasm->disassembleSingleCommand({0x18, 0x0E}, 0x8000, &len, &decoded);
    EXPECT_EQ(mnemonic, "jr TEST_ROUTINE (#8010)");
    EXPECT_EQ(decoded.relJumpAddr, 0x8010);

    // Label at the instruction address itself is reported via DecodedInstruction.label
    mnemonic = _disasm->disassembleSingleCommand({0xCD, 0x10, 0x80}, 0x8010, &len, &decoded);
    EXPECT_EQ(decoded.label, "TEST_ROUTINE");

    _context->pDebugManager = nullptr;
    delete debugManager;
}

TEST_F(Disassembler_Test, tableRegressionJumpCallForms)
{
    DecodedInstruction decoded;
    uint8_t len = 0;

    // FD 11 nn nn - LD DE,nn must be 4 bytes (OF_MWORD was missing from the FD table)
    std::string mnemonic = _disasm->disassembleSingleCommand({0xFD, 0x11, 0x34, 0x12}, 0x0000, &len, &decoded);
    EXPECT_EQ(len, 4);
    EXPECT_EQ(mnemonic, "ld de,#1234");

    // DD E4 nn nn - CALL PO,nn must carry OF_CALL (was OF_JUMP) so step-over works and the target resolves
    mnemonic = _disasm->disassembleSingleCommand({0xDD, 0xE4, 0x00, 0x90}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "call po,#9000");
    EXPECT_EQ(decoded.jumpAddr, 0x9000);
    EXPECT_TRUE(decoded.opcode.flags & OF_CALL);
    EXPECT_FALSE(decoded.opcode.flags & OF_JUMP);

    // FD FA nn nn - JP M,nn target resolves (OF_JUMP was missing from the FD table)
    mnemonic = _disasm->disassembleSingleCommand({0xFD, 0xFA, 0x00, 0x60}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "jp m,#6000");
    EXPECT_EQ(decoded.jumpAddr, 0x6000);

    // EC nn nn - CALL PE,nn must carry OF_CALL (was OF_JUMP) and be stepped over
    std::vector<uint8_t> callPe = {0xEC, 0x00, 0x50};
    mnemonic = _disasm->disassembleSingleCommand(callPe, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "call pe,#5000");
    EXPECT_TRUE(decoded.opcode.flags & OF_CALL);
    EXPECT_TRUE(decoded.hasCondition);
    EXPECT_TRUE(_disasm->shouldStepOver(callPe));

    // DD 02 - LD (BC),A is flagged indirect like its noprefix/FD counterparts
    mnemonic = _disasm->disassembleSingleCommand({0xDD, 0x02}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "ld (bc),a");
    EXPECT_TRUE(decoded.hasIndirect);

    // FD E3 - EX (SP),IY is flagged indirect like its noprefix/DD counterparts
    mnemonic = _disasm->disassembleSingleCommand({0xFD, 0xE3}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "ex (sp),iy");
    EXPECT_TRUE(decoded.hasIndirect);

    // DD/FD FC nn nn - CALL M,nn carries OF_CONDITION like the noprefix row
    mnemonic = _disasm->disassembleSingleCommand({0xFD, 0xFC, 0x00, 0x40}, 0x0000, &len, &decoded);
    EXPECT_EQ(mnemonic, "call m,#4000");
    EXPECT_TRUE(decoded.hasCondition);

    // All RST rows share the same flag set (OF_RST only, without OF_JUMP)
    for (uint16_t rst : {0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF})
    {
        mnemonic = _disasm->disassembleSingleCommand({static_cast<uint8_t>(rst)}, 0x0000, &len, &decoded);
        EXPECT_EQ(decoded.opcode.flags, OF_RST) << "RST opcode " << StringHelper::ToHex(rst);
        EXPECT_EQ(decoded.jumpAddr, rst & 0x38) << "RST opcode " << StringHelper::ToHex(rst);
    }
}

TEST_F(Disassembler_Test, displacementExtremesAndRuntimeHints)
{
    Z80Registers regs{};
    regs.ix = 0x8000;

    DecodedInstruction decoded;
    uint8_t len = 0;

    // Displacement +127
    std::string mnemonic = _disasm->disassembleSingleCommandWithRuntime({0xDD, 0x7E, 0x7F}, 0x0000, &len, &regs, nullptr, &decoded);
    EXPECT_EQ(mnemonic, "ld a,(ix+#7F)");
    EXPECT_EQ(decoded.displacement, 127);
    EXPECT_EQ(decoded.displacementAddr, 0x807F);

    // Displacement -128
    mnemonic = _disasm->disassembleSingleCommandWithRuntime({0xDD, 0x7E, 0x80}, 0x0000, &len, &regs, nullptr, &decoded);
    EXPECT_EQ(mnemonic, "ld a,(ix-#80)");
    EXPECT_EQ(decoded.displacement, -128);
    EXPECT_EQ(decoded.displacementAddr, 0x7F80);

    // Displacement 0
    mnemonic = _disasm->disassembleSingleCommandWithRuntime({0xDD, 0x7E, 0x00}, 0x0000, &len, &regs, nullptr, &decoded);
    EXPECT_EQ(mnemonic, "ld a,(ix+#00)");
    EXPECT_EQ(decoded.displacementAddr, 0x8000);

    // Runtime hints distinguish calls from jumps
    mnemonic = _disasm->disassembleSingleCommandWithRuntime({0xCD, 0x10, 0x80}, 0x0000, &len, &regs, nullptr, &decoded);
    EXPECT_EQ(_disasm->getRuntimeHints(decoded), "Calling: $8010");

    mnemonic = _disasm->disassembleSingleCommandWithRuntime({0xD7}, 0x0000, &len, &regs, nullptr, &decoded);
    EXPECT_EQ(_disasm->getRuntimeHints(decoded), "Calling RST: $10");

    mnemonic = _disasm->disassembleSingleCommandWithRuntime({0xC3, 0x00, 0x60}, 0x0000, &len, &regs, nullptr, &decoded);
    EXPECT_EQ(_disasm->getRuntimeHints(decoded), "Jump to: $6000");
}
