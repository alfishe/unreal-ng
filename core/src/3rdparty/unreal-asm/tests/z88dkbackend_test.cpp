// The z88dk backend (A6): the IR written as z88dk's z80asm source. Construct by construct (C's priorities on 32-bit
// words, labels with ":", PHASE, a SECTION per ORG, the keywords z80asm reserves), and the oracle programs of the other
// dialects converted to z80asm: with UNREAL_ASM_Z80ASM=<path to z88dk-z80asm> they are assembled and compared with the
// bytes the original assemblers built (STORM 1.3, ZAsm 3.15, TASM 5.0, ALASM 5.09).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <regex>

#include "codecs/storm/stormcodec.h"
#include "codecs/tasm/tasmcodec.h"
#include "codecs/text/textcodec.h"
#include "codecs/zxasm/zxasmcodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
/// The z80asm lines of a source in another dialect (blank and comment-only lines dropped, leading blanks trimmed)
std::vector<std::string> ToZ80asm(const std::string& text, const std::string& dialect, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, dialect), "z88dk");
    EXPECT_TRUE(r.ok);
    if (diagnostics)
        *diagnostics = r.diagnostics;
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        const size_t first = l.text.find_first_not_of(' ');
        if (first == std::string::npos || l.text[first] == ';')
            continue;
        out.push_back(l.text.substr(first, l.text.find(" ;", first) == std::string::npos ? std::string::npos : l.text.find(" ;", first) - first));
        while (!out.back().empty() && out.back().back() == ' ')
            out.back().pop_back();
    }
    return out;
}

bool Contains(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

containers::TrdosFile Hobeta(const std::string& relative)
{
    containers::TrdosFile file;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData(relative), file, error)) << relative << ": " << error;
    return file;
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

/// The converted project assembled by z80asm: its sections (one binary each, addresses in the map) put together, the
/// `length` bytes from `base`
std::vector<uint8_t> AssembleWithZ80asm(const char* z80asm, const ProjectResult& project, const std::string& main,
                                        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& binaries, uint16_t base, size_t length)
{
    std::random_device random;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    const codecs::TextCodec text;
    for (const ProjectFile& f : project.files)
        WriteBytes(dir / (f.name + ".asm"), text.Encode(f.document, {}).bytes);
    for (const auto& [name, bytes] : binaries)
        WriteBytes(dir / name, bytes);
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + z80asm + "\" -b -m -o=out.bin " + main + ".asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + z80asm + "\" -b -m -o=out.bin " + main + ".asm > out.txt 2>&1";
#endif
    EXPECT_EQ(std::system(command.c_str()), 0);
    std::vector<uint8_t> memory(65536, 0);
    std::ifstream map(dir / "out.map");
    const std::regex head(R"(^__(\w*?)_?head\s*=\s*\$([0-9A-F]+))");
    for (std::string line; std::getline(map, line);)
    {
        std::smatch m;
        if (!std::regex_search(line, m, head))
            continue;
        const std::string section = m[1];
        const std::vector<uint8_t> bytes = ReadBytes(dir / (section.empty() ? std::string("out.bin") : "out_" + section + ".bin"));
        const size_t address = std::stoul(m[2], nullptr, 16);
        for (size_t k = 0; k < bytes.size(); ++k)
            memory[(address + k) & 0xFFFF] = bytes[k];
    }
    std::filesystem::remove_all(dir);
    return std::vector<uint8_t>(memory.begin() + base, memory.begin() + base + static_cast<std::ptrdiff_t>(length));
}

ProjectResult Project(const std::string& name, const SourceDocument& document)
{
    return ConvertProject({{name, document}}, "z88dk");
}

SourceDocument Decoded(const ISourceCodec& codec, const containers::TrdosFile& file, const std::string& version = {})
{
    DecodeOptions options;
    options.catalog = file.Hints();
    options.subversion = version;
    return codec.Decode(file.data, options).document;
}
}  // namespace

TEST(Z88dkBackend_Test, CPrioritiesOn32BitWords)
{
    // C's priorities: (1+2)*3 keeps its parentheses, $hex; a 16-bit source's division masked (z80asm computes in 32
    // bits, ALASM's (0-1)/2 is #7FFF); STORM's true 1 is z80asm's own
    EXPECT_EQ(ToZ80asm("        LD A,(1+2)*3\n        DW #C000", "storm"), (std::vector<std::string>{"LD A,0+(1+2)*3", "DW $C000"}));
    EXPECT_EQ(ToZ80asm("        DW (0-1)/2,2<3", "storm"), (std::vector<std::string>{"DW ((0-1)&$FFFF)/2,2<3"}));
    // sjasmplus' true is -1: negated
    EXPECT_EQ(ToZ80asm("        DW 2<3", "sjasmplus"), (std::vector<std::string>{"DW (-(2<3))"}));
}

TEST(Z88dkBackend_Test, LabelsSectionsAndPhase)
{
    // A label ends with ":"; z80asm keywords (ABC, MOV, ...) and mnemonics as labels are renamed
    EXPECT_EQ(ToZ80asm("ABC     NOP\n        JP ABC", "sjasmplus"), (std::vector<std::string>{"L_ABC:", "NOP", "JP L_ABC"}));
    // Every ORG opens a section (z80asm takes one ORG per section); DISP is PHASE
    const std::vector<std::string> lines = ToZ80asm("        ORG #8000\n        DISP #C000\nX       NOP\n        ENT\n        ORG #9000", "sjasmplus");
    EXPECT_TRUE(Contains(lines, "SECTION s_main_1"));
    EXPECT_TRUE(Contains(lines, "PHASE ($C000&$FFFF)"));
    EXPECT_TRUE(Contains(lines, "DEPHASE"));
    EXPECT_TRUE(Contains(lines, "SECTION s_main_2"));
    // ORG computed from labels moves on within the section; nothing but labels after it: the labels are EQUs
    EXPECT_TRUE(Contains(ToZ80asm("        ORG #8000\nA1      NOP\n        ORG A1+16\n        NOP", "sjasmplus"), "DEFS ((A1+16&$FFFF))-$"));
    EXPECT_TRUE(Contains(ToZ80asm("        ORG #8000\nA1      NOP\n        ORG A1\nB1", "sjasmplus"), "B1 EQU (A1&$FFFF)"));
    // INCBIN is BINARY; IN F,(C) is z80asm's own
    EXPECT_EQ(ToZ80asm("        INCBIN \"F\"\n        IN F,(C)", "sjasmplus"), (std::vector<std::string>{"BINARY \"F\"", "IN F,(C)"}));
}

TEST(Z88dkBackend_Test, ProgramsAssembleToWhatTheOriginalsBuilt)
{
    const char* z80asm = std::getenv("UNREAL_ASM_Z80ASM");
    if (!z80asm)
        GTEST_SKIP() << "set UNREAL_ASM_Z80ASM to the z88dk-z80asm binary";
    const codecs::StormCodec storm;
    const codecs::ZxasmCodec zxasm;
    const codecs::TasmCodec tasm;
    for (const std::string& name : {std::string("STORMT1"), std::string("STORMT2")})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/" + name + ".bin");
        EXPECT_EQ(AssembleWithZ80asm(z80asm, Project(name, Decoded(storm, Hobeta("dialects/storm13/" + name + ".$C"))), name,
                                     {{"INC", Hobeta("dialects/storm13/INC.$C").data}}, 0x8000, expected.size()),
                  expected)
            << name;
    }
    // ZXT1's return from a nested PHASE to the outer one is an address computed from labels: z80asm's PHASE takes
    // constants only
    for (const std::string& name : {std::string("ZXT2"), std::string("ZXT3")})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleWithZ80asm(z80asm, Project(name, Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15")), name, {}, 0x8000,
                                     expected.size()),
                  expected)
            << name;
    }
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/tasm50/T50PROG.bin");
        EXPECT_EQ(AssembleWithZ80asm(z80asm, Project("T50PROG", Decoded(tasm, Hobeta("tasm/T50PROG.$A"))), "T50PROG", {}, 0x7000, expected.size()), expected);
    }
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/alasm-sjasmplus/constructs.bin");
        const SourceDocument constructs = SourceDocument::FromText(ReadTestText("dialects/alasm-sjasmplus/constructs.alasm.txt"), "alasm");
        EXPECT_EQ(AssembleWithZ80asm(z80asm, Project("constructs", constructs), "constructs", {}, 0x6000, expected.size()), expected);
    }
}
