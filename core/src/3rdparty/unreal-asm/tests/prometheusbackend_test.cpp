// The PROMETHEUS backend (part B, Z4): the IR written as PROMETHEUS 48K source. Construct by construct (chains left to
// right with + - * / ?, names of 8 characters, ORG / PUT, DS as DEFB zeros, the comment on a line of its own), and a
// round trip on programs whose bytes are known: each is written as sjasmplus, then as PROMETHEUS, the text goes through
// its codec (the records PROMETHEUS stores) and back to sjasmplus through its frontend, which models its arithmetic
// (research-prometheus.md). With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the result is assembled and compared with
// the original assemblers' bytes.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/masm/masmcodec.h"
#include "codecs/prometheus/prometheuscodec.h"
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
    for (const char c : t)
        if (c != ' ' || (!out.empty() && out.back() != ' '))
            out.push_back(c);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

/// The PROMETHEUS lines of a sjasmplus source (blank and comment lines dropped, blanks collapsed)
std::vector<std::string> ToProm(const std::string& sjasmplus, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "prometheus");
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

/// A source of any dialect as sjasmplus, then as PROMETHEUS through its codec and back: the text read back from the
/// records PROMETHEUS would hold (checked to be the text written)
SourceDocument PromRoundTrip(const SourceDocument& source, const std::string& label)
{
    SourceDocument sjasmplus = source;
    if (source.dialect != "sjasmplus")
    {
        const ConvertResult r = Convert(source, "sjasmplus");
        EXPECT_TRUE(r.ok) << label;
        sjasmplus = SourceDocument::FromText(r.document.Text(), "sjasmplus");
    }
    const ConvertResult written = Convert(sjasmplus, "prometheus");
    EXPECT_TRUE(written.ok) << label;
    for (const Diagnostic& d : written.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " line " << d.line << ": " << d.message;
    const codecs::PrometheusCodec codec;
    const EncodeResult bytes = codec.Encode(written.document, {});
    EXPECT_TRUE(bytes.ok) << label;
    for (const Diagnostic& d : bytes.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " (codec) line " << d.line << ": " << d.message;
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

/// The PROMETHEUS text assembled the way it computes: back to sjasmplus through its frontend
std::vector<uint8_t> AssembleProm(const char* sjasmplus, const SourceDocument& written, const std::string& start, const std::string& length, bool filled = false)
{
    const ConvertResult r = Convert(written, "sjasmplus");
    EXPECT_TRUE(r.ok);
    return One(sjasmplus, Bytes(r.document), start, length, filled);
}
}  // namespace

TEST(PrometheusBackend_Test, LinesAsPrometheusShowsThem)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToProm(
        "        ORG #6000\n"
        "longlabelname LD HL,(longlabelname+1)*2 ; a comment\n"
        "        LD A,high longlabelname\n"
        "        LD A,low longlabelname\n"
        "        LD BC,2+3*4\n"
        "        LD HL,lab2-(1+2)\n"
        "        LD A,ixh\n"
        "        SLI (IY-4)\n"
        "        EX AF,AF'\n"
        "        IN F,(C)\n"
        "        DS 3\n"
        "        DB \"Hello\",13\n"
        "lab2    DISP #9000\n"
        "w       NOP\n"
        "        ENT\n"
        "__tmp   JR __tmp\n"
        "        JR $\n"
        "        LD IX,$+3\n"
        "        LD (IY+5),low $\n",
        &diagnostics);
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "org #6000")) << text;
    EXPECT_TRUE(Contains(lines, "LONGLABE ld hl,LONGLABE+1*2")) << text;   // 8 characters, left to right
    EXPECT_TRUE(Contains(lines, "ld a,LONGLABE/256")) << text;
    EXPECT_TRUE(Contains(lines, "ld a,LONGLABE?256")) << text;
    EXPECT_TRUE(Contains(lines, "ld bc,3*4+2")) << text;   // 3*4 first: the atom moved to the right
    EXPECT_TRUE(Contains(lines, "ld hl,LAB2-1-2")) << text;
    EXPECT_TRUE(Contains(lines, "ld a,hx")) << text;
    EXPECT_TRUE(Contains(lines, "slia (iy-4)")) << text;
    EXPECT_TRUE(Contains(lines, "ex af,af'")) << text;
    EXPECT_TRUE(Contains(lines, "defb #ED,#70")) << text;   // no IN F,(C) in its table
    EXPECT_TRUE(Contains(lines, "defb 0,0,0")) << text;     // its DEFS leaves a hole
    EXPECT_TRUE(Contains(lines, "defm \"Hello\"")) << text;
    EXPECT_TRUE(Contains(lines, "defb 13")) << text;
    EXPECT_TRUE(Contains(lines, "LAB2")) << text;           // the place, before the ORG
    EXPECT_TRUE(Contains(lines, "org #9000")) << text;      // both addresses, then the bytes back
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("put ", 0) == 0; })) << text;
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("org $-#9000+", 0) == 0; })) << text;
    EXPECT_TRUE(Contains(lines, "TMP jr TMP")) << text;   // a name starts with a letter
    // its $ in an instruction is where the operand's value goes
    EXPECT_TRUE(Contains(lines, "jr $-1")) << text;
    EXPECT_TRUE(Contains(lines, "ld ix,$-2+3")) << text;
    EXPECT_TRUE(Contains(lines, "ld (iy+5),$-3?256")) << text;
    for (const Diagnostic& d : diagnostics)
        EXPECT_EQ(d.severity, Severity::Info) << d.message;
}

TEST(PrometheusBackend_Test, ALargeHoleStaysAHoleWithAWarning)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToProm("        ORG #6000\n        DS 5000\n", &diagnostics);
    EXPECT_TRUE(Contains(lines, "defs 5000")) << Joined(lines);
    EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::Warning; }));
    // up to 4096 bytes the zeros are written (FlexNavigator clears a place with DS #400 over code laid there before)
    Diagnostics none;
    const std::vector<std::string> zeros = ToProm("        ORG #6000\n        DS 1000\n", &none);
    EXPECT_FALSE(std::any_of(zeros.begin(), zeros.end(), [](const std::string& l) { return l.rfind("defs", 0) == 0; })) << Joined(zeros);
}

TEST(PrometheusBackend_Test, ProgramsKeepTheirBytesThroughPrometheus)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
        const std::vector<uint8_t> original = One(sjasmplus, Bytes(source), "#8000", "$-#8000");
        EXPECT_FALSE(original.empty());
        EXPECT_EQ(AssembleProm(sjasmplus, PromRoundTrip(source, "instructions"), "#8000", "$-#8000"), original);
    }
    // PROMETHEUS 48K's own probe: what it built from 60000, 60200 and (PUT) 62000 over memory filled with #AA
    {
        std::vector<containers::TapeBlock> blocks;
        std::string error;
        ASSERT_TRUE(containers::ReadTapeBlocks(ReadTestData("dialects/prometheus/PROBE.tap"), blocks, error)) << error;
        const SourceDocument probe = codecs::PrometheusCodec().Decode(blocks[1].data, {}).document;
        const ConvertResult r = Convert(PromRoundTrip(probe, "PROBE"), "sjasmplus");
        ASSERT_TRUE(r.ok);
        const auto out = Assemble(sjasmplus, {{"PROG.asm", Bytes(r.document)}}, "PROG.asm",
                                  "        ORG 60000\n        DS 256,#AA\n        ORG 62000\n        DS 16,#AA\n        ORG 0\n",
                                  {{"60000", "48"}, {"60200", "4"}, {"62000", "8"}});
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[0], ReadTestData("dialects/prometheus/PROBE-60000.bin"));
        EXPECT_EQ(out[1], ReadTestData("dialects/prometheus/PROBE-60200.bin"));
        EXPECT_EQ(out[2], ReadTestData("dialects/prometheus/PROBE-62000.bin"));
    }
    const codecs::ZxasmCodec zxasm;
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleProm(sjasmplus, PromRoundTrip(Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15"), name), "#8000",
                               std::to_string(expected.size())),
                  expected)
            << name;
    }
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/STORMT2.bin");
        EXPECT_EQ(AssembleProm(sjasmplus, PromRoundTrip(Decoded(codecs::StormCodec(), Hobeta("dialects/storm13/STORMT2.$C")), "STORMT2"), "#8000",
                               std::to_string(expected.size())),
                  expected);
    }
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/masm11/MT1.bin");
        EXPECT_EQ(AssembleProm(sjasmplus, PromRoundTrip(Decoded(codecs::MasmCodec(), Hobeta("dialects/masm11/MT1.$a"), "1.1"), "MT1"), "#6000",
                               std::to_string(expected.size())),
                  expected);
    }
    EXPECT_EQ(AssembleProm(sjasmplus, PromRoundTrip(Decoded(codecs::XasCodec(), Hobeta("dialects/xas7447/constrct.$X")), "constrct"), "#6000", "#110", true),
              ReadTestData("dialects/xas7447/constrct.bin"));
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssembleProm(sjasmplus, PromRoundTrip(Decoded(codecs::TasmCodec(), Hobeta("tasm/T50PROG.$A")), "T50PROG"), "#7000",
                               std::to_string(expected.size())),
                  expected);
    }
}
