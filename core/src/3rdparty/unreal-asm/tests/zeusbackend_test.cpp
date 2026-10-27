// The ZEUS backend (part B, Z4): the IR written as ZEUS 1983 source. Construct by construct (left-to-right chains of
// + - & !, values where ZEUS has no operator, DISP as an offset, the undocumented instructions as bytes), and a round
// trip on programs whose bytes are known: each is written as sjasmplus, then as ZEUS, the text goes through the ZEUS
// codec and back to sjasmplus through the ZEUS frontend, which models ZEUS' arithmetic (research-zeus-to-sjasmplus.md). With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the result is assembled and compared
// with the original assemblers' bytes.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/gens/genscodec.h"
#include "codecs/zeus/zeuscodec.h"
#include "codecs/masm/masmcodec.h"
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

/// The ZEUS lines of a sjasmplus source (blank and comment lines dropped, blanks collapsed)
std::vector<std::string> ToZeus(const std::string& sjasmplus, Diagnostics* diagnostics = nullptr, const std::string& version = "1983")
{
    BackendOptions options;
    options.targetVersion = version;
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "zeus", options);
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

/// A source of any dialect as sjasmplus, then as ZEUS 1983 through the codec and back: the ZEUS text read back from
/// the bytes ZEUS would hold (checked to be the text written, up to the editor's layout)
SourceDocument ZeusRoundTrip(const SourceDocument& source, const std::string& label, const std::string& version = "1983")
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
    const ConvertResult written = Convert(sjasmplus, "zeus", options);
    EXPECT_TRUE(written.ok) << label;
    for (const Diagnostic& d : written.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " line " << d.line << ": " << d.message;
    const codecs::ZeusCodec codec;
    EncodeOptions encode;
    encode.subversion = version;
    const EncodeResult bytes = codec.Encode(written.document, encode);
    EXPECT_TRUE(bytes.ok) << label;
    for (const Diagnostic& d : bytes.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " (codec) line " << d.line << ": " << d.message;
    DecodeOptions decode;
    decode.subversion = version;
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

/// The ZEUS text assembled the way ZEUS computes it: back to sjasmplus through the ZEUS frontend
std::vector<uint8_t> AssembleZeus(const char* sjasmplus, const SourceDocument& written, const std::string& start, const std::string& length,
                                 std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {}, bool filled = false)
{
    const ConvertResult r = Convert(written, "sjasmplus");
    EXPECT_TRUE(r.ok);
    files.push_back({"PROG.asm", Bytes(r.document)});
    return Assemble(sjasmplus, files, "PROG.asm", start, length, filled);
}
}  // namespace

TEST(ZeusBackend_Test, ExpressionsAreChainsOfZeusOperators)
{
    const std::vector<std::string> lines = ToZeus(
        "        ORG #6000\n"
        "lab_x   LD HL,lab_x+2+3\n"
        "        LD A,low lab_x\n"
        "        LD BC,-lab_x\n"
        "        LD A,5 | 3\n"
        "        LD A,(ix-3)\n"
        "        LD A,\"A\"\n"
        "        LD A,ixh\n"
        "        DISP #9000\n"
        "w       NOP\n"
        "        ENT\n"
        "        DB \"text\",0\n");
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "LABX LD HL,LABX+2+3")) << text;   // letters and digits only
    EXPECT_TRUE(Contains(lines, "LD A,LABX&255")) << text;
    EXPECT_TRUE(Contains(lines, "LD BC,0-LABX")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,5!3")) << text;          // ! is OR
    EXPECT_TRUE(Contains(lines, "LD A,(IX-3)")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,\"A")) << text;          // "c is a character
    EXPECT_TRUE(Contains(lines, "DEFB #DD,#7C")) << text;   // no IXH in ZEUS
    EXPECT_TRUE(Contains(lines, "ORG #9000")) << text;
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("DISP #D", 0) == 0; })) << text;   // the place minus the run address, in 16 bits
    EXPECT_TRUE(Contains(lines, "DISP 0")) << text;
    EXPECT_TRUE(Contains(lines, "DEFM /text/")) << text;
}

TEST(ZeusBackend_Test, ProgramsKeepTheirBytesThroughZeus)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
        const std::vector<uint8_t> original = Assemble(sjasmplus, {{"PROG.asm", Bytes(source)}}, "PROG.asm", "#8000", "$-#8000");
        EXPECT_FALSE(original.empty());
        EXPECT_EQ(AssembleZeus(sjasmplus, ZeusRoundTrip(source, "instructions"), "#8000", "$-#8000"), original);
    }
    const codecs::ZxasmCodec zxasm;
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleZeus(sjasmplus, ZeusRoundTrip(Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15"), name), "#8000",
                               std::to_string(expected.size())),
                  expected)
            << name;
    }
    const codecs::StormCodec storm;
    {
        // (STORMT1 is not here: it INCBINs a file, which ZEUS 1983 cannot)
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/STORMT2.bin");
        EXPECT_EQ(AssembleZeus(sjasmplus, ZeusRoundTrip(Decoded(storm, Hobeta("dialects/storm13/STORMT2.$C")), "STORMT2"), "#8000", std::to_string(expected.size())),
                  expected);
    }
    const codecs::MasmCodec masm;
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/masm11/MT1.bin");
        EXPECT_EQ(AssembleZeus(sjasmplus, ZeusRoundTrip(Decoded(masm, Hobeta("dialects/masm11/MT1.$a"), "1.1"), "MT1"), "#6000", std::to_string(expected.size())),
                  expected);
    }
    const codecs::XasCodec xas;
    EXPECT_EQ(AssembleZeus(sjasmplus, ZeusRoundTrip(Decoded(xas, Hobeta("dialects/xas7447/constrct.$X")), "constrct"), "#6000", "#110", {}, true),
              ReadTestData("dialects/xas7447/constrct.bin"));
    {
        const codecs::TasmCodec tasm;
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssembleZeus(sjasmplus, ZeusRoundTrip(Decoded(tasm, Hobeta("tasm/T50PROG.$A")), "T50PROG"), "#7000", std::to_string(expected.size())),
                  expected);
    }
}

/// GG and PHT's versions name the data words DB DW DM DS (their tokens #96-#99; DEFB is error 0 there); v7.E alone
/// multiplies: ZEUS 1.1 with the same table refuses * (error 0 in unreal-ng)
TEST(ZeusBackend_Test, TheLaterVersionsHaveTheirDataWordsAndOnlyV7eMultiplies)
{
    const char* source =
        "        ORG #C000\n"
        "SIZE    EQU 10\n"
        "        LD B,SIZE*2\n"
        "        DB 1\n"
        "        DW 2\n"
        "        DB \"text\"\n";
    const std::vector<std::string> v1983 = ToZeus(source);
    EXPECT_TRUE(Contains(v1983, "DEFB 1")) << Joined(v1983);
    EXPECT_TRUE(Contains(v1983, "DEFM /text/")) << Joined(v1983);
    // v7.E stops its product at the first carry: 35*#2000 would be #1800
    EXPECT_TRUE(Contains(ToZeus("        LD HL,35*#2000\n", nullptr, "pht"), "LD HL,#6000")) << Joined(ToZeus("        LD HL,35*#2000\n", nullptr, "pht"));
    for (const char* version : {"gg", "pht", "1.1"})
    {
        const std::vector<std::string> lines = ToZeus(source, nullptr, version);
        const std::string text = Joined(lines);
        EXPECT_TRUE(Contains(lines, "DB 1")) << version << text;
        EXPECT_TRUE(Contains(lines, "DW 2")) << version << text;
        EXPECT_TRUE(Contains(lines, "DM /text/")) << version << text;
        EXPECT_EQ(Contains(lines, "LD B,SIZE*2"), std::string(version) == "pht") << version << text;
        if (std::string(version) != "pht")
        {
            EXPECT_TRUE(Contains(lines, "LD B,20")) << version << text;
        }
    }
    // 1.1 is a version of the codec: PHT's table
    const WriteAsResult written = WriteAs(SourceDocument::FromText(source, "sjasmplus"), "zeus", "1.1", "T");
    ASSERT_TRUE(written.ok) << written.error;
    const WriteAsResult pht = WriteAs(SourceDocument::FromText("        ORG #C000\n        DB 1\n", "sjasmplus"), "zeus", "pht", "T");
    const WriteAsResult v11 = WriteAs(SourceDocument::FromText("        ORG #C000\n        DB 1\n", "sjasmplus"), "zeus", "1.1", "T");
    EXPECT_EQ(pht.bytes, v11.bytes);
}

TEST(ZeusBackend_Test, Zeus31TakesLabelsOfSixCharacters)
{
    // ZEUS+ 3.1: a seventh character is its error 1 (label too long, seen in unreal-ng); 1983 takes 14
    const char* source =
        "        ORG #C000\n"
        "mainloop:  djnz mainloop\n"
        "mainskip:  jr mainskip\n";
    const std::vector<std::string> lines = ToZeus(source, nullptr, "3.1");
    ASSERT_EQ(lines.size(), 3u) << Joined(lines);
    std::vector<std::string> labels;
    for (size_t k = 1; k < lines.size(); ++k)
    {
        const std::string label = lines[k].substr(0, lines[k].find(' '));
        EXPECT_LE(label.size(), 6u) << lines[k];
        EXPECT_EQ(lines[k].substr(lines[k].rfind(' ') + 1), label) << lines[k];   // the jump names its own line
        labels.push_back(label);
    }
    EXPECT_NE(labels[0], labels[1]) << Joined(lines);
    EXPECT_TRUE(Contains(ToZeus(source), "MAINLOOP DJNZ MAINLOOP")) << Joined(ToZeus(source));
}

TEST(ZeusBackend_Test, PrimusTakesItsOwnOperatorsAndOneOrg)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToZeus(
        "        ORG #6000\n"
        "lab     LD A,high lab\n"
        "        LD HL,lab*2/3\n"
        "        LD HL,lab*#7FFF\n"
        "        LD A,lab%7\n"
        "        LD A,\"A\"\n"
        "        INCLUDE \"lib.asm\"\n"
        "        ORG #7000\n"
        "        NOP\n"
        "        ORG #6800\n",
        &diagnostics, "primus");
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "LAB LD A,LAB/256")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,LAB*2/3")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,#A000")) << text;   // Primus stops at its first carry: #6000*#7FFF gives #2000
    EXPECT_TRUE(Contains(lines, "LD A,LAB+0?7")) << text;   // Primus reads LAB?7 as the name LAB?7
    EXPECT_TRUE(Contains(lines, "LD A,65")) << text;   // Primus refuses "A
    EXPECT_TRUE(Contains(lines, "DISK lib")) << text;
    // a second ORG ahead of $ is a hole up to it; one back is refused (Primus takes one ORG)
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("DEFS ", 0) == 0; })) << text;
    EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.message.find("a second ORG") != std::string::npos; }));
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        return;
    const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
    const std::vector<uint8_t> original = Assemble(sjasmplus, {{"PROG.asm", Bytes(source)}}, "PROG.asm", "#8000", "$-#8000");
    EXPECT_EQ(AssembleZeus(sjasmplus, ZeusRoundTrip(source, "instructions primus", "primus"), "#8000", "$-#8000"), original);
}
