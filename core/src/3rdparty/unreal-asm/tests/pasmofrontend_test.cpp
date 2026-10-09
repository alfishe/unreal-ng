// The pasmo frontend: the syntax of pasmo's manual (number spellings, operator priorities, labels, MACRO / REPT / IF
// forms, file names), converted to sjasmplus. The same sources through pasmo itself and the other assemblers are checked
// by tools/verification/unreal-asm/checks/pasmocheck.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"

using namespace unrealasm;

namespace
{
ir::Program ParsePasmo(const std::string& text, Diagnostics* diagnostics = nullptr)
{
    FrontendResult r = DialectRegistry::Builtin().Frontend("pasmo")->Parse(SourceDocument::FromText(text, "pasmo"));
    if (diagnostics)
        *diagnostics = r.diagnostics;
    return std::move(r.program);
}

const ir::Statement& First(const ir::Program& p)
{
    for (const ir::Line& l : p.lines)
        if (!l.statements.empty())
            return l.statements[0];
    static const ir::Statement none;
    return none;
}

std::vector<std::string> Sjasmplus(const std::string& text)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, "pasmo"), "sjasmplus");
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        const size_t from = l.text.find_first_not_of(" \t");
        if (from != std::string::npos && l.text[from] != ';')
            out.push_back(l.text.substr(from));
    }
    return out;
}

bool Has(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}
}  // namespace

TEST(PasmoFrontend_Test, NumbersInEverySpelling)
{
    const ir::Program p = ParsePasmo("        db $FF,#FF,&HFF,&XFF,&FF,%101,0xFF,0FFh,101b,17o,17q,12d,1$000\n");
    const ir::Statement& s = First(p);
    ASSERT_EQ(s.operands.size(), 13u);
    const int64_t expected[] = {255, 255, 255, 255, 255, 5, 255, 255, 5, 15, 15, 12, 1000};
    for (size_t k = 0; k < 13; ++k)
        EXPECT_EQ(s.operands[k].expr.value, expected[k]) << k;
}

TEST(PasmoFrontend_Test, FlagsOfTheProgram)
{
    const ir::Program p = ParsePasmo("        nop\n");
    EXPECT_EQ(p.expressionBits, 16);
    EXPECT_TRUE(p.unsignedArithmetic);
    EXPECT_EQ(p.trueValue, -1);
}

TEST(PasmoFrontend_Test, UnaryBindsLooserThanComparisonAndTighterThanAnd)
{
    // NOT a = b is NOT (a = b); a AND NOT b is a AND (NOT b); HIGH is the loosest
    const ir::Expr e = First(ParsePasmo("        ld a,not 1 = 2\n")).operands[1].expr;
    ASSERT_EQ(e.kind, ir::Expr::Kind::Unary);
    EXPECT_EQ(e.op, ir::Op::Not);
    EXPECT_EQ(e.args[0].kind, ir::Expr::Kind::Binary);
    EXPECT_EQ(e.args[0].op, ir::Op::Equal);
    const ir::Expr h = First(ParsePasmo("        ld a,high 1 + 2\n")).operands[1].expr;
    ASSERT_EQ(h.kind, ir::Expr::Kind::Unary);
    EXPECT_EQ(h.op, ir::Op::High);
    EXPECT_EQ(h.args[0].op, ir::Op::Add);
}

TEST(PasmoFrontend_Test, LabelsWithAndWithoutColonAndEquDefl)
{
    const ir::Program p = ParsePasmo("start: nop\nloop  djnz loop\nval   equ 5\nvar   defl val+1\n");
    EXPECT_EQ(p.lines[0].label, "start");
    EXPECT_EQ(p.lines[1].label, "loop");
    EXPECT_EQ(p.lines[2].statements[0].directive, ir::DirectiveKind::Equ);
    EXPECT_EQ(p.lines[3].statements[0].directive, ir::DirectiveKind::Defl);
}

TEST(PasmoFrontend_Test, ADollarInsideAnIdentifierIsIgnored)
{
    const ir::Program p = ParsePasmo("call$msg nop\n        ld hl,call$msg\n");
    EXPECT_EQ(p.lines[0].label, "callmsg");
    EXPECT_EQ(p.lines[1].statements[0].operands[1].expr.text, "callmsg");
}

TEST(PasmoFrontend_Test, BracketsMeanTheSameAsParenthesesForIndirections)
{
    const ir::Statement s = First(ParsePasmo("        ld a,[ix+5]\n"));
    EXPECT_EQ(s.operands[1].kind, ir::Operand::Kind::Indexed);
    EXPECT_EQ(First(ParsePasmo("        ld a,[hl]\n")).operands[1].kind, ir::Operand::Kind::Indirect);
}

TEST(PasmoFrontend_Test, MacroInBothSpellings)
{
    const ir::Program a = ParsePasmo("twice macro x,y\n        endm\n");
    EXPECT_EQ(a.lines[0].statements[0].text, "twice");
    EXPECT_EQ(a.lines[0].statements[0].params, (std::vector<std::string>{"x", "y"}));
    EXPECT_TRUE(a.lines[0].label.empty());
    const ir::Program b = ParsePasmo("        macro twice,x,y\n        endm\n");
    EXPECT_EQ(b.lines[0].statements[0].text, "twice");
    EXPECT_EQ(b.lines[0].statements[0].params.size(), 2u);
}

TEST(PasmoFrontend_Test, EndmClosesTheIfsOpenInsideAndReptEndsAsARepeat)
{
    const ir::Program p = ParsePasmo("        macro m\n        if 1\n        nop\n        endm\n        rept 3\n        nop\n        endm\n");
    // line 3 (index 3): ENDIF then ENDMACRO
    ASSERT_EQ(p.lines[3].statements.size(), 2u);
    EXPECT_EQ(p.lines[3].statements[0].directive, ir::DirectiveKind::EndIf);
    EXPECT_EQ(p.lines[3].statements[1].directive, ir::DirectiveKind::EndMacro);
    EXPECT_EQ(p.lines[4].statements[0].directive, ir::DirectiveKind::Repeat);
    EXPECT_EQ(p.lines[6].statements[0].directive, ir::DirectiveKind::EndRepeat);
}

TEST(PasmoFrontend_Test, ReptWithALoopVariable)
{
    const std::vector<std::string> lines = Sjasmplus("        rept 3,n,2,-1\n        db n\n        endm\n");
    EXPECT_TRUE(Has(lines, "n=2"));
    EXPECT_TRUE(Has(lines, "n=n+-1"));
    EXPECT_TRUE(Has(lines, "DUP 3"));
}

TEST(PasmoFrontend_Test, IfdefAndIfndefBecomeExistTests)
{
    const std::vector<std::string> lines = Sjasmplus("        ifdef foo\n        nop\n        endif\n        ifndef bar\n        nop\n        endif\n");
    EXPECT_TRUE(Has(lines, "IF exist foo"));
    EXPECT_TRUE(Has(lines, "IF !exist bar"));
}

TEST(PasmoFrontend_Test, IncludeNamesAreVerbatimFileNames)
{
    const ir::Statement s = First(ParsePasmo("        include \"lib/if.asm\"\n"));
    EXPECT_EQ(s.text, "lib/if.asm");
    EXPECT_EQ(s.params, std::vector<std::string>{"verbatim"});
    EXPECT_EQ(First(ParsePasmo("        include data.inc ; comment\n")).text, "data.inc");
}

TEST(PasmoFrontend_Test, LinesAfterEndAreIgnored)
{
    const ir::Program p = ParsePasmo("        nop\n        end start\n        ld a,1\n");
    EXPECT_EQ(p.lines[1].statements[0].directive, ir::DirectiveKind::End);
    EXPECT_TRUE(p.lines[2].statements.empty());
    EXPECT_TRUE(p.lines[2].hasComment);
}

TEST(PasmoFrontend_Test, WhatHasNoCounterpartIsReported)
{
    Diagnostics d;
    ConvertResult r = Convert(SourceDocument::FromText("        irp x,1,2\n        db x\n        endm\n", "pasmo"), "sjasmplus");
    EXPECT_TRUE(std::any_of(r.diagnostics.begin(), r.diagnostics.end(), [](const Diagnostic& g) { return g.message.find("IRP") != std::string::npos; }));
}

TEST(PasmoFrontend_Test, StringsKeepControlCharactersOutOfQuotes)
{
    const ConvertResult r = Convert(SourceDocument::FromText("        db \"a\\nb\"\n", "pasmo"), "pasmo");
    EXPECT_TRUE(r.ok);
    const std::string text = r.document.Text();
    EXPECT_NE(text.find("'a',10,'b'"), std::string::npos) << text;
}

TEST(PasmoFrontend_Test, TheCodecFindsPasmoOnlyDirectives)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    const ISourceCodec* pasmo = registry.Find("pasmo");
    ASSERT_NE(pasmo, nullptr);
    const std::string text = "start proc\n  local x\nx nop\n  endp\n";
    EXPECT_GT(pasmo->Detect({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, {}), 0);
    const std::string plain = "  ld a,1\n";
    EXPECT_EQ(pasmo->Detect({reinterpret_cast<const uint8_t*>(plain.data()), plain.size()}, {}), 0);
}
