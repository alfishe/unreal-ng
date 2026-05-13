#include "pch.h"

#include "z80cfdecoder_test.h"

#include "common/stringhelper.h"

// ============================================================================
// IsControlFlowOpcode — Fast Pre-filter Table Tests
// ============================================================================

/// @brief Verify that known control flow first-bytes return true.
TEST_F(Z80CFDecoder_Test, IsControlFlowOpcode_KnownOpcodes)
{
    // All opcodes that should be recognized as potential control flow
    const uint8_t cfOpcodes[] =
    {
        // DJNZ, JR, JR cc
        0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
        // RET cc
        0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8,
        // JP cc,nn and JP nn
        0xC2, 0xC3, 0xCA, 0xD2, 0xDA, 0xE2, 0xEA, 0xF2, 0xFA,
        // CALL cc,nn and CALL nn
        0xC4, 0xCC, 0xCD, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC,
        // RET, JP (HL)
        0xC9, 0xE9,
        // RST
        0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF,
        // Prefixes (DD, ED, FD)
        0xDD, 0xED, 0xFD,
    };

    for (uint8_t op : cfOpcodes)
    {
        EXPECT_TRUE(Z80ControlFlowDecoder::IsControlFlowOpcode(op))
            << "Expected true for opcode 0x" << StringHelper::Format("%02X", op);
    }
}

/// @brief Verify that non-control-flow opcodes return false.
TEST_F(Z80CFDecoder_Test, IsControlFlowOpcode_NonCFOpcodes)
{
    // Sample of opcodes that are definitely NOT control flow
    const uint8_t nonCfOpcodes[] =
    {
        0x00, // NOP
        0x01, // LD BC,nn
        0x06, // LD B,n
        0x0A, // LD A,(BC)
        0x3E, // LD A,n
        0x40, // LD B,B
        0x76, // HALT
        0x80, // ADD A,B
        0xA0, // AND B
        0xB8, // CP B
        0xCB, // CB prefix (bit ops)
        0xD3, // OUT (n),A
        0xDB, // IN A,(n)
        0xF3, // DI
        0xFB, // EI
        0xFE, // CP n
    };

    for (uint8_t op : nonCfOpcodes)
    {
        EXPECT_FALSE(Z80ControlFlowDecoder::IsControlFlowOpcode(op))
            << "Expected false for opcode 0x" << StringHelper::Format("%02X", op);
    }
}

/// @brief Exhaustive: count total CF opcodes in table matches expected Z80 set size.
TEST_F(Z80CFDecoder_Test, IsControlFlowOpcode_TotalCount)
{
    int count = 0;
    for (int i = 0; i < 256; ++i)
    {
        if (Z80ControlFlowDecoder::IsControlFlowOpcode(static_cast<uint8_t>(i)))
            count++;
    }

    // Expected count:
    //   DJNZ(1) + JR(1) + JR cc(4) = 6
    //   RET cc(8) + JP cc(8) + CALL cc(8) + RST(8) = 32
    //   JP nn(1) + RET(1) + CALL nn(1) + JP (HL)(1) = 4
    //   DD(1) + ED(1) + FD(1) = 3
    //   Total = 45
    EXPECT_EQ(count, 45) << "Total CF opcode table entries should be 45";
}


// ============================================================================
// Decode — Non-Control-Flow Rejection
// ============================================================================

/// @brief Non-CF instructions should return false from Decode.
TEST_F(Z80CFDecoder_Test, Decode_NonCFOpcode_ReturnsFalse)
{
    Z80ControlFlowResult result{};
    uint8_t nop[] = { 0x00, 0x00, 0x00, 0x00 };
    EXPECT_FALSE(Decode(nop, 0x0000, result));

    uint8_t ldBCnn[] = { 0x01, 0x34, 0x12, 0x00 };
    EXPECT_FALSE(Decode(ldBCnn, 0x0000, result));

    uint8_t halt[] = { 0x76, 0x00, 0x00, 0x00 };
    EXPECT_FALSE(Decode(halt, 0x0000, result));

    // CB prefix: BIT 7,A
    uint8_t cbOp[] = { 0xCB, 0x7F, 0x00, 0x00 };
    EXPECT_FALSE(Decode(cbOp, 0x0000, result));
}

/// @brief Null bytes pointer should return false.
TEST_F(Z80CFDecoder_Test, Decode_NullBytes_ReturnsFalse)
{
    Z80ControlFlowResult result{};
    EXPECT_FALSE(Z80ControlFlowDecoder::Decode(nullptr, 0x0000, 0, 0, 0, 0, 0, 0, nullptr, result));
}


// ============================================================================
// JP nn (0xC3) — Unconditional Absolute Jump
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_JP_Unconditional)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xC3, 0x34, 0x12, 0x00 };  // JP 0x1234
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::JP);
    EXPECT_EQ(result.target_addr, 0x1234);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 3);
}

TEST_F(Z80CFDecoder_Test, Decode_JP_Unconditional_HighAddress)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xC3, 0xFF, 0xFF, 0x00 };  // JP 0xFFFF
    ASSERT_TRUE(Decode(bytes, 0x8000, result));

    EXPECT_EQ(result.target_addr, 0xFFFF);
    EXPECT_TRUE(result.taken);
}


// ============================================================================
// JP cc,nn — Conditional Absolute Jumps
// ============================================================================

struct ConditionalJPTestCase
{
    uint8_t opcode;
    const char* name;
    uint8_t flags_taken;      // F register value that makes the condition TRUE
    uint8_t flags_not_taken;  // F register value that makes the condition FALSE
};

class Z80CFDecoder_ConditionalJP_Test : public Z80CFDecoder_Test,
                                         public ::testing::WithParamInterface<ConditionalJPTestCase> {};

TEST_P(Z80CFDecoder_ConditionalJP_Test, Decode_JP_Conditional)
{
    auto tc = GetParam();
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { tc.opcode, 0x78, 0x56, 0x00 };  // JP cc, 0x5678

    // Test condition taken
    ASSERT_TRUE(Decode(bytes, 0x0000, result, tc.flags_taken));
    EXPECT_EQ(result.type, Z80CFType::JP) << tc.name << " (taken)";
    EXPECT_EQ(result.target_addr, 0x5678) << tc.name << " (taken)";
    EXPECT_TRUE(result.taken) << tc.name << " should be taken with flags 0x"
        << StringHelper::Format("%02X", tc.flags_taken);
    EXPECT_EQ(result.instruction_len, 3);

    // Test condition not taken
    ASSERT_TRUE(Decode(bytes, 0x0000, result, tc.flags_not_taken));
    EXPECT_FALSE(result.taken) << tc.name << " should NOT be taken with flags 0x"
        << StringHelper::Format("%02X", tc.flags_not_taken);
}

INSTANTIATE_TEST_SUITE_P(AllConditions, Z80CFDecoder_ConditionalJP_Test,
    ::testing::Values(
        ConditionalJPTestCase{ 0xC2, "JP NZ", 0x00, 0x40 },  // NZ: Z clear
        ConditionalJPTestCase{ 0xCA, "JP Z",  0x40, 0x00 },  // Z:  Z set
        ConditionalJPTestCase{ 0xD2, "JP NC", 0x00, 0x01 },  // NC: C clear
        ConditionalJPTestCase{ 0xDA, "JP C",  0x01, 0x00 },  // C:  C set
        ConditionalJPTestCase{ 0xE2, "JP PO", 0x00, 0x04 },  // PO: P/V clear
        ConditionalJPTestCase{ 0xEA, "JP PE", 0x04, 0x00 },  // PE: P/V set
        ConditionalJPTestCase{ 0xF2, "JP P",  0x00, 0x80 },  // P:  S clear
        ConditionalJPTestCase{ 0xFA, "JP M",  0x80, 0x00 }   // M:  S set
    ));


// ============================================================================
// JP (HL) / JP (IX) / JP (IY) — Indirect Jumps
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_JP_HL)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xE9, 0x00, 0x00, 0x00 };  // JP (HL)
    ASSERT_TRUE(Decode(bytes, 0x0000, result, 0, 0, 0xFFF0, 0xABCD));

    EXPECT_EQ(result.type, Z80CFType::JP);
    EXPECT_EQ(result.target_addr, 0xABCD);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 1);
}

TEST_F(Z80CFDecoder_Test, Decode_JP_IX)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xDD, 0xE9, 0x00, 0x00 };  // JP (IX)
    // ix=0x1234
    ASSERT_TRUE(Z80ControlFlowDecoder::Decode(bytes, 0x0000, 0, 0, 0xFFF0, 0, 0x1234, 0, nullptr, result));

    EXPECT_EQ(result.type, Z80CFType::JP);
    EXPECT_EQ(result.target_addr, 0x1234);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 2);
}

TEST_F(Z80CFDecoder_Test, Decode_JP_IY)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xFD, 0xE9, 0x00, 0x00 };  // JP (IY)
    // iy=0xBEEF
    ASSERT_TRUE(Z80ControlFlowDecoder::Decode(bytes, 0x0000, 0, 0, 0xFFF0, 0, 0, 0xBEEF, nullptr, result));

    EXPECT_EQ(result.type, Z80CFType::JP);
    EXPECT_EQ(result.target_addr, 0xBEEF);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 2);
}

/// @brief DD prefix with non-E9 second byte should NOT be control flow.
TEST_F(Z80CFDecoder_Test, Decode_DD_NonCF_ReturnsFalse)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xDD, 0x21, 0x34, 0x12 };  // LD IX,nn
    EXPECT_FALSE(Decode(bytes, 0x0000, result));
}

/// @brief FD prefix with non-E9 second byte should NOT be control flow.
TEST_F(Z80CFDecoder_Test, Decode_FD_NonCF_ReturnsFalse)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xFD, 0x21, 0x34, 0x12 };  // LD IY,nn
    EXPECT_FALSE(Decode(bytes, 0x0000, result));
}


// ============================================================================
// JR e (0x18) — Unconditional Relative Jump
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_JR_Unconditional_Forward)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x18, 0x10, 0x00, 0x00 };  // JR +16
    ASSERT_TRUE(Decode(bytes, 0x1000, result));

    EXPECT_EQ(result.type, Z80CFType::JR);
    // Target = PC + 2 + offset = 0x1000 + 2 + 16 = 0x1012
    EXPECT_EQ(result.target_addr, 0x1012);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 2);
}

TEST_F(Z80CFDecoder_Test, Decode_JR_Unconditional_Backward)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x18, 0xFE, 0x00, 0x00 };  // JR -2 (infinite loop: jumps to itself)
    ASSERT_TRUE(Decode(bytes, 0x1000, result));

    // Target = 0x1000 + 2 + (-2) = 0x1000 (itself)
    EXPECT_EQ(result.target_addr, 0x1000);
    EXPECT_TRUE(result.taken);
}

TEST_F(Z80CFDecoder_Test, Decode_JR_Unconditional_MaxForward)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x18, 0x7F, 0x00, 0x00 };  // JR +127
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    // Target = 0x0000 + 2 + 127 = 0x0081
    EXPECT_EQ(result.target_addr, 0x0081);
}

TEST_F(Z80CFDecoder_Test, Decode_JR_Unconditional_MaxBackward)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x18, 0x80, 0x00, 0x00 };  // JR -128
    ASSERT_TRUE(Decode(bytes, 0x1000, result));

    // Target = 0x1000 + 2 + (-128) = 0x0F82
    EXPECT_EQ(result.target_addr, 0x0F82);
}


// ============================================================================
// JR cc,e — Conditional Relative Jumps
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_JR_NZ_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x20, 0x05, 0x00, 0x00 };  // JR NZ, +5
    ASSERT_TRUE(Decode(bytes, 0x2000, result, 0x00));  // Z flag clear -> taken

    EXPECT_EQ(result.type, Z80CFType::JR);
    EXPECT_EQ(result.target_addr, 0x2007);  // 0x2000 + 2 + 5
    EXPECT_TRUE(result.taken);
}

TEST_F(Z80CFDecoder_Test, Decode_JR_NZ_NotTaken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x20, 0x05, 0x00, 0x00 };  // JR NZ, +5
    ASSERT_TRUE(Decode(bytes, 0x2000, result, 0x40));  // Z flag set -> not taken

    EXPECT_FALSE(result.taken);
}

TEST_F(Z80CFDecoder_Test, Decode_JR_Z_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x28, 0x0A, 0x00, 0x00 };  // JR Z, +10
    ASSERT_TRUE(Decode(bytes, 0x3000, result, 0x40));  // Z flag set -> taken

    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.target_addr, 0x300C);  // 0x3000 + 2 + 10
}

TEST_F(Z80CFDecoder_Test, Decode_JR_NC_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x30, 0x03, 0x00, 0x00 };  // JR NC, +3
    ASSERT_TRUE(Decode(bytes, 0x4000, result, 0x00));  // C flag clear -> taken

    EXPECT_TRUE(result.taken);
}

TEST_F(Z80CFDecoder_Test, Decode_JR_C_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x38, 0xFC, 0x00, 0x00 };  // JR C, -4
    ASSERT_TRUE(Decode(bytes, 0x5000, result, 0x01));  // C flag set -> taken

    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.target_addr, 0x4FFE);  // 0x5000 + 2 + (-4)
}


// ============================================================================
// CALL nn (0xCD) — Unconditional Call
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_CALL_Unconditional)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xCD, 0x78, 0x56, 0x00 };  // CALL 0x5678
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::CALL);
    EXPECT_EQ(result.target_addr, 0x5678);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 3);
}


// ============================================================================
// CALL cc,nn — Conditional Calls (representative subset)
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_CALL_NZ_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xC4, 0x00, 0x10, 0x00 };  // CALL NZ, 0x1000
    ASSERT_TRUE(Decode(bytes, 0x0000, result, 0x00));  // Z clear -> taken

    EXPECT_EQ(result.type, Z80CFType::CALL);
    EXPECT_EQ(result.target_addr, 0x1000);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 3);
}

TEST_F(Z80CFDecoder_Test, Decode_CALL_NZ_NotTaken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xC4, 0x00, 0x10, 0x00 };  // CALL NZ, 0x1000
    ASSERT_TRUE(Decode(bytes, 0x0000, result, 0x40));  // Z set -> not taken

    EXPECT_FALSE(result.taken);
}

TEST_F(Z80CFDecoder_Test, Decode_CALL_PE_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xEC, 0xCD, 0xAB, 0x00 };  // CALL PE, 0xABCD
    ASSERT_TRUE(Decode(bytes, 0x0000, result, 0x04));  // P/V set -> PE taken

    EXPECT_EQ(result.type, Z80CFType::CALL);
    EXPECT_EQ(result.target_addr, 0xABCD);
    EXPECT_TRUE(result.taken);
}

TEST_F(Z80CFDecoder_Test, Decode_CALL_M_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xFC, 0xEF, 0xBE, 0x00 };  // CALL M, 0xBEEF
    ASSERT_TRUE(Decode(bytes, 0x0000, result, 0x80));  // S set -> M taken

    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.target_addr, 0xBEEF);
}


// ============================================================================
// RET (0xC9) — Unconditional Return
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_RET_Unconditional)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xC9, 0x00, 0x00, 0x00 };  // RET
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RET);
    // target_addr = ReadWord(nullptr, sp) = 0 (no memory)
    EXPECT_EQ(result.target_addr, 0x0000);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 1);
}


// ============================================================================
// RET cc — Conditional Returns (representative subset)
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_RET_NZ_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xC0, 0x00, 0x00, 0x00 };  // RET NZ
    ASSERT_TRUE(Decode(bytes, 0x0000, result, 0x00));  // Z clear -> taken

    EXPECT_EQ(result.type, Z80CFType::RET);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 1);
}

TEST_F(Z80CFDecoder_Test, Decode_RET_NZ_NotTaken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xC0, 0x00, 0x00, 0x00 };  // RET NZ
    ASSERT_TRUE(Decode(bytes, 0x0000, result, 0x40));  // Z set -> not taken

    EXPECT_FALSE(result.taken);
}

TEST_F(Z80CFDecoder_Test, Decode_RET_C_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xD8, 0x00, 0x00, 0x00 };  // RET C
    ASSERT_TRUE(Decode(bytes, 0x0000, result, 0x01));  // C set -> taken

    EXPECT_TRUE(result.taken);
}

TEST_F(Z80CFDecoder_Test, Decode_RET_P_NotTaken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xF0, 0x00, 0x00, 0x00 };  // RET P
    ASSERT_TRUE(Decode(bytes, 0x0000, result, 0x80));  // S set -> not P, not taken

    EXPECT_FALSE(result.taken);
}


// ============================================================================
// RST n — Restart Instructions
// ============================================================================

struct RSTTestCase
{
    uint8_t opcode;
    uint16_t expected_target;
};

class Z80CFDecoder_RST_Test : public Z80CFDecoder_Test,
                                public ::testing::WithParamInterface<RSTTestCase> {};

TEST_P(Z80CFDecoder_RST_Test, Decode_RST)
{
    auto tc = GetParam();
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { tc.opcode, 0x00, 0x00, 0x00 };
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RST);
    EXPECT_EQ(result.target_addr, tc.expected_target);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 1);
}

INSTANTIATE_TEST_SUITE_P(AllRSTVectors, Z80CFDecoder_RST_Test,
    ::testing::Values(
        RSTTestCase{ 0xC7, 0x0000 },  // RST 00h
        RSTTestCase{ 0xCF, 0x0008 },  // RST 08h
        RSTTestCase{ 0xD7, 0x0010 },  // RST 10h
        RSTTestCase{ 0xDF, 0x0018 },  // RST 18h
        RSTTestCase{ 0xE7, 0x0020 },  // RST 20h
        RSTTestCase{ 0xEF, 0x0028 },  // RST 28h
        RSTTestCase{ 0xF7, 0x0030 },  // RST 30h
        RSTTestCase{ 0xFF, 0x0038 }   // RST 38h
    ));


// ============================================================================
// DJNZ e (0x10) — Decrement B and Jump if Not Zero
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_DJNZ_Taken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x10, 0xFE, 0x00, 0x00 };  // DJNZ -2 (loop to self)
    // b_reg=5: after decrement, B=4 != 0 -> taken
    ASSERT_TRUE(Decode(bytes, 0x1000, result, 0x00, 5));

    EXPECT_EQ(result.type, Z80CFType::DJNZ);
    EXPECT_EQ(result.target_addr, 0x1000);  // PC + 2 + (-2) = self
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 2);
}

TEST_F(Z80CFDecoder_Test, Decode_DJNZ_NotTaken)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x10, 0xFE, 0x00, 0x00 };  // DJNZ -2
    // b_reg=1: after decrement, B=0 -> not taken
    ASSERT_TRUE(Decode(bytes, 0x1000, result, 0x00, 1));

    EXPECT_EQ(result.type, Z80CFType::DJNZ);
    EXPECT_FALSE(result.taken);
}

TEST_F(Z80CFDecoder_Test, Decode_DJNZ_B_WrapAround)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x10, 0x05, 0x00, 0x00 };  // DJNZ +5
    // b_reg=0: after decrement, B=0xFF (wraps) != 0 -> taken
    ASSERT_TRUE(Decode(bytes, 0x2000, result, 0x00, 0));

    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.target_addr, 0x2007);  // PC + 2 + 5
}


// ============================================================================
// ED-Prefixed: RETI (0xED 0x4D)
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_RETI_Documented)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xED, 0x4D, 0x00, 0x00 };  // RETI
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RETI);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 2);
}

TEST_F(Z80CFDecoder_Test, Decode_RETI_Undocumented_5D)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xED, 0x5D, 0x00, 0x00 };
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RETI);
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 2);
}

TEST_F(Z80CFDecoder_Test, Decode_RETI_Undocumented_6D)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xED, 0x6D, 0x00, 0x00 };
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RETI);
    EXPECT_EQ(result.instruction_len, 2);
}

TEST_F(Z80CFDecoder_Test, Decode_RETI_Undocumented_7D)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xED, 0x7D, 0x00, 0x00 };
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RETI);
    EXPECT_EQ(result.instruction_len, 2);
}


// ============================================================================
// ED-Prefixed: RETN (0xED 0x45 and undocumented variants)
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_RETN_Documented)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xED, 0x45, 0x00, 0x00 };  // RETN
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RET);  // RETN maps to RET type
    EXPECT_TRUE(result.taken);
    EXPECT_EQ(result.instruction_len, 2);
}

TEST_F(Z80CFDecoder_Test, Decode_RETN_Undocumented_55)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xED, 0x55, 0x00, 0x00 };
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RET);
    EXPECT_EQ(result.instruction_len, 2);
}

TEST_F(Z80CFDecoder_Test, Decode_RETN_Undocumented_65)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xED, 0x65, 0x00, 0x00 };
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RET);
}

TEST_F(Z80CFDecoder_Test, Decode_RETN_Undocumented_75)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xED, 0x75, 0x00, 0x00 };
    ASSERT_TRUE(Decode(bytes, 0x0000, result));

    EXPECT_EQ(result.type, Z80CFType::RET);
}


// ============================================================================
// ED-Prefixed: Non-CF ED opcodes should return false
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_ED_NonCF_ReturnsFalse)
{
    Z80ControlFlowResult result{};

    // IM 0
    uint8_t im0[] = { 0xED, 0x46, 0x00, 0x00 };
    EXPECT_FALSE(Decode(im0, 0x0000, result));

    // IN A,(C)
    uint8_t inAC[] = { 0xED, 0x78, 0x00, 0x00 };
    EXPECT_FALSE(Decode(inAC, 0x0000, result));

    // LDIR
    uint8_t ldir[] = { 0xED, 0xB0, 0x00, 0x00 };
    EXPECT_FALSE(Decode(ldir, 0x0000, result));

    // SBC HL,BC
    uint8_t sbcHL[] = { 0xED, 0x42, 0x00, 0x00 };
    EXPECT_FALSE(Decode(sbcHL, 0x0000, result));
}


// ============================================================================
// Condition Code Coverage — All 8 conditions with multiple flag combinations
// ============================================================================

TEST_F(Z80CFDecoder_Test, ConditionEvaluation_AllFlags_Combined)
{
    Z80ControlFlowResult result{};
    uint8_t jpNZ[] = { 0xC2, 0x00, 0x10, 0x00 };

    // All flags set: Z=1,C=1,P/V=1,S=1 => NZ should be not taken
    ASSERT_TRUE(Decode(jpNZ, 0x0000, result, 0xFF));
    EXPECT_FALSE(result.taken) << "NZ should NOT be taken when all flags set";

    // All flags clear: Z=0 => NZ should be taken
    ASSERT_TRUE(Decode(jpNZ, 0x0000, result, 0x00));
    EXPECT_TRUE(result.taken) << "NZ should be taken when all flags clear";

    // Only undocumented flags set (bits 3,5): should not affect NZ
    ASSERT_TRUE(Decode(jpNZ, 0x0000, result, 0x28));
    EXPECT_TRUE(result.taken) << "NZ should be taken when only X/Y flags set";
}

/// @brief Verify condition evaluation is independent of unrelated flag bits.
TEST_F(Z80CFDecoder_Test, ConditionEvaluation_IsolatedFlags)
{
    Z80ControlFlowResult result{};

    // JP C: carry flag is bit 0. Other flags should be irrelevant.
    uint8_t jpC[] = { 0xDA, 0x00, 0x10, 0x00 };

    // S=1, Z=1, H=1, P/V=1, N=1, C=0: C flag clear despite all others set
    ASSERT_TRUE(Decode(jpC, 0x0000, result, 0xFE));
    EXPECT_FALSE(result.taken) << "JP C should NOT be taken when C=0 even with other flags set";

    // All clear except C=1
    ASSERT_TRUE(Decode(jpC, 0x0000, result, 0x01));
    EXPECT_TRUE(result.taken) << "JP C should be taken when C=1";
}


// ============================================================================
// Address Wrapping Edge Cases
// ============================================================================

TEST_F(Z80CFDecoder_Test, Decode_JR_AddressWraps_Around_0xFFFF)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0x18, 0x7F, 0x00, 0x00 };  // JR +127
    // PC at 0xFFF0: target = 0xFFF0 + 2 + 127 = 0x10071, wraps to 0x0071 (16-bit)
    ASSERT_TRUE(Decode(bytes, 0xFFF0, result));

    uint16_t expected = static_cast<uint16_t>(0xFFF0 + 2 + 127);
    EXPECT_EQ(result.target_addr, expected);
}

TEST_F(Z80CFDecoder_Test, Decode_JP_TargetAddress_0x0000)
{
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { 0xC3, 0x00, 0x00, 0x00 };  // JP 0x0000
    ASSERT_TRUE(Decode(bytes, 0x8000, result));

    EXPECT_EQ(result.target_addr, 0x0000);
}


// ============================================================================
// Comprehensive Parameterized: All 8 Conditional RET opcodes
// ============================================================================

struct ConditionalRETTestCase
{
    uint8_t opcode;
    const char* name;
    uint8_t flags_taken;
    uint8_t flags_not_taken;
};

class Z80CFDecoder_ConditionalRET_Test : public Z80CFDecoder_Test,
                                          public ::testing::WithParamInterface<ConditionalRETTestCase> {};

TEST_P(Z80CFDecoder_ConditionalRET_Test, Decode_RET_Conditional)
{
    auto tc = GetParam();
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { tc.opcode, 0x00, 0x00, 0x00 };

    // Taken
    ASSERT_TRUE(Decode(bytes, 0x0000, result, tc.flags_taken));
    EXPECT_EQ(result.type, Z80CFType::RET) << tc.name;
    EXPECT_TRUE(result.taken) << tc.name << " should be taken";
    EXPECT_EQ(result.instruction_len, 1);

    // Not taken
    ASSERT_TRUE(Decode(bytes, 0x0000, result, tc.flags_not_taken));
    EXPECT_FALSE(result.taken) << tc.name << " should NOT be taken";
}

INSTANTIATE_TEST_SUITE_P(AllConditions, Z80CFDecoder_ConditionalRET_Test,
    ::testing::Values(
        ConditionalRETTestCase{ 0xC0, "RET NZ", 0x00, 0x40 },
        ConditionalRETTestCase{ 0xC8, "RET Z",  0x40, 0x00 },
        ConditionalRETTestCase{ 0xD0, "RET NC", 0x00, 0x01 },
        ConditionalRETTestCase{ 0xD8, "RET C",  0x01, 0x00 },
        ConditionalRETTestCase{ 0xE0, "RET PO", 0x00, 0x04 },
        ConditionalRETTestCase{ 0xE8, "RET PE", 0x04, 0x00 },
        ConditionalRETTestCase{ 0xF0, "RET P",  0x00, 0x80 },
        ConditionalRETTestCase{ 0xF8, "RET M",  0x80, 0x00 }
    ));


// ============================================================================
// Comprehensive Parameterized: All 8 Conditional CALL opcodes
// ============================================================================

struct ConditionalCALLTestCase
{
    uint8_t opcode;
    const char* name;
    uint8_t flags_taken;
    uint8_t flags_not_taken;
};

class Z80CFDecoder_ConditionalCALL_Test : public Z80CFDecoder_Test,
                                           public ::testing::WithParamInterface<ConditionalCALLTestCase> {};

TEST_P(Z80CFDecoder_ConditionalCALL_Test, Decode_CALL_Conditional)
{
    auto tc = GetParam();
    Z80ControlFlowResult result{};
    uint8_t bytes[] = { tc.opcode, 0xEF, 0xBE, 0x00 };  // CALL cc, 0xBEEF

    // Taken
    ASSERT_TRUE(Decode(bytes, 0x0000, result, tc.flags_taken));
    EXPECT_EQ(result.type, Z80CFType::CALL) << tc.name;
    EXPECT_EQ(result.target_addr, 0xBEEF) << tc.name;
    EXPECT_TRUE(result.taken) << tc.name << " should be taken";
    EXPECT_EQ(result.instruction_len, 3);

    // Not taken
    ASSERT_TRUE(Decode(bytes, 0x0000, result, tc.flags_not_taken));
    EXPECT_FALSE(result.taken) << tc.name << " should NOT be taken";
}

INSTANTIATE_TEST_SUITE_P(AllConditions, Z80CFDecoder_ConditionalCALL_Test,
    ::testing::Values(
        ConditionalCALLTestCase{ 0xC4, "CALL NZ", 0x00, 0x40 },
        ConditionalCALLTestCase{ 0xCC, "CALL Z",  0x40, 0x00 },
        ConditionalCALLTestCase{ 0xD4, "CALL NC", 0x00, 0x01 },
        ConditionalCALLTestCase{ 0xDC, "CALL C",  0x01, 0x00 },
        ConditionalCALLTestCase{ 0xE4, "CALL PO", 0x00, 0x04 },
        ConditionalCALLTestCase{ 0xEC, "CALL PE", 0x04, 0x00 },
        ConditionalCALLTestCase{ 0xF4, "CALL P",  0x00, 0x80 },
        ConditionalCALLTestCase{ 0xFC, "CALL M",  0x80, 0x00 }
    ));
