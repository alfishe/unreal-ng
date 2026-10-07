// The MASM frontend: MASM 1.x sources into the IR, written by the sjasmplus backend. Construct by construct (the rules
// of MASM 1.1's own assembler source), then two oracles built by MASM 1.1 in unreal-ng (testdata/dialects/masm11): a
// program with every construct (MT1) and MASM 1.1's own source (testdata/masm: LS2 including M1+ and M2+). With
// UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the conversions are assembled and compared with those bytes. The labels of
// the converted source are laid out as sjasmplus 1.24 gave them (testdata/symbols/fromsource/masm11-LS2.sym).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include "codecs/masm/masmcodec.h"
#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"
#include "unrealasm/layout.h"
#include "unrealasm/symbols/fromsource.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
/// The sjasmplus statements (blank and comment-only lines dropped, leading blanks trimmed) of a MASM source
std::vector<std::string> ToSjasmplus(const std::string& masm, const std::string& version = "1.1", Diagnostics* diagnostics = nullptr)
{
    SourceDocument document = SourceDocument::FromText(masm, "masm");
    document.subversion = version;
    const ConvertResult r = Convert(document, "sjasmplus");
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

containers::TrdosFile Hobeta(const std::string& relative)
{
    containers::TrdosFile file;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData(relative), file, error)) << relative << ": " << error;
    return file;
}

ProjectFile Decoded(const std::string& relative)
{
    const containers::TrdosFile file = Hobeta(relative);
    DecodeOptions options;
    options.catalog = file.Hints();
    return {file.TrimmedName(), codecs::MasmCodec().Decode(file.data, options).document};
}

/// MASM 1.1's own source: LS2, which INCLUDEs M1+ and M2+
std::vector<ProjectFile> MasmSource()
{
    return {Decoded("masm/MASM_SRC__LS2.$a"), Decoded("masm/MASM_SRC__M1.$a"), Decoded("masm/MASM_SRC__M2.$a")};
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

/// The project converted and assembled with sjasmplus from `main`; the bytes of each range (empty when skipped)
std::vector<std::vector<uint8_t>> Assemble(const char* sjasmplus, const std::vector<ProjectFile>& project, const std::string& main,
                                           const std::vector<std::pair<int, int>>& ranges)
{
    const ProjectResult converted = ConvertProject(project, "sjasmplus");
    EXPECT_TRUE(converted.ok);
    std::random_device random;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    for (const ProjectFile& f : converted.files)
        WriteBytes(dir / (f.name + ".asm"), codecs::SjasmplusCodec().Encode(f.document, {}).bytes);
    std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"" + main + ".asm\"\n";
    for (size_t k = 0; k < ranges.size(); ++k)
        harness += "        SAVEBIN \"out" + std::to_string(k) + ".bin\"," + std::to_string(ranges[k].first) + "," + std::to_string(ranges[k].second) + "\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    EXPECT_EQ(std::system(command.c_str()), 0);
    std::vector<std::vector<uint8_t>> out;
    for (size_t k = 0; k < ranges.size(); ++k)
        out.push_back(ReadBytes(dir / ("out" + std::to_string(k) + ".bin")));
    std::filesystem::remove_all(dir);
    return out;
}
}  // namespace

TEST(MasmFrontend_Test, ExpressionsRunLeftToRightOnWords)
{
    // MASM 1.1's BITE / CALC: no priorities, @ is XOR, | OR; the division is unsigned on 16 bits
    EXPECT_EQ(ToSjasmplus("        LD A,2+3*4"), std::vector<std::string>{"LD A,(2+3)*4"});
    EXPECT_EQ(ToSjasmplus("        LD HL,#1234@#FF00|1"), std::vector<std::string>{"LD HL,#1234^#FF00|1"});
    EXPECT_EQ(ToSjasmplus("        LD BC,100/7"), std::vector<std::string>{"LD BC,(100&#FFFF)/(7&#FFFF)"});
    // Numbers: #FF, %101, decimal, 0C000H; "AB" is a word; $ the current address
    EXPECT_EQ(ToSjasmplus("        LD DE,0C000H+%11+\"AB\"+$"), std::vector<std::string>{"LD DE,#0C000+%11+16706+$"});
    // No unary minus and no parentheses in expressions: such a line is kept and reported
    Diagnostics d;
    ToSjasmplus("        LD A,-1", "1.1", &d);
    EXPECT_TRUE(std::any_of(d.begin(), d.end(), [](const Diagnostic& x) { return x.message.find("not parsed") != std::string::npos; }));
}

TEST(MasmFrontend_Test, IndexDisplacementIsNegatedAsAWhole)
{
    // (IX-e): the whole e is computed, then negated (the IX_IY routine): (IX-2+1) is -3
    EXPECT_EQ(ToSjasmplus("        LD A,(IX-2+1)"), std::vector<std::string>{"LD A,(IX+0-(2+1))"});
    EXPECT_EQ(ToSjasmplus("        LD (IY+5),A"), std::vector<std::string>{"LD (IY+5),A"});
    EXPECT_EQ(ToSjasmplus("        LD A,(IX)"), std::vector<std::string>{"LD A,(IX)"});
}

TEST(MasmFrontend_Test, KeywordsAreCapitals)
{
    // The editor tokenizes only capitals: "ld a,b" is text, MASM reports "no command"; labels keep their case
    Diagnostics d;
    const std::vector<std::string> lines = ToSjasmplus("label   ld a,b\nLabel   LD A,B", "1.1", &d);
    EXPECT_TRUE(std::find(lines.begin(), lines.end(), "Label   LD A,B") != lines.end());
    EXPECT_TRUE(std::any_of(d.begin(), d.end(), [](const Diagnostic& x) { return x.message.find("no command ld") != std::string::npos; }));
    // A colon ends a label, even with no blank after it; a keyword in column 0 is a command (INCLUDE M1+)
    EXPECT_EQ(ToSjasmplus("INCLUD1:CALL INCLUST"), std::vector<std::string>{"INCLUD1 CALL INCLUST"});
    EXPECT_EQ(ToSjasmplus("INCLUDE M1+"), std::vector<std::string>{"INCLUDE \"M1+.asm\""});
    // NV / V are PO / PE, EXA is EX AF,AF', INF is IN F,(C), XH / YL the index halves
    EXPECT_EQ(ToSjasmplus("        JP NV,0\n        JP V,0\n        EXA\n        INF\n        LD A,XH\n        LD YL,A"),
              (std::vector<std::string>{"JP PO,0", "JP PE,0", "EX AF,AF'", "IN F,(C)", "LD A,IXH", "LD IYL,A"}));
    // ENDM is a label in 1.x (a 2.0 / 3.0 keyword)
    EXPECT_EQ(ToSjasmplus("ENDM    POP AF"), std::vector<std::string>{"ENDM    POP AF"});
}

TEST(MasmFrontend_Test, DataAndRepetition)
{
    // DEFB strings with "" for a quote; DEFS count,list repeats the list; BEGIN n ... END repeats the block
    EXPECT_EQ(ToSjasmplus("        DEFB \"AB\"\"C\",1,\"Z\"+1"), std::vector<std::string>{"DB 'AB\"C',1,'Z'+1"});
    EXPECT_EQ(ToSjasmplus("        DEFS 3,7,8\n        DS 2"), (std::vector<std::string>{"DUP 3", "DB 7,8", "EDUP", "DS 2"}));
    EXPECT_EQ(ToSjasmplus("        BEGIN 3\n        NOP\n        END"), (std::vector<std::string>{"DUP 3", "NOP", "EDUP"}));
}

TEST(MasmFrontend_Test, MacroCommandsAreWrittenOut)
{
    // DOWN rr / UP rr (the TAB / TAB2 tables), SYSTEM[+] (TAB_SYS), STOPKEY [address] (STOP_TB), as 1.1 writes them
    const std::vector<std::string> down = ToSjasmplus("        DOWN HL");
    EXPECT_EQ(down, (std::vector<std::string>{"INC H", "LD A,H", "AND 7", "JR NZ,$+12", "LD A,L", "ADD A,32", "LD L,A", "JR C,$+6", "LD A,H", "SUB 8", "LD H,A"}));
    EXPECT_EQ(ToSjasmplus("        UP DE")[1], "DEC D");
    EXPECT_EQ(ToSjasmplus("        SYSTEM+"), (std::vector<std::string>{"DI", "LD IY,#5C3A", "LD A,#3F", "LD I,A", "IM 1", "EI", "RET"}));
    EXPECT_EQ(ToSjasmplus("        STOPKEY").back(), "JR Z,$-6");
    EXPECT_EQ(ToSjasmplus("        STOPKEY 0").back(), "JP Z,0");
    // The 1.0 demo has none of them: a label "DOWN" there
    EXPECT_EQ(ToSjasmplus("DOWN    NOP", "1.0"), std::vector<std::string>{"DOWN    NOP"});
}

TEST(MasmFrontend_Test, PhaseInsidePhaseTakesTheLogicalAddress)
{
    // $ is the logical address: PHASE $-#1000 inside a PHASE is computed before the PHASE ends
    const std::vector<std::string> lines = ToSjasmplus("        ORG #6000\n        PHASE #8000\nP1      NOP\n        PHASE $-#1000\nP2      NOP\n        UNPHASE");
    EXPECT_TRUE(std::find(lines.begin(), lines.end(), "__UNREALASM_PHASE=$-#1000") != lines.end());
    EXPECT_TRUE(std::find(lines.begin(), lines.end(), "DISP __UNREALASM_PHASE") != lines.end());
}

TEST(MasmFrontend_Test, OwnSourceLabelsLayOutAsSjasmplusGaveThem)
{
    const ProjectResult converted = ConvertProject(MasmSource(), "sjasmplus");
    EXPECT_TRUE(converted.ok);
    for (const Diagnostic& d : converted.diagnostics)
        EXPECT_NE(d.severity, Severity::Warning) << d.message;
    const layout::LayoutResult r = layout::Layout(converted.files, 0);
    EXPECT_TRUE(r.ok);
    std::map<std::string, uint32_t> sym;
    std::istringstream in(ReadTestText("symbols/fromsource/masm11-LS2.sym"));
    std::string line;
    while (std::getline(in, line))
        if (const size_t colon = line.find(": EQU 0x"); colon != std::string::npos)
            sym[line.substr(0, colon)] = static_cast<uint32_t>(std::stoul(line.substr(colon + 8), nullptr, 16));
    ASSERT_EQ(r.labels.size(), sym.size());
    for (const layout::Label& l : r.labels)
    {
        ASSERT_TRUE(sym.count(l.name)) << l.name;
        EXPECT_EQ(static_cast<uint32_t>(static_cast<uint64_t>(l.value)), sym[l.name]) << l.name;
    }
    // Through SymbolsFromProject: the source's names, the conversion's helper left out
    const symbols::SourceSymbolsResult s = symbols::SymbolsFromProject(MasmSource(), 0);
    EXPECT_TRUE(s.ok);
    EXPECT_EQ(s.set.symbols.size(), sym.size() - 1);   // __UNREALASM_PHASE
    bool hx = false;
    for (const symbols::Symbol& x : s.set.symbols)
    {
        EXPECT_EQ(x.name.rfind("__UNREALASM", 0), std::string::npos);
        hx = hx || (x.name == "HX" && x.provenance.type == "written as L_HX");   // a sjasmplus register name, renamed
    }
    EXPECT_TRUE(hx);
}

TEST(MasmFrontend_Test, ProgramsAssembleToWhatMasm11Built)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    // Every construct (MT1): 140 bytes at #6000
    const std::vector<std::vector<uint8_t>> mt1 = Assemble(sjasmplus, {Decoded("dialects/masm11/MT1.$a")}, "MT1", {{0x6000, 140}});
    EXPECT_EQ(mt1[0], ReadTestData("dialects/masm11/MT1.bin"));
    // MASM 1.1's own source: #C000-#ED90 and #6000-#6016, what MASM 1.1 built from it
    const std::vector<std::vector<uint8_t>> own = Assemble(sjasmplus, MasmSource(), "LS2", {{0xC000, 11665}, {0x6000, 23}});
    EXPECT_EQ(own[0], ReadTestData("dialects/masm11/MASM_SRC-C000.bin"));
    EXPECT_EQ(own[1], ReadTestData("dialects/masm11/MASM_SRC-6000.bin"));
}
