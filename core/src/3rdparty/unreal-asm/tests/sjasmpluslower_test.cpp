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

TEST(SjasmplusLower_Test, AStructureAliasAndTheNewerSyntax)
{
    // sjasmplus 1.21's --sym: an alias (label STRUCT = address) gives the label and its fields, no bytes; 1_B / 1_F
    // reach temporary labels in any expression; `,,` separates instructions (--syntax=a)
    const std::string text = "        ORG #6000\n1       nop\n        ld hl,1_B,,de,1_F\n1       nop\n        STRUCT st\nx       BYTE 1\n"
                             "y       WORD 2\n        ENDS\nins     st\naka     st = ins+1\nend     nop\n";
    const layout::LayoutResult laid = layout::Layout({{"probe", SourceDocument::FromText(text, "sjasmplus")}}, 0);
    EXPECT_TRUE(laid.ok);
    std::map<std::string, int64_t> values;
    for (const layout::Label& l : laid.labels)
        values[l.name] = l.value;
    for (const auto& [name, value] : std::map<std::string, int64_t>{{"ins", 0x6008}, {"aka", 0x6009}, {"aka.x", 0x6009}, {"aka.y", 0x600A}, {"end", 0x600B}})
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

TEST(SjasmplusLower_Test, SjasmplusSyntaxTheTargetsSpellOut)
{
    // an instruction repeated for its operands, label+N / label+* (smart SMC), !label (not a parent), OUT n,A, 1000b
    // outside a jump, a macro on one line, a Z80N instruction as bytes, a jump to a label named like a register, ALIGN in
    // a displacement (the displaced address aligned)
    const std::string source = "        DEVICE ZXSPECTRUMNEXT\n"
                               "        ORG #8000\n"
                               "start   ld a,1,b,2\n"
                               "        push af,bc\n"
                               ".v+1    ld a,0\n"
                               ".w+*    ld hl,0\n"
                               "!keep   nop\n"
                               ".loc    jr .loc\n"
                               "        out #FE,a\n"
                               "X       EQU 1000b\n"
                               "ESX     MACRO s? : rst 8 : db s? : ENDM\n"
                               "        ESX 5\n"
                               "        nextreg $57,18\n"
                               "        djnz de\n"
                               "de      nop\n"
                               "        DISP #9001\n"
                               "        nop\n"
                               "        ALIGN 4\n"
                               "        ENT\n";
    Diagnostics diagnostics;
    const std::vector<std::string> lines = Lines(source, "tasm", &diagnostics, "4.12");
    const std::string text = Joined(lines);
    for (const char* expected : {"START LD A,1", "LD B,2", "PUSH AF", "PUSH BC", "START_V EQU $+1", "START_W EQU $+1", "KEEP NOP",
                                 "START_LOC JR START_LOC", "OUT (#FE),A", "X EQU %1000", "RST 8", "DEFB 5", "DEFB #ED,#91,87,18 ;NEXTREG #57,18",
                                 "DJNZ DE1", "DE1 NOP", "DEFS 2"})
        EXPECT_TRUE(Contains(lines, expected)) << expected << "\n" << text;
    for (const Diagnostic& d : diagnostics)
        EXPECT_EQ(d.severity, Severity::Info) << d.line << ": " << d.message;
}

namespace
{
ProjectFile Sjasmplus(const std::string& name, const std::string& text)
{
    SourceDocument d = SourceDocument::FromText(text, "sjasmplus");
    d.name = name;
    return {name, d};
}

std::string FileText(const ProjectResult& r, const std::string& name)
{
    for (const ProjectFile& f : r.files)
        if (f.name == name)
            return f.document.Text();
    return "(no " + name + ")";
}
}  // namespace

TEST(SjasmplusLower_Test, AProjectIsWrittenAsSjasmplusReadsIt)
{
    // the INCLUDEd file's macro reassigns a name the main file assigned (DEFL across files), a module's names are used
    // in the other file: the project written as one text and cut into its files again
    const std::vector<ProjectFile> project = {
        Sjasmplus("main", "        ORG #8000\nVORG = #8010\n        INCLUDE \"traps.asm\"\n        JP VORG\n        JP tools.go\n"),
        Sjasmplus("traps", "        MACRO T\n        JP VORG\nVORG = $\n        ENDM\n        T\n        T\n        MODULE tools\ngo      RET\n        ENDMODULE\n"),
    };
    BackendOptions options;
    options.targetVersion = "4.12";
    const ProjectResult r = ConvertProject(project, "tasm", options);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.files.size(), 2u);
    const std::string main = FileText(r, "main"), traps = FileText(r, "traps");
    EXPECT_NE(main.find(".INCLUDE traps"), std::string::npos) << main;
    // VORG's values one after another: the main file's, then each expansion's own (a name per definition), the last one
    // back in the main file
    EXPECT_NE(traps.find("JP      #8010\nVORG    EQU     $\n        JP      VORG\nVORG1   EQU     $"), std::string::npos) << traps;
    EXPECT_NE(main.find("JP      VORG1"), std::string::npos) << main;
    EXPECT_NE(main.find("JP      TOOLS_GO"), std::string::npos) << main;
    EXPECT_NE(traps.find("TOOLS_GO RET"), std::string::npos) << traps;
    EXPECT_EQ((main + traps).find("UNREALASM@"), std::string::npos);
}

TEST(SjasmplusLower_Test, ALongFileIsCutIntoIncludedParts)
{
    // splitLines: the parts at lines outside any block, each INCLUDEd where it stood, named for the disk
    std::string text = "        ORG #8000\n";
    for (int k = 0; k < 6; ++k)
        text += "        NOP\n";
    text += "        DUP 2\n        NOP\n        NOP\n        EDUP\n";
    for (int k = 0; k < 6; ++k)
        text += "        NOP\n";
    const std::vector<ProjectFile> project = {Sjasmplus("big", text)};
    BackendOptions options;
    options.targetVersion = "4.12";
    options.fileNames = ProjectDiskNames(project, {});
    options.splitLines = 4;
    const ProjectResult r = ConvertProject(project, "tasm", options);
    ASSERT_TRUE(r.ok);
    ASSERT_GE(r.files.size(), 3u);
    const std::string head = FileText(r, "big");
    EXPECT_NE(head.find(".INCLUDE big2"), std::string::npos) << head;
    EXPECT_NE(head.find(".INCLUDE big3"), std::string::npos) << head;
    size_t nops = 0;
    for (const ProjectFile& f : r.files)
        for (const SourceLine& l : f.document.lines)
        {
            nops += l.text.find("NOP") != std::string::npos;
            // the repeat is unrolled by TASM's writer: no cut fell inside it, so no part begins with its second NOP
        }
    EXPECT_EQ(nops, 16u);
}

TEST(SjasmplusLower_Test, DiskNamesAreShortAndUniqueInLettersAndDigits)
{
    const std::vector<ProjectFile> project = {Sjasmplus("zifi_sts", ""), Sjasmplus("dss.inc", ""), Sjasmplus("_spg/dss", "")};
    const std::map<std::string, std::string> names = ProjectDiskNames(project, {"_spg/download.tga.pix", "_spg/menu.tga.pal", "_spg/menu.tga.pix"});
    EXPECT_EQ(names.at("zifi_sts"), "zifi_sts");
    EXPECT_EQ(names.at("dss.inc"), "dss");
    EXPECT_EQ(names.at("_spg/dss"), "dss2");
    EXPECT_EQ(names.at("_spg/download.tga.pix"), "download");
    EXPECT_EQ(names.at("_spg/menu.tga.pal"), "menu");
    EXPECT_EQ(names.at("_spg/menu.tga.pix"), "menu2");
}

TEST(SjasmplusLower_Test, TheRelocationTableAsSjasmplusBuildsIt)
{
    // RELOCATE_START ... RELOCATE_END: the words that move with the block (a label of it, $), not a difference of two
    // of them, not norel; RELOCATE_TABLE their addresses, relocate_count their number (checked against sjasmplus'
    // tests/relocate: the same tables)
    const std::string source = "        ORG #8000\n"
                               "        RELOCATE_START\n"
                               "start   ld hl,data\n"
                               "        jp start\n"
                               "        ld de,data-start\n"
                               "        ld bc,norel data\n"
                               "data    dw start\n"
                               "        RELOCATE_END\n"
                               "        RELOCATE_TABLE\n"
                               "        dw relocate_count\n";
    const std::vector<std::string> lines = Lines(source, "tasm", nullptr, "4.12");
    EXPECT_TRUE(Contains(lines, "DEFW #8001,#8004,#800C")) << Joined(lines);
    EXPECT_TRUE(Contains(lines, "DEFW 3")) << Joined(lines);
    EXPECT_TRUE(Contains(lines, "LD BC,DATA")) << Joined(lines);
}

TEST(SjasmplusLower_Test, AConditionOnMemoryIsWrittenAsTaken)
{
    // IF {addr} reads memory while assembling, which no target does: its block is written, as pasmo's IF 1 does
    const std::vector<std::string> lines = Lines("        ORG #8000\n        IF {#8000}\n        nop\n        ENDIF\n", "tasm", nullptr, "4.12");
    EXPECT_TRUE(Contains(lines, "NOP")) << Joined(lines);
    EXPECT_FALSE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find(".IF") != std::string::npos; })) << Joined(lines);
}

TEST(SjasmplusLower_Test, ALasmGetsAMacroOfLocalAndGlobalLabelsPutInAtItsCalls)
{
    // ALASM's LOCAL block would make VORG local too (its @name is another name): the macro put in at each call, its
    // .label named for each expansion, its temporary label one per line, VORG the one global name
    const std::string source = "        ORG #8000\n"
                               "VORG = #8100\n"
                               "        MACRO TRAP addr\n"
                               "        JP VORG\n"
                               "1       DJNZ 1b\n"
                               ".l      JR .l\n"
                               "VORG = $\n"
                               "        ENDM\n"
                               "        TRAP 1\n"
                               "        TRAP 2\n"
                               "        JP VORG\n";
    const std::vector<std::string> lines = Lines(source, "alasm");
    const std::string text = Joined(lines);
    EXPECT_EQ(text.find("LOCAL"), std::string::npos) << text;
    EXPECT_EQ(std::count(lines.begin(), lines.end(), std::string("VORG=$")), 2) << text;
    EXPECT_TRUE(Contains(lines, "__M1_l JR __M1_l")) << text;
    EXPECT_TRUE(Contains(lines, "__M2_l JR __M2_l")) << text;
}

TEST(SjasmplusLower_Test, ASourceOfAnotherDialectGivesItsLabelValuesToo)
{
    // a TASM source into PASM, whose EQU sees only the names above it: LBL's value from the TASM source's layout (its
    // sjasmplus conversion), as for a sjasmplus source
    const SourceDocument source = SourceDocument::FromText("        ORG     #8000\nX       EQU     LBL+1\n        LD      HL,X\n        NOP\nLBL     NOP\n", "tasm");
    const ConvertResult r = Convert(source, "pasm");
    ASSERT_TRUE(r.ok);
    EXPECT_NE(r.document.Text().find("X\tEQU #8004+1"), std::string::npos) << r.document.Text();
}

/// What the real projects showed on the 48K and other targets (research-sjasmplus-projects.md §6, 2026-10-17)
TEST(SjasmplusLower_Test, TheRealProjectsOnEveryTargetShowedThese)
{
    // sjasmplus reads EX HL,DE as EX DE,HL and an ALU operation's or LD r's (n) as the number (no such Z80 forms)
    const std::vector<std::string> tasm = Lines("        ORG #8000\n        ex hl,de\n        add a,(5)\n        ld h,(#1234/256)\n", "tasm");
    EXPECT_TRUE(Contains(tasm, "EX DE,HL")) << Joined(tasm);
    EXPECT_TRUE(Contains(tasm, "ADD A,5")) << Joined(tasm);
    EXPECT_TRUE(Contains(tasm, "LD H,#1234/256")) << Joined(tasm);

    // GENS: *F from the line's first column; ^ and ` (the Spectrum's arrow and pound) in a string as numbers
    BackendOptions gens;
    gens.targetVersion = "2";
    const ConvertResult included = Convert(SourceDocument::FromText("        ORG #8000\n        INCLUDE \"lib.asm\"\n        DB \"a^b\"\n", "sjasmplus"), "gens", gens);
    ASSERT_TRUE(included.ok);
    EXPECT_TRUE(std::any_of(included.document.lines.begin(), included.document.lines.end(), [](const SourceLine& l) { return l.text == "*F lib"; }));
    EXPECT_TRUE(std::any_of(included.document.lines.begin(), included.document.lines.end(),
                            [](const SourceLine& l) { return l.text.find("97,94,98") != std::string::npos; }));

    // a target without INCBIN gets the file's bytes when the project holds the file
    BackendOptions withData;
    withData.targetVersion = "2";
    withData.dataFiles = [](const std::string& name) { return name == "d.bin" ? std::vector<uint8_t>{1, 2, 3} : std::vector<uint8_t>{}; };
    const ConvertResult data = Convert(SourceDocument::FromText("        ORG #8000\n        INCBIN \"d.bin\",1\n", "sjasmplus"), "gens", withData);
    ASSERT_TRUE(data.ok);
    EXPECT_TRUE(std::any_of(data.document.lines.begin(), data.document.lines.end(), [](const SourceLine& l) { return l.text.find("DEFB 2,3") != std::string::npos; }));

    // ZEUS 1983 has no INCLUDE: the project stays one file, the INCLUDEd text where its INCLUDE was
    std::vector<ProjectFile> project = {{"main", SourceDocument::FromText("        ORG #8000\n        INCLUDE \"lib.asm\"\n        RET\n", "sjasmplus")},
                                        {"lib", SourceDocument::FromText("LIBRARY NOP\n", "sjasmplus")}};
    BackendOptions zeus;
    zeus.targetVersion = "1983";
    const ProjectResult joined = ConvertProject(project, "zeus", zeus);
    ASSERT_TRUE(joined.ok);
    ASSERT_EQ(joined.files.size(), 2u);
    std::string main;
    for (const SourceLine& l : joined.files[0].document.lines)
        main += l.text + "\n";
    EXPECT_NE(main.find("LIBRARY"), std::string::npos) << main;
    for (const SourceLine& l : joined.files[0].document.lines)
        if (l.text.find("INCLUDE") != std::string::npos)
            EXPECT_EQ(l.text.find_first_not_of(" \t"), l.text.find(';')) << "only as a comment: " << l.text;
    ASSERT_EQ(joined.files[1].document.lines.size(), 1u);
    EXPECT_EQ(joined.files[1].document.lines[0].text[0], ';');

    // ZEUS: a DM delimited by " holds a : (ZEUS' statement separator elsewhere)
    SourceDocument text = SourceDocument::FromText("        ORG #8000\nT       DM \"http://z\"\n", "zeus");
    text.subversion = "pht";
    const ConvertResult back = Convert(text, "sjasmplus");
    ASSERT_TRUE(back.ok);
    std::string all;
    for (const SourceLine& l : back.document.lines)
        all += l.text + "\n";
    EXPECT_NE(all.find("http://z"), std::string::npos) << all;
    EXPECT_EQ(all.find("unparsed"), std::string::npos) << all;

    // ZX-ASM: an index offset made with a 16-bit function is its low byte ((IY+100.n+5) is IY-95)
    const ConvertResult zx = Convert(SourceDocument::FromText("        ORG #8000\n        LD (IY+100.n+5),A\n", "zxasm"), "sjasmplus");
    ASSERT_TRUE(zx.ok);
    std::string zxText;
    for (const SourceLine& l : zx.document.lines)
        zxText += l.text + "\n";
    EXPECT_NE(zxText.find("+#80)&#FF)-#80"), std::string::npos) << zxText;

    // back to sjasmplus, a SAVEBIN alone gets the plain 64K device (no screen attributes, no system variables)
    const ConvertResult saved = Convert(SourceDocument::FromText("        ORG #8000\nS       NOP\n        SAVEOBJ \"s.bin\",S,1\n", "zxasm"), "sjasmplus");
    ASSERT_TRUE(saved.ok);
    EXPECT_NE(saved.document.lines[0].text.find("DEVICE NOSLOT64K"), std::string::npos) << saved.document.lines[0].text;
}
