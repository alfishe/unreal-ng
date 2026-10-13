// sjasmplus' structure words for the retro targets (asm-synchronizer.md §15.3, Z5): STRUCT laid out as sjasmplus does,
// MODULE, STRUCT, ALIGN, DZ / DC / D24 / DD / ABYTE, IFUSED, DEFINE and EXA / EXD written in words each target has
// (sjasmpluslower.h), and a probe of all of them through every retro target and back (UNREAL_ASM_SJASMPLUS: the bytes
// sjasmplus builds from it, testdata/dialects/sjasmplus/z5probe.bin).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "testdata.h"
#include "unrealasm/dialect.h"
#include "unrealasm/layout.h"
#include "unrealasm/registry.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
std::vector<std::string> Lines(const std::string& sjasmplus, const std::string& target, Diagnostics* diagnostics = nullptr, const std::string& version = {})
{
    BackendOptions options;
    options.targetVersion = version;
    const ConvertResult r = Convert(SourceDocument::FromText(sjasmplus, "sjasmplus"), target, options);
    EXPECT_TRUE(r.ok);
    if (diagnostics)
        *diagnostics = r.diagnostics;
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        std::string squeezed;
        for (char c : l.text)
        {
            c = c == '\t' ? ' ' : c;
            if (c != ' ' || (!squeezed.empty() && squeezed.back() != ' '))
                squeezed.push_back(c);
        }
        while (!squeezed.empty() && squeezed.back() == ' ')
            squeezed.pop_back();
        out.push_back(squeezed);
    }
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

/// sjasmplus' bytes of a source from #8000 (`length` of them)
std::vector<uint8_t> Assemble(const char* sjasmplus, const SourceDocument& source, size_t length)
{
    std::random_device random;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    WriteBytes(dir / "prog.asm", codecs::SjasmplusCodec().Encode(source, {}).bytes);
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"prog.asm\"\n        SAVEBIN \"out.bin\",#8000," + std::to_string(length) + "\n";
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
}  // namespace

TEST(SjasmplusLower_Test, StructsAreLaidOutAsSjasmplusDoes)
{
    // sjasmplus 1.21's --sym of the probe: the structure's name is its size (the offset included), the fields their
    // offsets, an instance's fields their addresses, a module's names module.name
    const std::string text = ReadTestText("dialects/sjasmplus/z5probe.asm");
    const layout::LayoutResult laid = layout::Layout({{"z5probe", SourceDocument::FromText(text, "sjasmplus")}}, 0);
    EXPECT_TRUE(laid.ok);
    std::map<std::string, int64_t> values;
    for (const layout::Label& l : laid.labels)
        values[l.name] = l.value;
    const std::map<std::string, int64_t> expected = {
        {"POS", 2},           {"POS.x", 0},         {"POS.y", 1},         {"SPR", 12},          {"SPR.p", 2},
        {"SPR.p.x", 2},       {"SPR.p.y", 3},       {"SPR.w", 4},         {"SPR.n", 9},         {"s1", 0x8000},
        {"s1.p.y", 0x8003},   {"s1.n", 0x8009},     {"s2", 0x800C},       {"s2.w", 0x8010},     {"s3", 0x8018},
        {"s3.y", 0x8019},     {"main.start", 0x8028}, {"main.helper", 0x803D}, {"main.greeting", 0x803E}, {"aligned", 0x8050},
    };
    for (const auto& [name, value] : expected)
        EXPECT_EQ(values[name], value) << name;
}

TEST(SjasmplusLower_Test, WordsATargetHasNot)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines = Lines(ReadTestText("dialects/sjasmplus/z5probe.asm"), "tasm", &diagnostics, "4.12");
    const std::string text = Joined(lines);
    EXPECT_TRUE(Contains(lines, "SPR_P_Y EQU 3")) << text;                 // a field: its offset
    EXPECT_TRUE(Contains(lines, "SPR EQU 12")) << text;                    // the structure: its size
    EXPECT_TRUE(Contains(lines, "S2 DEFS 2 ;SPR")) << text;                // an instance: its data
    EXPECT_TRUE(Contains(lines, "S2_W EQU S2+4")) << text;                 // ... and its fields
    EXPECT_TRUE(Contains(lines, "MAIN_START LD HL,MAIN_DATA")) << text;    // MODULE: the full names
    EXPECT_TRUE(Contains(lines, "LD HL,MAIN_GREETING")) << text;           // a macro expanded in the module
    EXPECT_TRUE(Contains(lines, "LD B,3")) << text;                        // DEFINE COUNT 3
    EXPECT_TRUE(Contains(lines, "EX AF,AF'")) << text;                     // EXA
    EXPECT_TRUE(Contains(lines, "EX DE,HL")) << text;                      // EXD
    EXPECT_TRUE(Contains(lines, "DEFB \"needs \",97")) << text;            // TASM tokenizes A in a string: a number
    for (const Diagnostic& d : diagnostics)
        EXPECT_EQ(d.severity, Severity::Info) << d.line << ": " << d.message;
}

TEST(SjasmplusLower_Test, ALuaBlockIsLeftOutWithAWarning)
{
    Diagnostics diagnostics;
    const std::vector<std::string> lines =
        Lines("        ORG #8000\n        LUA\n        sj.add_byte(1)\n        ENDLUA\n        NOP\n", "tasm", &diagnostics, "4.12");
    EXPECT_FALSE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("add_byte") != std::string::npos && l[0] != ';'; }))
        << Joined(lines);
    EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::Warning; }), 1);
}

TEST(SjasmplusLower_Test, TheProbeKeepsItsBytesThroughEveryRetroTarget)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    const std::vector<uint8_t> expected = ReadTestData("dialects/sjasmplus/z5probe.bin");
    const SourceDocument source = SourceDocument::FromText(ReadTestText("dialects/sjasmplus/z5probe.asm"), "sjasmplus");
    for (const std::string target : {"tasm", "alasm", "storm", "xas", "zxasm", "masm", "gens", "zeus", "prometheus", "pasm", "lasergenius"})
    {
        const ConvertResult written = Convert(source, target);
        ASSERT_TRUE(written.ok) << target;
        for (const Diagnostic& d : written.diagnostics)
            if (d.severity != Severity::Info)
                ADD_FAILURE() << target << " line " << d.line << ": " << d.message;
        // through the target's file and its frontend
        const ISourceCodec* codec = CodecRegistry::Builtin().Find(target);
        ASSERT_NE(codec, nullptr) << target;
        const EncodeResult bytes = codec->Encode(written.document, {});
        EXPECT_TRUE(bytes.ok) << target;
        DecodeResult back = codec->Decode(bytes.bytes, {});
        back.document.dialect = target;
        const ConvertResult again = Convert(back.document, "sjasmplus");
        ASSERT_TRUE(again.ok) << target;
        EXPECT_EQ(Assemble(sjasmplus, SourceDocument::FromText(again.document.Text(), "sjasmplus"), expected.size()), expected) << target;
    }
}
