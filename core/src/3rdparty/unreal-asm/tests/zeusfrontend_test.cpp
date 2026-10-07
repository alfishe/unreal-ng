// The ZEUS frontend: ZEUS sources into the IR, written by the sjasmplus backend. Construct by construct, then the
// programs ZEUS itself assembled in unreal-ng (testdata/dialects/zeus: probes for ZEUS 1983, GG, ZEUS 1.1 (PHT) and
// ZEUS v7.E, the five Zeus Routines of ZXDB 19058 built by ZEUS 1983, ADS 2.0 built by ZEUS 1.1 in the PHT shell); with UNREAL_ASM_SJASMPLUS=<path to sjasmplus>
// the conversions are assembled and compared with those bytes. The labels of the ADS 2.0 project come through
// SymbolsFromProject equal to what sjasmplus 1.24 wrote with --sym for its conversion.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "codecs/zeus/zeuscodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"
#include "unrealasm/symbols/fromsource.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
/// The sjasmplus statements (blank and comment-only lines dropped, blanks trimmed) of a ZEUS source of a version
std::vector<std::string> ToSjasmplus(const std::string& zeus, const std::string& version = "1983", Diagnostics* diagnostics = nullptr)
{
    SourceDocument document = SourceDocument::FromText(zeus, "zeus");
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
        std::string text = l.text.substr(first);
        const size_t comment = text.find(" ;");
        if (comment != std::string::npos)
            text.resize(comment);
        while (!text.empty() && text.back() == ' ')
            text.pop_back();
        // "LABEL   LD A,1": one blank between the label and the statement
        const size_t gap = text.find("  ");
        if (gap != std::string::npos)
            text = text.substr(0, gap) + " " + text.substr(text.find_first_not_of(' ', gap));
        out.push_back(text);
    }
    return out;
}

using Lines = std::vector<std::string>;

/// A ZEUS file: hobeta (a disk version's file) or raw tape data (the Zeus Routines), decoded in the given version
SourceDocument Decode(const std::string& relative, const std::string& version)
{
    std::vector<uint8_t> bytes = ReadTestData(relative);
    DecodeOptions options;
    options.subversion = version;
    containers::TrdosFile file;
    std::string error;
    if (relative.find(".$") != std::string::npos && containers::ReadHobeta(bytes, file, error))
    {
        bytes = file.data;
        options.catalog = file.Hints();
    }
    return codecs::ZeusCodec().Decode(bytes, options).document;
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

struct Oracle
{
    const char* name;      ///< the expected bytes: dialects/zeus/<name>.bin
    const char* source;    ///< the main source (testdata path)
    const char* version;
    uint16_t address;
    std::vector<std::pair<std::string, std::string>> files;   ///< other project files: name, testdata path
    std::vector<std::pair<std::string, std::string>> binaries;   ///< INCBIN / PLACE files: name, testdata path (hobeta)
};
}  // namespace

TEST(ZeusFrontend_Test, StatementsCarryTheirOwnLabels)
{
    // Statements separated by ":", each with an optional label: a first word that is no keyword (manual 5.1)
    EXPECT_EQ(ToSjasmplus("START NOP:L2 NOP:LD A,1\nINIT EQU 8:FIND EQU #18"),
              (Lines{"START NOP", "L2 NOP", "LD A,1", "INIT EQU 8", "FIND EQU #18"}));
    // Only the table's words are keywords: INF, SLL and lower-case words are labels
    EXPECT_EQ(ToSjasmplus("INF CALL 0\nld NOP"), (Lines{"INF CALL 0", "ld NOP"}));
    // A label alone, ENT (the entry point, no code)
    EXPECT_EQ(ToSjasmplus("HERE\nGO ENT\n RET"), (Lines{"HERE", "GO", "RET"}));
}

TEST(ZeusFrontend_Test, ExpressionsRunLeftToRight)
{
    // No priorities (manual 5.3): 2+3&6 is (2+3)&6; "c is a character
    EXPECT_EQ(ToSjasmplus(" LD A,2+3&6\n LD HL,#FF00!#12\n LD C,\"A+1\n CP \":"),
              (Lines{"LD A,+((2+3)&6)", "LD HL,+(#FF00|#12)", "LD C,+('A'+1)", "CP ':'"}));
    // v7.E: * and / left to right on 16-bit words, %binary; the quotient rounded to the nearest, half down
    EXPECT_EQ(ToSjasmplus(" LD A,2+3*4\n LD C,%1010\n LD HL,3000/7", "pht"),
              (Lines{"LD A,+((2+3)*4)", "LD C,%1010",
                     "LD HL,+(((3000&#FFFF)/(7&#FFFF))+(-(((3000%7)&#FFFF)>(((7&#FFFF)/(2&#FFFF))&#FFFF))))"}));
    // V / NV are PE / PO
    EXPECT_EQ(ToSjasmplus(" JP V,0\n JP NV,0\n RET V"), (Lines{"JP PE,0", "JP PO,0", "RET PE"}));
}

TEST(ZeusFrontend_Test, DataDirectives)
{
    // DEFM /text/ with any delimiter; ":" and ";" inside are text; no closing delimiter: to the end of the line
    EXPECT_EQ(ToSjasmplus(" DEFM /AB;C:D/\n DEFM \"XYZ\"\n DEFM /TAIL\n DEFB 1,\"Z,2\n DEFW 1,2\n DEFS 3"),
              (Lines{"DB 'AB;C:D'", "DB 'XYZ'", "DB 'TAIL'", "DB 1,'Z',2", "DW 1,2", "DS 3"}));
    // GG / PHT / v7.E names
    EXPECT_EQ(ToSjasmplus(" DB 1:DW 2:DM /x/:DS 1", "pht"), (Lines{"DB 1", "DW 2", "DB 'x'", "DS 1"}));
    // In 1983 DB is no keyword: a label
    EXPECT_EQ(ToSjasmplus("DB NOP"), (Lines{"DB NOP"}));
}

TEST(ZeusFrontend_Test, DispIsAnOffsetFromOrg)
{
    // Manual 5.5: after ORG 30000 / DISP 10000 the code is put at 40000 and runs at 30000; a later ORG keeps the
    // offset; DISP 0 ends it
    EXPECT_EQ(ToSjasmplus(" ORG 30000\n DISP 10000\nA1 NOP\n ORG 31000\nA2 NOP\n DISP 0\nA3 NOP"),
              (Lines{"ORG 30000", "__UNREALASM_ZEUS0=$", "ORG (__UNREALASM_ZEUS0+10000)", "DISP __UNREALASM_ZEUS0", "A1 NOP",
                     "__UNREALASM_ZEUS1=31000", "ENT", "ORG (__UNREALASM_ZEUS1+10000)", "DISP __UNREALASM_ZEUS1", "A2 NOP",
                     "__UNREALASM_ZEUS2=$", "ENT", "ORG __UNREALASM_ZEUS2", "A3 NOP"}));
}

TEST(ZeusFrontend_Test, FilesOfTheDiskVersions)
{
    // PHT / v7.E: INCLUDE a source (type Z), PLACE a code file; GG: INCBIN
    EXPECT_EQ(ToSjasmplus(" INCLUDE cc0\nZN PLACE FONT$", "pht"), (Lines{"INCLUDE \"cc0.asm\"", "ZN INCBIN \"FONT$\""}));
    EXPECT_EQ(ToSjasmplus(" INCBIN data", "gg"), (Lines{"INCBIN \"data\""}));
}

TEST(ZeusFrontend_Test, ProgramsAssembleToWhatZeusBuilt)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    const std::vector<Oracle> oracles = {
        {"PROBE83", "dialects/zeus/PROBE83.$C", "1983", 40000, {}, {}},
        {"PROBE11", "dialects/zeus/PROBE11.$Z", "pht", 40000, {}, {}},
        {"PROBE7E", "dialects/zeus/PROBE7E.$Z", "pht", 40000, {}, {}},
        {"INCL7E", "dialects/zeus/INCL7E.$Z", "pht", 40000, {{"inc1", "dialects/zeus/inc1.$Z"}}, {{"dat", "dialects/zeus/dat.$C"}}},
        {"INCLGG", "dialects/zeus/INCLGG.$C", "gg", 40000, {}, {{"dat", "dialects/zeus/dat.$C"}}},
        // ZEUS 1.1 run from the PHT 3.6 shell (its INCLUDE / PLACE need the shell): the included source is type C
        {"INCL11", "dialects/zeus/INCL11.$C", "pht", 40000, {{"inc1", "dialects/zeus/inc11.$C"}}, {{"dat", "dialects/zeus/dat.$C"}}},
        // ADS 2.0 as ZEUS 1.1 (PHT shell, OPEN "adsobj": the code compiled to the disk) built it: 20155 bytes at #6000
        {"ADS20", "zeus/ADS20SRC__MAKE_ADS.bin", "pht", 0x6000,
         {{"cc0", "zeus/ADS20SRC__CC0.bin"}, {"cc1", "zeus/ADS20SRC__CC1.bin"}, {"cc2", "zeus/ADS20SRC__CC2.bin"}},
         {{"$ads", "dialects/zeus/ADSSCR.$C"}, {"FONT$", "dialects/zeus/ADSFONT.$C"}}},
        {"ZeusGlitter", "zeus/ZeusRoutines__ZeusGlitter.bin", "1983", 40000, {}, {}},
        {"ZeusMultiplot", "zeus/ZeusRoutines__ZeusMultiplot.bin", "1983", 50000, {}, {}},
        {"ZeusPrint", "zeus/ZeusRoutines__ZeusPrint.bin", "1983", 40000, {}, {}},
        {"ZeusScrolling", "zeus/ZeusRoutines__ZeusScrolling.bin", "1983", 40000, {}, {}},
        {"ZeusSelect", "zeus/ZeusRoutines__ZeusSelect.bin", "1983", 30000, {}, {}},
    };
    const codecs::SjasmplusCodec codec;
    for (const Oracle& o : oracles)
    {
        std::random_device random;
        const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                          ("unreal-asm-tests-zeus-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                                           std::to_string(random()));
        std::filesystem::create_directories(dir);
        std::vector<ProjectFile> project = {{"main", Decode(o.source, o.version)}};
        for (const auto& [name, path] : o.files)
            project.push_back({name, Decode(path, o.version)});
        const ProjectResult converted = ConvertProject(project, "sjasmplus");
        for (const ProjectFile& f : converted.files)
            WriteBytes(dir / (f.name + ".asm"), codec.Encode(f.document, {}).bytes);
        for (const auto& [name, path] : o.binaries)
        {
            containers::TrdosFile file;
            std::string error;
            ASSERT_TRUE(containers::ReadHobeta(ReadTestData(path), file, error)) << error;
            WriteBytes(dir / name, file.data);
        }
        const std::vector<uint8_t> expected = ReadTestData(std::string("dialects/zeus/") + o.name + ".bin");
        // The range filled with #AA first, as ZEUS's memory was (bytes ZEUS did not write stay #AA)
        std::ostringstream harness;
        harness << "        DEVICE ZXSPECTRUM48\n        ORG " << o.address << "\n        DS " << expected.size() << ",#AA\n        ORG 0\n"
                << "        INCLUDE \"main.asm\"\n        SAVEBIN \"out.bin\"," << o.address << "," << expected.size() << "\n";
        const std::string h = harness.str();
        WriteBytes(dir / "harness.asm", std::vector<uint8_t>(h.begin(), h.end()));
#ifdef _WIN32
        const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
        const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
        EXPECT_EQ(std::system(command.c_str()), 0) << o.name;
        const std::vector<uint8_t> built = ReadBytes(dir / "out.bin");
        ASSERT_EQ(built.size(), expected.size()) << o.name;
        for (size_t k = 0; k < expected.size(); ++k)
        {
            // DEFS / DS of ZEUS 1983 and GG leaves memory as it was; sjasmplus' DS writes zeros
            // (research-zeus-to-sjasmplus.md)
            if (expected[k] == 0xAA && built[k] == 0 && (std::string(o.version) == "1983" || std::string(o.version) == "gg"))
                continue;
            EXPECT_EQ(built[k], expected[k]) << o.name << " at " << (o.address + k);
        }
        std::filesystem::remove_all(dir);
    }
}

TEST(ZeusFrontend_Test, AdsLabelsComeThroughSymbolsFromProject)
{
    // The ADS 2.0 project: MAKE_ADS includes CC0-CC2, CC2 PLACEs the screen ($ads, 6912 bytes) and the font (FONT$, 768)
    std::vector<ProjectFile> project;
    for (const auto& [name, file] : std::vector<std::pair<std::string, std::string>>{
             {"make_ads", "MAKE_ADS"}, {"cc0", "CC0"}, {"cc1", "CC1"}, {"cc2", "CC2"}})
        project.push_back({name, Decode("zeus/ADS20SRC__" + file + ".bin", "pht")});
    symbols::SourceSymbolsOptions options;
    options.layout.fileSize = [](const std::string& name) -> std::optional<uint64_t> {
        if (name == "$ads")
            return 6912;
        if (name == "FONT$")
            return 768;
        return std::nullopt;
    };
    const symbols::SourceSymbolsResult r = symbols::SymbolsFromProject(project, 0, options);
    EXPECT_TRUE(r.ok);
    for (const Diagnostic& d : r.diagnostics)
        if (d.severity == Severity::Error)
            ADD_FAILURE() << d.message;
    std::map<std::string, uint32_t> sym;
    std::istringstream in(ReadTestText("symbols/fromsource/zeus-ADS20.sym"));
    std::string line;
    while (std::getline(in, line))
    {
        const size_t colon = line.find(": EQU 0x");
        if (colon != std::string::npos)
            sym[line.substr(0, colon)] = static_cast<uint32_t>(std::stoul(line.substr(colon + 8, 8), nullptr, 16));
    }
    size_t own = 0;
    for (const auto& [name, value] : sym)
        if (name.rfind("__UNREALASM", 0) != 0)
            ++own;
    EXPECT_EQ(r.set.symbols.size(), own);
    for (const symbols::Symbol& s : r.set.symbols)
    {
        const std::string written = s.provenance.type.rfind("written as ", 0) == 0 ? s.provenance.type.substr(11) : s.name;
        ASSERT_TRUE(sym.count(written)) << written;
        const auto address = symbols::CpuAddress(s);
        EXPECT_EQ(address ? *address : s.location.offset, sym.at(written)) << s.name;
        EXPECT_EQ(s.provenance.importer, "source-zeus");
    }
}

TEST(ZeusFrontend_Test, AnImageProjectTakesTheShortFileASourceIncludes)
{
    // inc1 (two lines) is too short for the codec detection; INCL11 INCLUDEs it, so the project reads it as ZEUS (PHT)
    std::vector<containers::TrdosFile> files;
    for (const char* path : {"dialects/zeus/INCL11.$C", "dialects/zeus/inc11.$C", "dialects/zeus/dat.$C"})
    {
        containers::TrdosFile file;
        std::string error;
        ASSERT_TRUE(containers::ReadHobeta(ReadTestData(path), file, error)) << error;
        files.push_back(file);
    }
    const std::vector<ProjectFile> project = ImageProject(files);
    ASSERT_EQ(project.size(), 2u);
    EXPECT_EQ(project[0].name, "INCL11");
    EXPECT_EQ(project[1].name, "inc1");
    EXPECT_EQ(project[1].document.dialect, "zeus");
    EXPECT_EQ(project[1].document.subversion, project[0].document.subversion);
    EXPECT_EQ(project[1].document.lines.size(), 2u);
}
