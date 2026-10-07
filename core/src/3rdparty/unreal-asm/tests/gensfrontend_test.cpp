// The GENS frontend: GENS (HiSoft Devpac) sources into the IR, written by the sjasmplus backend. Construct by construct
// (every rule checked on GENS4 running in unreal-ng, research-gens-to-sjasmplus.md), the constructs file
// (testdata/dialects/gens/constructs.gens) against its golden conversion, its labels laid out as sjasmplus lays them
// out, and with UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the constructs file and the five real sources of
// testdata/gens assembled and compared with the bytes GENS4 built.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include "codecs/gens/genscodec.h"
#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"
#include "unrealasm/layout.h"
#include "unrealasm/symbols/fromsource.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;
using unrealasm::testing::TestDataPath;

namespace
{
/// The sjasmplus statements of a GENS text (blank and comment-only lines dropped, leading blanks trimmed)
std::vector<std::string> ToSjasmplus(const std::string& gens, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(gens, "gens"), "sjasmplus");
    EXPECT_TRUE(r.ok);
    if (diagnostics)
        *diagnostics = r.diagnostics;
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        const size_t first = l.text.find_first_not_of(' ');
        if (first == std::string::npos || l.text[first] == ';')
            continue;
        const size_t comment = l.text.find(" ;", first);
        out.push_back(l.text.substr(first, comment == std::string::npos ? std::string::npos : comment - first));
        while (!out.back().empty() && out.back().back() == ' ')
            out.back().pop_back();
    }
    return out;
}

SourceDocument Constructs()
{
    const codecs::GensCodec codec;
    DecodeResult decoded = codec.Decode(ReadTestData("dialects/gens/constructs.gens"), {});
    EXPECT_TRUE(decoded.ok);
    decoded.document.name = "constructs";
    return decoded.document;
}

containers::TrdosFile Hobeta(const std::string& relative)
{
    containers::TrdosFile file;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData(relative), file, error)) << relative << ": " << error;
    return file;
}

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

/// The bytes sjasmplus builds from a converted source over memory filled with #AA (as the emulator's memory was
/// before GENS4 assembled), from `start` for `length` bytes
std::vector<uint8_t> Assembled(const char* sjasmplus, const std::vector<uint8_t>& source, int start, size_t length)
{
    const std::filesystem::path dir = ScratchDirectory();
    WriteBytes(dir / "src.asm", source);
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        ORG 0\n        DS 32768,#AA\n        DS 32768,#AA\n        ORG 0\n"
                                "        INCLUDE \"src.asm\"\n        SAVEBIN \"out.bin\"," + std::to_string(start) + "," + std::to_string(length) + "\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    EXPECT_EQ(std::system(command.c_str()), 0);
    std::vector<uint8_t> out = ReadBytes(dir / "out.bin");
    std::filesystem::remove_all(dir);
    return out;
}
}  // namespace

TEST(GensFrontend_Test, ExpressionsRunLeftToRightOnSignedWords)
{
    // No priorities (4+5*3-8 is 19), @ or, ! xor, ? mod; signed 16-bit division (#8000/2 is #C000 on GENS4)
    EXPECT_EQ(ToSjasmplus("        LD   A,4+5*3-8\n        DEFW 17@8,5!3,7?2"),
              (std::vector<std::string>{"LD A,(4+5)*3-8", "DW 17|8,5^3,7%2"}));
    EXPECT_EQ(ToSjasmplus("        DEFW #8000/2"), std::vector<std::string>{"DW (((#8000&#FFFF)^#8000)-#8000)/2"});
    EXPECT_EQ(ToSjasmplus("        DEFW 7/2,70016"), (std::vector<std::string>{"DW 7/2,4480"}));   // numbers modulo 65536
}

TEST(GensFrontend_Test, DollarIsTheAddressOfEachItem)
{
    // GENS4: DEFW A,$ puts the address of the second word there
    EXPECT_EQ(ToSjasmplus("        DEFW $,$,$\n        DEFB $,1,$"), (std::vector<std::string>{"DW $,$+2,$+4", "DB $,1,$+2"}));
}

TEST(GensFrontend_Test, LabelsKeepSixCharactersAndTheirCase)
{
    // Only the first 6 characters count: every spelling becomes the defining one; ':' after a label is skipped;
    // lower-case words are labels (a is no register), [ ] ^ are label characters (renamed for sjasmplus)
    const std::vector<std::string> out = ToSjasmplus("LONGNAME1: NOP\n        DEFW LONGNAME2,LONGNA\na       LD   A,a\nL[1]    DEFW L[1]");
    EXPECT_EQ(out, (std::vector<std::string>{"LONGNAME1 NOP", "DW LONGNAME1,LONGNAME1", "L_a     LD A,L_a", "L_L_1_  DW L_L_1_"}));
}

TEST(GensFrontend_Test, ConditionsEndWithEnd)
{
    EXPECT_EQ(ToSjasmplus("X       EQU  1\n        IF   X\n        NOP\n        ELSE\n        HALT\n        END\n        END"),
              (std::vector<std::string>{"X       EQU 1", "IF X", "NOP", "ELSE", "HALT", "ENDIF"}));   // END without IF: nothing
}

TEST(GensFrontend_Test, MacroParametersArePassedByValue)
{
    // =0..=31 become named parameters; a call passes +(expression): 2*=0 with 1+1 is 4 on GENS4, and LD HL,=0 stays
    // a value
    const std::vector<std::string> out = ToSjasmplus("M       MAC\n        LD   HL,=0\n        DEFB 2*=2\n        ENDM\n        M    1+1,2,3");
    EXPECT_EQ(out, (std::vector<std::string>{"MACRO M _g0,_g1,_g2", "LD HL,_g0", "DB 2*_g2", "ENDM", "M +(1+1),2,3"}));
}

TEST(GensFrontend_Test, DirectivesAndCommands)
{
    Diagnostics d;
    const std::vector<std::string> out = ToSjasmplus("*D+\n*F 1:PART\n        ENT  $\n        DEFM /a;b/ ;x\n        DEFS 3\n        ld   a,1", &d);
    // ld is converted with a warning; a lower-case a stays a label, as GENS reads it
    EXPECT_EQ(out, (std::vector<std::string>{"INCLUDE \"PART.asm\"", "DB 'a;b'", "DS 3", "LD L_a,1"}));
    bool capitals = false;
    for (const Diagnostic& x : d)
        capitals = capitals || x.message.find("capitals only") != std::string::npos;
    EXPECT_TRUE(capitals);   // GENS4 refuses lower-case mnemonics (*ERROR* 02)
}

TEST(GensFrontend_Test, ConstructsMatchTheGoldenConversion)
{
    const ConvertResult r = Convert(Constructs(), "sjasmplus");
    ASSERT_TRUE(r.ok);
    const codecs::SjasmplusCodec codec;
    const std::vector<uint8_t> bytes = codec.Encode(r.document, {}).bytes;
    const std::string golden = "dialects/gens/constructs.sjasmplus.asm";
    if (std::getenv("UNREAL_ASM_UPDATE_GOLDEN"))
        WriteBytes(TestDataPath(golden), bytes);
    EXPECT_EQ(bytes, ReadTestData(golden));
}

TEST(GensFrontend_Test, LabelsComeBackUnderTheirGensNamesAndNumbers)
{
    const symbols::SourceSymbolsResult r = symbols::SymbolsFromSource(Constructs());
    EXPECT_TRUE(r.ok);
    std::map<std::string, symbols::Symbol> byName;
    for (const symbols::Symbol& s : r.set.symbols)
        byName[s.name] = s;
    // Values as sjasmplus 1.24 gave them for the conversion (testdata/symbols/fromsource/gens-constructs.sym)
    std::map<std::string, uint32_t> sym;
    std::istringstream in(ReadTestText("symbols/fromsource/gens-constructs.sym"));
    std::string line;
    while (std::getline(in, line))
        if (line.find(": EQU 0x") != std::string::npos)
            sym[line.substr(0, line.find(':'))] = static_cast<uint32_t>(std::stoul(line.substr(line.find("0x") + 2), nullptr, 16));
    EXPECT_EQ(byName["START"].location.offset, sym["START"]);
    EXPECT_EQ(byName["a"].location.offset, sym["L_a"]);
    EXPECT_EQ(byName["two^5"].location.offset, sym["L_two_5"]);
    EXPECT_EQ(byName["LONGNAME1"].location.offset, sym["LONGNAME1"]);
    EXPECT_EQ(byName["X"].kind, symbols::SymbolKind::Const);
    EXPECT_EQ(byName["START"].kind, symbols::SymbolKind::Code);
    EXPECT_EQ(byName["START"].source.line, 50u);   // GENS' own line number
    EXPECT_EQ(r.set.symbols.size(), sym.size());
}

// --- Assembled by sjasmplus (opt-in: UNREAL_ASM_SJASMPLUS) ----------------------------------------------------------

TEST(GensFrontend_Test, ConstructsAssembleToWhatGens4Built)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    const std::vector<uint8_t> expected = ReadTestData("dialects/gens/constructs.bin");
    EXPECT_EQ(Assembled(sjasmplus, ReadTestData("dialects/gens/constructs.sjasmplus.asm"), 0xC000, expected.size()), expected);
}

TEST(GensFrontend_Test, RealSourcesAssembleToWhatGens4Built)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    struct Case
    {
        const char* file;     // testdata/gens/<file>.$<type>
        const char* type;
        int start;            // where the bytes GENS4 built begin (dialects/gens/<file>.bin)
        int org = -1;         // ORG $ in the source: the address GENS4 started at
    };
    const Case cases[] = {{"ISC11VRG__ISCOP.C", "C", 0x80E8},  {"PF212__BOOT.A", "C", 0x5E88},     {"ZX_NET__ZX_NET1", "A", 0xAFC8},
                          {"HISOFT-C__64-A95", "C", 0xF8A2},  {"WINDOW__WINDOW", "C", 0xCFBB, 0xCFBB}};
    const codecs::GensCodec gens;
    const codecs::SjasmplusCodec codec;
    for (const Case& c : cases)
    {
        const containers::TrdosFile file = Hobeta(std::string("gens/") + c.file + ".$" + c.type);
        const ConvertResult r = Convert(gens.Decode(file.data, {}).document, "sjasmplus");
        ASSERT_TRUE(r.ok) << c.file;
        std::vector<uint8_t> source = codec.Encode(r.document, {}).bytes;
        if (c.org >= 0)
        {
            std::string text(source.begin(), source.end());
            const size_t at = text.find("ORG $\n");
            ASSERT_NE(at, std::string::npos);
            text.replace(at, 6, "ORG " + std::to_string(c.org) + "\n");
            source.assign(text.begin(), text.end());
        }
        const std::vector<uint8_t> expected = ReadTestData(std::string("dialects/gens/") + c.file + ".bin");
        EXPECT_EQ(Assembled(sjasmplus, source, c.start, expected.size()), expected) << c.file;
    }
}
