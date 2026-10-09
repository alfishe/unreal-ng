// The ZX Spectrum Next (Z80N) instructions: read by the sjasmplus frontend only in the Next mode (DEVICE ZXSPECTRUMNEXT,
// OPT --zxnext, or the project / caller says so), sized by the layout module, written as z80asm mnemonics (-mz80n) and
// as bytes for pasmo. The same bytes through sjasmplus, z80asm and pasmo are checked by tools/verification/unreal-asm.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "unrealasm/dialect.h"
#include "unrealasm/layout.h"

using namespace unrealasm;

namespace
{
SourceDocument Sjasmplus(const std::string& text)
{
    return SourceDocument::FromText(text, "sjasmplus");
}

/// The lines of the converted source, leading blanks trimmed, empty and comment-only lines dropped
std::vector<std::string> Lines(const ConvertResult& r)
{
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        size_t from = l.text.find_first_not_of(" \t");
        if (from != std::string::npos && l.text[from] != ';')
            out.push_back(l.text.substr(from));
    }
    return out;
}

bool Has(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

const IFrontend& SjasmplusFrontend()
{
    return *DialectRegistry::Builtin().Frontend("sjasmplus");
}

ir::Statement::Kind FirstKind(const ir::Program& p)
{
    for (const ir::Line& l : p.lines)
        if (!l.statements.empty())
            return l.statements[0].kind;
    return ir::Statement::Kind::Raw;
}
}  // namespace

TEST(Z80n_Frontend_Test, AClassicSourceKeepsTestAndMirrorFreeForLabelsAndMacros)
{
    const ir::Program p = SjasmplusFrontend().Parse(Sjasmplus("        test 5\n")).program;
    EXPECT_EQ(FirstKind(p), ir::Statement::Kind::MacroCall);
    EXPECT_FALSE(p.z80n);
}

TEST(Z80n_Frontend_Test, TheModeComesFromTheDeviceOrTheOptionOrTheDocument)
{
    for (const char* enable : {"        DEVICE ZXSPECTRUMNEXT\n", "        OPT --zxnext=cspect\n", "        opt --zxnext\n"})
    {
        const ir::Program p = SjasmplusFrontend().Parse(Sjasmplus(std::string(enable) + "        swapnib\n")).program;
        EXPECT_TRUE(p.z80n) << enable;
        EXPECT_EQ(p.lines.back().statements[0].kind, ir::Statement::Kind::Instruction) << enable;
    }
    SourceDocument doc = Sjasmplus("        mirror a\n");
    doc.z80n = true;
    const ir::Program p = SjasmplusFrontend().Parse(doc).program;
    EXPECT_EQ(FirstKind(p), ir::Statement::Kind::Instruction);
}

TEST(Z80n_Frontend_Test, TheModeBeforeTheEnablingLineDoesNotApply)
{
    const ir::Program p = SjasmplusFrontend().Parse(Sjasmplus("        swapnib\n        OPT --zxnext\n        swapnib\n")).program;
    EXPECT_EQ(p.lines[0].statements[0].kind, ir::Statement::Kind::MacroCall);
    EXPECT_EQ(p.lines[2].statements[0].kind, ir::Statement::Kind::Instruction);
}

TEST(Z80n_Convert_Test, AnIncludedFileTakesTheModeFromTheProject)
{
    const std::vector<ProjectFile> files = {{"main", Sjasmplus("        OPT --zxnext\n        INCLUDE \"part\"\n")},
                                            {"part", Sjasmplus("        nextreg 7,3\n")}};
    const ProjectResult r = ConvertProject(files, "z88dk");
    ASSERT_EQ(r.files.size(), 2u);
    EXPECT_TRUE(std::any_of(r.files[1].document.lines.begin(), r.files[1].document.lines.end(),
                            [](const SourceLine& l) { return l.text.find("NEXTREG 7,3") != std::string::npos; }));
}

TEST(Z80n_Convert_Test, TheCallerCanSwitchTheModeOn)
{
    BackendOptions options;
    options.z80n = true;
    const ConvertResult r = Convert(Sjasmplus("        ldirx\n"), "z88dk", options);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(Has(Lines(r), "LDIRX"));
}

TEST(Z80n_Z88dk_Test, TheInstructionsAreWrittenInZ80asmSpelling)
{
    const ConvertResult r = Convert(Sjasmplus("        OPT --zxnext\n"
                                              "        swapnib a\n        mirror a\n        test 85\n        bsla de,b\n        mul de\n"
                                              "        add hl,a\n        add bc,$1234\n        push $1234\n        nextreg 7,3\n"
                                              "        nextreg 7,a\n        pixeldn hl\n        pixelad\n        setae\n        jp (c)\n"
                                              "        ldirx\n        ldpirx\n        lddrx\n"),
                                    "z88dk");
    EXPECT_TRUE(r.ok);
    const std::vector<std::string> lines = Lines(r);
    for (const char* expected : {"SWAPNIB", "MIRROR A", "TEST 85", "BSLA DE,B", "MUL D,E", "ADD HL,A", "ADD BC,$1234", "PUSH $1234", "NEXTREG 7,3",
                                 "NEXTREG 7,A", "PIXELDN", "PIXELAD", "SETAE", "JP (C)", "LDIRX", "LDPIRX", "LDDRX"})
        EXPECT_TRUE(Has(lines, expected)) << expected;
    // no warning about the directive that switches the mode on
    for (const Diagnostic& d : r.diagnostics)
        EXPECT_EQ(d.message.find("zxnext"), std::string::npos) << d.message;
}

TEST(Z80n_Pasmo_Test, PasmoHasNoZ80nSoItGetsTheBytes)
{
    const ConvertResult r = Convert(Sjasmplus("        OPT --zxnext\n        swapnib\n        add de,a\n        push $1234\n        add hl,$1234\n"
                                              "        nextreg 7,a\n        jp (c)\n        ldirscale\n"),
                                    "pasmo");
    EXPECT_TRUE(r.ok);
    const std::vector<std::string> lines = Lines(r);
    EXPECT_TRUE(Has(lines, "DB 237,35"));
    EXPECT_TRUE(Has(lines, "DB 237,50"));
    EXPECT_TRUE(Has(lines, "DB 237,152"));
    EXPECT_TRUE(Has(lines, "DB 237,182"));
    // PUSH nn is the one instruction of the set with its word high byte first
    const auto push = std::find_if(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("DB 237,138,", 0) == 0; });
    ASSERT_NE(push, lines.end());
    EXPECT_NE(push->find("/256,"), std::string::npos);
    const auto add = std::find_if(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("DB 237,52,", 0) == 0; });
    ASSERT_NE(add, add == lines.end() ? lines.begin() : lines.end());
    EXPECT_NE(add->find("&255,"), std::string::npos);
}

TEST(Z80n_Layout_Test, TheSizesFollowTheInstructionTable)
{
    // label after each instruction = the running size
    const std::string text =
        "        OPT --zxnext\n        ORG 0\n"
        "a1:     swapnib\n"          // 2
        "a2:     test 5\n"           // 3
        "a3:     add hl,a\n"         // 2
        "a4:     add de,1234h\n"     // 4
        "a5:     push 1234h\n"       // 4
        "a6:     nextreg 7,3\n"      // 4
        "a7:     nextreg 7,a\n"      // 3
        "a8:     jp (c)\n"           // 2
        "a9:     ldirx\n"            // 2
        "a10:    mul d,e\n"          // 2
        "end:\n";
    const layout::LayoutResult r = layout::Layout({{"main", Sjasmplus(text)}}, 0);
    ASSERT_TRUE(r.ok);
    std::map<std::string, int64_t> at;
    for (const layout::Label& l : r.labels)
        at[l.name] = l.value;
    EXPECT_EQ(at["a1"], 0);
    EXPECT_EQ(at["a2"], 2);
    EXPECT_EQ(at["a3"], 5);
    EXPECT_EQ(at["a4"], 7);
    EXPECT_EQ(at["a5"], 11);
    EXPECT_EQ(at["a6"], 15);
    EXPECT_EQ(at["a7"], 19);
    EXPECT_EQ(at["a8"], 22);
    EXPECT_EQ(at["a9"], 24);
    EXPECT_EQ(at["a10"], 26);
    EXPECT_EQ(at["end"], 28);
}

TEST(Z80n_Sjasmplus_Test, AnSjasmplusSourceKeepsItsLabelsNamedLikeRegisters)
{
    const ConvertResult r = Convert(Sjasmplus("a       DB 1\nb       DB 2\n        DW a,b\n"), "sjasmplus");
    EXPECT_TRUE(r.ok);
    const std::string text = r.document.Text();
    EXPECT_EQ(text.find("L_a"), std::string::npos) << text;
    EXPECT_NE(text.find("DW a,b"), std::string::npos) << text;
}

TEST(Z80n_Sjasmplus_Test, DoubleQuotedStringsReadTheEscapesInEitherCase)
{
    const ConvertResult r = Convert(Sjasmplus("        DB \"\\A\\a\\D\\?\\N\"\n"), "sjasmplus");
    EXPECT_TRUE(r.ok);
    const std::string text = r.document.Text();
    EXPECT_NE(text.find("7,7,127,63,10"), std::string::npos) << text;
}
