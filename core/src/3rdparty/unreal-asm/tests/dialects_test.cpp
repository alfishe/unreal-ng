// Dialect conversion (A5): the ALASM frontend, the sjasmplus frontend and backend. Construct by construct, a golden
// conversion whose binary ALASM 5.09 itself built (testdata/dialects/alasm-sjasmplus), a real project unit from The
// Link (testdata/dialects/thelink), and the sjasmplus round trip. With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the
// converted sources are also assembled and compared with the binaries ALASM built.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/alasm/alasmcodec.h"
#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;
using unrealasm::testing::TestDataPath;

namespace
{
/// The sjasmplus lines (statements only, blank lines dropped, leading blanks trimmed) of an ALASM source
std::vector<std::string> ToSjasmplus(const std::string& alasm, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(alasm, "alasm"), "sjasmplus");
    EXPECT_TRUE(r.ok);
    if (diagnostics)
        *diagnostics = r.diagnostics;
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        std::string t = l.text;
        const size_t first = t.find_first_not_of(' ');
        if (first == std::string::npos)
            continue;
        out.push_back(t.substr(first));
    }
    return out;
}

bool Has(const Diagnostics& diagnostics, const std::string& part)
{
    for (const Diagnostic& d : diagnostics)
        if (d.message.find(part) != std::string::npos)
            return true;
    return false;
}

ir::Program ParseSjasmplus(const std::string& text)
{
    return DialectRegistry::Builtin().Frontend("sjasmplus")->Parse(SourceDocument::FromText(text, "sjasmplus")).program;
}

/// The text again after sjasmplus frontend + backend
std::string Normalized(const std::string& sjasmplus)
{
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "sjasmplus");
    return r.document.Text();
}

std::string WithoutCarriageReturns(std::string s)
{
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    while (!s.empty() && s.back() == '\n')
        s.pop_back();
    return s;
}

/// A fresh directory for the files an external assembler run needs
std::filesystem::path ScratchDirectory()
{
    std::random_device random;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("unreal-asm-tests-" + std::to_string(stamp) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Runs sjasmplus on `harness` in `dir`; false when it reports errors
bool RunSjasmplus(const std::string& sjasmplus, const std::filesystem::path& dir, const std::string& harness)
{
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo " + harness + " > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo " + harness + " > out.txt 2>&1";
#endif
    return std::system(command.c_str()) == 0;
}

containers::TrdosFile Hobeta(const std::string& relative)
{
    containers::TrdosFile file;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData(relative), file, error)) << relative << ": " << error;
    return file;
}

/// The GSTUNNE4 unit of The Link (main source, the GS port names it includes, the object saver) converted together
ProjectResult TheLinkUnit()
{
    const codecs::AlasmCodec alasm;
    std::vector<ProjectFile> project;
    for (const char* name : {"GSTUNNE4", "gsports", "SAVEOBJ4"})
    {
        const containers::TrdosFile file = Hobeta(std::string("dialects/thelink/") + name + ".$H");
        DecodeOptions options;
        options.catalog = file.Hints();
        project.push_back({file.TrimmedName(), alasm.Decode(file.data, options).document});
    }
    return ConvertProject(project, "sjasmplus");
}
}  // namespace

// --- ALASM -> sjasmplus, construct by construct ---------------------------------------------------------------------

TEST(Dialects_Test, AlasmEvaluatesLeftToRightWithoutPriorities)
{
    EXPECT_EQ(ToSjasmplus("        LD A,1+2*3"), std::vector<std::string>{"LD A,(1+2)*3"});
    EXPECT_EQ(ToSjasmplus("        LD A,2*3+1"), std::vector<std::string>{"LD A,2*3+1"});
}

TEST(Dialects_Test, AlasmHighLowXorAndRotations)
{
    EXPECT_EQ(ToSjasmplus("        LD A,'x\n        LD A,.x\n        LD A,5!3"),
              (std::vector<std::string>{"LD A,high x", "LD A,low x", "LD A,5^3"}));
    // < rotates a 16-bit word; a value wholly in parentheses gets "+" so that sjasmplus does not read memory
    EXPECT_EQ(ToSjasmplus("        LD BC,#1234<4"), std::vector<std::string>{"LD BC,+(((#1234&#FFFF)<<4|(#1234&#FFFF)>>>(16-4))&#FFFF)"});
}

TEST(Dialects_Test, AlasmDivisionIsUnsigned16Bit)
{
    // (0-2)/2 is #7FFF in ALASM, -1 in 32-bit signed sjasmplus without the masks
    EXPECT_EQ(ToSjasmplus("        DW 0-2/2"), std::vector<std::string>{"DW (0-2&#FFFF)/(2&#FFFF)"});
}

TEST(Dialects_Test, AlasmDefinedTestBecomesExist)
{
    EXPECT_EQ(ToSjasmplus("        IF0 ?x\n        ENDIF\n        IFN ?x\n        ENDIF"),
              (std::vector<std::string>{"IF exist x", "ENDIF", "IF !exist x", "ENDIF"}));
    EXPECT_EQ(ToSjasmplus("        DW ?x"), std::vector<std::string>{"DW ((!exist x)*#FFFF)"});
}

TEST(Dialects_Test, AlasmParenthesisFirstOperandIsMemoryAndTheRestIgnored)
{
    // ALASM 5.09 assembles LD DE,(65536-46)*98/256 as LD DE,(#FFD2)
    Diagnostics diagnostics;
    EXPECT_EQ(ToSjasmplus("        LD DE,(65536-46)*98/256", &diagnostics), std::vector<std::string>{"LD DE,(65536-46)"});
    EXPECT_TRUE(Has(diagnostics, "as memory and ignores"));
}

TEST(Dialects_Test, AlasmPseudoInstructionsAndMultiOperandLines)
{
    EXPECT_EQ(ToSjasmplus("        EXA\n        EXD\n        JNZ $\n        INF\n        LD L,0,H,1"),
              (std::vector<std::string>{"EX AF,AF'", "EX DE,HL", "JR NZ,$", "IN F,(C)", "LD L,0", "LD H,1"}));
}

TEST(Dialects_Test, LabelsSjasmplusWouldReadAsRegistersAreRenamed)
{
    // ALASM keywords are capitals: a lower-case "iy" is a label (here or in another file of the project)
    EXPECT_EQ(ToSjasmplus("iy      EQU 7\n        LD IY,iy\n        SET b,(HL)"),
              (std::vector<std::string>{"L_iy    EQU 7", "LD IY,L_iy", "SET L_b,(HL)"}));
}

TEST(Dialects_Test, LocalBlocksGetUniqueNamesAndAtLabelsStayGlobal)
// (@ is part of the name in ALASM; sjasmplus reads "@X" as X, so the name is renamed)
{
    EXPECT_EQ(ToSjasmplus("        LOCAL\nloop    DJNZ loop\n@out    NOP\n        ENDL\n        LOCAL\nloop    DJNZ loop\n        ENDL\n        JP @out"),
              (std::vector<std::string>{"loop__L1 DJNZ loop__L1", "L__out  NOP", "loop__L2 DJNZ loop__L2", "JP L__out"}));
    // A label defined in a block and used outside it is global (ALASM help, LOCAL)
    EXPECT_EQ(ToSjasmplus("        JP inner\n        LOCAL\ninner   NOP\n        ENDL"), (std::vector<std::string>{"JP inner", "inner   NOP"}));
}

TEST(Dialects_Test, MacrosGetNamedParametersAndCallsGetEveryArgument)
{
    EXPECT_EQ(ToSjasmplus("        MACRO FILL\n        LD A,\\0\n        LD (\\1),A\n        ENDM\n        FILL 1,#4000\n        FILL 'x"),
              (std::vector<std::string>{"MACRO FILL _arg0,_arg1", "LD A,_arg0", "LD (_arg1),A", "ENDM", "FILL 1,#4000", "FILL high x,"}));
}

TEST(Dialects_Test, MacrosGluingOrWalkingParametersAreExpanded)
{
    Diagnostics diagnostics;
    const std::vector<std::string> glued = ToSjasmplus("        MACRO PAIR\nlbl\\0   DB \\1\n        ENDM\n        PAIR 1,10", &diagnostics);
    EXPECT_NE(std::find(glued.begin(), glued.end(), "lbl1    DB 10"), glued.end());
    EXPECT_TRUE(Has(diagnostics, "expanded at its calls"));
    // \P returns parameter 0 and shifts the numbering, \R restores it: \9 after one \P is the 11th parameter
    const std::vector<std::string> shifted = ToSjasmplus("        MACRO SKIP\n_=\\P\n        DB \\0,\\9\\R,\\0\n        ENDM\n        SKIP 0,1,2,3,4,5,6,7,8,9,10");
    EXPECT_NE(std::find(shifted.begin(), shifted.end(), "DB 1,10,0"), shifted.end());
    // \C is the symbol at the pointer, \N moves it (ALASM help, MACRO DOWN)
    const std::vector<std::string> walked = ToSjasmplus("        MACRO DOWN\n        INC \\C\n        INC \\N\\C\n        INC \\R\\C\n        ENDM\n        DOWN HL");
    EXPECT_NE(std::find(walked.begin(), walked.end(), "INC H"), walked.end());
    EXPECT_NE(std::find(walked.begin(), walked.end(), "INC L"), walked.end());
}

TEST(Dialects_Test, RepeatUntilBecomesWhile)
{
    EXPECT_EQ(ToSjasmplus("        REPEAT\n        NOP\n        UNTIL0 0"),
              (std::vector<std::string>{"__repeat1=1", "WHILE __repeat1", "NOP", "__repeat1=(0)!=0", "ENDW"}));
}

TEST(Dialects_Test, DsWithAPatternRepeatsIt)
{
    EXPECT_EQ(ToSjasmplus("        DS 4,#AA,#55"), (std::vector<std::string>{"DUP 4", "DB #AA,#55", "EDUP"}));
}

TEST(Dialects_Test, IncbinSizeBecomesOffsetAndLength)
{
    EXPECT_EQ(ToSjasmplus("        INCBIN \"pic\",#1000"), std::vector<std::string>{"INCBIN \"pic\",0,#1000"});
}

TEST(Dialects_Test, ConstructsMatchTheGoldenConversion)
{
    const ConvertResult r = Convert(SourceDocument::FromText(ReadTestText("dialects/alasm-sjasmplus/constructs.alasm.txt"), "alasm"), "sjasmplus");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(WithoutCarriageReturns(r.document.Text()), WithoutCarriageReturns(ReadTestText("dialects/alasm-sjasmplus/constructs.sjasmplus.asm")));
}

// --- The sjasmplus frontend ----------------------------------------------------------------------------------------

TEST(Dialects_Test, SjasmplusFrontendUsesOperatorPriorities)
{
    const ir::Program p = ParseSjasmplus("        LD A,1+2*3");
    ASSERT_EQ(p.lines.size(), 1u);
    const ir::Operand& o = p.lines[0].statements[0].operands[1];
    ASSERT_EQ(o.kind, ir::Operand::Kind::Immediate);
    EXPECT_EQ(o.expr.op, ir::Op::Add);
    EXPECT_EQ(o.expr.args[1].op, ir::Op::Mul);
}

TEST(Dialects_Test, SjasmplusFrontendReadsOperandsLabelsAndDirectives)
{
    const ir::Program p = ParseSjasmplus("start:  LD A,(IX-5)\n        LD HL,(table)\n        LD HL,+(1+2)\nx=5\ny EQU 6\n        DEVICE ZXSPECTRUM128");
    ASSERT_EQ(p.lines.size(), 6u);
    EXPECT_EQ(p.lines[0].label, "start");
    EXPECT_EQ(p.lines[0].statements[0].operands[1].kind, ir::Operand::Kind::Indexed);
    EXPECT_EQ(p.lines[1].statements[0].operands[1].kind, ir::Operand::Kind::Memory);
    EXPECT_EQ(p.lines[2].statements[0].operands[1].kind, ir::Operand::Kind::Immediate);
    EXPECT_EQ(p.lines[3].statements[0].directive, ir::DirectiveKind::Defl);
    EXPECT_EQ(p.lines[4].statements[0].directive, ir::DirectiveKind::Equ);
    EXPECT_EQ(p.lines[5].statements[0].directive, ir::DirectiveKind::Other);
}

TEST(Dialects_Test, SjasmplusMacrosComeBeforeDirectivesCaseSensitively)
{
    // sjasmplus 1.23: with MACRO dB defined, "dB 5" calls it and "DB 5" is the directive
    const ir::Program p = ParseSjasmplus("        MACRO dB a\n        DB a+1\n        ENDM\n        dB 5\n        DB 5");
    EXPECT_EQ(p.lines[0].statements[0].params, std::vector<std::string>{"a"});
    EXPECT_EQ(p.lines[3].statements[0].kind, ir::Statement::Kind::MacroCall);
    EXPECT_EQ(p.lines[4].statements[0].directive, ir::DirectiveKind::Db);
}

TEST(Dialects_Test, SjasmplusRoundTripKeepsTheText)
{
    const std::string text = ReadTestText("dialects/alasm-sjasmplus/constructs.sjasmplus.asm");
    EXPECT_EQ(WithoutCarriageReturns(Normalized(text)), WithoutCarriageReturns(text));
    const std::string hand = "        DEVICE ZXSPECTRUM128\n"
                             "        ORG #8000\n"
                             "start   LD A,(1+2)*3            ; a comment\n"   // comments start in column 32
                             "        MACRO PUT value,where\n"
                             "        LD (where),value\n"
                             "        ENDM\n"
                             "        PUT 1,#4000\n"
                             "        IFDEF DEBUG\n"
                             "        NOP\n"
                             "        ENDIF\n"
                             "        SAVEBIN \"out.bin\",start,$-start";
    EXPECT_EQ(Normalized(hand), hand);
}

TEST(Dialects_Test, TheLinkUnitMatchesTheGoldenConversion)
{
    const ProjectResult r = TheLinkUnit();
    ASSERT_TRUE(r.ok);
    const codecs::SjasmplusCodec codec;
    for (const ProjectFile& file : r.files)
    {
        const std::vector<uint8_t> bytes = codec.Encode(file.document, {}).bytes;
        const std::string golden = "dialects/thelink/" + file.name + ".asm";
        if (std::getenv("UNREAL_ASM_UPDATE_GOLDEN"))
            WriteBytes(TestDataPath(golden), bytes);
        EXPECT_EQ(bytes, ReadTestData(golden)) << golden;
    }
}

// --- Assembled by sjasmplus (opt-in: UNREAL_ASM_SJASMPLUS) ----------------------------------------------------------

TEST(Dialects_Test, ConstructsAssembleToWhatAlasmBuilt)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    const std::filesystem::path dir = ScratchDirectory();
    WriteBytes(dir / "constructs.asm", ReadTestData("dialects/alasm-sjasmplus/constructs.sjasmplus.asm"));
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"constructs.asm\"\n        SAVEBIN \"out.bin\",#6000,$-#6000\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
    EXPECT_TRUE(RunSjasmplus(sjasmplus, dir, "harness.asm"));
    EXPECT_EQ(ReadBytes(dir / "out.bin"), ReadTestData("dialects/alasm-sjasmplus/constructs.bin"));   // ALASM 5.09's bytes
    std::filesystem::remove_all(dir);
}

TEST(Dialects_Test, TheLinkUnitAssemblesToWhatAlasmBuilt)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    const std::filesystem::path dir = ScratchDirectory();
    const codecs::SjasmplusCodec codec;
    for (const ProjectFile& file : TheLinkUnit().files)
        WriteBytes(dir / (file.name + ".asm"), codec.Encode(file.document, {}).bytes);
    for (const char* binary : {"torusr.$p", "dplan2r.$p", "thelinkm.$C"})
    {
        const containers::TrdosFile file = Hobeta(std::string("dialects/thelink/") + binary);
        // INCBIN names the file as ALASM does: the type letter after a dot unless it is C
        WriteBytes(dir / (file.TrimmedName() + (file.type == 'C' ? std::string() : std::string(".") + file.type)), file.data);
    }
    // The objects GSTUNNE4's SAVEOBJ table lists, from the pages the source names
    const std::string harness = "        INCLUDE \"GSTUNNE4.asm\"\n        SLOT 3\n"
                                "        PAGE pgtunnelzx\n        SAVEBIN \"zx.bin\",#C000,LENTUNNELZX\n"
                                "        PAGE pgtunnelgs\n        SAVEBIN \"gs.bin\",#C000,LENTUNNELGS\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
    EXPECT_TRUE(RunSjasmplus(sjasmplus, dir, "harness.asm"));
    EXPECT_EQ(ReadBytes(dir / "zx.bin"), Hobeta("dialects/thelink/TUNNELZX.$C").data);
    EXPECT_EQ(ReadBytes(dir / "gs.bin"), Hobeta("dialects/thelink/TUNNELGS.$C").data);
    std::filesystem::remove_all(dir);
}

// --- Rules found on real sources -----------------------------------------------------------------------------------

TEST(Dialects_Test, AlasmQuotesColonParametersAndOldSpellings)
{
    // "" inside a text is one quote (ALASM's own sources write CP """)
    EXPECT_EQ(ToSjasmplus("        CP \"\"\""), std::vector<std::string>{"CP '\"'"});
    // ALASM 4.4x writes macro parameters as :0 (its SAVEOBJ 2.1)
    const std::vector<std::string> colon = ToSjasmplus("        MACRO SV\n        DB :0,:1\n        ENDM\n        SV 1,2");
    EXPECT_NE(std::find(colon.begin(), colon.end(), "DB _arg0,_arg1"), colon.end());
    // A word before EQU in column 0 is a label even when a later version made it a keyword
    EXPECT_EQ(ToSjasmplus("DD      EQU 5"), std::vector<std::string>{"DD      EQU 5"});
    // DD "text": the code older versions show as DEFM
    EXPECT_EQ(ToSjasmplus("        DD    \"XY\""), std::vector<std::string>{"DB 'XY'"});
    // DATA: is DATA; a character constant is a 16-bit word
    EXPECT_EQ(ToSjasmplus("DATA:   DS 5\n        LD DE,DATA\n        LD HL,\"ABC\""), (std::vector<std::string>{"DATA    DS 5", "LD DE,DATA", "LD HL,16963"}));   // "BC" = #4243
}

TEST(Dialects_Test, SjasmplusMacroArgumentsLabelsAndAtNames)
{
    // A call with more arguments than the body uses (ALASM ignores the rest): the macro declares them all
    const std::vector<std::string> extra = ToSjasmplus("        MACRO R\n        LD A,\\0\n        ENDM\n        R 1,2,3");
    EXPECT_NE(std::find(extra.begin(), extra.end(), "MACRO R _arg0,_arg1,_arg2"), extra.end());
    // An address label of a name reassigned with "=" elsewhere becomes redefinable
    EXPECT_EQ(ToSjasmplus("SAVE\n        NOP\nSAVE=0"), (std::vector<std::string>{"SAVE=$", "NOP", "SAVE=0"}));
}

TEST(Dialects_Test, SjasmplusSourcesSurviveTheRoundTrip)
{
    // Block comments, repeat prefixes, includes with other extensions, temporary labels, DS, multi-character constants
    const std::string text = "/* a\n"
                             "   block */\n"
                             "        .2 INC HL\n"
                             "        INCLUDE \"defs.inc\"\n"
                             "1       DJNZ 1B\n"
                             "        DS 4,#AA,#55\n"
                             "        DS 40,\"-=-\"";
    const std::vector<std::string> lines = [&] {
        const ConvertResult r = Convert(SourceDocument::FromText(text, "sjasmplus"), "sjasmplus");
        std::vector<std::string> out;
        for (const SourceLine& l : r.document.lines)
            out.push_back(l.text);
        return out;
    }();
    EXPECT_EQ(lines, (std::vector<std::string>{"; a", ";   block ", "        .2 INC HL", "        INCLUDE \"defs.inc\"", "1       DJNZ 1B",
                                               "        DS 4,#AA,#55", "        DS 40,\"-=-\""}));
}
