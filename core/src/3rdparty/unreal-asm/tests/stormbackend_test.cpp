// The STORM backend (part B, Z3): the IR written as STORM 1.3 source. Construct by construct (STORM's priorities and
// postfix operators, names of 7 characters, conditions decided while converting, a repeat of one line as ".n", DISP as
// ORG run,place), and a round trip on programs whose bytes are known: each is written as sjasmplus, then as STORM, the
// STORM text goes through the STORM codec (it must pack and read back as written) and back to sjasmplus through the
// STORM frontend, which models STORM's arithmetic (research-storm-to-sjasmplus.md). With UNREAL_ASM_SJASMPLUS=<path to
// sjasmplus> the result is assembled and compared with the original assemblers' bytes.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "codecs/storm/stormcodec.h"
#include "codecs/tasm/tasmcodec.h"
#include "codecs/xas/xascodec.h"
#include "codecs/zxasm/zxasmcodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
std::string Squeezed(std::string t)
{
    // the line as its parts: blanks collapsed, a comment dropped
    const size_t comment = t.find(';');
    if (comment != std::string::npos)
        t.resize(comment);
    std::string out;
    for (const char c : t)
        if (c != ' ' || (!out.empty() && out.back() != ' '))
            out.push_back(c);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::string Upper(std::string t)
{
    for (char& c : t)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return t;
}

/// The STORM lines of a sjasmplus source (blank and comment lines dropped, blanks collapsed)
std::vector<std::string> ToStorm(const std::string& sjasmplus, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "storm");
    EXPECT_TRUE(r.ok);
    if (diagnostics)
        *diagnostics = r.diagnostics;
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
        if (const std::string t = Squeezed(l.text); !t.empty())
            out.push_back(t);
    return out;
}

bool Contains(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

std::string Joined(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& l : lines)
        out += l + "\n";
    return out;
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

/// A source of any dialect as sjasmplus, then as STORM through the codec and back: the STORM text read back from the
/// bytes STORM would hold (checked to be the text written, up to the editor's layout)
SourceDocument StormRoundTrip(const SourceDocument& source, const std::string& label)
{
    SourceDocument sjasmplus = source;
    if (source.dialect != "sjasmplus")
    {
        const ConvertResult r = Convert(source, "sjasmplus");
        EXPECT_TRUE(r.ok) << label;
        sjasmplus = SourceDocument::FromText(r.document.Text(), "sjasmplus");
    }
    const ConvertResult storm = Convert(sjasmplus, "storm");
    EXPECT_TRUE(storm.ok) << label;
    for (const Diagnostic& d : storm.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " line " << d.line << ": " << d.message;
    const codecs::StormCodec codec;
    EncodeOptions encode;
    encode.subversion = "1.3";
    const EncodeResult bytes = codec.Encode(storm.document, encode);
    EXPECT_TRUE(bytes.ok) << label;
    for (const Diagnostic& d : bytes.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " (codec) line " << d.line << ": " << d.message;
    DecodeOptions decode;
    decode.subversion = "1.3";
    const DecodeResult back = codec.Decode(bytes.bytes, decode);
    EXPECT_EQ(back.document.lines.size(), storm.document.lines.size()) << label;
    for (size_t k = 0; k < std::min(back.document.lines.size(), storm.document.lines.size()); ++k)
        EXPECT_EQ(Upper(Squeezed(back.document.lines[k].text)), Upper(Squeezed(storm.document.lines[k].text))) << label << " line " << k + 1;
    return back.document;
}

/// sjasmplus run on a harness that INCLUDEs `main` and saves `start`..`end` (over memory filled with #AA when
/// `filled`, as the XAS oracles were assembled); empty on an error
std::vector<uint8_t> Assemble(const char* sjasmplus, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files, const std::string& main,
                              const std::string& start, const std::string& length, bool filled = false)
{
    std::random_device random;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    for (const auto& [name, bytes] : files)
        WriteBytes(dir / name, bytes);
    const std::string fill = filled ? "        ORG " + start + "\n        DS " + length + ",#AA\n" : std::string();
    const std::string harness = "        DEVICE ZXSPECTRUM48\n" + fill + "        INCLUDE \"" + main + "\"\n        SAVEBIN \"out.bin\"," + start + "," + length + "\n";
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

/// The STORM text assembled the way STORM computes it: back to sjasmplus through the STORM frontend
std::vector<uint8_t> AssembleStorm(const char* sjasmplus, const SourceDocument& storm, const std::string& start, const std::string& length,
                                 std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {}, bool filled = false)
{
    const ConvertResult r = Convert(storm, "sjasmplus");
    EXPECT_TRUE(r.ok);
    files.push_back({"PROG.asm", Bytes(r.document)});
    return Assemble(sjasmplus, files, "PROG.asm", start, length, filled);
}
}  // namespace

TEST(StormBackend_Test, ExpressionsFollowStormsPriorities)
{
    const std::vector<std::string> lines = ToStorm(
        "        ORG #6000\n"
        "lab     LD HL,lab+2*3\n"
        "        LD A,high lab\n"
        "        LD A,low (lab+1)\n"
        "        LD BC,-lab\n"
        "        LD DE,10-(4+2)\n"
        "        LD HL,(lab+1)*2\n"
        "        LD HL,lab<<2\n"
        "        LD A,~5\n"
        "        LD A,5 ^ 3 | 8\n"
        "        LD HL,-(lab*2)*3\n");
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "LAB LD HL,LAB+2*3")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,LAB[")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,(LAB+1)]")) << text;
    EXPECT_TRUE(Contains(lines, "LD BC,LAB~")) << text;
    EXPECT_TRUE(Contains(lines, "LD DE,10-(+4+2)")) << text;   // a part after - begins with an operator
    EXPECT_TRUE(Contains(lines, "LD HL,(LAB+1)*2")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,LAB<<2")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,5|#FFFF")) << text;   // ~x as XOR #FFFF (STORM's | is XOR, ! OR)
    EXPECT_TRUE(Contains(lines, "LD A,5|3!8")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,(LAB*2~)*3")) << text;      // STORM 1.3: 2*3~*2 = -6
}

TEST(StormBackend_Test, LowPrioritiesInParenthesesAreValues)
{
    // STORM 1.3 computes only priorities 4 and up in parentheses: 1+(6&3) gives 7 there
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToStorm(
        "        ORG #6000\n"
        "lab     LD HL,(lab&7)+1\n"
        "        LD HL,(lab+1)*2\n"
        "        LD HL,lab&7\n"
        "        LD A,(IX+5|2)\n"
        "        LD HL,(5|2)\n"
        "        LD HL,(1<2)+lab\n",
        &diagnostics);
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "LAB LD HL,1")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,(LAB+1)*2")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,LAB&7")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,(IX+7)")) << text;   // an index's parentheses too
    EXPECT_TRUE(Contains(lines, "LD HL,(5!2)")) << text;   // not a memory operand's
    EXPECT_TRUE(Contains(lines, "LD HL,#5FFF")) << text;   // -1 + lab
    EXPECT_TRUE(Mentions(diagnostics, "in parentheses")) << text;
}

TEST(StormBackend_Test, AWideLineGetsAnEquOfItsLongestExpression)
{
    // STORM edits 40 columns; its EQU takes no label defined further down, an instruction takes a later EQU
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToStorm(
        "value11_a equ 6\n"
        "value_t_a equ 3\n"
        "        ORG #8000\n"
        "        ld h,(iy+(value11_a*value11_a)+value_t_a-1)\n"
        "        ld (iy+(value11_a*value11_a)+value_t_a-1),h\n"
        "        ld hl,(value_e_a-value_4_a-2)*256+#18\n"
        "        ret\n"
        "value_e_a equ 300\n"
        "value_4_a equ 48\n"
        "        nop\n",
        &diagnostics);
    const std::string text = Joined(lines);
    const auto at = [&](const std::string& line) { return std::find(lines.begin(), lines.end(), line) - lines.begin(); };
    EXPECT_TRUE(Contains(lines, "S EQU VALUE11*VALUE11+VALUE_T-1")) << text;
    EXPECT_TRUE(Contains(lines, "LD H,(IY+S)")) << text;
    EXPECT_TRUE(Contains(lines, "LD (IY+S),H")) << text;   // the same value: the same name
    EXPECT_EQ(std::count_if(lines.begin(), lines.end(), [](const std::string& l) { return l.find("EQU VALUE11*") != std::string::npos; }), 1) << text;
    EXPECT_TRUE(Contains(lines, "S1 EQU (VALUE_E-VALUE_4-2)*256+#18")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,S1")) << text;
    EXPECT_LT(at("S EQU VALUE11*VALUE11+VALUE_T-1"), at("LD H,(IY+S)")) << text;
    EXPECT_GT(at("S1 EQU (VALUE_E-VALUE_4-2)*256+#18"), at("VALUE_4 EQU 48")) << text;   // after its last name
    EXPECT_FALSE(Mentions(diagnostics, "STORM edits 40")) << text;
}

TEST(StormBackend_Test, NamesHaveSevenCharacters)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToStorm(
        "        ORG #6000\n"
        "longname_a NOP\n"
        "longname_b NOP\n"
        "main.loop  NOP\n"
        "halt2   JR longname_b\n"
        "ld      NOP\n",
        &diagnostics);
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "LONGNAM NOP")) << text;
    EXPECT_TRUE(Contains(lines, "LONGNA1 NOP")) << text;
    EXPECT_TRUE(Contains(lines, "MAIN_LO NOP")) << text;
    EXPECT_TRUE(Contains(lines, "HALT2 JR LONGNA1")) << text;
    EXPECT_TRUE(Contains(lines, "LD1 NOP")) << text;
    EXPECT_TRUE(Mentions(diagnostics, "label longname_b written as LONGNA1"));
}

TEST(StormBackend_Test, ConditionsAreDecidedAndOneLineRepeats)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToStorm(
        "        ORG #6000\n"
        "SIZE    EQU 3\n"
        "        IF SIZE > 2\n"
        "        DB 1\n"
        "        ELSE\n"
        "        DB 2\n"
        "        ENDIF\n"
        "        DUP SIZE\n"
        "        LDI\n"
        "        EDUP\n"
        "        DUP 2\n"
        "        NOP\n"
        "        INC A\n"
        "        EDUP\n"
        "        DISP #9000\n"
        "w       NOP\n"
        "        ENT\n"
        "        DS 4,#AB\n"
        "        DB \"Hi\",0,\"there\",\"@AB\"\n"
        "        ex af,af'\n"
        "        in f,(c)\n"
        "        sll b\n",
        &diagnostics);
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "DB 1")) << text;
    EXPECT_FALSE(Contains(lines, "DB 2")) << text;
    EXPECT_TRUE(Contains(lines, ".3 LDI")) << text;
    EXPECT_EQ(std::count(lines.begin(), lines.end(), "INC A"), 2) << text;   // two lines: unrolled
    EXPECT_TRUE(Contains(lines, "ORG #9000,$")) << text;
    EXPECT_TRUE(Contains(lines, "ORG $-#9000+#600B")) << text;
    EXPECT_TRUE(Contains(lines, "DS 4,#AB")) << text;
    EXPECT_TRUE(Contains(lines, "DB 72,105,0,\"there\",64,65,66")) << text;
    EXPECT_TRUE(Contains(lines, "EXA")) << text;
    EXPECT_TRUE(Contains(lines, "INF")) << text;
    EXPECT_TRUE(Contains(lines, "SLI B")) << text;
}

TEST(StormBackend_Test, ADataListBreaksBeforeTheFortiethColumn)
{
    // the items fill 29 characters of the 40 only with their commas: a list one character longer goes on (the comma
    // was not counted: STORM got a 41-character line)
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToStorm("        DW 12,#2345,#3456,#4567,#56,#6789\n", &diagnostics);
    for (const Diagnostic& d : diagnostics)
        EXPECT_EQ(d.severity, Severity::Info) << d.message;
    EXPECT_EQ(lines, (std::vector<std::string>{"DW 12,#2345,#3456,#4567,#56", "DW #6789"})) << Joined(lines);
}

TEST(StormBackend_Test, ProgramsKeepTheirBytesThroughStorm)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    // Every instruction form: the same bytes as sjasmplus builds from the original
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
        const std::vector<uint8_t> original = Assemble(sjasmplus, {{"PROG.asm", Bytes(source)}}, "PROG.asm", "#8000", "$-#8000");
        EXPECT_FALSE(original.empty());
        EXPECT_EQ(AssembleStorm(sjasmplus, StormRoundTrip(source, "instructions"), "#8000", "$-#8000"), original);
    }
    // STORM 1.3's own test programs, through sjasmplus and back (from #8000; STORMT1 INCBINs INC)
    const codecs::StormCodec storm;
    for (const std::string name : {"STORMT1", "STORMT2"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/" + name + ".bin");
        EXPECT_EQ(AssembleStorm(sjasmplus, StormRoundTrip(Decoded(storm, Hobeta("dialects/storm13/" + name + ".$C")), name), "#8000",
                                std::to_string(expected.size()), {{"INC", Hobeta("dialects/storm13/INC.$C").data}}),
                  expected)
            << name;
    }
    // XAS 7.447's and 4.18's constructs
    const codecs::XasCodec xas;
    EXPECT_EQ(AssembleStorm(sjasmplus, StormRoundTrip(Decoded(xas, Hobeta("dialects/xas7447/constrct.$X")), "constrct"), "#6000", "#110", {}, true),
              ReadTestData("dialects/xas7447/constrct.bin"));
    EXPECT_EQ(AssembleStorm(sjasmplus, StormRoundTrip(Decoded(xas, Hobeta("dialects/xas418/cons418.$X")), "cons418"), "#6000", "#90", {}, true),
              ReadTestData("dialects/xas418/cons418.bin"));
    // ZAsm 3.15 (from #8000)
    const codecs::ZxasmCodec zxasm;
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleStorm(sjasmplus, StormRoundTrip(Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15"), name), "#8000",
                                std::to_string(expected.size())),
                  expected)
            << name;
    }
    // TASM 5.0 beta (from #7000)
    {
        const codecs::TasmCodec tasm;
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssembleStorm(sjasmplus, StormRoundTrip(Decoded(tasm, Hobeta("tasm/T50PROG.$A")), "T50PROG"), "#7000", std::to_string(expected.size())),
                  expected);
    }
}
