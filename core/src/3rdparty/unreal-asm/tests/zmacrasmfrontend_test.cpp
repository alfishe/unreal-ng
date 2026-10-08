// The zmac and rasm frontends: the constructs the dialects have that the other text dialects lack (zmac: two-character
// constants, IFEQ and friends, word operators, the backslash separator; rasm: macro parameters in parentheses, @ labels local
// to a macro, REPEAT with a counter, the colon separator, octal @17), converted to sjasmplus. The real assemblers decide on
// the sources of testdata/zmac and rasm's decrunch routines in tools/verification/unreal-asm/checks/dialectcheck.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "unrealasm/dialect.h"

using namespace unrealasm;

namespace
{
ir::Program Parse(const char* dialect, const std::string& text)
{
    return DialectRegistry::Builtin().Frontend(dialect)->Parse(SourceDocument::FromText(text, dialect)).program;
}

std::vector<std::string> Sjasmplus(const char* dialect, const std::string& text)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, dialect), "sjasmplus");
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

TEST(ZmacFrontend_Test, TwoCharacterConstantsAreLowCharacterFirst)
{
    // 'LH' is 'H' * 256 + 'L', so a DW stores L then H
    const ir::Expr e = Parse("zmac", "        defw 'LH'\n").lines[0].statements[0].operands[0].expr;
    EXPECT_EQ(e.value, 'H' * 256 + 'L');
}

TEST(ZmacFrontend_Test, WordOperatorsAndSynonyms)
{
    const ir::Statement s = Parse("zmac", "        defb 7 mod 3, 1 shl 4, 0ffh and 0fh, not 0, low 1234h, high 1234h\n").lines[0].statements[0];
    ASSERT_EQ(s.operands.size(), 6u);
    EXPECT_EQ(s.operands[0].expr.op, ir::Op::Mod);
    EXPECT_EQ(s.operands[1].expr.op, ir::Op::Shl);
    EXPECT_EQ(s.operands[2].expr.op, ir::Op::And);
    EXPECT_EQ(s.operands[3].expr.op, ir::Op::Not);
    EXPECT_EQ(s.operands[4].expr.op, ir::Op::Low);
    EXPECT_EQ(s.operands[5].expr.op, ir::Op::High);
}

TEST(ZmacFrontend_Test, IfComparisonDirectivesBecomeIfs)
{
    const std::vector<std::string> lines = Sjasmplus("zmac", "        ifeq 1,2\n        nop\n        endif\n        iflt 1,2\n        nop\n        endif\n");
    EXPECT_TRUE(Has(lines, "IF -(1==2)"));   // zmac's true is 1, sjasmplus' is -1
    EXPECT_TRUE(Has(lines, "IF -(1<2)"));
}

TEST(ZmacFrontend_Test, PseudoOpsMayStartWithAPeriodAndSetIsDefl)
{
    const ir::Program p = Parse("zmac", "        .org 100h\nv       set 5\n        set 3,a\n");
    EXPECT_EQ(p.lines[0].statements[0].directive, ir::DirectiveKind::Org);
    EXPECT_EQ(p.lines[2].statements[0].mnemonic, "set");
}

TEST(ZmacFrontend_Test, BackslashSeparatesStatements)
{
    EXPECT_EQ(Parse("zmac", "        ld d,h \\ ld e,l\n").lines[0].statements.size(), 2u);
}

TEST(RasmFrontend_Test, MacroWithParenthesizedParametersAndLocalLabels)
{
    const ir::Program p = Parse("rasm", "macro copy (src, dst)\n@again: ld a,src\n jr @again\nmend\n");
    const ir::Statement& m = p.lines[0].statements[0];
    EXPECT_EQ(m.directive, ir::DirectiveKind::Macro);
    EXPECT_EQ(m.text, "copy");
    EXPECT_EQ(m.params, (std::vector<std::string>{"src", "dst"}));
    EXPECT_EQ(p.lines[0].statements[1].directive, ir::DirectiveKind::LocalBlock);   // @labels are local to the expansion
    EXPECT_EQ(p.lines[1].label, "@again");
    EXPECT_EQ(p.lines[3].statements[0].directive, ir::DirectiveKind::EndLocalBlock);
    EXPECT_EQ(p.lines[3].statements[1].directive, ir::DirectiveKind::EndMacro);
}

TEST(RasmFrontend_Test, MacroLocalLabelsKeepTheirNamesWithoutTheAt)
{
    const std::vector<std::string> lines = Sjasmplus("rasm", "macro m\n@x: nop\n jr @x\nmend\n m\n");
    EXPECT_TRUE(Has(lines, ".x NOP") || Has(lines, ".x") ) << "label";
    EXPECT_TRUE(Has(lines, "JR .x"));
}

TEST(RasmFrontend_Test, NumbersAndOperators)
{
    const ir::Statement s = Parse("rasm", " defb #ff,$ff,0xff,0ffh,%101,0b101,101b,@17,10 %% 3, 6 and 3, 1 xor 3\n").lines[0].statements[0];
    const int64_t expected[] = {255, 255, 255, 255, 5, 5, 5, 15};
    ASSERT_EQ(s.operands.size(), 11u);
    for (size_t k = 0; k < 8; ++k)
        EXPECT_EQ(s.operands[k].expr.value, expected[k]) << k;
    EXPECT_EQ(s.operands[8].expr.op, ir::Op::Mod);
}

TEST(RasmFrontend_Test, ColonSeparatesStatementsAndVariablesAreAssigned)
{
    const ir::Program p = Parse("rasm", "n=5\n ld a,n : inc a\n");
    EXPECT_EQ(p.lines[0].statements[0].directive, ir::DirectiveKind::Defl);
    EXPECT_EQ(p.lines[1].statements.size(), 2u);
}

TEST(RasmFrontend_Test, RepeatWithACounter)
{
    const std::vector<std::string> lines = Sjasmplus("rasm", " repeat 3,cnt\n defb cnt\n rend\n");
    EXPECT_TRUE(Has(lines, "DUP 3"));
    EXPECT_TRUE(Has(lines, "cnt=0"));
    EXPECT_TRUE(Has(lines, "cnt=cnt+1"));
}
