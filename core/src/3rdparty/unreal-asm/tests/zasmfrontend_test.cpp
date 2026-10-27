// The zasm frontend: Megatokio's zasm syntax (labels with : and ::, #directives and .directives, numbers, hi() / lo(),
// string + n, numeric N$ labels, compound instructions, #code, macro parameter tags), converted to sjasmplus. The sources
// of zasm's own Test and Examples folders against the zasm binary are in tools/verification/unreal-asm/checks/zasmcheck.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "unrealasm/dialect.h"

using namespace unrealasm;

namespace
{
ir::Program ParseZasm(const std::string& text, bool z80n = false)
{
    SourceDocument doc = SourceDocument::FromText(text, "zasm");
    doc.z80n = z80n;
    return DialectRegistry::Builtin().Frontend("zasm")->Parse(doc).program;
}

std::vector<std::string> Sjasmplus(const std::string& text)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, "zasm"), "sjasmplus");
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        const size_t from = l.text.find_first_not_of(" \t");
        if (from == std::string::npos || l.text[from] == ';')
            continue;
        std::string line;
        for (size_t k = from; k < l.text.size(); ++k)
            if (!((l.text[k] == ' ' || l.text[k] == '\t') && !line.empty() && line.back() == ' '))
                line.push_back(l.text[k] == '\t' ? ' ' : l.text[k]);
        out.push_back(line);
    }
    return out;
}

bool Has(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}
}  // namespace

TEST(ZasmFrontend_Test, LabelsStartInColumnOneWithAnOptionalColonOrTwo)
{
    const ir::Program p = ParseZasm("start:  nop\nglob::  nop\nplain   nop\n        nop\n");
    EXPECT_EQ(p.lines[0].label, "start");
    EXPECT_EQ(p.lines[1].label, "glob");
    EXPECT_EQ(p.lines[2].label, "plain");
    EXPECT_EQ(p.lines[3].label, "");
}

TEST(ZasmFrontend_Test, NumbersAndFunctions)
{
    const ir::Statement s = ParseZasm("        db 12d,$1F,&1F,1Fh,0x1F,%101,101b,0b101,'a',hi(0x1234),lo(0x1234)\n").lines[0].statements[0];
    const int64_t expected[] = {12, 31, 31, 31, 31, 5, 5, 5, 97};
    ASSERT_EQ(s.operands.size(), 11u);
    for (size_t k = 0; k < 9; ++k)
        EXPECT_EQ(s.operands[k].expr.value, expected[k]) << k;
    EXPECT_EQ(s.operands[9].expr.op, ir::Op::High);
    EXPECT_EQ(s.operands[10].expr.op, ir::Op::Low);
}

TEST(ZasmFrontend_Test, BitMasksBindTighterThanMultiplication)
{
    // the manual lists & | ^ above * / %: 2 * 3 & 1 is 2 * (3 & 1)
    const ir::Expr e = ParseZasm("        db 2 * 3 & 1\n").lines[0].statements[0].operands[0].expr;
    ASSERT_EQ(e.kind, ir::Expr::Kind::Binary);
    EXPECT_EQ(e.op, ir::Op::Mul);
    EXPECT_EQ(e.args[1].op, ir::Op::And);
}

TEST(ZasmFrontend_Test, StringPlusModifiesTheLastCharacter)
{
    const std::vector<std::string> lines = Sjasmplus("        defm \"foobar\"+0x80\n");
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines[0], "DB 'fooba','r'+#80");
}

TEST(ZasmFrontend_Test, NumericLocalLabelsAreLocalToTheStretchBetweenOrdinaryLabels)
{
    const std::vector<std::string> lines = Sjasmplus("a1:  ld b,2\n1$:  djnz 1$\na2:  ld b,3\n1$:  djnz 1$\n");
    EXPECT_TRUE(Has(lines, "__loc1_1 DJNZ __loc1_1"));
    EXPECT_TRUE(Has(lines, "__loc2_1 DJNZ __loc2_1"));
}

TEST(ZasmFrontend_Test, CompoundInstructionsExpand)
{
    const std::vector<std::string> lines = Sjasmplus("        ld bc,de\n        ld (hl++),a\n        ld a,(--de)\n        srl hl\n        ld bc,(hl++)\n");
    for (const char* expected : {"LD B,D", "LD C,E", "LD (HL),A", "INC HL", "DEC DE", "LD A,(DE)", "SRL H", "RR L", "LD C,(HL)", "LD B,(HL)"})
        EXPECT_TRUE(Has(lines, expected)) << expected;
}

TEST(ZasmFrontend_Test, DirectivesWithAHashOrADot)
{
    const std::vector<std::string> lines = Sjasmplus("#if 0\n  nop\n#elif 1\n  halt\n#else\n  ret\n#endif\n.org 100\n.dup 2\n  nop\n.edup\n");
    EXPECT_TRUE(Has(lines, "IF 0"));
    EXPECT_TRUE(Has(lines, "IF 1"));
    EXPECT_TRUE(Has(lines, "ORG 100"));
    EXPECT_TRUE(Has(lines, "DUP 2"));
}

TEST(ZasmFrontend_Test, CodeSegmentSetsTheOrigin)
{
    const std::vector<std::string> lines = Sjasmplus("#target ram\n#code ram, 0x8000, 0x100\n  nop\n");
    EXPECT_TRUE(Has(lines, "ORG #8000"));
}

TEST(ZasmFrontend_Test, DefineMakesAConstantAndAliasesAreIgnored)
{
    const std::vector<std::string> lines = Sjasmplus("#define foo 123\n#define equ .equ\n");
    EXPECT_TRUE(Has(lines, "foo EQU 123"));
    EXPECT_EQ(std::count_if(lines.begin(), lines.end(), [](const std::string& l) { return l.find("equ") == 0 || l.find("EQU .equ") != std::string::npos; }), 0);
}

TEST(ZasmFrontend_Test, MacroParametersMayCarryATag)
{
    const ir::Program p = ParseZasm("head MACRO #name\n   DB #name\n   ENDM\n");
    EXPECT_EQ(p.lines[0].statements[0].params, std::vector<std::string>{"name"});
    EXPECT_EQ(p.lines[1].statements[0].operands[0].expr.text, "name");
}

TEST(ZasmFrontend_Test, SetWithALabelIsDeflAndTheBitInstructionStays)
{
    const ir::Program p = ParseZasm("v  set 5\n   set 3,a\n");
    EXPECT_EQ(p.lines[0].statements[0].directive, ir::DirectiveKind::Defl);
    EXPECT_EQ(p.lines[1].statements[0].mnemonic, "set");
}

TEST(ZasmFrontend_Test, ShebangOptionsAndBackslashSeparator)
{
    const ir::Program p = ParseZasm("#!/usr/local/bin/zasm --z80n -o out\n  swapnib \\ nop\n");
    EXPECT_TRUE(p.z80n);
    EXPECT_EQ(p.lines[1].statements.size(), 2u);
    EXPECT_EQ(p.lines[1].statements[0].kind, ir::Statement::Kind::Instruction);
}
