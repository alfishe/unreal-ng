// The XAS backend (part B, Z3): the IR written as XAS source. Construct by construct (left-to-right chains without
// parentheses, names of 7 significant characters, conditions decided while converting, repeats as !ASSM), and a
// round trip on programs whose bytes are known: each is written as sjasmplus, then as XAS, the XAS text goes through
// the XAS codec (it must pack and read back as written) and back to sjasmplus through the XAS frontend, which
// models XAS's arithmetic (research-xas-to-sjasmplus.md). With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the result is
// assembled and compared with the original assemblers' bytes.

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

#include "scratchdir.h"

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

/// The XAS lines of a sjasmplus source (blank and comment lines dropped, blanks collapsed)
std::vector<std::string> ToXas(const std::string& sjasmplus, Diagnostics* diagnostics = nullptr, const std::string& version = "7.43")
{
    BackendOptions options;
    options.targetVersion = version;
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "xas", options);
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

/// A source of any dialect as sjasmplus, then as XAS (codec version `version`) through the codec and back: the XAS
/// text read back from the bytes XAS would hold (checked to be the text written, up to the editor's layout)
SourceDocument XasRoundTrip(const SourceDocument& source, const std::string& label, const std::string& version = "7.43")
{
    SourceDocument sjasmplus = source;
    if (source.dialect != "sjasmplus")
    {
        const ConvertResult r = Convert(source, "sjasmplus");
        EXPECT_TRUE(r.ok) << label;
        sjasmplus = SourceDocument::FromText(r.document.Text(), "sjasmplus");
    }
    BackendOptions options;
    options.targetVersion = version;
    const ConvertResult xas = Convert(sjasmplus, "xas", options);
    EXPECT_TRUE(xas.ok) << label;
    for (const Diagnostic& d : xas.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " line " << d.line << ": " << d.message;
    const codecs::XasCodec codec;
    EncodeOptions encode;
    encode.subversion = version;
    const EncodeResult bytes = codec.Encode(xas.document, encode);
    EXPECT_TRUE(bytes.ok) << label;
    for (const Diagnostic& d : bytes.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " (codec) line " << d.line << ": " << d.message;
    DecodeOptions decode;
    decode.subversion = version;
    const DecodeResult back = codec.Decode(bytes.bytes, decode);
    EXPECT_EQ(back.document.lines.size(), xas.document.lines.size()) << label;
    for (size_t k = 0; k < std::min(back.document.lines.size(), xas.document.lines.size()); ++k)
        EXPECT_EQ(Upper(Squeezed(back.document.lines[k].text)), Upper(Squeezed(xas.document.lines[k].text))) << label << " line " << k + 1;
    return back.document;
}

/// sjasmplus run on a harness that INCLUDEs `main` and saves `start`..`end` (over memory filled with #AA when
/// `filled`, as the XAS oracles were assembled); empty on an error
std::vector<uint8_t> Assemble(const char* sjasmplus, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files, const std::string& main,
                              const std::string& start, const std::string& length, bool filled = false)
{
    const std::filesystem::path dir = unreal_asm_tests::UniqueDir("unreal-asm-tests");
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

/// The XAS text assembled the way XAS computes it: back to sjasmplus through the XAS frontend
std::vector<uint8_t> AssembleXas(const char* sjasmplus, const SourceDocument& xas, const std::string& start, const std::string& length,
                                 std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {}, bool filled = false)
{
    const ConvertResult r = Convert(xas, "sjasmplus");
    EXPECT_TRUE(r.ok);
    files.push_back({"PROG.asm", Bytes(r.document)});
    return Assemble(sjasmplus, files, "PROG.asm", start, length, filled);
}
}  // namespace

TEST(XasBackend_Test, ExpressionsAreChainsWithoutParentheses)
{
    const std::vector<std::string> lines = ToXas(
        "        ORG #6000\n"
        "lab     LD HL,lab+2*3\n"        // the tree a+(b*c): its value (a later label is not needed: lab is known)
        "        LD A,high lab\n"
        "        LD A,low lab\n"
        "        LD BC,-lab\n"
        "        LD DE,10-(4+2)\n"
        "        LD HL,(lab+1)*2\n"
        "        LD HL,lab<<2\n"
        "        LD A,~5\n");
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "lab LD HL,2*3+lab")) << text;   // a+(b*c): the atom to the right
    EXPECT_TRUE(Contains(lines, "LD A,lab/256")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,lab*256/256")) << text;
    EXPECT_TRUE(Contains(lines, "LD BC,0-lab")) << text;
    EXPECT_TRUE(Contains(lines, "LD DE,10-4-2")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,lab+1*2")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,lab*4")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,5!#ffff")) << text;
}

TEST(XasBackend_Test, NamesHaveSevenSignificantCharacters)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToXas(
        "        ORG #6000\n"
        "longname_a NOP\n"
        "longname_b NOP\n"
        "main.loop  NOP\n"
        "halt2   JR longname_b\n"
        "ld      NOP\n",
        &diagnostics);
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "longnam NOP")) << text;
    EXPECT_TRUE(Contains(lines, "longna1 NOP")) << text;
    EXPECT_TRUE(Contains(lines, "main_lo NOP")) << text;
    EXPECT_TRUE(Contains(lines, "halt2 JR longna1")) << text;
    EXPECT_TRUE(Contains(lines, "ld1 NOP")) << text;
    EXPECT_TRUE(Mentions(diagnostics, "label longname_b written as longna1"));
}

TEST(XasBackend_Test, KnownConditionsAreDecidedAndRepeatsStay)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToXas(
        "        ORG #6000\n"
        "SIZE    EQU 3\n"
        "        IF SIZE > 2\n"
        "        DB 1\n"
        "        ELSE\n"
        "        DB 2\n"
        "        ENDIF\n"
        "        DUP SIZE\n"
        "        NOP\n"
        "        EDUP\n"
        "        DISP #9000\n"
        "w       NOP\n"
        "        ENT\n"
        "        DS 4,#AB\n"
        "        DB \"Hi\",0,\"there\"\n",
        &diagnostics);
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "DB 1")) << text;
    EXPECT_FALSE(Contains(lines, "DB 2")) << text;
    EXPECT_TRUE(Contains(lines, "!ASSM size")) << text;
    EXPECT_TRUE(Contains(lines, "!CONT")) << text;
    EXPECT_TRUE(Contains(lines, "WORK #9000")) << text;
    EXPECT_TRUE(Contains(lines, "WORK")) << text;
    EXPECT_TRUE(Contains(lines, "DS 4,#abab")) << text;
    EXPECT_TRUE(Contains(lines, "DB 72,105,0,\"there\"")) << text;
    // XAS 4.18: DEFB without strings, DEFM for them; 9.07m's dot words
    const std::vector<std::string> old = ToXas("        ORG #6000\n        DB 1,\"abc\"\n        DUP 2\n        NOP\n        EDUP\n", nullptr, "4.18");
    EXPECT_TRUE(Contains(old, "DEFB 1")) << Joined(old);
    EXPECT_TRUE(Contains(old, "DEFM \"abc\"")) << Joined(old);
    const std::vector<std::string> dots = ToXas("        ORG #6000\n        DUP 2\n        NOP\n        EDUP\n", nullptr, "9.07m");
    EXPECT_TRUE(Contains(dots, ".ASM 2")) << Joined(dots);
    EXPECT_TRUE(Contains(dots, ".END")) << Joined(dots);
}

TEST(XasBackend_Test, ProgramsKeepTheirBytesThroughXas)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    // Every instruction form: the same bytes as sjasmplus builds from the original
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
        const std::vector<uint8_t> original = Assemble(sjasmplus, {{"PROG.asm", Bytes(source)}}, "PROG.asm", "#8000", "$-#8000");
        EXPECT_FALSE(original.empty());
        for (const std::string version : {"7.43", "4.18", "9.10"})
            EXPECT_EQ(AssembleXas(sjasmplus, XasRoundTrip(source, "instructions " + version, version), "#8000", "$-#8000"), original) << version;
    }
    // XAS 7.447's and 4.18's constructs, through sjasmplus and back to XAS
    const codecs::XasCodec xas;
    EXPECT_EQ(AssembleXas(sjasmplus, XasRoundTrip(Decoded(xas, Hobeta("dialects/xas7447/constrct.$X")), "constrct"), "#6000", "#110", {}, true),
              ReadTestData("dialects/xas7447/constrct.bin"));
    EXPECT_EQ(AssembleXas(sjasmplus, XasRoundTrip(Decoded(xas, Hobeta("dialects/xas418/cons418.$X")), "cons418", "4.18"), "#6000", "#90", {}, true),
              ReadTestData("dialects/xas418/cons418.bin"));
    // STORM 1.3 (from #8000; STORMT1 INCBINs INC)
    const codecs::StormCodec storm;
    for (const std::string name : {"STORMT1", "STORMT2"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/" + name + ".bin");
        EXPECT_EQ(AssembleXas(sjasmplus, XasRoundTrip(Decoded(storm, Hobeta("dialects/storm13/" + name + ".$C")), name), "#8000",
                              std::to_string(expected.size()), {{"INC", Hobeta("dialects/storm13/INC.$C").data}}),
                  expected)
            << name;
    }
    // ZAsm 3.15 (from #8000)
    const codecs::ZxasmCodec zxasm;
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleXas(sjasmplus, XasRoundTrip(Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15"), name), "#8000",
                              std::to_string(expected.size())),
                  expected)
            << name;
    }
    // TASM 5.0 beta (from #7000)
    {
        const codecs::TasmCodec tasm;
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssembleXas(sjasmplus, XasRoundTrip(Decoded(tasm, Hobeta("tasm/T50PROG.$A")), "T50PROG"), "#7000", std::to_string(expected.size())),
                  expected);
    }
}
