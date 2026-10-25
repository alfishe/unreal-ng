// The TASM backend (part B, Z3): the IR written as TASM 4.12 source. Construct by construct (left to right with
// parentheses, postfix { } ^, 4.12's .IF that assembles on 0, DISP as .PHASE), and a round trip on programs whose
// bytes are known: each is written as sjasmplus, then as TASM, the TASM text goes through the TASM codec (it must
// tokenize and read back as written) and back to sjasmplus through the TASM frontend, which models TASM's arithmetic
// (research-tasm-to-sjasmplus.md). With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the result is assembled and compared
// with the original assemblers' bytes.

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

/// The TASM lines of a sjasmplus source (blank and comment lines dropped, blanks collapsed)
std::vector<std::string> ToTasm(const std::string& sjasmplus, Diagnostics* diagnostics = nullptr, const std::string& version = "4.12")
{
    BackendOptions options;
    options.targetVersion = version;
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "tasm", options);
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

/// A source of any dialect as sjasmplus, then as TASM 4.12 through the codec and back: the TASM text read back from
/// the bytes TASM would hold (checked to be the text written, up to the editor's layout)
SourceDocument TasmRoundTrip(const SourceDocument& source, const std::string& label)
{
    SourceDocument sjasmplus = source;
    if (source.dialect != "sjasmplus")
    {
        const ConvertResult r = Convert(source, "sjasmplus");
        EXPECT_TRUE(r.ok) << label;
        sjasmplus = SourceDocument::FromText(r.document.Text(), "sjasmplus");
    }
    BackendOptions options;
    options.targetVersion = "4.12";
    const ConvertResult tasm = Convert(sjasmplus, "tasm", options);
    EXPECT_TRUE(tasm.ok) << label;
    for (const Diagnostic& d : tasm.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " line " << d.line << ": " << d.message;
    const codecs::TasmCodec codec;
    EncodeOptions encode;
    encode.subversion = "4.12";
    const EncodeResult bytes = codec.Encode(tasm.document, encode);
    EXPECT_TRUE(bytes.ok) << label;
    for (const Diagnostic& d : bytes.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " (codec) line " << d.line << ": " << d.message;
    DecodeOptions decode;
    decode.subversion = "4.12";
    const DecodeResult back = codec.Decode(bytes.bytes, decode);
    EXPECT_EQ(back.document.lines.size(), tasm.document.lines.size()) << label;
    for (size_t k = 0; k < std::min(back.document.lines.size(), tasm.document.lines.size()); ++k)
        EXPECT_EQ(Upper(Squeezed(back.document.lines[k].text)), Upper(Squeezed(tasm.document.lines[k].text))) << label << " line " << k + 1;
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

/// The TASM text assembled the way TASM computes it: back to sjasmplus through the TASM frontend
std::vector<uint8_t> AssembleTasm(const char* sjasmplus, const SourceDocument& tasm, const std::string& start, const std::string& length,
                                 std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {}, bool filled = false)
{
    const ConvertResult r = Convert(tasm, "sjasmplus");
    EXPECT_TRUE(r.ok);
    files.push_back({"PROG.asm", Bytes(r.document)});
    return Assemble(sjasmplus, files, "PROG.asm", start, length, filled);
}
}  // namespace

TEST(TasmBackend_Test, ExpressionsGoLeftToRight)
{
    const std::vector<std::string> lines = ToTasm(
        "        ORG #6000\n"
        "lab     LD HL,lab+2*3\n"
        "        LD A,high lab\n"
        "        LD A,low (lab+1)\n"
        "        LD BC,-lab\n"
        "        LD DE,10-(4+2)\n"
        "        LD HL,(lab+1)*2\n"
        "        LD HL,lab<<2\n"
        "        LD A,~5\n"
        "        LD A,(lab)\n");
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "LAB LD HL,LAB+(2*3)")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,LAB/256")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,LAB+1&255")) << text;
    EXPECT_TRUE(Contains(lines, "LD BC,0-LAB")) << text;
    EXPECT_TRUE(Contains(lines, "LD DE,10-(4+2)")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,LAB+1*2")) << text;   // left to right
    EXPECT_TRUE(Contains(lines, "LD HL,LAB*4")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,#FFFF-5")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,(LAB)")) << text;
}

/// What the older versions refuse or hang on, seen in unreal-ng: 4.0 a label of more than 7 characters (error 1) and
/// DEFS with a fill (pass 1 never ends); 3.x also the postfix { } (error 8) and the index registers' halves (error 5)
TEST(TasmBackend_Test, TheOlderVersionsGetWhatTheyTake)
{
    const char* source =
        "        ORG #6000\n"
        "main_loop LD A,high main_loop\n"
        "        JR main_loop\n"
        "        LD A,IXH\n"
        "        SLI B\n"
        "        DS 3,#AA\n"
        "        DS 2\n";
    const std::vector<std::string> v40 = ToTasm(source, nullptr, "4.0");
    std::string text = Joined(v40);
    EXPECT_TRUE(Contains(v40, "MAIN_LO LD A,MAIN_LO{")) << text;
    EXPECT_TRUE(Contains(v40, "JR MAIN_LO")) << text;
    EXPECT_TRUE(Contains(v40, "LD A,HX")) << text;
    EXPECT_TRUE(Contains(v40, "SLI B")) << text;
    EXPECT_TRUE(Contains(v40, "DEFB 170,170,170")) << text;
    EXPECT_TRUE(Contains(v40, "DEFS 2")) << text;
    for (const char* version : {"3", "2.0"})
    {
        const std::vector<std::string> v3 = ToTasm(source, nullptr, version);
        text = Joined(v3);
        EXPECT_TRUE(Contains(v3, "MAIN_LO LD A,MAIN_LO/256")) << text;
        EXPECT_TRUE(std::none_of(v3.begin(), v3.end(), [](const std::string& l) { return l.find("HX") != std::string::npos || l.rfind("SLI", 0) == 0; })) << text;
        EXPECT_TRUE(std::any_of(v3.begin(), v3.end(), [](const std::string& l) { return l.rfind("DEFB #DD,#7C", 0) == 0; })) << text;
        EXPECT_TRUE(Contains(v3, "DEFB 170,170,170")) << text;
    }
    const std::vector<std::string> v412 = ToTasm(source);
    EXPECT_TRUE(Contains(v412, "MAIN_LOOP LD A,MAIN_LOOP/256")) << Joined(v412);
    EXPECT_TRUE(Contains(v412, "DEFS 3,#AA")) << Joined(v412);
}

TEST(TasmBackend_Test, ConditionsGoToDotIfAndBlocksUnroll)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToTasm(
        "        ORG #6000\n"
        "SIZE    EQU 3\n"
        "        IF SIZE > 2\n"
        "        DB 1\n"
        "        ELSE\n"
        "        DB 2\n"
        "        ENDIF\n"
        "        IF ext == 5\n"
        "        DB 3\n"
        "        ENDIF\n"
        "        DUP 2\n"
        "        NOP\n"
        "        EDUP\n"
        "        DISP #9000\n"
        "w       NOP\n"
        "        ENT\n"
        "        DS 4,#AB\n"
        "        DB \"Hi\",0,\"there\"\n"
        "        in f,(c)\n"
        "        sll b\n"
        "later   EQU 5\n",
        &diagnostics);
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "DEFB 1")) << text;
    EXPECT_FALSE(Contains(lines, "DEFB 2")) << text;
    EXPECT_TRUE(Contains(lines, ".IF EXT-5")) << text;   // assembled when 0
    EXPECT_TRUE(Contains(lines, ".ENDIF")) << text;
    EXPECT_EQ(std::count(lines.begin(), lines.end(), "NOP"), 2) << text;
    EXPECT_TRUE(Contains(lines, ".PHASE #9000")) << text;
    EXPECT_TRUE(Contains(lines, ".UNPHASE")) << text;
    EXPECT_TRUE(Contains(lines, "DEFS 4,#AB")) << text;
    EXPECT_TRUE(Contains(lines, "DEFB 72,105,0,\"there\"")) << text;
    EXPECT_TRUE(Contains(lines, "DEFB #ED,#70")) << text;   // no IN F,(C) in 4.12
    EXPECT_TRUE(Contains(lines, "DEFB #CB,48")) << text;    // no SLI
}

TEST(TasmBackend_Test, ProgramsKeepTheirBytesThroughTasm)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    // Every instruction form: the same bytes as sjasmplus builds from the original
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
        const std::vector<uint8_t> original = Assemble(sjasmplus, {{"PROG.asm", Bytes(source)}}, "PROG.asm", "#8000", "$-#8000");
        EXPECT_FALSE(original.empty());
        EXPECT_EQ(AssembleTasm(sjasmplus, TasmRoundTrip(source, "instructions"), "#8000", "$-#8000"), original);
    }
    // (TASM 4.12's SIN7 is not here: a macro calling itself 64 times, steered by values carried from TASM's first pass
    // to its second (.IF PASS), which an expansion cannot keep)
    const codecs::TasmCodec tasm;
    // STORM 1.3's test programs (from #8000; STORMT1 INCBINs INC; the rest of its last sector, which TASM's INCBIN
    // copies, is overwritten by the code after it)
    const codecs::StormCodec storm;
    for (const std::string name : {"STORMT1", "STORMT2"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/" + name + ".bin");
        EXPECT_EQ(AssembleTasm(sjasmplus, TasmRoundTrip(Decoded(storm, Hobeta("dialects/storm13/" + name + ".$C")), name), "#8000",
                               std::to_string(expected.size()),
                               {{"INC", Hobeta("dialects/storm13/INC.$C").data}, {"INC.slack", {}}}),   // TASM's INCBIN copies its last sector
                  expected)
            << name;
    }
    // XAS 7.447's and 4.18's constructs
    const codecs::XasCodec xas;
    EXPECT_EQ(AssembleTasm(sjasmplus, TasmRoundTrip(Decoded(xas, Hobeta("dialects/xas7447/constrct.$X")), "constrct"), "#6000", "#110", {}, true),
              ReadTestData("dialects/xas7447/constrct.bin"));
    EXPECT_EQ(AssembleTasm(sjasmplus, TasmRoundTrip(Decoded(xas, Hobeta("dialects/xas418/cons418.$X")), "cons418"), "#6000", "#90", {}, true),
              ReadTestData("dialects/xas418/cons418.bin"));
    // ZAsm 3.15 (from #8000)
    const codecs::ZxasmCodec zxasm;
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleTasm(sjasmplus, TasmRoundTrip(Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15"), name), "#8000",
                               std::to_string(expected.size())),
                  expected)
            << name;
    }
    // TASM 5.0 beta (from #7000)
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssembleTasm(sjasmplus, TasmRoundTrip(Decoded(tasm, Hobeta("tasm/T50PROG.$A")), "T50PROG"), "#7000", std::to_string(expected.size())),
                  expected);
    }
}
