// The ALASM backend (part B, Z0): the IR written as ALASM 5.07 source. Construct by construct (left-to-right
// expressions, a later label only in + and -, operators ALASM lacks, local and temporary labels, macros), and a round
// trip on programs whose bytes are known: each is written as sjasmplus, then as ALASM, the ALASM text goes through the
// ALASM codec (it must tokenize and read back as written) and back to sjasmplus through the ALASM frontend, whose
// output assembles to what ALASM 5.09 built (research-alasm-to-sjasmplus.md). With UNREAL_ASM_SJASMPLUS=<path to
// sjasmplus> the result is assembled and compared with the original assemblers' bytes.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/alasm/alasmcodec.h"
#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "codecs/storm/stormcodec.h"
#include "codecs/tasm/tasmcodec.h"
#include "codecs/zxasm/zxasmcodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
/// The ALASM lines of a sjasmplus source (blank lines dropped, leading blanks and trailing comments trimmed)
std::vector<std::string> ToAlasm(const std::string& sjasmplus, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "alasm");
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
        const size_t comment = t.find(';');
        if (comment != std::string::npos && comment > 0)
            t.resize(comment);
        while (!t.empty() && t.back() == ' ')
            t.pop_back();
        // "LABEL   CMD" keeps one blank between them; a command alone loses its indentation
        if (t[0] == ' ')
            t = t.substr(t.find_first_not_of(' '));
        else
        {
            const size_t gap = t.find(' ');
            if (gap != std::string::npos)
                t = t.substr(0, gap) + " " + t.substr(t.find_first_not_of(' ', gap));
        }
        out.push_back(t);
    }
    return out;
}

bool Contains(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

bool Mentions(const Diagnostics& diagnostics, const std::string& text)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& d) { return d.message.find(text) != std::string::npos; });
}

containers::TrdosFile Hobeta(const std::string& relative)
{
    containers::TrdosFile file;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData(relative), file, error)) << relative << ": " << error;
    return file;
}

SourceDocument Decoded(const ISourceCodec& codec, const containers::TrdosFile& file, const std::string& version = {})
{
    DecodeOptions options;
    options.catalog = file.Hints();
    options.subversion = version;
    return codec.Decode(file.data, options).document;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string Trimmed(std::string s)
{
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

/// A source of any dialect as sjasmplus (an ALASM source as it is), then as ALASM through the codec and back: the
/// ALASM text read back from the bytes ALASM would hold (checked to be the text written)
SourceDocument AlasmRoundTrip(const SourceDocument& source, const std::string& label)
{
    SourceDocument sjasmplus = source;
    if (source.dialect != "sjasmplus" && source.dialect != "alasm")
    {
        const ConvertResult r = Convert(source, "sjasmplus");
        EXPECT_TRUE(r.ok) << label;
        sjasmplus = SourceDocument::FromText(r.document.Text(), "sjasmplus");
    }
    const ConvertResult alasm = Convert(sjasmplus, "alasm");
    EXPECT_TRUE(alasm.ok) << label;
    for (const Diagnostic& d : alasm.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " line " << d.line << ": " << d.message;
    SourceDocument named = alasm.document;
    named.name = "PROG";
    const codecs::AlasmCodec codec;
    EncodeOptions encode;
    encode.subversion = "5.07";
    const EncodeResult bytes = codec.Encode(named, encode);
    EXPECT_TRUE(bytes.ok) << label;
    DecodeOptions decode;
    decode.subversion = "5.07";
    const DecodeResult back = codec.Decode(bytes.bytes, decode);
    EXPECT_EQ(back.document.lines.size(), named.lines.size()) << label;
    for (size_t k = 0; k < std::min(back.document.lines.size(), named.lines.size()); ++k)
        EXPECT_EQ(Trimmed(back.document.lines[k].text), Trimmed(named.lines[k].text)) << label << " line " << k + 1;
    SourceDocument text = back.document;
    text.dialect = "alasm";
    return text;
}

/// sjasmplus run on a harness that INCLUDEs `main` and saves `start`..`end`; empty on an error
std::vector<uint8_t> Assemble(const char* sjasmplus, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files, const std::string& main,
                              const std::string& start, const std::string& length)
{
    std::random_device random;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    for (const auto& [name, bytes] : files)
        WriteBytes(dir / name, bytes);
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"" + main + "\"\n        SAVEBIN \"out.bin\"," + start + "," + length + "\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    std::vector<uint8_t> out;
    if (std::system(command.c_str()) == 0)
        out = ReadBytes(dir / "out.bin");
    else
    {
        const std::vector<uint8_t> log = ReadBytes(dir / "out.txt");
        ADD_FAILURE() << "sjasmplus: " << std::string(log.begin(), log.end());
    }
    std::filesystem::remove_all(dir);
    return out;
}

std::vector<uint8_t> Bytes(const SourceDocument& document)
{
    return codecs::SjasmplusCodec().Encode(document, {}).bytes;
}

/// The ALASM text assembled the way ALASM computes it: back to sjasmplus through the ALASM frontend
std::vector<uint8_t> AssembleAlasm(const char* sjasmplus, const SourceDocument& alasm, const std::string& start, const std::string& length,
                                   std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {})
{
    const ConvertResult r = Convert(alasm, "sjasmplus");
    EXPECT_TRUE(r.ok);
    files.push_back({"PROG.asm", Bytes(r.document)});
    return Assemble(sjasmplus, files, "PROG.asm", start, length);
}
}  // namespace

TEST(AlasmBackend_Test, ExpressionsLeftToRight)
{
    // ALASM computes left to right: a right operand that is an operation gets parentheses, a left one never does
    EXPECT_EQ(ToAlasm("        LD A,1+2*3\n        LD A,(1+2)*3"), (std::vector<std::string>{"LD A,1+(2*3)", "LD A,0+(1+2)*3"}));
    // high / low are ' and .; XOR is !; ~ inverts the result so far
    EXPECT_EQ(ToAlasm("X       EQU 5\n        LD A,high X\n        LD A,low (X+1)\n        LD A,X^3\n        LD A,~X+1"),
              (std::vector<std::string>{"X EQU 5", "LD A,'X", "LD A,.(X+1)", "LD A,X!3", "LD A,X~+1"}));
    // Shifts by a number: * and / (the value shifted right lies in 0..#FFFF here)
    EXPECT_EQ(ToAlasm("X       EQU 5\n        LD HL,X<<4\n        LD A,X>>1"), (std::vector<std::string>{"X EQU 5", "LD HL,X*16", "LD A,X/2"}));
    // A value outside ALASM's word for a division is written as its value
    Diagnostics diagnostics;
    EXPECT_EQ(ToAlasm("        LD A,-2/2", &diagnostics), (std::vector<std::string>{"LD A,#FFFF"}));
    EXPECT_TRUE(Mentions(diagnostics, "16-bit unsigned"));
}

TEST(AlasmBackend_Test, ALaterLabelOnlyInPlusAndMinus)
{
    // ALASM assembles in one pass: a later label may stand in + and - only; elsewhere its value is written
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToAlasm("        ORG #6000\n        LD HL,T+1\n        LD A,high T\n        LD A,T*2\nT       NOP", &diagnostics);
    EXPECT_TRUE(Contains(lines, "LD HL,T+1"));
    EXPECT_TRUE(Contains(lines, "LD A,96"));
    EXPECT_TRUE(Contains(lines, "LD A,#C00E"));
    EXPECT_TRUE(Mentions(diagnostics, "defined further down"));
    // ORG, EQU, DS and IF need the value when the line is assembled: no later label at all
    EXPECT_TRUE(Contains(ToAlasm("        ORG #6000\nS       NOP\n        DS EN-$\nEN      EQU #6005"), "DS 4"));
}

TEST(AlasmBackend_Test, Conditions)
{
    // IF0 takes the block on 0, IFN on not 0; a comparison is a subtraction or ALASM's division by #8000
    EXPECT_EQ(ToAlasm("X       EQU 5\n        IF X == 5\n        ENDIF\n        IF X != 5\n        ENDIF\n        IF X < 7\n        ENDIF"),
              (std::vector<std::string>{"X EQU 5", "IF0 X-5", "ENDIF", "IFN X-5", "ENDIF", "IF0 X-7+#8000/#8000", "ENDIF"}));
    // exist label: ?label is 0 when it is defined
    EXPECT_EQ(ToAlasm("X       EQU 5\n        IF exist X\n        ENDIF\n        IF !exist X\n        ENDIF"),
              (std::vector<std::string>{"X EQU 5", "IF0 ?X", "ENDIF", "IFN ?X", "ENDIF"}));
    // IF before 5.03 is 5.03's IF0
    BackendOptions old;
    old.targetVersion = "4.44";
    const ConvertResult r = Convert(SourceDocument::FromText("X       EQU 5\n        IF exist X\n        ENDIF", "sjasmplus"), "alasm", old);
    EXPECT_NE(r.document.Text().find("IF ?X"), std::string::npos);
}

TEST(AlasmBackend_Test, NamesAndLabels)
{
    // sjasmplus' local labels hang on their parent; temporary labels are numbered; a register's name is renamed
    const std::vector<std::string> lines = ToAlasm("main    NOP\n.loop   DJNZ .loop\n1       JR 1B\n        JR 1F\n1       NOP\nEXA     EQU 3\n        LD A,EXA");
    EXPECT_TRUE(Contains(lines, "main_loop DJNZ main_loop"));
    EXPECT_TRUE(Contains(lines, "_T1_1 JR _T1_1"));
    EXPECT_TRUE(Contains(lines, "JR _T1_2"));
    EXPECT_TRUE(Contains(lines, "L_EXA EQU 3"));
    EXPECT_TRUE(Contains(lines, "LD A,L_EXA"));
    // Instructions ALASM spells its own way
    EXPECT_EQ(ToAlasm("        ex af,af'\n        ld a,ixh\n        sll b\n        in f,(c)"), (std::vector<std::string>{"EXA", "LD A,HX", "SLI B", "INF"}));
    // EXA is a keyword from 4.44 (not in 4.5): an older ALASM (4.2) skips the line, so EX AF,AF' there
    BackendOptions old;
    for (const char* version : {"4.42", "4.5"})
    {
        old.targetVersion = version;
        EXPECT_NE(Convert(SourceDocument::FromText("        ex af,af'", "sjasmplus"), "alasm", old).document.Text().find("EX AF,AF'"), std::string::npos)
            << version;
    }
    // 4.2-4.5 take macro parameters as :0..:9 (\0 is a label there); 4.2 / 4.42 have no REPEAT: a WHILE is a DUP of
    // the count it ran (each seen in ALASM 4.2 / 4.42 / 4.5 / 5.0 in unreal-ng)
    old.targetVersion = "4.2";
    const std::string text = Convert(SourceDocument::FromText("        MACRO WAIT n\n        ld b,n\n        ENDM\n        WAIT 5\n"
                                                              "n       = 0\n        WHILE n < 3\n        db n\nn       = n+1\n        ENDW\n"
                                                              "        WHILE n < 3\n        db n\n        ENDW",
                                                              "sjasmplus"),
                                     "alasm", old)
                                 .document.Text();
    EXPECT_NE(text.find("LD B,:0"), std::string::npos) << text;
    EXPECT_NE(text.find("DUP 3"), std::string::npos) << text;
    EXPECT_NE(text.find("IFN 0"), std::string::npos) << text;
    EXPECT_EQ(text.find("REPEAT"), std::string::npos) << text;
    // 3.8 has no parentheses in expressions ("(" is memory), no MACRO and no IF (each seen in ALASM 3.8c): values,
    // macros put in at their calls, an IF decided by its value
    old.targetVersion = "3.8";
    const std::string text38 = Convert(SourceDocument::FromText("SIZE    EQU 10\n        ld a,1+(2*3)\n        MACRO WAIT n\n        ld b,n\n        ENDM\n"
                                                                "        WAIT 5\n        IF SIZE > 5\n        db 1\n        ELSE\n        db 2\n        ENDIF\n"
                                                                "        ds 4,#AA\n        ld a,~SIZE & #0F",
                                                                "sjasmplus"),
                                       "alasm", old)
                                   .document.Text();
    EXPECT_NE(text38.find("LD A,7"), std::string::npos) << text38;
    EXPECT_EQ(text38.find("        MACRO"), std::string::npos) << text38;
    EXPECT_NE(text38.find("LD B,5"), std::string::npos) << text38;
    EXPECT_NE(text38.find("DEFB 1"), std::string::npos) << text38;
    EXPECT_EQ(text38.find("DEFB 2"), std::string::npos) << text38;
    EXPECT_EQ(text38.find("        IF"), std::string::npos) << text38;
    EXPECT_NE(text38.find("DEFS 4,#AA"), std::string::npos) << text38;   // no DS, no & | ! ~
    EXPECT_NE(text38.find("LD A,5"), std::string::npos) << text38;
    // ALASM 5.09 puts nothing down for ADD B (seen in unreal-ng): always A,x
    EXPECT_EQ(ToAlasm("        add b\n        adc (hl)\n        sbc 5"), (std::vector<std::string>{"ADD A,B", "ADC A,(HL)", "SBC A,5"}));
    // sjasmplus' several operands and fake instructions, written out
    EXPECT_EQ(ToAlasm("        sub a,b\n        push af,bc\n        ld bc,de\n        sub hl,de"),
              (std::vector<std::string>{"SUB A", "SUB B", "PUSH AF", "PUSH BC", "LD B,D", "LD C,E", "OR A", "SBC HL,DE"}));
    // DW 'AB' is a word to sjasmplus, the characters to ALASM 5.09 (41 42): the number
    EXPECT_EQ(ToAlasm("        dw 'AB'"), (std::vector<std::string>{"DW 16706"}));
}

TEST(AlasmBackend_Test, MacrosAndRepeats)
{
    // Named parameters become \0..\9; a macro's .labels are its own in each expansion: a LOCAL block
    const std::vector<std::string> lines = ToAlasm("        MACRO WAIT n\n        LD B,n\n.w      DJNZ .w\n        ENDM\n        WAIT 3*2");
    EXPECT_EQ(lines, (std::vector<std::string>{"MACRO WAIT", "LOCAL", "LD B,\\0", "w DJNZ w", "ENDL", "ENDM", "WAIT 3*2"}));
    // WHILE tests first: IF around ALASM's REPEAT ... UNTIL0
    const std::vector<std::string> loop = ToAlasm("n       = 0\n        WHILE n < 3\n        NOP\nn       = n+1\n        ENDW");
    EXPECT_TRUE(Contains(loop, "IF0 n-3+#8000/#8000"));
    EXPECT_TRUE(Contains(loop, "REPEAT"));
    EXPECT_TRUE(Contains(loop, "UNTIL0 n-3+#8000/#8000-1"));
}

TEST(AlasmBackend_Test, ProgramsKeepTheirBytesThroughAlasm)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    // Every instruction form: the same bytes as sjasmplus builds from the original
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
        const std::vector<uint8_t> original = Assemble(sjasmplus, {{"PROG.asm", Bytes(source)}}, "PROG.asm", "#8000", "$-#8000");
        EXPECT_FALSE(original.empty());
        EXPECT_EQ(AssembleAlasm(sjasmplus, AlasmRoundTrip(source, "instructions"), "#8000", "$-#8000"), original);
    }
    // ALASM 5.09's constructs (from #6000), written back as ALASM
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("dialects/alasm-sjasmplus/constructs.alasm.txt"), "alasm");
        EXPECT_EQ(AssembleAlasm(sjasmplus, AlasmRoundTrip(source, "constructs"), "#6000", "$-#6000"), ReadTestData("dialects/alasm-sjasmplus/constructs.bin"));
    }
    // STORM 1.3 (from #8000; STORMT1 INCBINs INC)
    const codecs::StormCodec storm;
    for (const std::string name : {"STORMT1", "STORMT2"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/" + name + ".bin");
        EXPECT_EQ(AssembleAlasm(sjasmplus, AlasmRoundTrip(Decoded(storm, Hobeta("dialects/storm13/" + name + ".$C")), name), "#8000",
                                std::to_string(expected.size()), {{"INC", Hobeta("dialects/storm13/INC.$C").data}}),
                  expected)
            << name;
    }
    // ZAsm 3.15 (from #8000)
    const codecs::ZxasmCodec zxasm;
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleAlasm(sjasmplus, AlasmRoundTrip(Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15"), name), "#8000",
                                std::to_string(expected.size())),
                  expected)
            << name;
    }
    // TASM 5.0 beta (from #7000)
    {
        const codecs::TasmCodec tasm;
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssembleAlasm(sjasmplus, AlasmRoundTrip(Decoded(tasm, Hobeta("tasm/T50PROG.$A")), "T50PROG"), "#7000", std::to_string(expected.size())),
                  expected);
    }
}
