// Labels from sources: the layout (src/layout) against what sjasmplus 1.24 wrote with --sym for the same text
// (testdata/symbols/fromsource): every Z80 instruction form, the layout rules (probe.asm), and the sjasmplus conversions
// of five projects whose bytes equal what the original assemblers built (the General Sound ROM in TASM 4.0, The Link
// in ALASM, STORM 1.3, ZAsm 3.15 and TASM 4.12 programs). Then SymbolsFromProject gives the labels back their names
// in the source.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <sstream>

#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/layout.h"
#include "unrealasm/registry.h"
#include "unrealasm/symbols/fromsource.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;
using unrealasm::testing::TestDataPath;

namespace
{
/// sjasmplus --sym: NAME: EQU 0x0000HHHH
std::map<std::string, uint32_t> ReadSym(const std::string& relative)
{
    std::map<std::string, uint32_t> out;
    std::istringstream in(ReadTestText(relative));
    std::string line;
    while (std::getline(in, line))
    {
        const size_t colon = line.find(": EQU 0x");
        if (colon == std::string::npos)
            continue;
        out[line.substr(0, colon)] = static_cast<uint32_t>(std::stoul(line.substr(colon + 8, 8), nullptr, 16));
    }
    return out;
}

std::map<std::string, uint32_t> Laid(const layout::LayoutResult& r)
{
    std::map<std::string, uint32_t> out;
    for (const layout::Label& l : r.labels)
        out[l.name] = static_cast<uint32_t>(static_cast<uint64_t>(l.value));
    return out;
}

void ExpectSameLabels(const std::map<std::string, uint32_t>& laid, const std::map<std::string, uint32_t>& sym, const std::string& what)
{
    for (const auto& [name, value] : sym)
    {
        const auto found = laid.find(name);
        if (found == laid.end())
        {
            ADD_FAILURE() << what << ": " << name << " not laid out";
            continue;
        }
        EXPECT_EQ(found->second, value) << what << ": " << name;
    }
    for (const auto& [name, value] : laid)
        EXPECT_TRUE(sym.count(name)) << what << ": " << name << " is no sjasmplus label";
}

ProjectFile TextFile(const std::string& name, const std::string& relative)
{
    return {name, SourceDocument::FromText(ReadTestText(relative), "sjasmplus")};
}

std::string Lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// A folder of hobeta files as an assembler's disk: the sources (decoded by the codec detection picks) and the sizes of
/// the other files for INCBIN, as the converted text names them (NAME for type C, NAME.T otherwise, NAME.slack for
/// the rest of the last sector)
struct Disk
{
    std::vector<ProjectFile> sources;
    std::map<std::string, uint64_t> sizes;

    explicit Disk(const std::string& folder)
    {
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(TestDataPath(folder)))
            if (entry.path().filename().string().find(".$") != std::string::npos)
                paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths)
        {
            containers::TrdosFile file;
            std::string error;
            if (!containers::ReadHobeta(ReadTestData(folder + "/" + path.filename().string()), file, error))
            {
                ADD_FAILURE() << path << ": " << error;
                continue;
            }
            const std::string name = file.TrimmedName() + (file.type == 'C' ? std::string() : std::string(".") + file.type);
            sizes[Lower(name)] = file.data.size();
            sizes[Lower(file.TrimmedName() + "." + std::string(1, file.type))] = file.data.size();
            sizes[Lower(name + ".slack")] = file.tail.size();
            const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
            if (detected.chosen && detected.chosen->Info().family == CodecFamily::Tokenized)
            {
                DecodeOptions options;
                options.catalog = file.Hints();
                sources.push_back({file.TrimmedName(), detected.chosen->Decode(file.data, options).document});
            }
        }
    }

    size_t Main(const std::string& name) const
    {
        for (size_t k = 0; k < sources.size(); ++k)
            if (sources[k].name == name)
                return k;
        ADD_FAILURE() << "no source " << name;
        return 0;
    }

    layout::LayoutOptions Options() const
    {
        layout::LayoutOptions options;
        options.fileSize = [this](const std::string& name) -> std::optional<uint64_t> {
            const auto found = sizes.find(Lower(name));
            if (found == sizes.end())
                return std::nullopt;
            return found->second;
        };
        return options;
    }
};

struct ProjectCase
{
    const char* folder;
    const char* main;
};

constexpr ProjectCase kProjects[] = {
    {"gs104", "MAIN"},      {"thelink", "GSTUNNE4"}, {"storm13", "STORMT1"}, {"storm13", "STORMT2"}, {"zasm315", "ZXT1"},
    {"zasm315", "ZXT2"},    {"zasm315", "ZXT3"},     {"zasm315", "ZXT4"},    {"tasm412", "SIN7"},
};
}  // namespace

TEST(FromSource_Test, EveryInstructionFormTakesTheBytesSjasmplusGaveIt)
{
    const layout::LayoutResult r = layout::Layout({TextFile("instructions", "symbols/fromsource/instructions.asm")}, 0);
    EXPECT_TRUE(r.ok);
    for (const Diagnostic& d : r.diagnostics)
        ADD_FAILURE() << d.message;
    ExpectSameLabels(Laid(r), ReadSym("symbols/fromsource/instructions.sym"), "instructions");
}

TEST(FromSource_Test, LayoutRulesMatchSjasmplus)
{
    // Locals after EQU / DEFL / @labels, DEFL's last value, macro arguments inside names, temporary labels, MODULE,
    // DISP, IF / IFDEF / WHILE / DUP, INCLUDE, INCBIN with offset and length, ALIGN, DZ / DC / DD, 32-bit arithmetic
    layout::LayoutOptions options;
    options.fileSize = [](const std::string& name) -> std::optional<uint64_t> {
        if (name == "probe.bin")
            return ReadTestData("symbols/fromsource/probe.bin").size();
        return std::nullopt;
    };
    const layout::LayoutResult r = layout::Layout(
        {TextFile("probe", "symbols/fromsource/probe.asm"), TextFile("probepart", "symbols/fromsource/probepart.asm")}, 0, options);
    EXPECT_TRUE(r.ok);
    for (const Diagnostic& d : r.diagnostics)
        ADD_FAILURE() << d.message;
    ExpectSameLabels(Laid(r), ReadSym("symbols/fromsource/probe.sym"), "probe");

    std::map<std::string, layout::Label> byName;
    for (const layout::Label& l : r.labels)
        byName[l.name] = l;
    EXPECT_EQ(byName["main"].use, layout::LabelUse::Code);
    EXPECT_EQ(byName["inbin"].use, layout::LabelUse::Data);
    EXPECT_EQ(byName["CONST"].use, layout::LabelUse::Equ);
    EXPECT_EQ(byName["var"].use, layout::LabelUse::Defl);
    EXPECT_EQ(byName["main.loop"].parent, "main");
    EXPECT_EQ(byName["mod.inmod"].module, "mod");
    EXPECT_EQ(byName["part"].file, "probepart");
    EXPECT_EQ(byName["part"].line, 1u);
}

TEST(FromSource_Test, ConvertedProjectsLayOutAsSjasmplusAssemblesThem)
{
    for (const ProjectCase& c : kProjects)
    {
        const Disk disk(std::string("dialects/") + c.folder);
        const ProjectResult converted = ConvertProject(disk.sources, "sjasmplus");
        const layout::LayoutResult r = layout::Layout(converted.files, disk.Main(c.main), disk.Options());
        const std::string what = std::string(c.folder) + "/" + c.main;
        for (const Diagnostic& d : r.diagnostics)
            if (d.severity == Severity::Error)
                ADD_FAILURE() << what << ": " << d.message;
        EXPECT_TRUE(r.ok) << what;
        ExpectSameLabels(Laid(r), ReadSym(std::string("symbols/fromsource/") + c.folder + "-" + c.main + ".sym"), what);
    }
}

TEST(FromSource_Test, SymbolsKeepTheSourceNamesAndLines)
{
    const Disk disk("dialects/thelink");
    symbols::SourceSymbolsOptions options;
    options.layout = disk.Options();
    const symbols::SourceSymbolsResult r = symbols::SymbolsFromProject(disk.sources, disk.Main("GSTUNNE4"), options);
    EXPECT_TRUE(r.ok);
    for (const Diagnostic& d : r.diagnostics)
        if (d.severity == Severity::Error)
            ADD_FAILURE() << d.message;
    std::map<std::string, std::vector<symbols::Symbol>> byName;
    for (const symbols::Symbol& s : r.set.symbols)
        byName[s.name].push_back(s);
    const std::map<std::string, uint32_t> sym = ReadSym("symbols/fromsource/thelink-GSTUNNE4.sym");
    // A LOCAL block's label: sjasmplus knows it as Hloop__L1, the source as Hloop
    ASSERT_EQ(byName["Hloop"].size(), 1u);
    const symbols::Symbol& hloop = byName["Hloop"][0];
    EXPECT_EQ(hloop.kind, symbols::SymbolKind::Local);
    EXPECT_EQ(symbols::CpuAddress(hloop), sym.at("Hloop__L1"));
    EXPECT_EQ(hloop.location.space.kind, symbols::SpaceKind::Ram);   // ORG #C000,page: the page ALASM put it in
    EXPECT_EQ(hloop.provenance.importer, "source-alasm");
    EXPECT_EQ(hloop.provenance.type, "written as Hloop__L1");
    EXPECT_EQ(hloop.source.file, "GSTUNNE4");
    EXPECT_NE(hloop.provenance.raw.find("Hloop"), std::string::npos);
    // Labels of an INCLUDEd file carry its name; nothing the conversion added is a symbol
    bool fromPorts = false;
    for (const symbols::Symbol& s : r.set.symbols)
    {
        fromPorts = fromPorts || s.source.file == "gsports";
        EXPECT_EQ(s.name.rfind("__UNREALASM", 0), std::string::npos) << s.name;
        EXPECT_GT(s.source.line, 0u) << s.name;
    }
    EXPECT_TRUE(fromPorts);
    // Every label sjasmplus wrote is a symbol under its source name with the value sjasmplus gave it, except what the
    // conversion added
    size_t named = 0;
    for (const auto& [name, value] : sym)
        if (name.rfind("__UNREALASM", 0) != 0)
            ++named;
    EXPECT_EQ(r.set.symbols.size(), named);
    for (const symbols::Symbol& s : r.set.symbols)
    {
        const std::string written = s.provenance.type.rfind("written as ", 0) == 0 ? s.provenance.type.substr(11) : s.name;
        ASSERT_TRUE(sym.count(written)) << written;
        const auto address = symbols::CpuAddress(s);
        EXPECT_EQ(address ? *address : s.location.offset, sym.at(written)) << s.name;
    }
}

TEST(FromSource_Test, SjasmplusSourceGivesFullNames)
{
    const symbols::SourceSymbolsResult r = symbols::SymbolsFromSource(SourceDocument::FromText(
        "        ORG #C000\nmain    nop\n.loop   jr .loop\nlen     EQU $-main\ntab     DB 1,2,3\n", "sjasmplus"));
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.set.symbols.size(), 4u);
    EXPECT_EQ(r.set.symbols[0].name, "main");
    EXPECT_EQ(r.set.symbols[0].kind, symbols::SymbolKind::Code);
    EXPECT_EQ(r.set.symbols[0].location.offset, 0xC000u);
    EXPECT_EQ(r.set.symbols[1].name, "main.loop");
    EXPECT_EQ(r.set.symbols[1].kind, symbols::SymbolKind::Local);
    EXPECT_EQ(r.set.symbols[1].parent, "main");
    EXPECT_EQ(r.set.symbols[2].name, "len");
    EXPECT_EQ(r.set.symbols[2].kind, symbols::SymbolKind::Const);
    EXPECT_EQ(r.set.symbols[2].location.offset, 3u);
    EXPECT_EQ(r.set.symbols[3].kind, symbols::SymbolKind::Data);
    EXPECT_EQ(r.set.symbols[3].source.line, 5u);
}

TEST(FromSource_Test, ForwardReferencesSettleAndUnknownNamesAreReported)
{
    const layout::LayoutResult r = layout::Layout(
        {{"f", SourceDocument::FromText("        ORG size\nstart   ds later-start\nlater   nop\nsize    EQU #100\n        ld a,nowhere\n", "sjasmplus")}}, 0);
    EXPECT_FALSE(r.ok);
    std::map<std::string, int64_t> v;
    for (const layout::Label& l : r.labels)
        v[l.name] = l.value;
    EXPECT_EQ(v["start"], 0x100);
    EXPECT_EQ(v["later"], 0x100);   // DS of 0 bytes: the value stable from pass 2
    bool reported = false;
    for (const Diagnostic& d : r.diagnostics)
        reported = reported || d.message.find("unknown symbol nowhere") != std::string::npos;
    EXPECT_TRUE(reported);
}

TEST(FromSource_Test, StringsTakeTheirBytesInTheFilesCodePage)
{
    // A converted source keeps the Spectrum's code page: one byte a letter, as sjasmplus reads the written file; a
    // character the code page lacks is written as '?'
    SourceDocument document = SourceDocument::FromText("        ORG #8000\n        DB 'Привет'\nafter   DB \"€x\"\nend\n", "sjasmplus");
    document.codePage = encoding::CodePage::Cp866;
    const layout::LayoutResult r = layout::Layout({{"cp", document}}, 0);
    EXPECT_TRUE(r.ok);
    std::map<std::string, int64_t> v;
    for (const layout::Label& l : r.labels)
        v[l.name] = l.value;
    EXPECT_EQ(v["after"], 0x8006);
    EXPECT_EQ(v["end"], 0x8008);
}

TEST(FromSource_Test, AProjectFromAnImagesFilesGivesTheSameLabels)
{
    // The files of the disk as an image reader gives them: the project, its main source picked, the labels as the test
    // project above (the INCBIN sizes from the same files)
    std::vector<containers::TrdosFile> files;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(TestDataPath("dialects/thelink")))
        if (entry.path().filename().string().find(".$") != std::string::npos)
            paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths)
    {
        containers::TrdosFile file;
        std::string error;
        ASSERT_TRUE(containers::ReadHobeta(ReadTestData("dialects/thelink/" + path.filename().string()), file, error)) << error;
        files.push_back(file);
    }
    const symbols::SourceProject project = symbols::ProjectFromFiles(files);
    std::string error;
    EXPECT_EQ(symbols::FindMainSource(project, "", error), project.sources.size());
    EXPECT_NE(error.find("GSTUNNE4"), std::string::npos) << "the sources are listed";
    EXPECT_EQ(symbols::FindMainSource(project, "NOSUCH", error), project.sources.size());
    const size_t main = symbols::FindMainSource(project, "GSTUNNE4", error);
    ASSERT_LT(main, project.sources.size());
    const symbols::SourceSymbolsResult viaProject = symbols::SymbolsFromSourceProject(project, main);

    const Disk disk("dialects/thelink");
    symbols::SourceSymbolsOptions options;
    options.layout = disk.Options();
    const symbols::SourceSymbolsResult direct = symbols::SymbolsFromProject(disk.sources, disk.Main("GSTUNNE4"), options);
    EXPECT_TRUE(viaProject.ok);
    ASSERT_EQ(viaProject.set.symbols.size(), direct.set.symbols.size());
    for (size_t i = 0; i < direct.set.symbols.size(); ++i)
    {
        EXPECT_EQ(viaProject.set.symbols[i].name, direct.set.symbols[i].name);
        EXPECT_EQ(viaProject.set.symbols[i].location, direct.set.symbols[i].location) << direct.set.symbols[i].name;
    }

    // A text source is a project of one
    const std::string text = "start: ld a,1\nloop: jr loop\n";
    const symbols::SourceProject one = symbols::ProjectFromText("dir/game.asm", std::vector<uint8_t>(text.begin(), text.end()));
    ASSERT_EQ(one.sources.size(), 1u);
    EXPECT_EQ(one.sources[0].name, "game");
    EXPECT_EQ(symbols::FindMainSource(one, "", error), 0u);
}
