// The PASM backend (part B, Z4): the IR written as Power Assembler 3.0 text. Construct by construct (chains left to
// right with + - * /, names of 14 characters, bytes in 0..255, DUP fills, one ORG), and a round trip on programs whose
// bytes are known: each is written as sjasmplus, then as PASM, the text goes through its codec and back to sjasmplus
// through its frontend, which models its arithmetic and its $ (research-power-assembler.md). With
// UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the result is assembled and compared with the original assemblers' bytes.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/masm/masmcodec.h"
#include "codecs/pasm/pasmcodec.h"
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
    // the line as its parts: blanks collapsed, a comment line dropped
    if (!t.empty() && t[0] == ';')
        return {};
    std::string out;
    for (char c : t)
    {
        c = c == '\t' ? ' ' : c;
        if (c != ' ' || (!out.empty() && out.back() != ' '))
            out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

/// The PASM lines of a sjasmplus source (blank and comment lines dropped, blanks and TABs collapsed)
std::vector<std::string> ToPasm(const std::string& sjasmplus, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "pasm");
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

/// A source of any dialect as sjasmplus, then as PASM through its codec (the text file it loads) and back
SourceDocument PasmRoundTrip(const SourceDocument& source, const std::string& label)
{
    SourceDocument sjasmplus = source;
    if (source.dialect != "sjasmplus")
    {
        const ConvertResult r = Convert(source, "sjasmplus");
        EXPECT_TRUE(r.ok) << label;
        sjasmplus = SourceDocument::FromText(r.document.Text(), "sjasmplus");
    }
    const ConvertResult written = Convert(sjasmplus, "pasm");
    EXPECT_TRUE(written.ok) << label;
    for (const Diagnostic& d : written.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " line " << d.line << ": " << d.message;
    const codecs::PasmCodec codec;
    const EncodeResult bytes = codec.Encode(written.document, {});
    EXPECT_TRUE(bytes.ok) << label;
    EXPECT_TRUE(bytes.bytes.size() >= 2 && bytes.bytes[bytes.bytes.size() - 2] == '\r') << label << ": its lines end with CR LF";
    const DecodeResult back = codec.Decode(bytes.bytes, {});
    EXPECT_EQ(back.document.lines.size(), written.document.lines.size()) << label;
    for (size_t k = 0; k < std::min(back.document.lines.size(), written.document.lines.size()); ++k)
        EXPECT_EQ(back.document.lines[k].text, written.document.lines[k].text) << label << " line " << k + 1;
    return back.document;
}

/// sjasmplus run on a harness (`before` the INCLUDE of `main`, `after` it: the SAVEBINs into out*.bin); the files
/// out0.bin, out1.bin ... read back, empty on an error
std::vector<std::vector<uint8_t>> Assemble(const char* sjasmplus, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files, const std::string& main,
                                           const std::string& before, const std::vector<std::pair<std::string, std::string>>& ranges)
{
    const std::filesystem::path dir = unreal_asm_tests::UniqueDir("unreal-asm-tests");
    std::filesystem::create_directories(dir);
    for (const auto& [name, bytes] : files)
        WriteBytes(dir / name, bytes);
    std::string harness = "        DEVICE ZXSPECTRUM48\n" + before + "        INCLUDE \"" + main + "\"\n";
    for (size_t k = 0; k < ranges.size(); ++k)
        harness += "        SAVEBIN \"out" + std::to_string(k) + ".bin\"," + ranges[k].first + "," + ranges[k].second + "\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    std::vector<std::vector<uint8_t>> out;
    if (std::system(command.c_str()) == 0)
        for (size_t k = 0; k < ranges.size(); ++k)
            out.push_back(ReadBytes(dir / ("out" + std::to_string(k) + ".bin")));
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

/// One range of a source (sjasmplus text) assembled
std::vector<uint8_t> One(const char* sjasmplus, const std::vector<uint8_t>& text, const std::string& start, const std::string& length, bool filled = false)
{
    const std::string fill = filled ? "        ORG " + start + "\n        DS " + length + ",#AA\n" : std::string();
    const auto out = Assemble(sjasmplus, {{"PROG.asm", text}}, "PROG.asm", fill, {{start, length}});
    return out.empty() ? std::vector<uint8_t>{} : out[0];
}

/// The PASM text assembled the way it computes: back to sjasmplus through its frontend
std::vector<uint8_t> AssemblePasm(const char* sjasmplus, const SourceDocument& written, const std::string& start, const std::string& length, bool filled = false)
{
    const ConvertResult r = Convert(written, "sjasmplus");
    EXPECT_TRUE(r.ok);
    return One(sjasmplus, Bytes(r.document), start, length, filled);
}
}  // namespace

TEST(PasmBackend_Test, LinesAsPasmTakesThem)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToPasm(
        "        ORG #6000\n"
        "verylonglabelname LD HL,(verylonglabelname+1)*2 ; a comment\n"
        "        LD A,high verylonglabelname\n"
        "        LD BC,2+3*4\n"
        "        LD HL,lab2-(1+2)\n"
        "        LD A,-1\n"
        "        CP -2\n"
        "        LD (IX-3),-4\n"
        "        LD A,(IX)\n"
        "        LD A,ixh\n"
        "        SLL B\n"
        "        EX AF,AF'\n"
        "        IN F,(C)\n"
        "        DS 3\n"
        "        DS 4,#E5\n"
        "        DB \"Hello\",13,-1,'A'\n"
        "        DW 'AB',-1\n"
        "lab2    NOP\n"
        "__tmp   JR __tmp\n"
        "        JR $\n"
        "        LD IX,$+3\n"
        "        LD ($),A\n"
        "        LD BC,($)\n"
        "here    EQU $\n"
        "        ORG #6080\n"
        "        NOP\n",
        &diagnostics);
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "ORG #6000")) << text;
    EXPECT_TRUE(Contains(lines, "VERYLONGLABELN LD HL,VERYLONGLABELN+1*2 ; a comment")) << text;   // 14 characters
    EXPECT_TRUE(Contains(lines, "LD A,VERYLONGLABELN/256")) << text;
    EXPECT_TRUE(Contains(lines, "LD BC,3*4+2")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,LAB2-1-2")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,255")) << text;   // a byte in 0..255
    EXPECT_TRUE(Contains(lines, "CP 254")) << text;
    EXPECT_TRUE(Contains(lines, "LD (IX-3),252")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,(IX+0)")) << text;
    EXPECT_TRUE(Contains(lines, "DB #DD,#7C ;LD A,HX")) << text;
    EXPECT_TRUE(Contains(lines, "SLI B")) << text;
    EXPECT_TRUE(Contains(lines, "EX AF,AF'")) << text;
    EXPECT_TRUE(Contains(lines, "DB #ED,#70 ;IN F,(C)")) << text;
    EXPECT_TRUE(Contains(lines, "DB 0 DUP 3")) << text;
    EXPECT_TRUE(Contains(lines, "DB 229 DUP 4")) << text;
    EXPECT_TRUE(Contains(lines, "DB 'Hello',13,255,65")) << text;
    EXPECT_TRUE(Contains(lines, "DW #4142,0-1")) << text;   // no 'AB'
    EXPECT_TRUE(Contains(lines, "TMP JR TMP")) << text;        // a name starts with a letter
    // its $ in an instruction is what it has put when it reads the operand
    EXPECT_TRUE(Contains(lines, "JR $-1")) << text;
    EXPECT_TRUE(Contains(lines, "LD IX,$-2+3")) << text;
    EXPECT_TRUE(Contains(lines, "LD ($),A")) << text;
    EXPECT_TRUE(Contains(lines, "LD BC,($-2)")) << text;
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("HERE EQU #", 0) == 0; })) << text;   // its $ in EQU is 0
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("DB 0 DUP ", 0) == 0 && l != "DB 0 DUP 3"; })) << text;   // one ORG
    for (const Diagnostic& d : diagnostics)
        EXPECT_EQ(d.severity, Severity::Info) << d.message;
}

TEST(PasmBackend_Test, ADisplacedBlockKeepsItsRunAddresses)
{
    // no DISP in PASM: the block's labels as EQU of their run addresses, its $ as a value, a relative jump by its
    // distance at the run address
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToPasm(
        "        ORG #6000\n        JP run\n        DISP #9000\nrun     LD HL,$\nloop    DJNZ loop\n        JR out\n        ENT\nout     RET\n",
        &diagnostics);
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "RUN EQU #9000")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,36864")) << text;
    EXPECT_TRUE(Contains(lines, "LOOP EQU #9003")) << text;
    EXPECT_TRUE(Contains(lines, "DJNZ $-1")) << text;
    EXPECT_TRUE(Contains(lines, "JR $-12284")) << text;   // to OUT at #6008 from #9005
    for (const Diagnostic& d : diagnostics)
        EXPECT_EQ(d.severity, Severity::Info) << d.message;
}

TEST(PasmBackend_Test, WhatPasmCannotTakeIsReported)
{
    Diagnostics diagnostics;
    ToPasm("        ORG #6000\n        NOP\n        ORG #5000\n        NOP\n", &diagnostics);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::Warning; }), 1);
}

TEST(PasmBackend_Test, ProgramsKeepTheirBytesThroughPasm)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
        const std::vector<uint8_t> original = One(sjasmplus, Bytes(source), "#8000", "$-#8000");
        EXPECT_FALSE(original.empty());
        EXPECT_EQ(AssemblePasm(sjasmplus, PasmRoundTrip(source, "instructions"), "#8000", "$-#8000"), original);
    }
    // PASM 3.0's own probes: what it built at #6000
    for (const std::string name : {"PROBE", "CURRENT"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/pasm/" + name + ".bin");
        const SourceDocument probe = SourceDocument::FromText(ReadTestText(name == "PROBE" ? "pasm/PROBE.txt" : "dialects/pasm/CURRENT.txt"), "pasm");
        EXPECT_EQ(AssemblePasm(sjasmplus, PasmRoundTrip(probe, name), "#6000", std::to_string(expected.size())), expected) << name;
    }
    const codecs::ZxasmCodec zxasm;
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssemblePasm(sjasmplus, PasmRoundTrip(Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15"), name), "#8000",
                               std::to_string(expected.size())),
                  expected)
            << name;
    }
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/STORMT2.bin");
        EXPECT_EQ(AssemblePasm(sjasmplus, PasmRoundTrip(Decoded(codecs::StormCodec(), Hobeta("dialects/storm13/STORMT2.$C")), "STORMT2"), "#8000",
                               std::to_string(expected.size())),
                  expected);
    }
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/masm11/MT1.bin");
        EXPECT_EQ(AssemblePasm(sjasmplus, PasmRoundTrip(Decoded(codecs::MasmCodec(), Hobeta("dialects/masm11/MT1.$a"), "1.1"), "MT1"), "#6000",
                               std::to_string(expected.size())),
                  expected);
    }
    {
        // PASM takes one ORG: the gap before XAS' ORG #6100 is zeros there (XAS left the #AA the memory held)
        const std::vector<uint8_t> expected = ReadTestData("dialects/xas7447/constrct.bin");
        std::vector<uint8_t> built = AssemblePasm(sjasmplus, PasmRoundTrip(Decoded(codecs::XasCodec(), Hobeta("dialects/xas7447/constrct.$X")), "constrct"), "#6000", "#110", true);
        ASSERT_EQ(built.size(), expected.size());
        const auto gap = std::find_if(expected.rbegin() + 0x10, expected.rend(), [](uint8_t b) { return b != 0xAA; }).base();
        for (auto at = gap; at != expected.end() - 0x10; ++at)
        {
            EXPECT_EQ(built[static_cast<size_t>(at - expected.begin())], 0) << "the gap at #" << std::hex << 0x6000 + (at - expected.begin());
            built[static_cast<size_t>(at - expected.begin())] = 0xAA;
        }
        EXPECT_EQ(built, expected);
    }
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssemblePasm(sjasmplus, PasmRoundTrip(Decoded(codecs::TasmCodec(), Hobeta("tasm/T50PROG.$A")), "T50PROG"), "#7000",
                               std::to_string(expected.size())),
                  expected);
    }
}
