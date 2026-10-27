// The ZX-ASM backend (part B, Z3): the IR written as ZAsm 3.15 source. Construct by construct (left to right with
// parentheses, the functions .h .b .n, IF / ELSE / ENDIF, REPT, PHASE), and a round trip on programs whose bytes are
// known: each is written as sjasmplus, then as ZX-ASM, the text goes through the ZX-ASM codec (it must tokenize and
// read back as written) and back to sjasmplus through the ZX-ASM frontend, which models ZX-ASM's arithmetic
// (research-zxasm-to-sjasmplus.md). With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the result is assembled and compared
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

/// The ZX-ASM lines of a sjasmplus source (blank and comment lines dropped, blanks collapsed)
std::vector<std::string> ToZxasm(const std::string& sjasmplus, Diagnostics* diagnostics = nullptr, const std::string& version = "3.15")
{
    BackendOptions options;
    options.targetVersion = version;
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), "zxasm", options);
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

/// A source of any dialect as sjasmplus, then as ZAsm 3.15 through the codec and back: the ZX-ASM text read back
/// from the bytes ZAsm would hold (checked to be the text written, up to the editor's layout)
SourceDocument ZxasmRoundTrip(const SourceDocument& source, const std::string& label)
{
    SourceDocument sjasmplus = source;
    if (source.dialect != "sjasmplus")
    {
        const ConvertResult r = Convert(source, "sjasmplus");
        EXPECT_TRUE(r.ok) << label;
        sjasmplus = SourceDocument::FromText(r.document.Text(), "sjasmplus");
    }
    BackendOptions options;
    options.targetVersion = "3.15";
    const ConvertResult written = Convert(sjasmplus, "zxasm", options);
    EXPECT_TRUE(written.ok) << label;
    for (const Diagnostic& d : written.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " line " << d.line << ": " << d.message;
    const codecs::ZxasmCodec codec;
    EncodeOptions encode;
    encode.subversion = "3.15";
    const EncodeResult bytes = codec.Encode(written.document, encode);
    EXPECT_TRUE(bytes.ok) << label;
    for (const Diagnostic& d : bytes.diagnostics)
        if (d.severity != Severity::Info)
            ADD_FAILURE() << label << " (codec) line " << d.line << ": " << d.message;
    DecodeOptions decode;
    decode.subversion = "3.15";
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

/// The ZX-ASM text assembled the way ZX-ASM computes it: back to sjasmplus through the ZX-ASM frontend
std::vector<uint8_t> AssembleZxasm(const char* sjasmplus, const SourceDocument& written, const std::string& start, const std::string& length,
                                 std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {}, bool filled = false)
{
    const ConvertResult r = Convert(written, "sjasmplus");
    EXPECT_TRUE(r.ok);
    files.push_back({"PROG.asm", Bytes(r.document)});
    return Assemble(sjasmplus, files, "PROG.asm", start, length, filled);
}
}  // namespace

TEST(ZxasmBackend_Test, ExpressionsGoLeftToRightWithFunctions)
{
    const std::vector<std::string> lines = ToZxasm(
        "        ORG #6000\n"
        "lab     LD HL,lab+2*3\n"
        "        LD A,high lab\n"
        "        LD A,low (lab+1)\n"
        "        LD BC,-lab\n"
        "        LD DE,10-(4+2)\n"
        "        LD HL,(lab+1)*2\n"
        "        LD A,~5\n"
        "        LD A,5 ^ 3\n"
        "        LD A,low ~lab\n"
        "        LD HL,lab | #8000\n"
        "        LD HL,7*#4000\n");
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "LAB LD HL,LAB+(2*3)")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,LAB.h")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,0+(LAB+1).b")) << text;
    EXPECT_TRUE(Contains(lines, "LD BC,LAB.n")) << text;
    EXPECT_TRUE(Contains(lines, "LD DE,10-(4+2)")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,LAB+1*2")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,5.c")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,5!3")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,0+(LAB.c).b")) << text;   // ZAsm 3.15 refuses LAB.c.b
    EXPECT_TRUE(Contains(lines, "LD HL,#E000")) << text;
    EXPECT_TRUE(Contains(lines, "LD HL,#C000")) << text;   // its 7*#4000 stops at the first carry: #8000   // its | keeps the left high byte: LAB|#8000 would be #6000
}

/// What ZX ASM 3.0 itself and ZX-ASM 2.x take, seen in unreal-ng (each construct assembled alone): no & | ! and no .
/// functions, no REPT, EXA, OUT (C),0 or DS fill; 2.x no parentheses either. 3.10 and later have them all
TEST(ZxasmBackend_Test, TheOldestVersionsGetWhatTheyTake)
{
    const char* source =
        "        ORG #6000\n"
        "SIZE    EQU 10\n"
        "lab     LD A,high lab\n"
        "        LD A,low lab\n"
        "        LD A,SIZE & 7\n"
        "        LD A,1+(2*SIZE)\n"
        "        EX AF,AF'\n"
        "        OUT (C),0\n"
        "        DS 2,#AA\n"
        "        DUP 2\n"
        "        NOP\n"
        "        EDUP\n";
    const std::vector<std::string> v310 = ToZxasm(source, nullptr, "3.0");
    std::string text = Joined(v310);
    EXPECT_TRUE(Contains(v310, "LAB LD A,LAB.h")) << text;
    EXPECT_TRUE(Contains(v310, "LD A,SIZE&7")) << text;
    EXPECT_TRUE(Contains(v310, "EXA")) << text;
    EXPECT_TRUE(Contains(v310, "REPT 2")) << text;
    for (const char* version : {"3.00", "2"})
    {
        const std::vector<std::string> lines = ToZxasm(source, nullptr, version);
        text = Joined(lines);
        EXPECT_TRUE(Contains(lines, "LAB LD A,LAB/256")) << version << text;
        EXPECT_TRUE(Contains(lines, "LD A,0")) << version << text;   // low #6000 as its value
        EXPECT_TRUE(Contains(lines, "LD A,2")) << version << text;   // SIZE&7
        EXPECT_TRUE(Contains(lines, "EX AF,AF'")) << version << text;
        EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("DB #ED,#71", 0) == 0; })) << version << text;
        EXPECT_TRUE(Contains(lines, "DB 170,170")) << version << text;
        EXPECT_FALSE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("REPT") != std::string::npos; })) << version << text;
        EXPECT_EQ(Contains(lines, "LD A,1+(2*SIZE)"), std::string(version) == "3.00") << version << text;
    }
    EXPECT_TRUE(Contains(ToZxasm(source, nullptr, "2"), "LD A,1+20")) << Joined(ToZxasm(source, nullptr, "2"));
    // 3.00 is 3.0's table to the codec
    const WriteAsResult a = WriteAs(SourceDocument::FromText("        NOP\n", "sjasmplus"), "zxasm", "3.0", "T");
    const WriteAsResult b = WriteAs(SourceDocument::FromText("        NOP\n", "sjasmplus"), "zxasm", "3.00", "T");
    ASSERT_TRUE(a.ok && b.ok) << b.error;
    EXPECT_EQ(a.bytes, b.bytes);
}

TEST(ZxasmBackend_Test, BlocksStayWhereZxAsmHasThem)
{
    const std::vector<std::string> lines = ToZxasm(
        "        ORG #6000\n"
        "        IF ext != 5\n"
        "        DB 3\n"
        "        ENDIF\n"
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
    EXPECT_TRUE(Contains(lines, "IF EXT-5")) << text;
    EXPECT_TRUE(Contains(lines, "ENDIF")) << text;
    EXPECT_TRUE(Contains(lines, "REPT 2")) << text;
    EXPECT_TRUE(Contains(lines, "ENDR")) << text;
    EXPECT_TRUE(Contains(lines, "PHASE #9000")) << text;
    EXPECT_TRUE(Contains(lines, "UNPHASE")) << text;
    EXPECT_TRUE(Contains(lines, "DS 4,#AB")) << text;
    EXPECT_TRUE(Contains(lines, "EXA")) << text;
    EXPECT_TRUE(Contains(lines, "LD A,XH")) << text;
}

TEST(ZxasmBackend_Test, ProgramsKeepTheirBytesThroughZxAsm)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    {
        const SourceDocument source = SourceDocument::FromText(ReadTestText("symbols/fromsource/instructions.asm"), "sjasmplus");
        const std::vector<uint8_t> original = Assemble(sjasmplus, {{"PROG.asm", Bytes(source)}}, "PROG.asm", "#8000", "$-#8000");
        EXPECT_FALSE(original.empty());
        EXPECT_EQ(AssembleZxasm(sjasmplus, ZxasmRoundTrip(source, "instructions"), "#8000", "$-#8000"), original);
    }
    // ZAsm 3.15's own test programs (from #8000)
    const codecs::ZxasmCodec zxasm;
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleZxasm(sjasmplus, ZxasmRoundTrip(Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15"), name), "#8000",
                                std::to_string(expected.size())),
                  expected)
            << name;
    }
    const codecs::StormCodec storm;
    for (const std::string name : {"STORMT1", "STORMT2"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/" + name + ".bin");
        EXPECT_EQ(AssembleZxasm(sjasmplus, ZxasmRoundTrip(Decoded(storm, Hobeta("dialects/storm13/" + name + ".$C")), name), "#8000",
                                std::to_string(expected.size()), {{"INC", Hobeta("dialects/storm13/INC.$C").data}}),
                  expected)
            << name;
    }
    const codecs::XasCodec xas;
    EXPECT_EQ(AssembleZxasm(sjasmplus, ZxasmRoundTrip(Decoded(xas, Hobeta("dialects/xas7447/constrct.$X")), "constrct"), "#6000", "#110", {}, true),
              ReadTestData("dialects/xas7447/constrct.bin"));
    EXPECT_EQ(AssembleZxasm(sjasmplus, ZxasmRoundTrip(Decoded(xas, Hobeta("dialects/xas418/cons418.$X")), "cons418"), "#6000", "#90", {}, true),
              ReadTestData("dialects/xas418/cons418.bin"));
    {
        const codecs::TasmCodec tasm;
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssembleZxasm(sjasmplus, ZxasmRoundTrip(Decoded(tasm, Hobeta("tasm/T50PROG.$A")), "T50PROG"), "#7000", std::to_string(expected.size())),
                  expected);
    }
}
