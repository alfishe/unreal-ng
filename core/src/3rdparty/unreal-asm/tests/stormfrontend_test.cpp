// The STORM frontend (A6): STORM 1.3 sources into the IR, written by the sjasmplus backend. Construct by construct,
// and two programs written for the test, saved as STORM files and assembled by STORM 1.3 in unreal-ng
// (testdata/dialects/storm13: STORMT1 / STORMT2 and the bytes STORM built at #8000; INC.C is the file STORMT1
// includes). With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the converted programs are assembled and compared with
// those bytes.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "codecs/storm/stormcodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;

namespace
{
/// The sjasmplus statements (blank lines and comment-only lines dropped, leading blanks trimmed) of a STORM source
std::vector<std::string> ToSjasmplus(const std::string& storm, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(storm, "storm"), "sjasmplus");
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
    return out;
}

/// The statements after the conditional ENT a file's first ORG gets (a displacement may continue into an INCL)
std::vector<std::string> AfterFirstOrg(const std::string& storm)
{
    std::vector<std::string> lines = ToSjasmplus(storm);
    const auto org = std::find_if(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("ORG ", 0) == 0; });
    return std::vector<std::string>(org, lines.end());
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

/// Converts the STORM file, assembles it with sjasmplus and returns `length` bytes from #8000 (empty when skipped)
std::vector<uint8_t> AssembleConverted(const char* sjasmplus, const std::string& name, size_t length)
{
    const containers::TrdosFile file = Hobeta("dialects/storm13/" + name + ".$C");
    const codecs::StormCodec storm;
    DecodeOptions options;
    options.catalog = file.Hints();
    const ConvertResult r = Convert(storm.Decode(file.data, options).document, "sjasmplus");
    EXPECT_TRUE(r.ok);
    std::random_device random;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    WriteBytes(dir / "PROG.asm", codecs::SjasmplusCodec().Encode(r.document, {}).bytes);
    const containers::TrdosFile inc = Hobeta("dialects/storm13/INC.$C");
    WriteBytes(dir / "INC", inc.data);
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"PROG.asm\"\n        SAVEBIN \"out.bin\",#8000," + std::to_string(length) + "\n";
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
}  // namespace

TEST(StormFrontend_Test, SeveralOperandsAreSeveralInstructions)
{
    // help: LD HL,1,DE,2 is two loads; ADD A,A,A,B,HL,DE three additions; PUSH BC,DE,HL three pushes
    EXPECT_EQ(ToSjasmplus("        LD HL,1,DE,2"), (std::vector<std::string>{"LD HL,1", "LD DE,2"}));
    EXPECT_EQ(ToSjasmplus("        ADD A,A,A,B,HL,DE"), (std::vector<std::string>{"ADD A,A", "ADD A,B", "ADD HL,DE"}));
    EXPECT_EQ(ToSjasmplus("        PUSH BC,DE,HL"), (std::vector<std::string>{"PUSH BC", "PUSH DE", "PUSH HL"}));
    // JR NZ,L1,L2 is JR NZ,L1 / JR L2; JR C,$+5,Z,$+20 two conditional JRs, $ of each its own
    EXPECT_EQ(ToSjasmplus("L1      JR NZ,L1,L1"), (std::vector<std::string>{"L1      JR NZ,L1", "JR L1"}));
    EXPECT_EQ(ToSjasmplus("        JR C,$+5,Z,$+20:RET NZ,Z"), (std::vector<std::string>{"JR C,$+5", "JR Z,$+20", "RET NZ", "RET Z"}));
    // RLC (IX+1),B is two instructions in STORM 1.3 (checked), not the undocumented copy
    EXPECT_EQ(ToSjasmplus("        RLC (IX+1),B"), (std::vector<std::string>{"RLC (IX+1)", "RLC B"}));
    // Any number of statements per line
    EXPECT_EQ(ToSjasmplus("        CP 5:JR NC,X : RET"), (std::vector<std::string>{"CP 5", "JR NC,X", "RET"}));
}

TEST(StormFrontend_Test, BuiltInMacros)
{
    // OUT B,A,(#FE) = OUT (C),B / OUT (C),A / OUT (#FE),A; IN D = IN D,(C); IN (#FE) = IN A,(#FE)
    EXPECT_EQ(ToSjasmplus("        OUT B,A,(#FE)"), (std::vector<std::string>{"OUT (C),B", "OUT (C),A", "OUT (#FE),A"}));
    EXPECT_EQ(ToSjasmplus("        IN D:IN (#FE):IN D,(C),E"), (std::vector<std::string>{"IN D,(C)", "IN A,(#FE)", "IN D,(C)", "IN E,(C)"}));
    // LD HL,BC = LD H,B / LD L,C; LD BC,IX = LD B,HX / LD C,LX; LD SP,HL stays
    EXPECT_EQ(ToSjasmplus("        LD HL,BC,BC,IX,SP,HL"), (std::vector<std::string>{"LD H,B", "LD L,C", "LD B,IXH", "LD C,IXL", "LD SP,HL"}));
    // ADD DE,HL = EX DE,HL / ADD HL,DE / EX DE,HL; EX HL,DE = EX DE,HL; EXA and EX AF,AF = EX AF,AF'
    EXPECT_EQ(ToSjasmplus("        ADD DE,HL:EX HL,DE:EXA:EX AF,AF"),
              (std::vector<std::string>{"EX DE,HL", "ADD HL,DE", "EX DE,HL", "EX DE,HL", "EX AF,AF'", "EX AF,AF'"}));
    EXPECT_EQ(ToSjasmplus("        INF"), (std::vector<std::string>{"IN F,(C)"}));
}

TEST(StormFrontend_Test, ExpressionsWithPrioritiesAndPostfixOperators)
{
    // * / \ before + -, << >> between them, & ! | below, comparisons last; equal priorities left to right
    EXPECT_EQ(ToSjasmplus("        DW 1+2*3,1<<4+1,3&5!8,7\\3"), (std::vector<std::string>{"DW 1+2*3,(1<<4)+1,3&5|8,7%3"}));
    // [ high, ] low, ^ round up and ` round down to a multiple of 256, ' times 256: postfix, on what is before them
    EXPECT_EQ(ToSjasmplus("        LD A,X[:LD A,X]+1"), (std::vector<std::string>{"LD A,high X", "LD A,low X+1"}));
    EXPECT_EQ(ToSjasmplus("        DW X^,X`,X'"), (std::vector<std::string>{"DW ((X+#FF)&#FF00),(X&#FF00),(X<<8&#FFFF)"}));
    // ~ negates what the operators of priority 7 and up built (2*3~ = -6); @ is 1 for 0, else 0
    EXPECT_EQ(ToSjasmplus("        DW 1+2*3~,X@"), (std::vector<std::string>{"DW 1+-(2*3),-(!X)"}));
    // Comparisons give 1 (sjasmplus -1), in 16-bit unsigned words: 0-1<1 is 0
    EXPECT_EQ(ToSjasmplus("        DW 2<3,0-1<1"), (std::vector<std::string>{"DW -(2<3),-(((0-1)&#FFFF)<1)"}));
    // A whole operand in parentheses is memory; (X')+6 and 0+(5) are values
    EXPECT_EQ(ToSjasmplus("        LD HL,(X):LD DE,(X')+6:LD HL,0+(5)"), (std::vector<std::string>{"LD HL,(X)", "LD DE,((X<<8&#FFFF))+6", "LD HL,0+(5)"}));
    // "A" a byte, "AB" a word with the first character high
    EXPECT_EQ(ToSjasmplus("        LD A,\"A\"+1:LD HL,\"AB\""), (std::vector<std::string>{"LD A,'A'+1", "LD HL,16706"}));
}

TEST(StormFrontend_Test, DataAndRepetition)
{
    // "@AEDF" is a text of hex bytes; texts of two or more characters are texts; $ is each item's address
    EXPECT_EQ(ToSjasmplus("        DB \"@AEDFC825\",\"AB\",1"), (std::vector<std::string>{"DB #AE,#DF,#C8,#25,'AB',1"}));
    EXPECT_EQ(ToSjasmplus("        DB 1,X-$"), (std::vector<std::string>{"DB 1", "DB X-$"}));
    // DS count,pattern: count bytes of the pattern repeated (DS 7,#AA,#BB is 7 bytes)
    EXPECT_EQ(ToSjasmplus("        DS 7,#AA,#BB"), (std::vector<std::string>{"DUP 3", "DB #AA,#BB", "EDUP", "DB #AA"}));
    EXPECT_EQ(ToSjasmplus("        DS N,1,2"), (std::vector<std::string>{"DUP (N)/2", "DB 1,2", "EDUP", "IF (N)%2>=1", "DB 1", "ENDIF"}));
    EXPECT_EQ(ToSjasmplus("        DS 10:DS 3,7"), (std::vector<std::string>{"DS 10", "DS 3,7"}));
    // .n repeats the line, .0 256 times; _ puts a command in column 0
    EXPECT_EQ(ToSjasmplus(".32     LDI\n.0      NOP\n_DB 5+14*2"),
              (std::vector<std::string>{"DUP 32", "LDI", "EDUP", "DUP 256", "NOP", "EDUP", "DB 5+14*2"}));
}

TEST(StormFrontend_Test, OrgAndFiles)
{
    // ORG run,place: the code is put at place and runs at run; the next ORG ends the displacement
    EXPECT_EQ(AfterFirstOrg("        ORG #8000\n        ORG #9000,#8400\nRUN     JR RUN\n        ORG #8500"),
              (std::vector<std::string>{"ORG #8000", "ORG #8400", "DISP #9000", "DEFINE __UNREALASM_DISP", "RUN     JR RUN", "ENT",
                                        "UNDEFINE __UNREALASM_DISP", "ORG #8500"}));
    // INCB / INCL take several names; the 9th character of a name is its TR-DOS type; INCB moves the address by the
    // file's length (no sector slack, checked)
    EXPECT_EQ(ToSjasmplus("        INCB \"SPR\",\"TABLE   B\"\n        INCL \"PART2\""),
              (std::vector<std::string>{"INCBIN \"SPR\"", "INCBIN \"TABLE.B\"", "INCLUDE \"PART2.asm\""}));
    // Keywords STORM 1.3 has in its table but gives no meaning
    Diagnostics diagnostics;
    ToSjasmplus("        REPT 3", &diagnostics);
    EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.message.find("REPT") != std::string::npos; }));
}

TEST(StormFrontend_Test, ProgramsAssembleToWhatStorm13Built)
{
    // Written for the test, saved as STORM files and assembled by STORM 1.3 in unreal-ng; the bytes it built at #8000.
    // STORMT1: several operands, built-in macros, implied JP P / M / PE / PO, .3, $ per DB item, "@hex", priorities,
    // postfix operators, comparisons, 16-bit unsigned words, DS patterns, ORG run,place, INCB. STORMT2: IN / OUT
    // forms, LD of register pairs, SLI, INF, EX forms, RET with several conditions, DS with a symbol count, chained
    // comparisons, ~ and @ priorities
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    for (const std::string name : {"STORMT1", "STORMT2"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/storm13/" + name + ".bin");
        EXPECT_EQ(AssembleConverted(sjasmplus, name, expected.size()), expected) << name;
    }
}
