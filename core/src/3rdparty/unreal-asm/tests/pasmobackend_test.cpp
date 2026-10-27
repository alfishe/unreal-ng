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
    const std::filesystem::path dir = unreal_asm_tests::UniqueDir("unreal-asm-tests");
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

TEST(PasmoBackend_Test, GensSignedDivisionOnUnsignedWords)
{
    // GENS divides signed words (#8000/2 is #C000), pasmo unsigned ones: the magnitudes divided and the sign put back
    // through s = 0-(x SHR 15); numbers known when converting are computed; the remainder takes the dividend's sign
    EXPECT_EQ(ToPasmo("        DEFW #8000/2,7?2", "gens"), (std::vector<std::string>{"DW 49152,1"}));
    const std::string magnitude = "(((N) XOR (0-((N) SHR 15)))-(0-((N) SHR 15)))";
    EXPECT_EQ(ToPasmo("N       EQU  0-7\n        DEFW N/2,N?3", "gens"),
              (std::vector<std::string>{"N EQU 0-7", "DW (((" + magnitude + "/2) XOR (0-((N) SHR 15)))-(0-((N) SHR 15))),(((" + magnitude +
                                                       " MOD 3) XOR (0-((N) SHR 15)))-(0-((N) SHR 15)))"}));
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

TEST(PasmoBackend_Test, MacroParametersNamedLikeRegisters)
{
    // sjasmplus puts the argument where the parameter's name stands, case kept: in LD A,a the A is the register and
    // the a the parameter, in LD HL,(b) memory (LD B,(b) has no memory form: the parentheses group, sjasmplus' LD B,7);
    // the header names the parameters as the body does; an operand starting with a parameter gets "0+" (an argument
    // in parentheses, GENS' MOVE (X+1), would make it memory to pasmo)
    EXPECT_EQ(ToPasmo("        MACRO T a, b\n        LD A,a\n        LD B,(b)\n        LD HL,(b)\n        LD HL,a+b\n        ENDM\n        T 5, 7",
                      "sjasmplus"),
              (std::vector<std::string>{"T MACRO L_a,L_b", "LD A,0+L_a", "LD B,0+L_b", "LD HL,(L_b)", "LD HL,0+L_a+L_b", "ENDM", "T 5,7"}));
}

TEST(PasmoBackend_Test, AnAlasmArgumentThatIsTheWholeOperandIsPutIn)
{
    // ALASM puts the argument's text in: RAR's "MACRO l / LD \0,\1" is called "l A,(KOLODIN)", where "0+" would make
    // "LD 0+A" (pasmo: "expanding macro")
    const std::vector<std::string> lines = ToPasmo("        MACRO l\n        LD \\0,\\1\n        ENDM\n        l A,(5)", "alasm");
    EXPECT_NE(std::find(lines.begin(), lines.end(), "LD _arg0,_arg1"), lines.end());
}

TEST(PasmoBackend_Test, ANameDefinedInTheTakenBranchOnlyIsAnEqu)
{
    // RAR's rarLZ4: IFN fastCPIQ / MAXPOSL=$+1 / ENDIF, then IF0 fastCPIQ / MAXPOSL=$+1 / ENDIF, used before both: the
    // layout decides the IFs, so MAXPOSL is defined once (pasmo refuses a DEFL name used before its definition)
    const std::vector<std::string> lines = ToPasmo("fast=1\n        ORG #8000\n        LD A,(MX)\n        IFN fast\nMX=$+1\n        LD A,0\n"
                                                   "        ENDIF\n        IF0 fast\nMX=$+1\n        LD B,0\n        ENDIF",
                                                   "alasm");
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("MX EQU", 0) == 0; })) << lines.size();
    EXPECT_FALSE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("MX DEFL", 0) == 0; }));
}

TEST(PasmoBackend_Test, SjasmplusTemporaryLabelsAndStructures)
{
    // a temporary label (2 ... 2_B / 2F) gets a name of each one's own; a STRUCT is its EQUs, an instance its data, an
    // alias (aka st = ins+1) the fields' EQUs only; `,,` (--syntax=a) separates the instructions of one line
    EXPECT_EQ(ToPasmo("2       nop\n        ld hl,2_B,,de,2F\n2       nop\n        STRUCT st\nx       BYTE 1\ny       WORD 2\n        ENDS\n"
                      "ins     st\naka     st = ins+1\n        ld hl,aka.y",
                      "sjasmplus"),
              (std::vector<std::string>{"__T2_1", "NOP", "LD HL,__T2_1", "LD DE,__T2_2", "__T2_2", "NOP", "st__x EQU 0", "st__y EQU 1",
                                        "st EQU 3", "ins", "DB 1", "DW 2", "ins__x EQU ins+0", "ins__y EQU ins+1", "aka EQU ins+1",
                                        "aka__x EQU aka+0", "aka__y EQU aka+1", "LD HL,aka__y"}));
}

TEST(PasmoBackend_Test, SjasmplusWordsPasmoHasNot)
{
    // an undocumented indexed shift / RES / SET with a register copy: its bytes; a fake instruction: its instructions;
    // RELOCATE_TABLE +1: the addresses less one; an IF on memory read while assembling: not converted, IF 1 keeps the
    // ENDIF paired
    Diagnostics diagnostics;
    const std::vector<std::string> lines = ToPasmo("        ORG #8000\n        RELOCATE_START\nst      ld hl,st\n        RELOCATE_END\n        RELOCATE_TABLE +1\n"
                                                   "        rlc (ix+3),b\n        set 5,(iy),e\n        ldi a,(hl)\n        IF {#8000}\n        nop\n        ENDIF",
                                                   "sjasmplus", &diagnostics);
    for (const char* expected : {"DW #8001-((+1))", "DB #DD,#CB,((+3) AND #FF),0", "DB #FD,#CB,0,#C3+((5) AND 7)*8", "LD A,(HL)", "INC HL", "IF 1", "ENDIF"})
        EXPECT_TRUE(Contains(lines, expected)) << expected;
    EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::Warning; }));
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

TEST(PasmoBackend_Test, PassesAndExclusiveBranches)
{
    // TASM's two passes (`.IF PASS` is true in pass 1 only, pass 2 starts with the values pass 1 left): the IF on a name
    // defined after it is decided from the layout, a redefinable name only its branch set starts with the last value
    const std::vector<std::string> passes = ToPasmo("        .IF     PASS\nPASS    EQU     1\nR1      EQU     1\n        .ENDIF\n"
                                                    "        DEFB    R1\nR1      =       5\n", "tasm");
    EXPECT_EQ(passes.size(), 3u) << ::testing::PrintToString(passes);
    EXPECT_TRUE(Contains(passes, "R1 DEFL 5"));
    EXPECT_FALSE(Contains(passes, "PASS EQU 1"));
    // A name set once in each branch of an IF is set once: an EQU, which pasmo takes before its definition
    const std::vector<std::string> branches = ToPasmo("        CALL X\n        IF 1\nX=$\n        ELSE\nX=#3D13\n        ENDIF", "sjasmplus");
    EXPECT_TRUE(Contains(branches, "X EQU $"));
    EXPECT_TRUE(Contains(branches, "X EQU #3D13"));
    // A disk of two programs: each one's IF decided by its own layout
    std::vector<ProjectFile> files = {{"A", SourceDocument::FromText("        IF P==9\n        DB 1\n        ENDIF\nP       EQU 9\n", "sjasmplus")},
                                      {"B", SourceDocument::FromText("        IF P==9\n        DB 1\n        ENDIF\nP       EQU 0\n", "sjasmplus")}};
    const ProjectResult project = ConvertProject(files, "pasmo");
    ASSERT_EQ(project.files.size(), 2u);
    const auto has = [](const SourceDocument& d, const std::string& text) {
        return std::any_of(d.lines.begin(), d.lines.end(), [&](const SourceLine& l) { return l.text.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(has(project.files[0].document, "DB 1"));
    EXPECT_FALSE(has(project.files[1].document, "DB 1"));
    // A file both include, testing a later name they give different values: each main source answers its IF with a
    // constant, unless both take the same branch
    std::vector<ProjectFile> shared = {{"A", SourceDocument::FromText("        INCLUDE \"C\"\nT       EQU 1\n", "sjasmplus")},
                                       {"B", SourceDocument::FromText("        INCLUDE \"C\"\nT       EQU 0\n", "sjasmplus")},
                                       {"C", SourceDocument::FromText("        IF T==0\n        DB 1\n        ELSE\n        DB 2\n        ENDIF\n"
                                                                     "        IF T<5\n        DB 3\n        ELSE\n        DB 4\n        ENDIF\n",
                                                                     "sjasmplus")}};
    const ProjectResult both = ConvertProject(shared, "pasmo");
    ASSERT_EQ(both.files.size(), 3u);
    EXPECT_TRUE(has(both.files[2].document, "DB 1"));
    EXPECT_TRUE(has(both.files[2].document, "DB 2"));
    EXPECT_TRUE(has(both.files[2].document, "DB 3"));
    EXPECT_FALSE(has(both.files[2].document, "DB 4"));
    EXPECT_TRUE(has(both.files[2].document, "IF __UNREALASM_IFL1"));
    EXPECT_TRUE(has(both.files[0].document, "__UNREALASM_IFL1 EQU 0"));
    EXPECT_TRUE(has(both.files[1].document, "__UNREALASM_IFL1 EQU 1"));
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
    // sjasmplus: a HIGH relocation table, EQU parents, D24 / TEXT fields, fake and undocumented instructions, from #8000
    {
        const SourceDocument relocfake = SourceDocument::FromText(ReadTestText("dialects/sjasmplus/relocfake.asm"), "sjasmplus");
        const Built built = AssembleWithPasmo(pasmo, Project({{"relocfake", relocfake}}), "relocfake", {});
        EXPECT_EQ(built.bytes, ReadTestData("dialects/sjasmplus/relocfake.bin"));
    }
    // GENS4's constructs (signed division on pasmo's unsigned words), from #C000
    {
        const SourceDocument constructs = codecs::GensCodec().Decode(ReadTestData("dialects/gens/constructs.gens"), {}).document;
        const Built built = AssembleWithPasmo(pasmo, Project({{"constructs", constructs}}), "constructs", {});
        EXPECT_EQ(built.bytes, ReadTestData("dialects/gens/constructs.bin"));
    }
}
