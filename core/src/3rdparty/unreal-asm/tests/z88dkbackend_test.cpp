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
#include <iterator>
#include <map>
#include <random>
#include <regex>

#include "codecs/gens/genscodec.h"
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
/// The statements of a written text: without indentation, comments and blank lines
std::vector<std::string> Statements(const SourceDocument& document)
{
    std::vector<std::string> out;
    for (const SourceLine& l : document.lines)
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

/// The z80asm lines of a source in another dialect (blank and comment-only lines dropped, leading blanks trimmed)
std::vector<std::string> ToZ80asm(const std::string& text, const std::string& dialect, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, dialect), "z88dk");
    EXPECT_TRUE(r.ok);
    if (diagnostics)
        *diagnostics = r.diagnostics;
    return Statements(r.document);
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
    // GENS' signed 16-bit division: its operands widened from 16 bits; "%" between blanks ("7%100" is 7 and the
    // binary number %100 to z80asm)
    EXPECT_EQ(ToZ80asm("N       EQU  0-7\n        DEFW #8000/2,N?100", "gens"),
              (std::vector<std::string>{"N EQU 0-7", "DW (((($8000&$FFFF)^$8000)-$8000)/2),((((N&$FFFF)^$8000)-$8000) % 100)"}));
    EXPECT_EQ(ToZ80asm("        DW 7\\100", "storm"), (std::vector<std::string>{"DW 7 % 100"}));
    // GENS keeps 16 bits (X EQU #FFFF+2 is 1): a value beyond #FFFF masked where it is defined, counted or tested
    EXPECT_EQ(ToZ80asm("X       EQU  #FFFF+2\n        DEFS X\n        DEFS #FFFF+3", "gens"),
              (std::vector<std::string>{"L_X EQU ($FFFF+2&$FFFF)", "DS L_X", "DS ($FFFF+3&$FFFF)"}));
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
    // ORG $-1 (the next byte writes over the last one) cannot move back in a section: a section of its own at the
    // address the layout gives, from a sjasmplus source and through another dialect's conversion
    for (const char* dialect : {"sjasmplus", "alasm", "storm"})
    {
        const std::vector<std::string> back = ToZ80asm("        ORG #8000\n        NOP\n        NOP\n        ORG $-1\n        HALT", dialect);
        EXPECT_TRUE(Contains(back, "SECTION s_main_2")) << dialect << ": " << ::testing::PrintToString(back);
        EXPECT_TRUE(Contains(back, "ORG $8001")) << dialect << ": " << ::testing::PrintToString(back);
        // more statements after the ORG on its line: the address its value gives, not the next line's
        if (std::string(dialect) == "sjasmplus")
            EXPECT_TRUE(Contains(ToZ80asm("        ORG #8000\n        NOP\n        NOP\n        ORG $-1 : HALT : NOP\n        NOP", dialect), "ORG $8001"));
        // back to a redefinable name that holds an address (its value where the ORG is: the address the next line has;
        // STORM has no name = value)
        if (std::string(dialect) == "storm")
            continue;
        const std::vector<std::string> named =
            ToZ80asm("        ORG #7000\nP=$\n        ORG #8000\n        NOP\n        ORG P\n        HALT\nP=$", dialect);
        EXPECT_TRUE(Contains(named, "ORG $7000")) << dialect << ": " << ::testing::PrintToString(named);
    }
    // TASM's INCBIN copies the rest of the file's last sector, then the address moves back: the tail in a section of
    // its own at the file's end (s_tail_, first in the map), the code going on in a new section from there
    {
        SourceDocument tasm = SourceDocument::FromText("        ORG #8000\n        INCBIN BPM\n        NOP", "tasm");
        tasm.subversion = "4.0";
        BackendOptions options;
        options.dataFiles = [](const std::string&) { return std::vector<uint8_t>(512, 1); };
        options.fileSize = [](const std::string&) { return std::optional<uint64_t>(300); };
        std::vector<std::string> tail;
        for (const SourceLine& l : Convert(tasm, "z88dk", options).document.lines)
            tail.push_back(l.text.substr(l.text.find_first_not_of(' ') == std::string::npos ? 0 : l.text.find_first_not_of(' ')));
        EXPECT_TRUE(Contains(tail, "BINARY \"BPM.slack\"")) << ::testing::PrintToString(tail);
        EXPECT_TRUE(Contains(tail, "SECTION s_tail_main_2")) << ::testing::PrintToString(tail);
        EXPECT_EQ(std::count(tail.begin(), tail.end(), "ORG $812C"), 2) << ::testing::PrintToString(tail);
    }
    // ZX-ASM's IFUSED X (used and not defined so far) decided when converting: P1 is called, P2 only from P1's block,
    // P3 from nowhere; X is defined before its test
    {
        const std::vector<std::string> lib = ToZ80asm("        org #8000\n        call P1\n        ifused P1\nP1      call P2\n        ret\n        endif\n"
                                                      "        ifused P2\nP2      ret\n        endif\n        ifused P3\nP3      nop\n        endif\n"
                                                      "X       equ 1\n        call X\n        ifused X\nX       halt\n        endif", "zxasm");
        std::vector<std::string> tests;
        std::copy_if(lib.begin(), lib.end(), std::back_inserter(tests), [](const std::string& t) { return t.rfind("IF", 0) == 0; });
        EXPECT_EQ(tests, (std::vector<std::string>{"IF 1", "IF 1", "IF 0", "IF 0"})) << ::testing::PrintToString(lib);
    }
    // A library two programs INCLUDE, one of them calling its routine: the test is a constant each main source defines
    {
        const ProjectResult r = ConvertProject({{"ONE", SourceDocument::FromText("        call P1\n        include \"LIB\"", "zxasm")},
                                                {"TWO", SourceDocument::FromText("        nop\n        include \"LIB\"", "zxasm")},
                                                {"LIB", SourceDocument::FromText("        ifused P1\nP1      ret\n        endif", "zxasm")}},
                                               "z88dk");
        ASSERT_TRUE(r.ok);
        ASSERT_EQ(r.files.size(), 3u);
        const std::vector<std::string> one = Statements(r.files[0].document), two = Statements(r.files[1].document),
                                       lib = Statements(r.files[2].document);
        EXPECT_TRUE(Contains(one, "__UNREALASM_IFU1 EQU 1")) << ::testing::PrintToString(one);
        EXPECT_TRUE(Contains(two, "__UNREALASM_IFU1 EQU 0")) << ::testing::PrintToString(two);
        EXPECT_TRUE(Contains(lib, "IF __UNREALASM_IFU1")) << ::testing::PrintToString(lib);
    }
    // A file INCLUDEd twice defines its macro again: z80asm refuses that ("duplicate definition"), so the definition is
    // guarded; pasmo takes it again, unguarded
    {
        const std::vector<ProjectFile> project = {
            {"MAIN", SourceDocument::FromText("        ORG #8000\n        INCLUDE \"LIB\"\n        INCLUDE \"LIB\"\n        m\n        RET", "alasm")},
            {"LIB", SourceDocument::FromText("        MACRO m\n        NOP\n        ENDM\n        DB 7", "alasm")}};
        const ProjectResult z = ConvertProject(project, "z88dk");
        ASSERT_TRUE(z.ok);
        ASSERT_EQ(z.files.size(), 2u);
        EXPECT_EQ(Statements(z.files[1].document), (std::vector<std::string>{"IFNDEF __UNREALASM_M_L_m", "DEFINE __UNREALASM_M_L_m",
                                                                             "L_m MACRO", "NOP", "ENDM", "ENDIF", "DB 7"}))
            << ::testing::PrintToString(Statements(z.files[1].document));
        const ProjectResult p = ConvertProject(project, "pasmo");
        ASSERT_TRUE(p.ok);
        ASSERT_EQ(p.files.size(), 2u);
        EXPECT_FALSE(Contains(Statements(p.files[1].document), "ENDIF"));
    }
    // A name made of an address beyond a sum stops z80asm 2.3 on an assertion where an address expression uses it: the
    // layout's value (TDU's CAT_PLC)
    EXPECT_TRUE(Contains(ToZ80asm("        ORG #8000\nTR      NOP\nCAT     EQU (HIGH TR+1)<<8\n        LD HL,CAT+TR", "sjasmplus"),
                         "CAT EQU $8100"));
    // z80asm's $ in DEFS counts from the section's start: DS #6010-$ is the layout's count (TSW's DS #6060-$)
    EXPECT_TRUE(Contains(ToZ80asm("        ORG #6000\n        NOP\n        DS #6010-$\nX1      NOP", "sjasmplus"), "DS 15"));
    // z80asm rejects a displacement with parentheses next to the index register: it goes between zeros
    EXPECT_EQ(ToZ80asm("DD EQU 5\nKK EQU 2\n        BIT 7,(IX+(DD-KK))\n        LD A,(IY+3)", "sjasmplus"),
              (std::vector<std::string>{"DD EQU 5", "KK EQU 2", "BIT 7,(IX+0+(DD-KK)+0)", "LD A,(IY+3)"}));
    // INCBIN is BINARY; IN F,(C) is z80asm's own
    EXPECT_EQ(ToZ80asm("        INCBIN \"F\"\n        IN F,(C)", "sjasmplus"), (std::vector<std::string>{"BINARY \"F\"", "IN F,(C)"}));
}

TEST(Z88dkBackend_Test, TextMacrosAndSectionOffsets)
{
    // z80asm's DEFL is a text macro: SV=$ is the address the layout gives there (else SV would be the $ where it is
    // used); in a macro, where the layout has no address, it names a LOCAL label put there
    const std::vector<std::string> defl = ToZ80asm("        ORG #8000\n        NOP\nSV=$\n        NOP\nSV=$\n        DW SV", "sjasmplus");
    EXPECT_TRUE(Contains(defl, "SV DEFL $8001")) << ::testing::PrintToString(defl);
    EXPECT_TRUE(Contains(defl, "SV DEFL $8002"));
    const std::vector<std::string> macro = ToZ80asm("        MACRO M\nY=$\n        DW Y\nY=$\n        ENDM\n        ORG #8000\n        M", "sjasmplus");
    EXPECT_TRUE(Contains(macro, "LOCAL __UNREALASM_PC_main_1")) << ::testing::PrintToString(macro);
    // Its labels count from their section's start in an IF: an IF on an address is decided from the layout
    const std::vector<std::string> address = ToZ80asm("        ORG #F045\nT       NOP\n        ORG #6000\n        IF high T&#40\n        NOP\n"
                                                      "        ELSE\n        HALT\n        ENDIF", "sjasmplus");
    EXPECT_FALSE(Contains(address, "HALT")) << ::testing::PrintToString(address);
    EXPECT_EQ(std::count(address.begin(), address.end(), "NOP"), 2);
    // Names assigned from each other would grow at every assignment: each value an EQU of its own (LOCAL in a macro)
    const std::vector<std::string> grow = ToZ80asm("SX=0\nSY=100\nSX=SX+SY/41\nSY=SY-SX/41", "sjasmplus");
    EXPECT_TRUE(Contains(grow, "SX DEFL __UNREALASM_V_main_3")) << ::testing::PrintToString(grow);
    EXPECT_TRUE(std::any_of(grow.begin(), grow.end(), [](const std::string& t) { return t.rfind("__UNREALASM_V_main_4 EQU (SY-", 0) == 0; }))
        << ::testing::PrintToString(grow);
    const std::vector<std::string> inMacro = ToZ80asm("        MACRO M\nSX=SX+SY/41\nSY=SY-SX/41\n        ENDM\nSX=0\nSY=100\n        M", "sjasmplus");
    EXPECT_TRUE(Contains(inMacro, "LOCAL __UNREALASM_V_main_1")) << ::testing::PrintToString(inMacro);
    // A text macro has no parentheses of its own: an expression is put in them (ADR*2 must not read ADR+1*2)
    const std::vector<std::string> paren = ToZ80asm("ADR=#7000\nADR=ADR+1\n        DW ADR*2", "sjasmplus");
    EXPECT_TRUE(Contains(paren, "ADR DEFL (ADR+1)")) << ::testing::PrintToString(paren);
    // An ORG in a macro on a name that holds a number (not an address) may move back: a LOCAL section per expansion
    // (TASM's SINUS steps a pointer and an ORG back and forth at every REPT)
    const std::vector<std::string> sections =
        ToZ80asm("        MACRO G\n        ORG ADR\n        NOP\nADR=ADR+1\n        ENDM\nADR=#7000\n        G\n        G", "sjasmplus");
    EXPECT_TRUE(Contains(sections, "LOCAL s_m_main_1")) << ::testing::PrintToString(sections);
    EXPECT_TRUE(Contains(sections, "SECTION s_m_main_1")) << ::testing::PrintToString(sections);
    EXPECT_TRUE(Contains(sections, "ORG (ADR&$FFFF)")) << ::testing::PrintToString(sections);
    // ORG $+n there skips the bytes (DEFS would write zeros over the other sections): the next section past them
    const std::vector<std::string> skip =
        ToZ80asm("        MACRO G\n        ORG ADR\n        DB 1,2\n        ORG $+127\n        DB 3\nADR=ADR+1\n        ENDM\nADR=#7000\n        G", "sjasmplus");
    EXPECT_TRUE(Contains(skip, "ORG ((ADR&$FFFF)+129&$FFFF)")) << ::testing::PrintToString(skip);
    EXPECT_FALSE(Contains(skip, "DEFS 127")) << ::testing::PrintToString(skip);
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
    // ZXT1's return from a nested PHASE to the outer one is an address computed from labels: z80asm's PHASE takes a
    // constant, the layout's address
    for (const std::string& name : {std::string("ZXT1"), std::string("ZXT2"), std::string("ZXT3")})
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
    // GENS4's constructs (signed division), from #C000
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/gens/constructs.bin");
        const SourceDocument constructs = codecs::GensCodec().Decode(ReadTestData("dialects/gens/constructs.gens"), {}).document;
        EXPECT_EQ(AssembleWithZ80asm(z80asm, Project("constructs", constructs), "constructs", {}, 0xC000, expected.size()), expected);
    }
}
