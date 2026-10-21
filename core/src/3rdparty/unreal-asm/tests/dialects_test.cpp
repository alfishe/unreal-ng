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
    // INCBIN sizes for the layout of the converted project (byte operands marked with low)
    BackendOptions options;
    options.fileSize = [](const std::string& name) -> std::optional<uint64_t> {
        for (const char* binary : {"torusr.$p", "dplan2r.$p", "thelinkm.$C"})
        {
            const containers::TrdosFile file = Hobeta(std::string("dialects/thelink/") + binary);
            if (file.TrimmedName() + (file.type == 'C' ? std::string() : std::string(".") + file.type) == name)
                return file.data.size();
        }
        return std::nullopt;
    };
    return ConvertProject(project, "sjasmplus", options);
}
}  // namespace

// --- ALASM -> sjasmplus, construct by construct ---------------------------------------------------------------------

TEST(Dialects_Test, AlasmIfTakesTheBlockOnZero)
{
    // #D3 is IF before 5.03 and IF0 after it, the same directive: the block when the expression is 0 (ALASM 4.2's help;
    // checked in ALASM 5.0); IFN when it is not
    EXPECT_EQ(ToSjasmplus("        IF X-3\n        NOP\n        ENDIF"), (std::vector<std::string>{"IF (X-3)==0", "NOP", "ENDIF"}));
    EXPECT_EQ(ToSjasmplus("        IF0 X-3\n        NOP\n        ENDIF"), (std::vector<std::string>{"IF (X-3)==0", "NOP", "ENDIF"}));
    EXPECT_EQ(ToSjasmplus("        IFN X-3\n        NOP\n        ENDIF"), (std::vector<std::string>{"IF X-3", "NOP", "ENDIF"}));
}

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
    EXPECT_EQ(ToSjasmplus("        LD BC,#1234<4"), std::vector<std::string>{"LD BC,+((#1234*#10001>>>12)&#FFFF)"});
    // the word doubled in 32 bits and 16 bits taken out of it: an operand written once, so nested rotations stay
    // short; the count modulo 16 (ALASM: #1234<20 is #1234<4)
    EXPECT_EQ(ToSjasmplus("        DW x*257<1+#303>1"), std::vector<std::string>{"DW ((((((x*257&#FFFF)*#10001>>>15)&#FFFF)+#303&#FFFF)*#10001>>>1)&#FFFF)"});
    EXPECT_EQ(ToSjasmplus("        DW #1234<20\n        DW #1234>n"),
              (std::vector<std::string>{"DW ((#1234*#10001>>>12)&#FFFF)", "DW ((#1234*#10001>>>(n&15))&#FFFF)"}));
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

TEST(Dialects_Test, DsOfANegativeCountFillsToTheEndOfMemory)
{
    // The Link's THELINK.asm: DS -$ after a picture zeroes the page to #FFFF; ALASM's count is a 16-bit word, sjasmplus'
    // negative DS would move $ back
    EXPECT_EQ(ToSjasmplus("        DS -$"), (std::vector<std::string>{"DS (-$&#FFFF)"}));
    EXPECT_EQ(ToSjasmplus("        DS 10"), (std::vector<std::string>{"DS 10"}));
}

TEST(Dialects_Test, AByteOperandTheSourceTruncatesSaysLow)
{
    // ALASM keeps the low byte of a byte operand without a word (The Link: LD (HL),IMER with IMER = #BFBF, the IM 2
    // vector byte); sjasmplus builds the same byte but warns, and that warning cannot be silenced. The conversion
    // writes low where the converted project's layout finds a value outside -256..255
    const std::vector<std::string> out = ToSjasmplus("IMER=#BFBF\n"
                                                     "        LD (HL),IMER\n"
                                                     "        LD HL,IMER\n"
                                                     "        DB IMER,1\n"
                                                     "        ADD A,#123\n"
                                                     "        IN A,(#1FE)\n"
                                                     "        LD A,-200\n"
                                                     "        LD (IX+1),IMER+1");
    auto has = [&out](const std::string& line) { return std::find(out.begin(), out.end(), line) != out.end(); };
    EXPECT_TRUE(has("LD (HL),low IMER"));
    EXPECT_TRUE(has("LD HL,IMER"));            // a word
    EXPECT_TRUE(has("DB low IMER,1"));
    EXPECT_TRUE(has("ADD A,low #123"));
    EXPECT_TRUE(has("IN A,(low #1FE)"));
    EXPECT_TRUE(has("LD A,-200"));             // sjasmplus takes -256..255 as a byte
    EXPECT_TRUE(has("LD (IX+1),low (IMER+1)"));
}

TEST(Dialects_Test, AnOrgPageOutsideItsSlotIsMeant)
{
    // ALASM maps an ORG page at #C000 whatever the address, sjasmplus too (into the current slot) but warns
    const std::vector<std::string> out = ToSjasmplus("        ORG #C000,1\n        ORG $,0");
    ASSERT_GE(out.size(), 2u);
    EXPECT_EQ(std::count_if(out.begin(), out.end(), [](const std::string& l) { return l.find("orgpage-ok") != std::string::npos; }), 1);
    EXPECT_NE(std::find_if(out.begin(), out.end(), [](const std::string& l) { return l.rfind("ORG $,0", 0) == 0 && l.find("orgpage-ok") != std::string::npos; }), out.end());
}

TEST(Dialects_Test, TheObjectSaversTableBecomesSavebin)
{
    // Alone Coder's object saver: RUN in ALASM saves each ObjTab entry (name and type, address, length, page, start);
    // the conversion writes the same objects as SAVEBIN at the end of the unit
    const std::vector<ProjectFile> project = {
        {"MAIN", SourceDocument::FromText("pgzx=#B0\n        ORG #C000,pgzx\nBEG     NOP\nLENZX=$-BEG\n        ORG #8000\nObjTab\n"
                                          "        DB \"EFFECTZXC\"\n        DW #C000\n        DW LENZX\n        DB pgzx\n        DW #C000\n"
                                          "        DB \"table   0\"\n        DW #8000,16\n        DB #10\n        DW 0\n        INCLUDE \"SAVEOBJ4\"",
                                          "alasm")},
        {"SAVEOBJ4", SourceDocument::FromText("        NOP\n        RET", "alasm")}};
    const ProjectResult r = ConvertProject(project, "sjasmplus");
    ASSERT_EQ(r.files.size(), 2u);
    std::vector<std::string> lines;
    for (const SourceLine& l : r.files[0].document.lines)
        lines.push_back(l.text);
    auto has = [&lines](const std::string& line) { return std::find(lines.begin(), lines.end(), "        " + line) != lines.end(); };
    EXPECT_TRUE(has("SLOT 3"));
    EXPECT_TRUE(has("PAGE pgzx"));
    EXPECT_TRUE(has("SAVEBIN \"EFFECTZX\",#C000,LENZX"));   // type C: the name alone, as ALASM's INCBIN names it
    EXPECT_TRUE(has("PAGE #10"));
    EXPECT_TRUE(has("SAVEBIN \"table.0\",#8000,16"));      // another type: after a dot
}

TEST(Dialects_Test, AWalkingMacroOfAnotherFileIsExpanded)
{
    // RAR's packer calls `c HUFFMAN`, a macro walking its parameter (\C, \N) defined in another file of the project:
    // the call is expanded as in the file that defines it
    const std::vector<ProjectFile> project = {
        {"MAIN", SourceDocument::FromText("        INCLUDE \"MACROS\"\n        DOWN HL", "alasm")},
        {"MACROS", SourceDocument::FromText("        MACRO DOWN\n        INC \\C\n        INC \\N\\C\n        ENDM", "alasm")}};
    const ProjectResult r = ConvertProject(project, "sjasmplus");
    ASSERT_EQ(r.files.size(), 2u);
    std::vector<std::string> lines;
    for (const SourceLine& l : r.files[0].document.lines)
        if (const size_t at = l.text.find_first_not_of(' '); at != std::string::npos)
            lines.push_back(l.text.substr(at));
    EXPECT_NE(std::find(lines.begin(), lines.end(), "INC H"), lines.end());
    EXPECT_NE(std::find(lines.begin(), lines.end(), "INC L"), lines.end());
}

TEST(Dialects_Test, LocalBlockAroundAnIncludeIsAModule)
{
    // ALASM's LOCAL ... ENDL around an INCLUDE makes the included file's labels local (Pro Tracker's PLAYTSmK includes
    // its player twice, once in a block): the block becomes a sjasmplus MODULE; the file's macros, defined again by
    // the second INCLUDE, are defined once
    const std::vector<ProjectFile> project = {
        {"MAIN", SourceDocument::FromText("        ORG #8000\n        LOCAL\nts=1\n        INCLUDE \"PLAY\"\n        ENDL\nts=0\n"
                                          "        INCLUDE \"PLAY\"", "alasm")},
        {"PLAY", SourceDocument::FromText("        MACRO m\nEef     NOP\n        ENDM\n        LD A,ts\n        m\n        DW Eef", "alasm")}};
    const ProjectResult r = ConvertProject(project, "sjasmplus");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.files.size(), 2u);
    auto text = [](const ProjectFile& f) {
        std::vector<std::string> lines;
        for (const SourceLine& l : f.document.lines)
            if (const size_t at = l.text.find_first_not_of(' '); at != std::string::npos)
                lines.push_back(l.text.substr(at));
        return lines;
    };
    const std::vector<std::string> main = text(r.files[0]), play = text(r.files[1]);
    EXPECT_NE(std::find(main.begin(), main.end(), "MODULE __UNREALASM_LB1"), main.end());
    EXPECT_NE(std::find(main.begin(), main.end(), "ENDMODULE"), main.end());
    EXPECT_NE(std::find(play.begin(), play.end(), "IFNDEF __UNREALASM_M_m"), play.end());

    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        return;
    const std::filesystem::path dir = ScratchDirectory();
    for (const ProjectFile& f : r.files)
    {
        std::string body;
        for (const SourceLine& l : f.document.lines)
            body += l.text + "\n";
        WriteBytes(dir / (f.name + ".asm"), std::vector<uint8_t>(body.begin(), body.end()));   // as the INCLUDEs name them
    }
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"MAIN.asm\"\n        SAVEBIN \"out.bin\",#8000,$-#8000\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
    const bool assembled = RunSjasmplus(sjasmplus, dir, "harness.asm");
    const std::vector<uint8_t> log = ReadBytes(dir / "out.txt");
    EXPECT_TRUE(assembled) << std::string(log.begin(), log.end());
    EXPECT_EQ(ReadBytes(dir / "out.bin"), (std::vector<uint8_t>{0x3E, 0x01, 0x00, 0x02, 0x80, 0x3E, 0x00, 0x00, 0x07, 0x80}));
    std::filesystem::remove_all(dir);
}

TEST(Dialects_Test, NameDefinedOnceInEachBranchIsAnEqu)
{
    // ACE: the routine's label in one branch, JUSTIFY=RETER (a later label) in the other; sjasmplus' "=" cannot refer
    // forward, its EQU can. A name assigned once stays "="
    EXPECT_EQ(ToSjasmplus("        IF 0\nJUSTIFY  NOP\n        ELSE\nJUSTIFY=RETER\n        ENDIF\nN=5\nRETER   RET"),
              (std::vector<std::string>{"IF (0)==0", "JUSTIFY NOP", "ELSE", "JUSTIFY EQU RETER", "ENDIF", "N=5", "RETER   RET"}));
}

TEST(Dialects_Test, AWordAfterALabelIsACommandNotAReference)
{
    // ACE's acemem: "bof" is a macro of another file, called alone and after labels (LASTLN0 bof); a call after a
    // label is no reference to a name, so the lone call stays a call
    const FrontendResult r =
        DialectRegistry::Builtin().Frontend("alasm")->Parse(SourceDocument::FromText("        LD DE,1\n        bof\nL0      bof", "alasm"));
    ASSERT_EQ(r.program.lines.size(), 3u);
    EXPECT_TRUE(r.program.lines[1].label.empty());
    ASSERT_EQ(r.program.lines[1].statements.size(), 1u);
    EXPECT_EQ(r.program.lines[1].statements[0].kind, ir::Statement::Kind::MacroCall);
}

TEST(Dialects_Test, ATestBeforeTheDefinitionSeesTheNameUndefined)
{
    // ALASM is one pass: ?x before x's definition says "not defined" (ACE's aceTO wraps its library routines in
    // IFN ?H2PAG ... H2PAG ... ENDIF); sjasmplus' exist would see the label of pass 1 and drop the block
    EXPECT_EQ(ToSjasmplus("        IFN ?H2PAG\nH2PAG   RET\n        ENDIF\n        IFN ?H2PAG\n        NOP\n        ENDIF"),
              (std::vector<std::string>{"IFNDEF __UNREALASM_DEF_H2PAG", "H2PAG   RET", "DEFINE __UNREALASM_DEF_H2PAG", "ENDIF",
                                        "IFNDEF __UNREALASM_DEF_H2PAG", "NOP", "ENDIF"}));
    // a name defined before the test, or nowhere, stays exist
    EXPECT_EQ(ToSjasmplus("X       NOP\n        IFN ?X\n        NOP\n        ENDIF"),
              (std::vector<std::string>{"X       NOP", "IF !exist X", "NOP", "ENDIF"}));
}

TEST(Dialects_Test, AControlCharacterInATextGoesAsANumberAlone)
{
    // ACE's aceFROM: DB "Ё<DEL>Ї<" was written as the UTF-8 bytes 208,129,127,208,135,60; the text around stays quoted
    EXPECT_EQ(ToSjasmplus("        DB \"\xD0\x81\x7F\xD0\x87<\"\n        DB 7,\"a\x07'\"\"\""),
              (std::vector<std::string>{"DB '\xD0\x81',127,'\xD0\x87<'", "DB 7,'a',7,39,34"}));
}

TEST(Dialects_Test, TheTestOfAMacroNameIsTwo)
{
    // ?name of a macro is 2 (ALASM help, "?label"): "IFN ?j\1-2" in RAR's macro c tells a call with two parameters
    EXPECT_EQ(ToSjasmplus("        MACRO j\n        NOP\n        ENDM\n        IFN ?j-2\n        DB 1\n        ENDIF"),
              (std::vector<std::string>{"MACRO j", "NOP", "ENDM", "IF 2-2", "DB 1", "ENDIF"}));
}

TEST(Dialects_Test, ALabelNamedLikeARegisterInDataIsTheLabel)
{
    // ALASM's calculator reads DW E / DB A as the labels E / A: their uses get the definition's rename
    EXPECT_EQ(ToSjasmplus("E       EQU 5\nA       NOP\n        DW E,A+1\n        DB A\n        LD A,E"),
              (std::vector<std::string>{"L_E     EQU 5", "L_A     NOP", "DW L_E,L_A+1", "DB L_A", "LD A,E"}));
}

TEST(Dialects_Test, TheTestOfANameUsedBeforeItsDefinitionIsOne)
{
    // ?name of a label not defined yet but used before is 1 (ALASM help, "?label"); unused before, the test reads the DEFINE
    EXPECT_EQ(ToSjasmplus("        DW X\n        DB ?X\nX       NOP"), (std::vector<std::string>{"DW X", "DB 1", "X       NOP"}));
    EXPECT_EQ(ToSjasmplus("        IFN ?X\n        DB 1\n        ENDIF\nX       NOP"),
              (std::vector<std::string>{"IFNDEF __UNREALASM_DEF_X", "DB 1", "ENDIF", "X       NOP", "DEFINE __UNREALASM_DEF_X"}));
}

TEST(Dialects_Test, EntAfterAConditionalDispEndsItWhenActive)
{
    // ACE's aceTO: DISP only when v1 is defined, ENT for both; sjasmplus refuses an ENT without DISP
    EXPECT_EQ(ToSjasmplus("        IF0 ?v1\n        DISP #6000\n        ENDIF\n        NOP\n        ENT"),
              (std::vector<std::string>{"IF exist v1", "DISP #6000", "DEFINE __UNREALASM_DISP", "ENDIF", "NOP", "IFDEF __UNREALASM_DISP", "ENT",
                                        "UNDEFINE __UNREALASM_DISP", "ENDIF"}));
}

TEST(Dialects_Test, AssembleOnceMarksInColumnZeroAreAssembled)
{
    // ALASM 5.09 assembled this to 3E 01 06 02 16 04 04 60 C9 (LBL = #6004): "+" / "-" in column 0 mark a line it
    // assembles once ("-" read from the disk is "+" again); "+LBL" is the label LBL
    EXPECT_EQ(ToSjasmplus("        ORG #6000\n+       LD A,1\n-       LD B,2\n+LBL    LD D,4\n        DW LBL\n        RET"),
              (std::vector<std::string>{"ORG #6000", "LD A,1", "LD B,2", "LBL     LD D,4", "DW LBL", "RET"}));
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

TEST(Dialects_Test, Alasm50And505ProbesAssembleToWhatTheyBuilt)
{
    // U50: typed for ALASM 5.0 (IF / IFN on a zero expression, REPEAT / UNTIL, DD, JNZ, EXD); U505: for ALASM 5.05
    // (UNTIL, not 5.07's UNTIL0, at #D2), DD with commas, JNZ / JC / EXA / EXD. Each ALASM built its .bin at #6000
    // in unreal-ng
    struct Probe
    {
        const char* name;
        const char* version;
        const char* line;   ///< a line as that version spells it
    };
    for (const Probe& probe : {Probe{"alasm50/U50", "5.0", "        IF CNT-3"}, Probe{"alasm505/U505", "5.05", "        UNTIL CNT-5"}})
    {
        const std::string name = std::string("dialects/") + probe.name;
        const containers::TrdosFile file = Hobeta(name + ".$H");
        const codecs::AlasmCodec alasm;
        DecodeOptions options;
        options.catalog = file.Hints();
        options.subversion = probe.version;
        const DecodeResult decoded = alasm.Decode(file.data, options);
        ASSERT_TRUE(decoded.ok) << name;
        EXPECT_NE(decoded.document.Text().find(probe.line), std::string::npos) << name;
        const ConvertResult r = Convert(decoded.document, "sjasmplus");
        ASSERT_TRUE(r.ok) << name;
        const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
        if (!sjasmplus)
            GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
        const std::filesystem::path dir = ScratchDirectory();
        const codecs::SjasmplusCodec codec;
        WriteBytes(dir / "probe.asm", codec.Encode(r.document, {}).bytes);
        const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"probe.asm\"\n        SAVEBIN \"out.bin\",#6000,$-#6000\n";
        WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
        EXPECT_TRUE(RunSjasmplus(sjasmplus, dir, "harness.asm")) << name;
        EXPECT_EQ(ReadBytes(dir / "out.bin"), ReadTestData(name + ".bin")) << name << ": ALASM's bytes";
        std::filesystem::remove_all(dir);
    }
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
    // The converted source as it is: its SAVEBIN lines (from the object saver's table) write the objects, and
    // sjasmplus has nothing to warn about
    EXPECT_TRUE(RunSjasmplus(sjasmplus, dir, "GSTUNNE4.asm"));
    const std::vector<uint8_t> log = ReadBytes(dir / "out.txt");
    EXPECT_NE(std::string(log.begin(), log.end()).find("warnings: 0"), std::string::npos) << std::string(log.begin(), log.end());
    EXPECT_EQ(ReadBytes(dir / "TUNNELZX"), Hobeta("dialects/thelink/TUNNELZX.$C").data);
    EXPECT_EQ(ReadBytes(dir / "TUNNELGS"), Hobeta("dialects/thelink/TUNNELGS.$C").data);
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
    // DATA: is DATA; a character constant is a 16-bit word, the first character low, the last high ("ABC" = #4341)
    EXPECT_EQ(ToSjasmplus("DATA:   DS 5\n        LD DE,DATA\n        LD HL,\"ABC\""), (std::vector<std::string>{"DATA    DS 5", "LD DE,DATA", "LD HL,17217"}));
}

TEST(Dialects_Test, AlasmBuildsWhatAlasm509Builds)
{
    // ALASM 5.09 assembled each line in unreal-ng (research-sjasmplus-to-alasm.md §3): LD HL,"AB" = 21 41 42,
    // DW "AB","C" = 41 42 43 00, DW "ABC" = 41 43, LD DE,"AB"+1 = 11 42 42; ADD B, ADC 5, SBC (HL) are errors (code #00)
    // and put nothing down
    EXPECT_EQ(ToSjasmplus("        LD HL,\"AB\""), std::vector<std::string>{"LD HL,16961"});
    EXPECT_EQ(ToSjasmplus("        DW \"AB\",\"C\""), std::vector<std::string>{"DW 16961,'C'"});
    EXPECT_EQ(ToSjasmplus("        DW \"ABC\""), std::vector<std::string>{"DW 17217"});
    EXPECT_EQ(ToSjasmplus("        LD DE,\"AB\"+1"), std::vector<std::string>{"LD DE,16961+1"});
    for (const std::string line : {"L1      ADD B", "L1      ADC 5", "L1      SBC (HL)"})
    {
        const ConvertResult r = Convert(SourceDocument::FromText(line, "alasm"), "sjasmplus");
        ASSERT_EQ(r.document.lines.size(), 1u) << line;
        const std::string& out = r.document.lines[0].text;
        EXPECT_EQ(out.rfind("L1", 0), 0u) << "the label stays: " << out;
        EXPECT_NE(out.find(';'), std::string::npos) << "the instruction is a comment: " << out;
        EXPECT_EQ(out.find("ADD A") , std::string::npos) << out;
        EXPECT_TRUE(std::any_of(r.diagnostics.begin(), r.diagnostics.end(), [](const Diagnostic& d) { return d.message.find("ALASM rejects") != std::string::npos; }))
            << line;
    }
    EXPECT_EQ(ToSjasmplus("        ADD A,B"), std::vector<std::string>{"ADD A,B"});
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
