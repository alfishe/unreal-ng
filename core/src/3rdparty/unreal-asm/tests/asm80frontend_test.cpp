// The ASM80 frontend: ASM80 / Asm80Win 2.02 sources into the IR, written by the sjasmplus backend. Construct by
// construct (the rules of Asm80win.cpp, checked against asm80win.exe: research-asm80-to-sjasmplus.md), the codec's
// detection, and with UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the converted sources assembled and compared with the
// bytes asm80win.exe built: its own TEST.A80 / TEST2.A80, a constructs probe, a pages probe, and the AEDIT editor
// project (four files with *F and *B), equal to the ed.exe shipped with its sources.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <tuple>

#include "codecs/asm80/asm80codec.h"
#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "testdata.h"
#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"
#include "unrealasm/symbols/fromsource.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
/// The sjasmplus statements of an ASM80 text (blank and comment-only lines dropped, leading blanks trimmed)
std::vector<std::string> ToSjasmplus(const std::string& asm80, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(asm80, "asm80"), "sjasmplus");
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

bool Warned(const Diagnostics& diagnostics, const std::string& part)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& d) { return d.message.find(part) != std::string::npos; });
}

SourceDocument Decoded(const std::string& relative, const std::string& name)
{
    const codecs::Asm80Codec codec;
    DecodeResult decoded = codec.Decode(ReadTestData(relative), {});
    EXPECT_TRUE(decoded.ok) << relative;
    decoded.document.name = name;
    return decoded.document;
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

/// Writes the converted files (and the binaries they INCBIN) to a scratch directory, assembles harness.asm there with
/// sjasmplus and returns the directory (the caller reads the outputs and removes it)
std::filesystem::path Assemble(const char* sjasmplus, const std::vector<ProjectFile>& files, const std::vector<std::pair<std::string, std::string>>& binaries,
                               const std::string& harness)
{
    const std::filesystem::path dir = ScratchDirectory();
    const codecs::SjasmplusCodec codec;
    for (const ProjectFile& f : files)
        WriteBytes(dir / (f.name + ".asm"), codec.Encode(f.document, {}).bytes);
    for (const auto& [relative, name] : binaries)
        WriteBytes(dir / name, ReadTestData(relative));
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    EXPECT_EQ(std::system(command.c_str()), 0);
    return dir;
}

ProjectResult Converted(const std::vector<std::pair<std::string, std::string>>& sources)
{
    std::vector<ProjectFile> project;
    for (const auto& [relative, name] : sources)
        project.push_back({name, Decoded(relative, name)});
    const ProjectResult r = ConvertProject(project, "sjasmplus");
    EXPECT_TRUE(r.ok);
    return r;
}
}  // namespace

TEST(Asm80Frontend_Test, ExpressionsRunLeftToRightOnUnsignedWords)
{
    // No priorities, % mod, | or, ^ xor; a product is cut to 16 bits before a division sees it (-2*3/2 is 32765)
    EXPECT_EQ(ToSjasmplus("        LD A,2+3*4\n        DEFW 7&3|8^1,20%3"), (std::vector<std::string>{"LD A,(2+3)*4", "DW (7&3|8)^1,20%3"}));
    EXPECT_EQ(ToSjasmplus("        DEFW -2*3/2"), std::vector<std::string>{"DW ((-2*3)&#FFFF)/2"});
    // An EQU keeps an unsigned 16-bit value (N EQU -1 is 65535: N/2 is 32767)
    EXPECT_EQ(ToSjasmplus("N       EQU -1"), std::vector<std::string>{"N       EQU ((-1)&#FFFF)"});
    // Numbers modulo 65536; "c" is a character; # hex, % binary
    EXPECT_EQ(ToSjasmplus("        DEFW 70000,\"A\",#1F,%101"), (std::vector<std::string>{"DW 4464,'A',#1F,%101"}));
    // JR / DJNZ count in 16 bits: a target past #FFFF wraps
    EXPECT_EQ(ToSjasmplus("Lp      JR Lp+#FFFF"), std::vector<std::string>{"Lp      JR #FFFF&(Lp+#FFFF)"});
}

TEST(Asm80Frontend_Test, ConditionsAreTrueOnZeroOrOnEqual)
{
    EXPECT_EQ(ToSjasmplus("        IF X-1\n        NOP\n        ELSE\n        HALT\n        ENDIF\n        IF X=2\n        NOP\n        ENDIF"),
              (std::vector<std::string>{"IF ((X-1)&#FFFF)==0", "NOP", "ELSE", "HALT", "ENDIF", "IF X==2", "NOP", "ENDIF"}));
}

TEST(Asm80Frontend_Test, LabelsKeepSixteenCharactersAndTheirCase)
{
    // 16 characters count (VeryLongLabelNameXYZ is VeryLongLabelName01); $ and # are label characters (renamed for
    // sjasmplus), mnemonics and registers in any case
    EXPECT_EQ(ToSjasmplus("VeryLongLabelName01 EQU 5\n        defb VeryLongLabelNameXYZ\nN$1     ld a,N$1"),
              (std::vector<std::string>{"VeryLongLabelName01 EQU 5", "DB VeryLongLabelName01", "L_N_1   LD A,L_N_1"}));
}

TEST(Asm80Frontend_Test, KeysIncludeFilesAndPickPages)
{
    // *F: a text file by its name without drive, directory and .a80; *B name,start,length: a binary; *Pn: the page
    // of the next ORG at #C000; the other keys are comments
    EXPECT_EQ(ToSjasmplus("*F c:\\speccy\\aedit\\window.a80\n*B ..\\DATA\\font.fnt,#10,#20\n*P3\n        ORG #C000\n*L-"),
              (std::vector<std::string>{"DEVICE ZXSPECTRUM4096", "INCLUDE \"window.asm\"", "INCBIN \"font.fnt\",#10,#20", "ORG #C000,3"}));
}

TEST(Asm80Frontend_Test, DispEndsWithEnddOrOrg)
{
    const std::vector<std::string> out = ToSjasmplus("        DISP #9000\nL       NOP\n        ENDD\n        DISP #A000\n        ORG #6000");
    EXPECT_EQ(std::count(out.begin(), out.end(), "ENT"), 2) << "ENDD and ORG each end a DISP";
}

TEST(Asm80Frontend_Test, MacrosTakeValuesAtTheCall)
{
    // Parameters =0..=9; arguments are values taken at the call: $ is the call's address (a label on the call line)
    EXPECT_EQ(ToSjasmplus("Fill    MAC\n        LD BC,=0*16\n        LD HL,=1\n        ENDM\n        Fill 3,$"),
              (std::vector<std::string>{"MACRO Fill _m0,_m1", "LD BC,_m0*16", "LD HL,_m1", "ENDM", "__ASM80_AT__5 Fill 3,__ASM80_AT__5"}));
}

TEST(Asm80Frontend_Test, WhatAsm80ReadsItsOwnWay)
{
    // "sub hl,de": SUB reads one operand and takes HL as an undefined label: SUB 0 (asm80win.exe); INF is IN F,(C);
    // EX AF,AF; DEFM stops at its quote; DEFB ? is a random byte (0 written, with a warning)
    Diagnostics diagnostics;
    EXPECT_EQ(ToSjasmplus("        sub hl,de\n        INF\n        EX AF,AF\n        DEFM \"a;b\",0\n        DEFB ?", &diagnostics),
              (std::vector<std::string>{"SUB 0", "IN (C)", "EX AF,AF'", "DB 'a;b'", "DB 0"}));
    EXPECT_TRUE(Warned(diagnostics, "undefined label"));
    EXPECT_TRUE(Warned(diagnostics, "text after DEFM"));
    EXPECT_TRUE(Warned(diagnostics, "random value"));
}

TEST(Asm80Frontend_Test, Detection)
{
    // TEST.A80 has key lines and a macro: asm80; TEST2.A80 only INF (one mark: 66, +5 for the .a80 extension)
    const CodecRegistry& registry = CodecRegistry::Builtin();
    CatalogHints hints;
    hints.extension = "A80";
    const DetectResult test = registry.Detect(ReadTestData("asm80/TEST.A80"), hints);
    ASSERT_NE(test.chosen, nullptr) << test.reason;
    EXPECT_EQ(test.chosen->Info().id, "asm80");
    const codecs::Asm80Codec codec;
    EXPECT_EQ(codec.Detect(ReadTestData("asm80/TEST2.A80"), hints), 71);
    EXPECT_EQ(codec.Detect(ReadTestData("sjasmplus/hello.asm"), {}), 0);
    // A text codec: the file comes back byte for byte
    const std::vector<uint8_t> bytes = ReadTestData("asm80/TEST.A80");
    const DecodeResult decoded = codec.Decode(bytes, {});
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.document.dialect, "asm80");
    EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, bytes);
}

// --- Assembled by sjasmplus (opt-in: UNREAL_ASM_SJASMPLUS) ----------------------------------------------------------

TEST(Asm80Frontend_Test, ConvertedSourcesAssembleToWhatAsm80Built)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    // ASM80's own tests: every instruction form (1485 bytes at 40000), and its mixed-case test (113 bytes at 0)
    for (const auto& [name, length, start] : {std::tuple<std::string, int, int>{"TEST", 1485, 40000}, {"TEST2", 113, 0}})
    {
        const ProjectResult r = Converted({{"asm80/" + name + ".A80", "t"}});
        const std::filesystem::path dir = Assemble(sjasmplus, r.files, {},
                                                   "        DEVICE ZXSPECTRUM128\n        INCLUDE \"t.asm\"\n        SAVEBIN \"t.bin\"," + std::to_string(start) + "," +
                                                       std::to_string(length) + "\n");
        EXPECT_EQ(ReadBytes(dir / "t.bin"), ReadTestData("asm80/" + name + ".bin")) << name;
        std::filesystem::remove_all(dir);
    }
    // The constructs probe (129 bytes at #6000); DEFB ?,? at offsets 55, 56 are rand() bytes asm80win.exe wrote
    {
        const ProjectResult r = Converted({{"dialects/asm80/probe/PROBE.A80", "probe"}});
        const std::filesystem::path dir = Assemble(sjasmplus, r.files, {{"dialects/asm80/probe/bin.dat", "bin.dat"}},
                                                   "        DEVICE ZXSPECTRUM128\n        INCLUDE \"probe.asm\"\n        SAVEBIN \"p.bin\",#6000,129\n");
        std::vector<uint8_t> expected = ReadTestData("dialects/asm80/probe/PROBE.bin");
        ASSERT_EQ(expected.size(), 129u);
        expected[55] = expected[56] = 0;
        EXPECT_EQ(ReadBytes(dir / "p.bin"), expected);
        std::filesystem::remove_all(dir);
    }
    // Pages: *P3 / *P4 before ORG #C000; asm80win.exe's *O wrote each ORG's bytes to PAGES.B00-B03
    {
        const ProjectResult r = Converted({{"dialects/asm80/probe/PAGES.A80", "pages"}});
        const std::filesystem::path dir = Assemble(sjasmplus, r.files, {},
                                                   "        DEVICE ZXSPECTRUM128\n        INCLUDE \"pages.asm\"\n        SAVEDEV \"b0\",2,0,4\n"
                                                   "        SAVEDEV \"b1\",3,0,2\n        SAVEDEV \"b2\",4,0,3\n        SAVEDEV \"b3\",2,#1000,3\n");
        for (int n = 0; n < 4; ++n)
            EXPECT_EQ(ReadBytes(dir / ("b" + std::to_string(n))), ReadTestData("dialects/asm80/probe/PAGES.B0" + std::to_string(n))) << n;
        std::filesystem::remove_all(dir);
    }
    // AEDIT (Sprinter Team): editor.a80 *F's window, graphic and setscr, setscr *B's 6x8.fnt; the 6917 bytes from
    // #7E00 equal ed.exe, which its authors built with Asm80Win
    {
        const ProjectResult r = Converted({{"dialects/asm80/aedit/EDITOR.A80", "editor"},
                                           {"dialects/asm80/aedit/WINDOW.A80", "window"},
                                           {"dialects/asm80/aedit/GRAPHIC.A80", "graphic"},
                                           {"dialects/asm80/aedit/SETSCR.A80", "setscr"}});
        const std::filesystem::path dir = Assemble(sjasmplus, r.files, {{"dialects/asm80/aedit/6X8.FNT", "6x8.fnt"}},
                                                   "        DEVICE ZXSPECTRUM128\n        INCLUDE \"editor.asm\"\n        SAVEBIN \"ed.bin\",#7E00,6917\n");
        EXPECT_EQ(ReadBytes(dir / "ed.bin"), ReadTestData("dialects/asm80/aedit/ed.bin"));
        std::filesystem::remove_all(dir);
    }
}

TEST(Asm80Frontend_Test, AeditLabelsComeThroughSymbolsFromProject)
{
    // The 207 labels of asm80win.exe's listing (editor.lst's table: name, value) against SymbolsFromProject over the
    // four files; the names are the source's (N$1 kept, not sjasmplus' rename)
    std::vector<ProjectFile> project;
    for (const auto& [name, file] : std::vector<std::pair<std::string, std::string>>{{"editor", "EDITOR"}, {"window", "WINDOW"}, {"graphic", "GRAPHIC"}, {"setscr", "SETSCR"}})
        project.push_back({name, Decoded("dialects/asm80/aedit/" + file + ".A80", name)});
    symbols::SourceSymbolsOptions options;
    options.layout.fileSize = [](const std::string& name) -> std::optional<uint64_t> {
        if (name == "6x8.fnt")
            return 2040;
        return std::nullopt;
    };
    const symbols::SourceSymbolsResult r = symbols::SymbolsFromProject(project, 0, options);
    EXPECT_TRUE(r.ok);
    for (const Diagnostic& d : r.diagnostics)
        if (d.severity == Severity::Error)
            ADD_FAILURE() << d.message;
    std::map<std::string, uint32_t> expected;
    std::istringstream in(ReadTestText("dialects/asm80/aedit/editor.labels.txt"));
    std::string name, value;
    while (in >> name >> value)
        expected[name] = static_cast<uint32_t>(std::stoul(value.substr(1), nullptr, 16));
    ASSERT_EQ(expected.size(), 207u);
    EXPECT_EQ(r.set.symbols.size(), expected.size());
    for (const symbols::Symbol& s : r.set.symbols)
    {
        if (!expected.count(s.name)) { ADD_FAILURE() << "extra: " << s.name; continue; }
        const auto address = symbols::CpuAddress(s);
        EXPECT_EQ(address ? *address : s.location.offset, expected.at(s.name)) << s.name;
    }
}
