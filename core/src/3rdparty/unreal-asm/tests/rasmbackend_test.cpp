// The rasm backend (research-sjasmplus-to-rasm.md): the IR written as rasm 3.x source. Rule by rule; with
// UNREAL_ASM_RASM=<path to rasm> the programs whose bytes are known are converted, assembled by rasm and compared.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "codecs/gens/genscodec.h"
#include "codecs/storm/stormcodec.h"
#include "codecs/tasm/tasmcodec.h"
#include "codecs/text/textcodec.h"
#include "codecs/zxasm/zxasmcodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"

#include "scratchdir.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
/// The rasm lines of a source (blank and comment-only lines dropped, blanks collapsed, a comment after a statement cut)
std::vector<std::string> ToRasm(const std::string& text, const std::string& dialect = "sjasmplus", Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, dialect), "rasm");
    EXPECT_TRUE(r.ok);
    if (diagnostics)
        *diagnostics = r.diagnostics;
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        std::string t = l.text;
        if (const size_t comment = t.find(';'); comment != std::string::npos && t.find('"') == std::string::npos)
            t.resize(comment);
        std::string squeezed;
        for (const char c : t)
            if (c != ' ' || (!squeezed.empty() && squeezed.back() != ' '))
                squeezed.push_back(c);
        while (!squeezed.empty() && squeezed.back() == ' ')
            squeezed.pop_back();
        if (!squeezed.empty())
            out.push_back(squeezed);
    }
    return out;
}

bool Contains(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

bool Warned(const Diagnostics& diagnostics, const std::string& text)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [&](const Diagnostic& d) { return d.severity == Severity::Warning && d.message.find(text) != std::string::npos; });
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

/// The converted project assembled by rasm: its output file (from the lowest address written to the highest)
std::vector<uint8_t> AssembleWithRasm(const char* rasm, const ProjectResult& project, const std::string& main,
                                      const std::vector<std::pair<std::string, std::vector<uint8_t>>>& binaries)
{
    const std::filesystem::path dir = unreal_asm_tests::UniqueDir("unreal-asm-tests");
    std::filesystem::create_directories(dir);
    const codecs::TextCodec text;
    for (const ProjectFile& f : project.files)
        WriteBytes(dir / (f.name + ".asm"), text.Encode(f.document, {}).bytes);
    for (const auto& [name, bytes] : binaries)
        WriteBytes(dir / name, bytes);
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + rasm + "\" " + main + ".asm -o out > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + rasm + "\" " + main + ".asm -o out > out.txt 2>&1";
#endif
    const int status = std::system(command.c_str());
    const std::vector<uint8_t> log = ReadBytes(dir / "out.txt");
    EXPECT_EQ(status, 0) << main << ": " << std::string(log.begin(), log.end());
    std::vector<uint8_t> bytes = ReadBytes(dir / "out.bin");
    // a source with SAVE: rasm writes that file only
    if (!std::filesystem::exists(dir / "out.bin"))
        for (const ProjectFile& f : project.files)
            for (const SourceLine& l : f.document.lines)
                if (const size_t at = l.text.find("SAVE \""); at != std::string::npos && f.name == main)
                    bytes = ReadBytes(dir / l.text.substr(at + 6, l.text.find('"', at + 6) - at - 6));
    if (std::getenv("UNREAL_TEST_KEEP_SCRATCH") == nullptr)
        std::filesystem::remove_all(dir);
    return bytes;
}

ProjectResult Project(const std::vector<std::pair<std::string, SourceDocument>>& documents)
{
    std::vector<ProjectFile> files;
    for (const auto& [name, document] : documents)
        files.push_back({name, document});
    return ConvertProject(files, "rasm");
}

SourceDocument Decoded(const ISourceCodec& codec, const containers::TrdosFile& file, const std::string& version = {})
{
    DecodeOptions options;
    options.catalog = file.Hints();
    options.subversion = version;
    return codec.Decode(file.data, options).document;
}
}  // namespace

TEST(RasmBackend_Test, Registered)
{
    ASSERT_NE(DialectRegistry::Builtin().Backend("rasm"), nullptr);
    const std::vector<std::string> lines = ToRasm("        ORG #8000\nstart   NOP\n        JP start\n");
    EXPECT_TRUE(Contains(lines, "ORG #8000")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "start NOP")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "JP start")) << ::testing::PrintToString(lines);
}

TEST(RasmBackend_Test, Expressions)
{
    // Division is rasm's "\" (its "/" divides in floating point and rounds); comparisons share one priority level
    EXPECT_TRUE(Contains(ToRasm("        DW 7/2,7%3,1<<4,-7>>1\n"), "DW 7\\2,7%3,1<<4,-7>>1"));
    // sjasmplus' true is -1, rasm's 1: a comparison used as a value is negated, a condition is not
    EXPECT_TRUE(Contains(ToRasm("        DW (1<2)+1, 1==1 && 2<3\n"), "DW (-(1<2))+1,(-(1==1&&2<3))"));
    // C's == below <: a==(b<c) keeps its parentheses on rasm's one comparison level
    EXPECT_TRUE(Contains(ToRasm("        DW 1==2<3\n"), "DW (-(1==(-(2<3))))"));
    // no ~ in rasm; HIGH / LOW as hi() / lo(); a whole operand in parentheses is a value, not memory
    const std::vector<std::string> unary = ToRasm("        DW ~5\n        LD A,high X\n        LD A,low X\n        LD HL,(X)+1\n"
                                                  "        LD A,+(X+1)\nX EQU #1234\n");
    EXPECT_TRUE(Contains(unary, "DW (-1-5)")) << ::testing::PrintToString(unary);
    EXPECT_TRUE(Contains(unary, "LD A,hi(X)")) << ::testing::PrintToString(unary);
    EXPECT_TRUE(Contains(unary, "LD A,lo(X)")) << ::testing::PrintToString(unary);
    EXPECT_TRUE(Contains(unary, "LD HL,(X)+1")) << ::testing::PrintToString(unary);
    EXPECT_TRUE(Contains(unary, "LD A,0+(X+1)")) << ::testing::PrintToString(unary);
    // rasm takes a value past 32 bits as #7FFFFFFF: such an expression as its value (a 16-bit rotation)
    EXPECT_TRUE(Contains(ToRasm("        DW (#8001*#10001>>>15)&#FFFF\n"), "DW 3"));
    // binary numbers as 0b... ("%" is rasm's modulo); a quote character as its value
    EXPECT_TRUE(Contains(ToRasm("        DB %101,\"'\",'A'\n"), "DB 0b101,39,'A'"));
}

TEST(RasmBackend_Test, SixteenBitSources)
{
    // Another dialect goes through its sjasmplus form (the sjasmplus backend's arithmetic): STORM's true is -1
    // as sjasmplus', its DW keeps the low word as rasm's does
    const std::vector<std::string> storm = ToRasm("        DW #FFFF+2\n        DW 5>3\n", "storm");
    EXPECT_TRUE(Contains(storm, "DW #FFFF+2")) << ::testing::PrintToString(storm);
    EXPECT_TRUE(Contains(storm, "DW -(-(5>3))")) << ::testing::PrintToString(storm);
    // a label of a name rasm reads as a number ("FCH" is #FC in rasm) is renamed
    const std::vector<std::string> hex = ToRasm("FCH     NOP\n        LD HL,(FCH+1)\n");
    EXPECT_FALSE(Contains(hex, "LD HL,(FCH+1)")) << ::testing::PrintToString(hex);
}

TEST(RasmBackend_Test, NamesRasmReadsOtherwise)
{
    // rasm's words and registers are no labels, and it takes names in any case as the same one
    const std::vector<std::string> lines = ToRasm("hi      NOP\nrepeat  NOP\nStart   NOP\nstart   NOP\n        DW hi,repeat,Start,start\n");
    EXPECT_FALSE(Contains(lines, "hi NOP")) << ::testing::PrintToString(lines);
    EXPECT_FALSE(Contains(lines, "repeat NOP")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "Start NOP")) << ::testing::PrintToString(lines);
    EXPECT_FALSE(Contains(lines, "start NOP")) << ::testing::PrintToString(lines);
    // sjasmplus' local labels and modules: plain names
    const std::vector<std::string> local = ToRasm("main    NOP\n.loop   DJNZ .loop\n        MODULE m\nx       NOP\n        ENDMODULE\n        DW m.x\n");
    EXPECT_TRUE(Contains(local, "main_loop DJNZ main_loop")) << ::testing::PrintToString(local);
    EXPECT_TRUE(Contains(local, "m_x NOP")) << ::testing::PrintToString(local);
}

TEST(RasmBackend_Test, LaterLabelsInCountsAndConditions)
{
    // rasm computes IF, DEFS and REPEAT counts in the order of the text: a later label there is its value
    const std::vector<std::string> lines = ToRasm("        ORG #8000\n        IF fin>#8000\n        DB 1\n        ENDIF\n"
                                                  "        DS cnt\n        DUP cnt\n        NOP\n        EDUP\ncnt     EQU 2\nfin     NOP\n");
    EXPECT_TRUE(Contains(lines, "DB 1")) << ::testing::PrintToString(lines);
    EXPECT_FALSE(Contains(lines, "IF fin>#8000")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "DEFS 2")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "REPEAT 2")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "REND")) << ::testing::PrintToString(lines);
}

TEST(RasmBackend_Test, DirectivesAndInstructions)
{
    const std::vector<std::string> lines = ToRasm("        ORG #8000\n        DISP #C000\nrun     NOP\n        ENT\n        DS 4,#AA\n"
                                                  "        INCBIN \"data.bin\",2,3\n        IN F,(C)\n        EX AF,AF'\n        LD A,IXH\n"
                                                  "        SAVEBIN \"out.bin\",#8000,16\n        DB \"a\\\"b\",0\n");
    EXPECT_TRUE(Contains(lines, "ORG #C000,$$")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "ORG $$")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "DEFS 4,#AA")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "INCBIN \"data.bin\",2,3")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "IN F,(C)")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "EX AF,AF'")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "LD A,IXH")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "SAVE \"out.bin\",#8000,16")) << ::testing::PrintToString(lines);
    EXPECT_TRUE(Contains(lines, "DB 97,34,98,0")) << ::testing::PrintToString(lines);
    // a fill pattern of several bytes: REPEAT ... REND
    const std::vector<std::string> pattern = ToRasm("        DS 3,1,2\n", "tasm");
    EXPECT_TRUE(Contains(pattern, "REND")) << ::testing::PrintToString(pattern);
    // what rasm has no form for: a warning and a comment
    Diagnostics diagnostics;
    ToRasm("        SLDOPT COMMENT WPMEM\n", "sjasmplus", &diagnostics);
    EXPECT_TRUE(Warned(diagnostics, "SLDOPT")) << ::testing::PrintToString(diagnostics.size());
}

TEST(RasmBackend_Test, MacrosAndLoopsExpanded)
{
    // macros are expanded at their calls, a DUP with labels unrolled (a label each pass)
    const std::vector<std::string> lines = ToRasm("        MACRO two a\n        DB a,a\n        ENDM\n        two 5\n"
                                                  "        DUP 2\n1       DJNZ 1B\n        EDUP\n");
    EXPECT_TRUE(Contains(lines, "DB 5,5")) << ::testing::PrintToString(lines);
    EXPECT_FALSE(Contains(lines, "MACRO two a")) << ::testing::PrintToString(lines);
}

TEST(RasmBackend_Test, ProgramsAssembleToWhatTheOriginalsBuilt)
{
    const char* rasm = std::getenv("UNREAL_ASM_RASM");
    if (!rasm)
        GTEST_SKIP() << "set UNREAL_ASM_RASM to the rasm binary";
    const codecs::StormCodec storm;
    const codecs::ZxasmCodec zxasm;
    const codecs::TasmCodec tasm;
    for (const std::string& name : {std::string("STORMT1"), std::string("STORMT2")})
        EXPECT_EQ(AssembleWithRasm(rasm, Project({{name, Decoded(storm, Hobeta("dialects/storm13/" + name + ".$C"))}}), name,
                                   {{"INC", Hobeta("dialects/storm13/INC.$C").data}}),
                  ReadTestData("dialects/storm13/" + name + ".bin"))
            << name;
    for (const std::string& name : {std::string("ZXT1"), std::string("ZXT2"), std::string("ZXT3")})
        EXPECT_EQ(AssembleWithRasm(rasm, Project({{name, Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15")}}), name, {}),
                  ReadTestData("dialects/zasm315/" + name + ".bin"))
            << name;
    EXPECT_EQ(AssembleWithRasm(rasm, Project({{"T50PROG", Decoded(tasm, Hobeta("tasm/T50PROG.$A"))}}), "T50PROG", {}),
              ReadTestData("dialects/tasm50/T50PROG.bin"));
    EXPECT_EQ(AssembleWithRasm(rasm, Project({{"constructs", SourceDocument::FromText(ReadTestText("dialects/alasm-sjasmplus/constructs.alasm.txt"), "alasm")}}),
                               "constructs", {}),
              ReadTestData("dialects/alasm-sjasmplus/constructs.bin"));
    EXPECT_EQ(AssembleWithRasm(rasm, Project({{"relocfake", SourceDocument::FromText(ReadTestText("dialects/sjasmplus/relocfake.asm"), "sjasmplus")}}),
                               "relocfake", {}),
              ReadTestData("dialects/sjasmplus/relocfake.bin"));
    EXPECT_EQ(AssembleWithRasm(rasm, Project({{"constructs", codecs::GensCodec().Decode(ReadTestData("dialects/gens/constructs.gens"), {}).document}}),
                               "constructs", {}),
              ReadTestData("dialects/gens/constructs.bin"));
}
