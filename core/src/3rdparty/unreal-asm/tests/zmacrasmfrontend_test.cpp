// The zmac frontend: the constructs the dialect has that the other text dialects lack (two-character constants, IFEQ and
// friends, word operators, the backslash separator), converted to sjasmplus. The real assembler decides on the sources of
// testdata/zmac in tools/verification/unreal-asm/checks/dialectcheck.py. The rasm frontend: rasmfrontend_test.cpp.

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
