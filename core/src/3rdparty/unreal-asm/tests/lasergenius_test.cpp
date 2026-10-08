// Laser Genius: the codec (tokenized paragraphs, byte-exact and canonical) on the example sources of its tape and a
// probe of every statement form typed into Laser Genius 1.04 (Beta Disk) in unreal-ng: each form as typed encodes to
// the tokens the editor stored; the frontend (Laser Genius -> sjasmplus) construct by construct, and with
// UNREAL_ASM_SJASMPLUS the example sources and an expression probe assembled to the bytes Laser Genius built from
// them. research-laser-genius.md.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "codecs/lasergenius/lasergeniuscodec.h"
#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "testdata.h"
#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
const char* const kFiles[] = {"lasergenius/SIEVE.ASM.lg", "lasergenius/ELLIPSE.ASM.lg", "lasergenius/LIB.MAKER.lg",
                              "lasergenius/MEANED.lg", "lasergenius/SIEVE.PHX.lg", "lasergenius/PROBE.bin"};

std::vector<uint8_t> FromHex(const std::string& hex)
{
    std::vector<uint8_t> out;
    std::istringstream in(hex);
    std::string byte;
    while (in >> byte)
        out.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
    return out;
}

/// The sjasmplus lines of a Laser Genius text (comments and blank lines left out)
std::vector<std::string> ToSjasmplus(const std::string& text)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, "lasergenius"), "sjasmplus");
    EXPECT_TRUE(r.ok);
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

/// The bytes sjasmplus builds at `address` from a converted document (`size` of them)
std::vector<uint8_t> AssembleAt(const char* sjasmplus, const SourceDocument& document, int address, size_t size)
{
    const ConvertResult r = Convert(document, "sjasmplus");
    EXPECT_TRUE(r.ok);
    const std::filesystem::path dir = ScratchDirectory();
    WriteBytes(dir / "source.asm", codecs::SjasmplusCodec().Encode(r.document, {}).bytes);
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"source.asm\"\n        SAVEBIN \"out.bin\"," + std::to_string(address) + "," + std::to_string(size) + "\n";
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

TEST(LaserGeniusCodec_Test, DecodeThenEncodeIsByteExact)
{
    const codecs::LaserGeniusCodec codec;
    for (const char* file : kFiles)
    {
        const std::vector<uint8_t> bytes = ReadTestData(file);
        ASSERT_FALSE(bytes.empty()) << file;
        const DecodeResult d = codec.Decode(bytes, {});
        ASSERT_TRUE(d.ok) << file;
        const EncodeResult e = codec.Encode(d.document, {});
        ASSERT_TRUE(e.ok) << file;
        EXPECT_EQ(e.bytes, bytes) << file;
    }
}

TEST(LaserGeniusCodec_Test, TheTextAloneEncodesToTheSameTokens)
{
    // Without the kept bytes: every statement written again from its text (the paragraph numbers stay)
    const codecs::LaserGeniusCodec codec;
    for (const char* file : kFiles)
    {
        const std::vector<uint8_t> bytes = ReadTestData(file);
        DecodeResult d = codec.Decode(bytes, {});
        ASSERT_TRUE(d.ok) << file;
        for (SourceLine& line : d.document.lines)
            line.attrs = {};
        const EncodeResult e = codec.Encode(d.document, {});
        ASSERT_TRUE(e.ok) << file << ": " << (e.diagnostics.empty() ? "" : e.diagnostics[0].message);
        EXPECT_EQ(e.bytes, bytes) << file;
    }
}

TEST(LaserGeniusCodec_Test, EveryTypedFormEncodesToWhatTheEditorStored)
{
    std::istringstream in(ReadTestText("lasergenius/PROBE.forms.tsv"));
    std::string row;
    int forms = 0;
    while (std::getline(in, row))
    {
        if (row.empty() || row[0] == '#')
            continue;
        const size_t tab = row.find('\t');
        ASSERT_NE(tab, std::string::npos) << row;
        const std::string text = row.substr(0, tab);
        std::vector<uint8_t> tokens;
        std::string error;
        ASSERT_TRUE(codecs::LaserGeniusCodec::EncodeStatement(text, tokens, error)) << text << ": " << error;
        EXPECT_EQ(tokens, FromHex(row.substr(tab + 1))) << text;
        ++forms;
    }
    EXPECT_GT(forms, 800);
}

TEST(LaserGeniusCodec_Test, DecodedText)
{
    const codecs::LaserGeniusCodec codec;
    const DecodeResult d = codec.Decode(ReadTestData("lasergenius/SIEVE.ASM.lg"), {});
    ASSERT_TRUE(d.ok);
    std::vector<std::string> lines;
    for (const SourceLine& l : d.document.lines)
        lines.push_back((l.number >= 0 ? std::to_string(l.number) + " " : "      ") + l.text);
    // Paragraph 20: a comment sentence after a statement starts a sentence of its own
    const std::vector<std::string> expected = {"20 ;", "      ; Workspace.", "      ;", "      primeflags: DS 5001", "      ;", "30 ;",
                                               "      start: LD A,2", "      CALL #1601", "      ; BC is equivalent to \"count\".",
                                               "      LD BC,2", "      ;", "      main.loop: LD HL,2500 ; while count<=2500"};
    ASSERT_GE(lines.size(), 4 + expected.size());
    for (size_t k = 0; k < expected.size(); ++k)
        EXPECT_EQ(lines[3 + k], expected[k]) << k;
}

TEST(LaserGeniusCodec_Test, StatementForms)
{
    const codecs::LaserGeniusCodec codec;
    struct Case
    {
        const char* text;
        const char* hex;
    };
    const Case cases[] = {
        {"LD (IX+2),7", "B2 FD 02 FD 07"},            // the mnemonic and its first operand in one token, no comma kept
        {"LD A,(IX-2)", "B5 28 FD 02"},
        {"DEFW start,$,lab+2*3", "77 EC 73 74 61 72 F4 E9 EC 6C 61 E2 D5 FD 02 D7 FD 03"},
        {"LD HL,[1+2]*3", "BE CB FD 01 D5 FD 02 D0 D7 FD 03"},
        {"DEFM \"NAME\\0\"", "75 ED 4E 41 4D 45 FD 00 00"},   // an escape inside a string: #FD and the code
        {"LD A,\"\\13\"", "B5 EB FD 0D"},
        {"exg: MACRO \\p1,\\p2", "F1 65 78 E7 7F F2 70 B1 F2 70 B2"},
        {"\\exg BC,DE", "81 65 78 E7 05 06"},
        {"*WHILE cnt<=10", "F5 8B EC 63 6E F4 DA FD 0A"},
        {"LD HL,1000", "BE FC E8 03"},
        {"LD HL,0255", "BE FC FF 00"},                // a word that fits a byte, written with four digits
        {"nop ;x", "33 EE 78 F0"},
        {";", "EF F0"},
    };
    for (const Case& c : cases)
    {
        std::vector<uint8_t> tokens;
        std::string error;
        ASSERT_TRUE(codecs::LaserGeniusCodec::EncodeStatement(c.text, tokens, error)) << c.text << ": " << error;
        EXPECT_EQ(tokens, FromHex(c.hex)) << c.text;
    }
}

TEST(LaserGeniusCodec_Test, DetectionAndPhoenix)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    for (const char* file : kFiles)
    {
        const DetectResult r = registry.Detect(ReadTestData(file), {});
        ASSERT_NE(r.chosen, nullptr) << file << ": " << r.reason;
        EXPECT_EQ(r.chosen->Info().id, "lasergenius") << file;
    }
    const codecs::LaserGeniusCodec codec;
    // Phoenix paragraphs are kept as bytes, with a warning each
    const DecodeResult d = codec.Decode(ReadTestData("lasergenius/SIEVE.PHX.lg"), {});
    ASSERT_TRUE(d.ok);
    EXPECT_FALSE(d.diagnostics.empty());
    // Not Laser Genius: zeros, text, another codec's file
    EXPECT_EQ(codec.Detect(std::vector<uint8_t>(512, 0), {}), 0);
    const std::string text = "10 LD A,2\n20 RET\n";
    EXPECT_EQ(codec.Detect(std::vector<uint8_t>(text.begin(), text.end()), {}), 0);
    EXPECT_EQ(codec.Detect(ReadTestData("prometheus/d80-pfill1.bin"), {}), 0);
}

// --- The frontend ---------------------------------------------------------------------------------------------------

TEST(LaserGeniusFrontend_Test, Expressions)
{
    // Unsigned 16-bit words, & | ^ on one level, comparisons and && || ! giving 1 (sjasmplus: -1), [ ] parentheses
    const std::vector<std::string> out = ToSjasmplus("DW 1|2&4\nDW 2&&1\nDW -10/3\nDW 1<<2+1\nDW [1+2]*3\nDW #1234@<4\nLD A,\"\\13\"\nDW ^5\n");
    const std::vector<std::string> expected = {"DW (1|2)&4", "DW -(2&&1)", "DW (-10&#FFFF)/(3&#FFFF)", "DW 1<<2+1", "DW (1+2)*3",
                                               "DW (((#1234&#FFFF)<<4|(#1234&#FFFF)>>>(16-4))&#FFFF)", "LD A,13", "DW ~5"};
    EXPECT_EQ(out, expected);
}

TEST(LaserGeniusFrontend_Test, Constructs)
{
    const std::vector<std::string> out = ToSjasmplus(
        "10 ORG #8000\n"
        "PUT #9000\n"
        "start: LD HL,.\n"
        "DEFM 13,\"OK\\255\"\n"
        "DS 2\n"
        "v: DL 1\n"
        "COND v\n"
        "NOP\n"
        "ELSE\n"
        "HALT\n"
        "ENDC\n"
        "exg: MACRO \\p1,\\p2\n"
        "PUSH \\p1\n"
        "ENDM\n"
        "\\exg BC,DE\n"
        "*WHILE v<=3\n"
        "v: DL v+1\n"
        "*ENDW\n"
        "*LIST OFF\n");
    const std::vector<std::string> expected = {"ORG #8000", "__LG_PUT1=$", "ORG #9000", "DISP __LG_PUT1", "start   LD HL,$$$",
                                               "DB 13,'OK',255", "DS 2", "v=1", "IF v", "NOP", "ELSE", "HALT", "ENDIF",
                                               "MACRO exg p1,p2", "PUSH p1", "ENDM", "exg BC,DE", "WHILE -((v&#FFFF)<=3)", "v=v+1", "ENDW"};
    EXPECT_EQ(out, expected);
}

TEST(LaserGeniusFrontend_Test, PhoenixParagraphsAreReported)
{
    const codecs::LaserGeniusCodec codec;
    const DecodeResult d = codec.Decode(ReadTestData("lasergenius/SIEVE.PHX.lg"), {});
    ASSERT_TRUE(d.ok);
    const ConvertResult r = Convert(d.document, "sjasmplus");
    bool reported = false;
    for (const Diagnostic& g : r.diagnostics)
        reported = reported || g.message.find("Phoenix") != std::string::npos;
    EXPECT_TRUE(reported);
}

// --- Assembled by sjasmplus (opt-in: UNREAL_ASM_SJASMPLUS) ----------------------------------------------------------

TEST(LaserGeniusFrontend_Test, ExamplesAssembleToWhatLaserGeniusBuilt)
{
    // Laser Genius 1.04 (Beta Disk) in unreal-ng loaded each example with two paragraphs in front, ORG #6900 and
    // PUT #6900, and assembled it: the bytes from #6900
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    const codecs::LaserGeniusCodec codec;
    for (const char* name : {"SIEVE.ASM", "ELLIPSE.ASM", "LIB.MAKER"})
    {
        DecodeResult d = codec.Decode(ReadTestData(std::string("lasergenius/") + name + ".lg"), {});
        ASSERT_TRUE(d.ok) << name;
        SourceLine org, put;
        org.text = "ORG #6900";
        put.text = "PUT #6900";
        d.document.lines.insert(d.document.lines.begin(), {org, put});
        const std::vector<uint8_t> built = ReadTestData(std::string("dialects/lasergenius/") + name + ".built.bin");
        EXPECT_EQ(AssembleAt(sjasmplus, d.document, 0x6900, built.size()), built) << name;
    }
    // Expressions, data and the location counters (EXPR.txt starts with its own ORG #6A00 and PUT #6A00)
    const std::vector<uint8_t> built = ReadTestData("dialects/lasergenius/EXPR.built.bin");
    EXPECT_EQ(AssembleAt(sjasmplus, SourceDocument::FromText(ReadTestText("dialects/lasergenius/EXPR.txt"), "lasergenius"), 0x6A00, built.size()), built);
}
