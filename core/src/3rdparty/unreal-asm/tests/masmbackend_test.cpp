// The MASM backend (part B, Z3): the IR written as MASM 1.1 source. Construct by construct (left-to-right chains
// without parentheses, labels of 10 characters, BEGIN / END, PHASE), and a round trip on programs whose bytes are
// known: each is written as sjasmplus, then as MASM, the text goes through the MASM codec (it must tokenize and read
// back as written) and back to sjasmplus through the MASM frontend, which models MASM's arithmetic
// (research-masm-to-sjasmplus.md). With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the result is assembled and compared
// with the original assemblers' bytes.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/masm/masmcodec.h"
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

/// The MASM lines of a sjasmplus source (blank and comment lines dropped, blanks collapsed)
std::vector<std::string> ToMasm(const std::string& sjasmplus, Diagnostics* diagnostics = nullptr, const std::string& version = "1.1")
{
    BackendOptions options;
    options.targetVersion = version;
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "masm", options);
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

/// A source of any dialect as sjasmplus, then as MASM 1.1 through the codec and back: the MASM text read back from
/// the bytes MASM would hold (checked to be the text written, up to the editor's layout)
SourceDocument MasmRoundTrip(const SourceDocument& source, const std::string& label)
{
    SourceDocument sjasmplus = source;
    if (source.dialect != "sjasmplus")
    {
        const ConvertResult r = Convert(source, "sjasmplus");
        EXPECT_TRUE(r.ok) << label;
        sjasmplus = SourceDocument::FromText(r.document.Text(), "sjasmplus");
    }
    BackendOptions options;
    options.targetVersion = "1.1";
    const ConvertResult written = Convert(sjasmplus, "masm", options);
    EXPECT_TRUE(written.ok) << label;
    for (const Diagnostic& d : written.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " line " << d.line << ": " << d.message;
    const codecs::MasmCodec codec;
    EncodeOptions encode;
    encode.subversion = "1.1";
    const EncodeResult bytes = codec.Encode(written.document, encode);
    EXPECT_TRUE(bytes.ok) << label;
    for (const Diagnostic& d : bytes.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " (codec) line " << d.line << ": " << d.message;
    DecodeOptions decode;
    decode.subversion = "1.1";
    const DecodeResult back = codec.Decode(bytes.bytes, decode);
    EXPECT_EQ(back.document.lines.size(), written.document.lines.size()) << label;
    for (size_t k = 0; k < std::min(back.document.lines.size(), written.document.lines.size()); ++k)
        EXPECT_EQ(Upper(Squeezed(back.document.lines[k].text)), Upper(Squeezed(written.document.lines[k].text))) << label << " line " << k + 1;
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

/// The MASM text assembled the way MASM computes it: back to sjasmplus through the MASM frontend
std::vector<uint8_t> AssembleMasm(const char* sjasmplus, const SourceDocument& written, const std::string& start, const std::string& length,
                                 std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {}, bool filled = false)
{
    const ConvertResult r = Convert(written, "sjasmplus");
    EXPECT_TRUE(r.ok);
    files.push_back({"PROG.asm", Bytes(r.document)});
    return Assemble(sjasmplus, files, "PROG.asm", start, length, filled);
}
}  // namespace

/// MASM 3.0 refuses @ (XOR, error 0 in unreal-ng): the value instead; 1.1 keeps it
TEST(MasmBackend_Test, Masm30HasNoXor)
{
    const char* source = "        ORG #6000\nSIZE    EQU 10\n        LD A,SIZE ^ 3\n";
    EXPECT_TRUE(Contains(ToMasm(source), "LD A,SIZE+0@3")) << Joined(ToMasm(source));
    EXPECT_TRUE(Contains(ToMasm(source, nullptr, "3.0"), "LD A,9")) << Joined(ToMasm(source, nullptr, "3.0"));
}

TEST(MasmBackend_Test, ExpressionsAreChainsWithoutParentheses)
{
    const std::vector<std::string> lines = ToMasm(
        "        ORG #6000\n"
        "lab     LD HL,lab+2*3\n"
        "        LD A,high lab\n"
        "        LD A,low lab\n"
        "        LD BC,-lab\n"
        "        LD DE,10-(4+2)\n"
        "        LD HL,(lab+1)*2\n"
        "        LD A,5 ^ 3\n"
        "        LD A,lab ^ 3\n"
        "        LD A,3 ^ lab ^ 5\n"
        "longname_a NOP\n"
        "longname_b NOP\n");
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "LAB LD HL,2*3+LAB")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,LAB/256")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,LAB&255")) << text;
    EXPECT_TRUE(Contains(lines, "LD BC,0-LAB")) << text;
    EXPECT_TRUE(Contains(lines, "LD DE,10-4-2")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,LAB+1*2")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,5@3")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,LAB+0@3")) << text;   // MASM reads LAB@3 as the name LAB@
    EXPECT_TRUE(Contains(lines, "LD A,3@LAB+0@5")) << text;   // the name after an @ too
    EXPECT_TRUE(Contains(lines, "LONGNAME_A NOP")) << text;   // 10 characters
    EXPECT_TRUE(Contains(lines, "LONGNAME_B NOP")) << text;
}

TEST(MasmBackend_Test, BlocksStayWhereMasmHasThem)
{
    const std::vector<std::string> lines = ToMasm(
        "        ORG #6000\n"
        "        DUP 2\n"
        "        NOP\n"
        "        EDUP\n"
        "        DISP #9000\n"
        "w       NOP\n"
        "        ENT\n"
        "        DS 4,#AB\n"
        "        ex af,af'\n"
        "        ld a,ixh\n");
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "BEGIN 2")) << text;
    EXPECT_TRUE(Contains(lines, "END")) << text;
    EXPECT_TRUE(Contains(lines, "PHASE #9000")) << text;
    EXPECT_TRUE(Contains(lines, "UNPHASE")) << text;
    EXPECT_TRUE(Contains(lines, "DEFS 4,#AB")) << text;
    EXPECT_TRUE(Contains(lines, "EXA")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,XH")) << text;
}

TEST(MasmBackend_Test, ProgramsKeepTheirBytesThroughMasm)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
        const std::vector<uint8_t> original = Assemble(sjasmplus, {{"PROG.asm", Bytes(source)}}, "PROG.asm", "#8000", "$-#8000");
        EXPECT_FALSE(original.empty());
        EXPECT_EQ(AssembleMasm(sjasmplus, MasmRoundTrip(source, "instructions"), "#8000", "$-#8000"), original);
    }
    // MASM 1.1's own test program (from #6000)
    {
        const codecs::MasmCodec masm;
        const std::vector<uint8_t> expected = ReadTestData("dialects/masm11/MT1.bin");
        EXPECT_EQ(AssembleMasm(sjasmplus, MasmRoundTrip(Decoded(masm, Hobeta("dialects/masm11/MT1.$a"), "1.1"), "MT1"), "#6000", std::to_string(expected.size())),
                  expected);
    }
    const codecs::ZxasmCodec zxasm;
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleMasm(sjasmplus, MasmRoundTrip(Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15"), name), "#8000",
                               std::to_string(expected.size())),
                  expected)
            << name;
    }
    const codecs::StormCodec storm;
    for (const std::string name : {"STORMT1", "STORMT2"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/" + name + ".bin");
        EXPECT_EQ(AssembleMasm(sjasmplus, MasmRoundTrip(Decoded(storm, Hobeta("dialects/storm13/" + name + ".$C")), name), "#8000",
                               std::to_string(expected.size()), {{"INC", Hobeta("dialects/storm13/INC.$C").data}, {"INC.slack", {}}}),
                  expected)
            << name;
    }
    const codecs::XasCodec xas;
    EXPECT_EQ(AssembleMasm(sjasmplus, MasmRoundTrip(Decoded(xas, Hobeta("dialects/xas7447/constrct.$X")), "constrct"), "#6000", "#110", {}, true),
              ReadTestData("dialects/xas7447/constrct.bin"));
    EXPECT_EQ(AssembleMasm(sjasmplus, MasmRoundTrip(Decoded(xas, Hobeta("dialects/xas418/cons418.$X")), "cons418"), "#6000", "#90", {}, true),
              ReadTestData("dialects/xas418/cons418.bin"));
    {
        const codecs::TasmCodec tasm;
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssembleMasm(sjasmplus, MasmRoundTrip(Decoded(tasm, Hobeta("tasm/T50PROG.$A")), "T50PROG"), "#7000", std::to_string(expected.size())),
                  expected);
    }
}
