// The FantASM frontend: labels with a colon or without, local .labels, CONST = expr, // comments, a colon between
// instructions, DH / DZ, MACRO with blank-separated parameters, STRUCT blocks kept as text; converted to sjasmplus. The
// sources of FantASM's tests against the FantASM binary are in tools/verification/unreal-asm/checks/fantasmcheck.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "unrealasm/dialect.h"

using namespace unrealasm;

namespace
{
ir::Program ParseFantasm(const std::string& text, bool z80n = false)
{
    SourceDocument doc = SourceDocument::FromText(text, "fantasm");
    doc.z80n = z80n;
    return DialectRegistry::Builtin().Frontend("fantasm")->Parse(doc).program;
}

std::vector<std::string> Sjasmplus(const std::string& text)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, "fantasm"), "sjasmplus");
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

TEST(FantasmFrontend_Test, LabelsWithAndWithoutColonAndLocalLabels)
{
    const ir::Program p = ParseFantasm("start:\n.loop\n  djnz .loop\nlabel0x40 ret\n");
    EXPECT_EQ(p.lines[0].label, "start");
    EXPECT_EQ(p.lines[1].label, ".loop");
    EXPECT_EQ(p.lines[3].label, "label0x40");
    EXPECT_EQ(p.lines[3].statements[0].mnemonic, "ret");
}

TEST(FantasmFrontend_Test, ConstantsAndNumbers)
{
    const ir::Program p = ParseFantasm("meaning = 42\nmask equ $FF\n  db 0x12EF & 0xFF, 012EFh & 255, %101, 101b\n");
    EXPECT_EQ(p.lines[0].statements[0].directive, ir::DirectiveKind::Equ);
    EXPECT_EQ(p.lines[1].statements[0].directive, ir::DirectiveKind::Equ);
}

TEST(FantasmFrontend_Test, BothCommentStylesAndAColonBetweenInstructions)
{
    const ir::Program p = ParseFantasm("  ld a,1 : inc a // add one\n  nop ; also a comment\n");
    ASSERT_EQ(p.lines[0].statements.size(), 2u);
    EXPECT_EQ(p.lines[0].statements[1].mnemonic, "inc");
    EXPECT_TRUE(p.lines[0].hasComment);
    EXPECT_EQ(p.lines[0].comment, " add one");
    EXPECT_TRUE(p.lines[1].hasComment);
}

TEST(FantasmFrontend_Test, HexAndZeroTerminatedStrings)
{
    const std::vector<std::string> lines = Sjasmplus("  dh \"12FF\"\n  dz \"AB\"\n");
    EXPECT_TRUE(Has(lines, "DB #12,#FF"));
    EXPECT_TRUE(Has(lines, "DB 'AB',0"));
}

TEST(FantasmFrontend_Test, MacroWithBlankSeparatedParameters)
{
    const ir::Statement s = ParseFantasm("  macro cls fore,back\n  endm\n").lines[0].statements[0];
    EXPECT_EQ(s.text, "cls");
    EXPECT_EQ(s.params, (std::vector<std::string>{"fore", "back"}));
}

TEST(FantasmFrontend_Test, StructBlocksAreKeptAsText)
{
    const ir::Program p = ParseFantasm("STRUCT Window\n  top.b\n  left.b\nEND\n  nop\n");
    EXPECT_EQ(p.lines[0].statements[0].directive, ir::DirectiveKind::Other);
    EXPECT_TRUE(p.lines[1].statements.empty());   // a member line
    EXPECT_EQ(p.lines[3].statements[0].directive, ir::DirectiveKind::Other);   // END of the struct, not the program's
    EXPECT_EQ(p.lines[4].statements[0].mnemonic, "nop");
}

TEST(FantasmFrontend_Test, Z80nMnemonicsOnlyWhenTheDocumentSaysSo)
{
    EXPECT_EQ(ParseFantasm("  swapnib\n").lines[0].statements[0].kind, ir::Statement::Kind::MacroCall);
    EXPECT_EQ(ParseFantasm("  swapnib\n", true).lines[0].statements[0].kind, ir::Statement::Kind::Instruction);
}

TEST(FantasmFrontend_Test, LocalLabelsGetUniqueNamesInPasmoAndZ80asm)
{
    const ConvertResult r = Convert(SourceDocument::FromText("a:\n.l  djnz .l\nb:\n.l  djnz .l\n", "fantasm"), "z88dk");
    const std::string text = r.document.Text();
    EXPECT_NE(text.find("a__l"), std::string::npos) << text;
    EXPECT_NE(text.find("b__l"), std::string::npos) << text;
}
