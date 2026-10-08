// The z80asm frontend: z88dk's syntax (labels, numbers, DEFC / DEFVARS / DEFGROUP, macros, # directives, the backslash
// statement separator, escapes in single-quoted constants), converted to sjasmplus. The suite's own cases and the
// assembler itself are the oracle in tools/verification/unreal-asm/checks/z80asmcheck.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "unrealasm/dialect.h"

using namespace unrealasm;

namespace
{
ir::Program ParseZ80asm(const std::string& text, bool z80n = false)
{
    SourceDocument doc = SourceDocument::FromText(text, "z80asm");
    doc.z80n = z80n;
    return DialectRegistry::Builtin().Frontend("z80asm")->Parse(doc).program;
}

std::vector<std::string> Sjasmplus(const std::string& text)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, "z80asm"), "sjasmplus");
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        const size_t from = l.text.find_first_not_of(" \t");
        if (from == std::string::npos || l.text[from] == ';')
            continue;
        std::string line;   // runs of blanks as one
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

TEST(Z80asmFrontend_Test, LabelsNeedAColonOrADot)
{
    const ir::Program p = ParseZ80asm("start: nop\n.loop\n        djnz loop\nnop\n");
    EXPECT_EQ(p.lines[0].label, "start");
    EXPECT_EQ(p.lines[1].label, "loop");
    EXPECT_EQ(p.lines[3].label, "");   // a bare name in column 0 is an opcode
    EXPECT_EQ(p.lines[3].statements[0].mnemonic, "nop");
}

TEST(Z80asmFrontend_Test, NumbersInEverySpelling)
{
    const ir::Program p = ParseZ80asm("        defb $1F,0x1F,1Fh,101b,%101,@101,0b101,'a'\n");
    const int64_t expected[] = {31, 31, 31, 5, 5, 5, 5, 97};
    const ir::Statement& s = p.lines[0].statements[0];
    ASSERT_EQ(s.operands.size(), 8u);
    for (size_t k = 0; k < 8; ++k)
        EXPECT_EQ(s.operands[k].expr.value, expected[k]) << k;
}

TEST(Z80asmFrontend_Test, AsmpcIsTheLocationCounter)
{
    EXPECT_EQ(ParseZ80asm("        defw asmpc\n").lines[0].statements[0].operands[0].expr.kind, ir::Expr::Kind::Current);
}

TEST(Z80asmFrontend_Test, CPriorities)
{
    // 1 + 2 << 3 is (1 + 2) << 3 in C
    const ir::Expr e = ParseZ80asm("        defb 1 + 2 << 3\n").lines[0].statements[0].operands[0].expr;
    ASSERT_EQ(e.kind, ir::Expr::Kind::Binary);
    EXPECT_EQ(e.op, ir::Op::Shl);
    EXPECT_EQ(e.args[0].op, ir::Op::Add);
}

TEST(Z80asmFrontend_Test, DefcEquAndAssignment)
{
    const ir::Program p = ParseZ80asm("        defc ca = 1, cb = ca + 1\ncc equ 3\ncd = 4\n");
    EXPECT_EQ(p.lines[0].label, "ca");
    EXPECT_EQ(p.lines[1].label, "cb");
    EXPECT_EQ(p.lines[1].statements[0].directive, ir::DirectiveKind::Equ);
    EXPECT_EQ(p.lines[3].label, "cc");   // line 2 is the DEFC line itself, now empty
    EXPECT_EQ(p.lines[4].label, "cd");
}

TEST(Z80asmFrontend_Test, DefineAndDefgroupAndDefvars)
{
    const std::vector<std::string> lines = Sjasmplus("        define x, y\n        defgroup\n        {\n ga, gb = 10, gc\n        }\n"
                                                     "        defvars 100\n        {\n v1 ds.b 2\n v2 ds.w 1\n v3\n        }\n");
    EXPECT_TRUE(Has(lines, "x EQU 1"));
    EXPECT_TRUE(Has(lines, "y EQU 1"));
    EXPECT_TRUE(Has(lines, "ga EQU 0"));
    EXPECT_TRUE(Has(lines, "gb EQU 10"));
    EXPECT_TRUE(Has(lines, "gc EQU gb+1"));
    EXPECT_TRUE(Has(lines, "v1 EQU 100"));
    EXPECT_TRUE(Has(lines, "v2 EQU v1+1*2"));
    EXPECT_TRUE(Has(lines, "v3 EQU v2+2*1"));
}

TEST(Z80asmFrontend_Test, HashDirectivesAndElif)
{
    const std::vector<std::string> lines = Sjasmplus("        #if 1\n        nop\n        #elif 2\n        halt\n        #else\n        ret\n        #endif\n");
    EXPECT_TRUE(Has(lines, "IF 1"));
    EXPECT_TRUE(Has(lines, "ELSE"));
    EXPECT_TRUE(Has(lines, "IF 2"));
    EXPECT_EQ(std::count(lines.begin(), lines.end(), "ENDIF"), 2);
}

TEST(Z80asmFrontend_Test, MacroForms)
{
    for (const char* text : {"        macro pushreg reg\n        endm\n", "pushreg macro reg\n        endm\n", "pushreg: macro reg\n        endm\n"})
    {
        const ir::Program p = ParseZ80asm(text);
        const ir::Statement& s = p.lines[0].statements[0];
        EXPECT_EQ(s.directive, ir::DirectiveKind::Macro) << text;
        EXPECT_EQ(s.text, "pushreg") << text;
        EXPECT_EQ(s.params, std::vector<std::string>{"reg"}) << text;
    }
}

TEST(Z80asmFrontend_Test, BackslashSeparatesStatements)
{
    const ir::Program p = ParseZ80asm("        defb 0 \\ defb 1 \\ defb 2\n");
    EXPECT_EQ(p.lines[0].statements.size(), 3u);
}

TEST(Z80asmFrontend_Test, SingleQuotedConstantsReadEscapes)
{
    const std::vector<std::string> lines = Sjasmplus("        defb '\\n','\\\\','\\377'\n");
    EXPECT_TRUE(Has(lines, "DB 10,'\\',255")) << "escapes";
}

TEST(Z80asmFrontend_Test, Z80nMnemonicsOnlyWhenTheDocumentSaysSo)
{
    EXPECT_EQ(ParseZ80asm("        swapnib\n").lines[0].statements[0].kind, ir::Statement::Kind::MacroCall);
    EXPECT_EQ(ParseZ80asm("        swapnib\n", true).lines[0].statements[0].kind, ir::Statement::Kind::Instruction);
}

TEST(Z80asmFrontend_Test, LinkerDirectivesAreKeptAsText)
{
    const ConvertResult r = Convert(SourceDocument::FromText("        section code\n        public main\n", "z80asm"), "sjasmplus");
    EXPECT_GE(std::count_if(r.diagnostics.begin(), r.diagnostics.end(), [](const Diagnostic& d) { return d.message.find("not converted") != std::string::npos; }), 2);
}
