// The XAS frontend: XAS sources into the IR, written by the sjasmplus backend. Construct by construct, and three
// programs written for the test, saved as XAS files and assembled by XAS 7.447 and 4.18 in unreal-ng
// (testdata/dialects/xas7447: constrct and proj with the files proj loads, xas418: cons418, each with the bytes XAS
// built at #6000 over memory filled with #AA). With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the converted programs
// are assembled and compared with those bytes; the labels laid out without sjasmplus are checked against what
// sjasmplus 1.24 wrote with --sym.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "codecs/xas/xascodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"
#include "unrealasm/symbols/fromsource.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
/// The sjasmplus statements (blank and comment-only lines dropped, leading blanks trimmed) of an XAS source
std::vector<std::string> ToSjasmplus(const std::string& xas, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(xas, "xas"), "sjasmplus");
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

ProjectFile Source(const std::string& relative)
{
    const containers::TrdosFile file = Hobeta(relative);
    DecodeOptions options;
    options.catalog = file.Hints();
    return {file.TrimmedName(), codecs::XasCodec().Decode(file.data, options).document};
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

/// The project converted, assembled with sjasmplus over memory filled with #AA from #6000; `length` bytes from #6000
std::vector<uint8_t> AssembleConverted(const char* sjasmplus, const std::vector<ProjectFile>& project, const std::string& main,
                                       const std::map<std::string, std::vector<uint8_t>>& binaries, size_t length)
{
    const ProjectResult r = ConvertProject(project, "sjasmplus");
    EXPECT_TRUE(r.ok);
    std::random_device random;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    for (const ProjectFile& f : r.files)
        WriteBytes(dir / (f.name + ".asm"), codecs::SjasmplusCodec().Encode(f.document, {}).bytes);
    for (const auto& [name, bytes] : binaries)
        WriteBytes(dir / name, bytes);
    const std::string size = std::to_string(length);
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        ORG #6000\n        DS " + size + ",#AA\n        INCLUDE \"" + main +
                                ".asm\"\n        SAVEBIN \"out.bin\",#6000," + size + "\n";
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

std::map<std::string, std::vector<uint8_t>> ProjBinaries()
{
    return {{"dat", Hobeta("dialects/xas7447/dat.$C").data}, {"dat2.Z", Hobeta("dialects/xas7447/dat2.$Z").data}};
}
}  // namespace

TEST(XasFrontend_Test, ExpressionsRunLeftToRightOnUnsignedWords)
{
    // 2+3*4 is 20, 0-7/2 is #7FFC, ! is XOR, &L / &H and 'L / 'R act on the value so far (XAS 7.447)
    EXPECT_EQ(ToSjasmplus("        DW    2+3*4,0-7/2,LAB!.FF"),
              (std::vector<std::string>{"DW (2+3)*4,(0-7&#FFFF)/(2&#FFFF),LAB^#FF"}));
    EXPECT_EQ(ToSjasmplus("        DW    1+LAB&H,LAB&H&L"), (std::vector<std::string>{"DW high (1+LAB),low high LAB"}));
    EXPECT_EQ(ToSjasmplus("        DW    LAB'L"), (std::vector<std::string>{"DW (((LAB&#FFFF)<<1|(LAB&#FFFF)>>>(16-1))&#FFFF)"}));
    // 65536 is 0, a division by 0 is 0, "AB" is #4142
    EXPECT_EQ(ToSjasmplus("        DW    65536,LAB/0,\"AB\""), (std::vector<std::string>{"DW 0,0,16706"}));
    // No unary minus, no parentheses, no H suffix: XAS reports a syntax error, the line stays text
    Diagnostics d;
    ToSjasmplus("        DW    -5", &d);
    EXPECT_FALSE(d.empty());
}

TEST(XasFrontend_Test, Xas910PutsLeadingStringCharactersIntoTheCode)
{
    // XAS 9.10: LD HL,"AB" is #41, then LD HL,#42; DW "AB" is #41, then DW #42 (checked: dialects/xas910/s9)
    SourceDocument doc = SourceDocument::FromText("        LD    HL,\"AB\"\n        DW    \"AB\",1\n        LD    A,\"A\"", "xas");
    doc.subversion = "9.10";
    const ConvertResult r = Convert(doc, "sjasmplus");
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
        if (l.text.find_first_not_of(' ') != std::string::npos && l.text[l.text.find_first_not_of(' ')] != ';')
            out.push_back(l.text.substr(l.text.find_first_not_of(' ')));
    EXPECT_EQ(out, (std::vector<std::string>{"DB #41", "LD HL,'B'", "DB #41", "DW 'B'", "DW 1", "LD A,'A'"}));
    // 7.447 keeps the two characters as one word
    EXPECT_EQ(ToSjasmplus("        LD    HL,\"AB\""), (std::vector<std::string>{"LD HL,16706"}));
}

TEST(XasFrontend_Test, InstructionForms)
{
    EXPECT_EQ(ToSjasmplus("        EX    AF,AF"), (std::vector<std::string>{"EX AF,AF'"}));
    EXPECT_EQ(ToSjasmplus("        OUT   PORT,A\n        IN    A,PORT"), (std::vector<std::string>{"OUT (PORT),A", "IN A,(PORT)"}));
    EXPECT_EQ(ToSjasmplus("        PUSH  HL,IX,AF"), (std::vector<std::string>{"PUSH HL", "PUSH IX", "PUSH AF"}));
    EXPECT_EQ(ToSjasmplus("        LD    A,HX\n        LD    (IY-3),A"), (std::vector<std::string>{"LD A,IXH", "LD (IY+0-3),A"}));
}

TEST(XasFrontend_Test, Directives)
{
    // DS n,w repeats the word; a string in the command place is DM; $ in a list is each item's address
    EXPECT_EQ(ToSjasmplus("        DS    5,#AB12"), (std::vector<std::string>{"DUP 2", "DB low #AB12,high #AB12", "EDUP", "DB low #AB12"}));
    EXPECT_EQ(ToSjasmplus("        \"XY\""), (std::vector<std::string>{"DB 'XY'"}));
    EXPECT_EQ(ToSjasmplus("        DW    1,$"), (std::vector<std::string>{"DW 1", "DW $"}));
    // ENT only names the start address for Run; LTEXT / LCODE include a source / a code file
    EXPECT_EQ(ToSjasmplus("        ENT\n        LTEXT \"B:inc\"\n        LCODE \"dat2.Z\""),
              (std::vector<std::string>{"INCLUDE \"inc.asm\"", "INCBIN \"dat2.Z\""}));
}

TEST(XasFrontend_Test, OneBlockUpToCont)
{
    EXPECT_EQ(ToSjasmplus("        !ASSM 3\n        DB    1\n        !CONT"), (std::vector<std::string>{"DUP 3", "DB 1", "EDUP"}));
    EXPECT_EQ(ToSjasmplus("        !ASSM !ON\n        DB    1\n        !CONT"), (std::vector<std::string>{"DB 1"}));
    EXPECT_EQ(ToSjasmplus("        IFZ   X\n        DB    1\n        !CONT"), (std::vector<std::string>{"IF (X)==0", "DB 1", "ENDIF"}));
    // A block inside a block is ignored by XAS: its !CONT ends the outer one
    Diagnostics d;
    EXPECT_EQ(ToSjasmplus("        !ASSM 2\n        IFNZ  0\n        DB    6\n        !CONT\n        DB    7\n        !CONT", &d),
              (std::vector<std::string>{"DUP 2", "DB 6", "EDUP", "DB 7"}));
    EXPECT_FALSE(d.empty());
}

TEST(XasFrontend_Test, LabelsCompareOnSevenCharacters)
{
    // longlab is longlabelname (4.18 and 7.447 compare the first 7 characters)
    EXPECT_EQ(ToSjasmplus("longlabelname EQU 9\n        DB    longlab,longlabx"),
              (std::vector<std::string>{"longlabelname EQU 9", "DB longlabelname,longlabelname"}));
}

TEST(XasFrontend_Test, WorkIsADisplacementOrgKeeps)
{
    EXPECT_EQ(ToSjasmplus("        WORK  #9000\nw1      DW    w1\n        ORG   #6100\n        WORK"),
              (std::vector<std::string>{"__xas_work=#9000-$", "DISP #9000", "w1      DW w1", "ENT", "ORG #6100", "DISP #6100+__xas_work", "ENT"}));
}

TEST(XasFrontend_Test, ConstructsConvertAsExpected)
{
    const ProjectFile file = Source("dialects/xas7447/constrct.$X");
    const ConvertResult r = Convert(file.document, "sjasmplus");
    EXPECT_EQ(r.document.Text() + "\n", ReadTestText("dialects/xas7447/constrct.asm"));
}

TEST(XasFrontend_Test, ProgramsAssembleToWhatXasBuilt)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    EXPECT_EQ(AssembleConverted(sjasmplus, {Source("dialects/xas7447/constrct.$X")}, "constrct", {}, 0x110), ReadTestData("dialects/xas7447/constrct.bin"));
    EXPECT_EQ(AssembleConverted(sjasmplus, {Source("dialects/xas418/cons418.$X")}, "cons418", {}, 0x90), ReadTestData("dialects/xas418/cons418.bin"));
    // XAS 9.10: strings of several characters in LD / DW (the leading characters as bytes)
    EXPECT_EQ(AssembleConverted(sjasmplus, {Source("dialects/xas910/s9.$X")}, "s9", {}, 21), ReadTestData("dialects/xas910/s9.bin"));
    EXPECT_EQ(AssembleConverted(sjasmplus, {Source("dialects/xas7447/proj.$X"), Source("dialects/xas7447/inc.$X")}, "proj", ProjBinaries(), 0x20),
              ReadTestData("dialects/xas7447/proj.bin"));
}

TEST(XasFrontend_Test, LabelsComeWithTheValuesXasGaveThem)
{
    // proj: CALL incl, LTEXT "inc", LCODE "dat" (3 bytes), LCODE "dat2.Z" (2 bytes)
    symbols::SourceSymbolsOptions options;
    const auto binaries = ProjBinaries();
    options.layout.fileSize = [&binaries](const std::string& name) -> std::optional<uint64_t> {
        const auto found = binaries.find(name);
        return found == binaries.end() ? std::nullopt : std::optional<uint64_t>(found->second.size());
    };
    const symbols::SourceSymbolsResult r = symbols::SymbolsFromProject({Source("dialects/xas7447/proj.$X"), Source("dialects/xas7447/inc.$X")}, 0, options);
    EXPECT_TRUE(r.ok);
    std::map<std::string, symbols::Symbol> byName;
    for (const symbols::Symbol& s : r.set.symbols)
        byName[s.name] = s;
    EXPECT_EQ(byName["main"].location.offset, 0x6000u);
    EXPECT_EQ(byName["incl"].location.offset, 0x6003u);
    EXPECT_EQ(byName["incl"].source.file, "inc");
    EXPECT_EQ(byName["incv"].location.offset, 0x42u);
    EXPECT_EQ(byName["incv"].kind, symbols::SymbolKind::Const);
    EXPECT_EQ(byName["dat"].location.offset, 0x6006u);
    EXPECT_EQ(byName["dat2"].location.offset, 0x6009u);
    EXPECT_EQ(byName["dat2"].kind, symbols::SymbolKind::Data);

    // constrct: every label as sjasmplus --sym gave it for the conversion (WORK and ORG under WORK included)
    const symbols::SourceSymbolsResult c = symbols::SymbolsFromProject({Source("dialects/xas7447/constrct.$X")}, 0, {});
    EXPECT_TRUE(c.ok);
    std::map<std::string, uint32_t> laid;
    for (const symbols::Symbol& s : c.set.symbols)
        laid[s.name] = s.location.offset;
    std::istringstream sym(ReadTestText("symbols/fromsource/xas7447-constrct.sym"));
    std::string line;
    size_t compared = 0;
    while (std::getline(sym, line))
    {
        const size_t colon = line.find(": EQU 0x");
        if (colon == std::string::npos || line.rfind("__", 0) == 0)
            continue;
        const std::string name = line.substr(0, colon);
        ASSERT_TRUE(laid.count(name)) << name;
        EXPECT_EQ(laid[name], std::stoul(line.substr(colon + 8, 8), nullptr, 16)) << name;
        ++compared;
    }
    EXPECT_EQ(compared, 8u);
}
