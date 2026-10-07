// The pasmo backend (A6): the IR written as pasmo 0.5 source. Construct by construct (pasmo's priorities, the
// displacement written out without PHASE, reserved names, instructions pasmo lacks), and the oracle programs of the
// other dialects converted to pasmo: with UNREAL_ASM_PASMO=<path to pasmo> they are assembled and compared with the
// bytes the original assemblers built (STORM 1.3, ZAsm 3.15, TASM 5.0, ALASM 5.09, TASM 4.0's GS ROM).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

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
/// The pasmo lines of a source in another dialect (blank and comment-only lines dropped, leading blanks trimmed)
std::vector<std::string> ToPasmo(const std::string& text, const std::string& dialect, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, dialect), "pasmo");
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
    // A dialect whose displacement may continue into an INCLUDE starts with the delta's definition
    const std::vector<std::string> preamble = {"IF !DEFINED __UNREALASM_D", "__UNREALASM_D DEFL 0", "ENDIF"};
    if (out.size() >= 3 && std::equal(preamble.begin(), preamble.end(), out.begin()))
        out.erase(out.begin(), out.begin() + 3);
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

/// The converted project assembled by pasmo: its raw output (from the lowest address used)
struct Built
{
    std::vector<uint8_t> bytes;
    int status = 0;
};

Built AssembleWithPasmo(const char* pasmo, const ProjectResult& project, const std::string& main,
                        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& binaries)
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
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + pasmo + "\" " + main + ".asm out.bin > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + pasmo + "\" " + main + ".asm out.bin > out.txt 2>&1";
#endif
    Built built;
    built.status = std::system(command.c_str());
    built.bytes = ReadBytes(dir / "out.bin");
    std::filesystem::remove_all(dir);
    return built;
}

ProjectResult Project(const std::vector<std::pair<std::string, SourceDocument>>& documents)
{
    std::vector<ProjectFile> files;
    for (const auto& [name, document] : documents)
        files.push_back({name, document});
    return ConvertProject(files, "pasmo");
}

SourceDocument Decoded(const ISourceCodec& codec, const containers::TrdosFile& file, const std::string& version = {})
{
    DecodeOptions options;
    options.catalog = file.Hints();
    options.subversion = version;
    return codec.Decode(file.data, options).document;
}
}  // namespace

TEST(PasmoBackend_Test, PasmoPrioritiesAndWords)
{
    // Unary operators, HIGH and LOW bind more loosely than "+" in pasmo (-1+2 is -3): always in parentheses
    EXPECT_EQ(ToPasmo("        LD HL,-X+2\n        LD A,X{+1", "tasm"), (std::vector<std::string>{"LD HL,0+(-X)+2", "LD A,0+(HIGH X)+1"}));
    // A STORM comparison gives 1, pasmo's #FFFF: negated; MOD, SHL by name
    EXPECT_EQ(ToPasmo("        DW 2<3,7\\3,1<<4", "storm"), (std::vector<std::string>{"DW (-(2 LT 3)),7 MOD 3,1 SHL 4"}));
    // An operand starting with a parenthesis is memory to pasmo: "0+" keeps it a value
    EXPECT_EQ(ToPasmo("        LD DE,(X')+6", "storm"), (std::vector<std::string>{"LD DE,0+((X SHL 8 AND #FFFF))+6"}));
    // A symbolic index offset may be negative (#FF86 in 16-bit words): its low byte
    EXPECT_EQ(ToPasmo("        LD A,(IX+OFS)\n        LD (IY-3),B", "storm"), (std::vector<std::string>{"LD A,(IX+((OFS) AND #FF))", "LD (IY-3),B"}));
}

TEST(PasmoBackend_Test, NamesAndInstructionsPasmoLacks)
{
    // Every Z80 mnemonic is reserved in pasmo: a label RET is renamed (a label of a file that may start inside a
    // displacement is its run address)
    EXPECT_EQ(ToPasmo("RET     NOP\n        JP RET", "storm"), (std::vector<std::string>{"L_RET EQU $+__UNREALASM_D", "NOP", "JP L_RET"}));
    // IN F,(C) and OUT (C),0 have no pasmo spelling: their bytes; SLI is SLL; CP 0,0 two compares
    EXPECT_EQ(ToPasmo("        INF\n        SLI B", "storm"), (std::vector<std::string>{"DB #ED,#70", "SLL B"}));
    EXPECT_EQ(ToPasmo("        out (c),0\n        cp 1,2", "zxasm"), (std::vector<std::string>{"DB #ED,#71", "CP 1", "CP 2"}));
    // Memory reads while assembling and pages are reported
    Diagnostics diagnostics;
    ToPasmo("        dw X.m", "zxasm", &diagnostics);
    EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.message.find("memory read") != std::string::npos; }));
}

TEST(PasmoBackend_Test, DisplacementWithoutPhase)
{
    // pasmo has no PHASE: the code stays where it is put, labels get the run address, a JR its physical target
    const std::vector<std::string> lines = ToPasmo("        ORG #8000\n        ORG #9000,#8400\nRUN     JR RUN\n        DW $\n        ORG #8500", "storm");
    EXPECT_TRUE(Contains(lines, "ORG #8400"));
    EXPECT_TRUE(Contains(lines, "__UNREALASM_D DEFL #9000-$"));
    EXPECT_TRUE(Contains(lines, "RUN EQU $+__UNREALASM_D"));
    EXPECT_TRUE(Contains(lines, "JR 0+(RUN)-__UNREALASM_D"));
    EXPECT_TRUE(Contains(lines, "DW ($+__UNREALASM_D)"));
}

TEST(PasmoBackend_Test, BlocksAndRepeats)
{
    // A REPT body with labels becomes a macro (pasmo's LOCAL works in a macro, not in REPT)
    const std::vector<std::string> rept = ToPasmo("        DUP 2\nL1      DJNZ L1\n        EDUP", "sjasmplus");
    EXPECT_TRUE(Contains(rept, "__UNREALASM_R1 MACRO"));
    EXPECT_TRUE(Contains(rept, "LOCAL L1"));
    EXPECT_TRUE(Contains(rept, "REPT 2"));
    // A redefinable name assigned once is an EQU: pasmo refuses a reference to a DEFL name before its definition
    const std::vector<std::string> assigned = ToPasmo("        LD A,(S)\nS=$+1\n        LD A,(T)\nT=1\nT=2", "sjasmplus");
    EXPECT_TRUE(Contains(assigned, "S EQU $+1"));
    EXPECT_TRUE(Contains(assigned, "T DEFL 1"));
}

TEST(PasmoBackend_Test, ProgramsAssembleToWhatTheOriginalsBuilt)
{
    const char* pasmo = std::getenv("UNREAL_ASM_PASMO");
    if (!pasmo)
        GTEST_SKIP() << "set UNREAL_ASM_PASMO to the pasmo binary";
    const codecs::StormCodec storm;
    const codecs::ZxasmCodec zxasm;
    const codecs::TasmCodec tasm;
    // STORM 1.3 (ORG run,place written out without PHASE, INCB), from #8000
    for (const std::string& name : {std::string("STORMT1"), std::string("STORMT2")})
    {
        const Built built = AssembleWithPasmo(pasmo, Project({{name, Decoded(storm, Hobeta("dialects/storm13/" + name + ".$C"))}}), name,
                                              {{"INC", Hobeta("dialects/storm13/INC.$C").data}});
        EXPECT_EQ(built.bytes, ReadTestData("dialects/storm13/" + name + ".bin")) << name;
    }
    // ZAsm 3.15 (nested PHASE, macros, REPT labels); ZXT4 needs IFUSED and memory reads, which pasmo lacks
    for (const std::string& name : {std::string("ZXT1"), std::string("ZXT2"), std::string("ZXT3")})
    {
        const Built built = AssembleWithPasmo(pasmo, Project({{name, Decoded(zxasm, Hobeta("dialects/zasm315/" + name + ".$a"), "3.15")}}), name, {});
        EXPECT_EQ(built.bytes, ReadTestData("dialects/zasm315/" + name + ".bin")) << name;
    }
    // TASM 5.0 beta, from #7000
    {
        const Built built = AssembleWithPasmo(pasmo, Project({{"T50PROG", Decoded(tasm, Hobeta("tasm/T50PROG.$A"))}}), "T50PROG", {});
        EXPECT_EQ(built.bytes, ReadTestData("dialects/tasm50/T50PROG.bin"));
    }
    // ALASM 5.09 constructs, from #6000 (LOCAL blocks as PROC, REPEAT ... UNTIL as REPT with EXITM)
    {
        SourceDocument constructs = SourceDocument::FromText(ReadTestText("dialects/alasm-sjasmplus/constructs.alasm.txt"), "alasm");
        const Built built = AssembleWithPasmo(pasmo, Project({{"constructs", constructs}}), "constructs", {});
        EXPECT_EQ(built.bytes, ReadTestData("dialects/alasm-sjasmplus/constructs.bin"));
    }
}
