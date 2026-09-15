/// @file z80assembler_test.cpp
/// @brief Unit tests for the Z80TextAssembler (M7i / M9).
///
/// The assembler is deliberately context-free so these tests drive it
/// directly: instruction encoding, labels/expressions/pseudo-ops, the two
/// LD (HL) register-indirect forms (regression for the "(nn),A mis-parse"
/// fixed during M8 verification), and pass-2 error reporting.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/assembler/z80textassembler.h"

namespace
{

std::vector<uint8_t> AssembleOk(const std::string& source, uint16_t org = 0x8000)
{
    Z80TextAssembler assembler;
    AsmResult result = assembler.Assemble(source, org);
    EXPECT_TRUE(result.ok) << (result.ok ? "" : result.error.message + " @ line " + std::to_string(result.error.line));
    return result.bytes;
}

} // namespace

// ===========================================================================
// Basic instruction encoding
// ===========================================================================

TEST(Z80TextAssembler_Test, EncodesSimpleInstructions)
{
    std::vector<uint8_t> bytes = AssembleOk("nop\nld a,0x5A\nhalt");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x00, 0x3E, 0x5A, 0x76}));
}

TEST(Z80TextAssembler_Test, EncodesLdRegisterIndirectHLForms)
{
    // Regression: both forms used to be captured by the (nn),A / A,(nn)
    // handlers and failed with "Undefined symbol: hl"
    std::vector<uint8_t> bytes = AssembleOk("ld (hl),a\nld a,(hl)\nld (hl),0x1F");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x77, 0x7E, 0x36, 0x1F}));
}

TEST(Z80TextAssembler_Test, EncodesLdBCDEIndirectForms)
{
    std::vector<uint8_t> bytes = AssembleOk("ld a,(bc)\nld (de),a\nld a,(0x1234)\nld (0x1234),a");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x0A, 0x12, 0x3A, 0x34, 0x12, 0x32, 0x34, 0x12}));
}

TEST(Z80TextAssembler_Test, EncodesIndexedForms)
{
    std::vector<uint8_t> bytes = AssembleOk("ld (ix+2),0x42\nld a,(iy-1)");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0xDD, 0x36, 0x02, 0x42, 0xFD, 0x7E, 0xFF}));
}

TEST(Z80TextAssembler_Test, EncodesControlFlow)
{
    std::vector<uint8_t> bytes = AssembleOk("jp 0x9000\ncall 0x9000\nret\ndjnz $-2");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0xC3, 0x00, 0x90, 0xCD, 0x00, 0x90, 0xC9, 0x10, 0xFC}));
}

TEST(Z80TextAssembler_Test, EncodesPortAndSpecialRegisters)
{
    std::vector<uint8_t> bytes = AssembleOk("out (0xFE),a\nin a,(0xFE)\nld i,a\nim 2\nei\nreti");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0xD3, 0xFE, 0xDB, 0xFE, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0xED, 0x4D}));
}

// ===========================================================================
// Labels, expressions, pseudo-ops
// ===========================================================================

TEST(Z80TextAssembler_Test, LabelsResolveForwardReferences)
{
    std::string source = "start:\nld b,3\nloop:\ndjnz loop\njp start";
    Z80TextAssembler assembler;
    AsmResult result = assembler.Assemble(source, 0x8000);

    ASSERT_TRUE(result.ok) << result.error.message;
    // start=0x8000, loop=0x8002; layout: ld b,3 | djnz loop | jp start
    ASSERT_EQ(result.bytes.size(), 7u);
    EXPECT_EQ(result.bytes[0], 0x06);
    EXPECT_EQ(result.bytes[1], 0x03);
    // djnz loop from 0x8002 → offset = 0x8002 - 0x8004 = -2
    EXPECT_EQ(result.bytes[2], 0x10);
    EXPECT_EQ(result.bytes[3], 0xFE);
    // jp start → 0x8000 little-endian
    EXPECT_EQ(result.bytes[4], 0xC3);
    EXPECT_EQ(result.bytes[5], 0x00);
    EXPECT_EQ(result.bytes[6], 0x80);
    EXPECT_EQ(result.symbols.count("start"), 1u);
    EXPECT_EQ(result.symbols.count("loop"), 1u);
}

TEST(Z80TextAssembler_Test, EquAndDollarExpressions)
{
    // $ is the address of the current instruction: consumed by a 16-bit operand
    // (an 8-bit immediate like "and $+3" would exceed the byte range at 0x8002)
    std::string source = "MASK equ 0x0F\nld a,MASK\nld hl,$+3";
    Z80TextAssembler assembler;
    AsmResult result = assembler.Assemble(source, 0x8000);

    ASSERT_TRUE(result.ok) << result.error.message;
    ASSERT_EQ(result.bytes.size(), 5u);
    EXPECT_EQ(result.bytes[0], 0x3E);
    EXPECT_EQ(result.bytes[1], 0x0F);
    EXPECT_EQ(result.bytes[2], 0x21); // ld hl,nn
    EXPECT_EQ(result.bytes[3], 0x05); // low byte of $+3 = 0x8005
    EXPECT_EQ(result.bytes[4], 0x80);
    EXPECT_EQ(result.symbols.at("MASK"), 0x0Fu);
}

TEST(Z80TextAssembler_Test, PseudoOpsEmitData)
{
    std::vector<uint8_t> bytes = AssembleOk("db 1,2,3\ndw 0x1234\nds 2");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x01, 0x02, 0x03, 0x34, 0x12, 0x00, 0x00}));
}

TEST(Z80TextAssembler_Test, CommentsAndBlankLinesIgnored)
{
    std::vector<uint8_t> bytes = AssembleOk("; header comment\nnop ; trailing\n\nld a,1 ; end");
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x00, 0x3E, 0x01}));
}

// ===========================================================================
// Error reporting
// ===========================================================================

TEST(Z80TextAssembler_Test, UndefinedSymbol_FailsWithLineInfo)
{
    Z80TextAssembler assembler;
    AsmResult result = assembler.Assemble("ld a,(missing)", 0x8000);

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.message.find("missing"), std::string::npos);
    EXPECT_EQ(result.error.line, 1);
}

TEST(Z80TextAssembler_Test, UnknownMnemonic_Fails)
{
    Z80TextAssembler assembler;
    AsmResult result = assembler.Assemble("frobnicate a", 0x8000);

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.message.find("Unknown mnemonic"), std::string::npos);
}

TEST(Z80TextAssembler_Test, JrOutOfRange_FailsInPassTwo)
{
    Z80TextAssembler assembler;
    // jr target; 200 filler bytes; target beyond the ±127 relative range
    std::string source = "jr target\nds 200\ntarget: nop";
    AsmResult range = assembler.Assemble(source, 0x8000);
    EXPECT_FALSE(range.ok);
    EXPECT_NE(range.error.message.find("range"), std::string::npos);
}
