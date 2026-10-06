// The TASM frontend (A5b): TASM 3 / 4.0 / 4.12 sources into the IR, written by the sjasmplus backend. Construct by
// construct, and the General Sound 1.04 ROM (testdata/dialects/gs104: the sources, the files they INCBIN and GS.C,
// the ROM TASM built) converted as one project. With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the converted ROM is
// assembled and compared with GS.C byte for byte.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "codecs/tasm/tasmcodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;

namespace
{
/// The sjasmplus statements (blank lines and comment-only lines dropped, leading blanks trimmed) of a TASM source
std::vector<std::string> ToSjasmplus(const std::string& tasm, const std::string& version = "4.12", Diagnostics* diagnostics = nullptr)
{
    SourceDocument document = SourceDocument::FromText(tasm, "tasm");
    document.subversion = version;
    const ConvertResult r = Convert(document, "sjasmplus");
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

const char* const kGsSources[] = {"MAIN", "INIT_L", "COM_L", "MEM_L", "LOAD_L", "PLAY", "QUANTUM", "INTTST", "COMM", "GEN_L",
                                  "TABLES_L", "INIT_H", "COM_H", "MEM_H", "ENGINE_L", "FX_H", "VOL_H", "TEST_H", "TABLES_H", "DIHO", "LOADER_"};

ProjectResult GsRom()
{
    const codecs::TasmCodec tasm;
    std::vector<ProjectFile> project;
    for (const char* name : kGsSources)
    {
        const containers::TrdosFile file = Hobeta(std::string("dialects/gs104/") + name + ".$A");
        DecodeOptions options;
        options.catalog = file.Hints();
        project.push_back({file.TrimmedName(), tasm.Decode(file.data, options).document});
    }
    return ConvertProject(project, "sjasmplus");
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
}  // namespace

TEST(TasmFrontend_Test, ExpressionsRunLeftToRightWithPostfixOperators)
{
    EXPECT_EQ(ToSjasmplus("        LD A,1+2*3"), std::vector<std::string>{"LD A,(1+2)*3"});
    // { high byte, } low byte, ^ bytes exchanged: of the value so far (TASM 4.0 description); sjasmplus' unary
    // operators bind first, so "low TAB+1" is (low TAB)+1
    EXPECT_EQ(ToSjasmplus("        LD H,TAB{\n        ADD A,TAB}+1\n        DW #9C40^", "4.0"),
              (std::vector<std::string>{"LD H,high TAB", "ADD A,low TAB+1", "DW ((#9C40&#FF)<<8|(#9C40&#FFFF)>>8)"}));
    // 4.0: [ ] rotate a 16-bit word by one bit
    EXPECT_EQ(ToSjasmplus("        DW %1[", "4.0"), std::vector<std::string>{"DW (((%1&#FFFF)<<1|(%1&#FFFF)>>>(16-1))&#FFFF)"});
    // 4.12: [address] reads memory while assembling
    EXPECT_EQ(ToSjasmplus("KEY     =       [#5C08]"), (std::vector<std::string>{"DEVICE ZXSPECTRUM4096", "KEY={#5C08}"}));
    EXPECT_EQ(ToSjasmplus("        LD A,40H\n        LD A,\"\"\"\"", "4.0"), (std::vector<std::string>{"LD A,#40", "LD A,'\"'"}));
}

TEST(TasmFrontend_Test, IfCompilesItsFirstPartWhenTheValueIsZero)
{
    EXPECT_EQ(ToSjasmplus("        .IF     DEBUG\n        NOP\n        .ELSE\n        HALT\n        .ENDIF"),
              (std::vector<std::string>{"IF (DEBUG)==0", "NOP", "ELSE", "HALT", "ENDIF"}));
}

TEST(TasmFrontend_Test, OrgAndPhaseEndAnActivePhase)
{
    // A file does not know whether a PHASE is active where it starts (an INCLUDE inside PHASE): that end is conditional
    const std::vector<std::string> lines = ToSjasmplus("        ORG #8000\n        PHASE #0000\nA1      NOP\n        ORG #8030\n        PHASE #0030\n        UNPHASE\n        UNPHASE", "4.0");
    EXPECT_EQ(lines, (std::vector<std::string>{"IFDEF __UNREALASM_DISP", "ENT", "UNDEFINE __UNREALASM_DISP", "ENDIF", "ORG #8000",
                                               "DISP #0000", "DEFINE __UNREALASM_DISP", "A1      NOP", "ENT", "UNDEFINE __UNREALASM_DISP",
                                               "ORG #8030", "DISP #0030", "DEFINE __UNREALASM_DISP", "ENT", "UNDEFINE __UNREALASM_DISP"}));
}

TEST(TasmFrontend_Test, IncbinWritesTheRestOfTheLastSector)
{
    // TASM's INCBIN copies whole sectors; the address moves by the file's length (the GS 1.04 ROM keeps such bytes)
    const std::vector<std::string> lines = ToSjasmplus("TAB     INCBIN BPM", "4.0");
    EXPECT_EQ(lines[0], "TAB     INCBIN \"BPM\"");
    EXPECT_TRUE(Contains(lines, "INCBIN \"BPM.slack\""));
    EXPECT_TRUE(Contains(lines, "ORG __UNREALASM_INCBIN_D"));
    EXPECT_EQ(ToSjasmplus("        INCBIN PIC,6912", "4.0"), std::vector<std::string>{"INCBIN \"PIC\",0,6912"});
}

TEST(TasmFrontend_Test, InstructionsAndRegisterForms)
{
    EXPECT_EQ(ToSjasmplus("        PUSH AF,BC,DE\n        JP NV,$\n        RET V\n        INF\n        LD A,LX\n        EX AF,AF'", "4.0"),
              (std::vector<std::string>{"PUSH AF", "PUSH BC", "PUSH DE", "JP PO,$", "RET PE", "IN F,(C)", "LD A,IXL", "EX AF,AF'"}));
    // An operand starting with "(" is memory: "0+" makes it a value (TASM 4.12 article)
    EXPECT_EQ(ToSjasmplus("        LD DE,0+((X)+1)\n        LD DE,(X)"), (std::vector<std::string>{"LD DE,0+((X)+1)", "LD DE,(X)"}));
    // Operands without a comma (TASM keeps them as tokens); a label the source defines tells HLCOUNT from a label
    EXPECT_EQ(ToSjasmplus("COUNT   NOP\n        LD HL#4000\n        LD C(HL)\n        LD (PTR)A\n        LD HLCOUNT\n        JR NZCOUNT", "4.0"),
              (std::vector<std::string>{"COUNT   NOP", "LD HL,#4000", "LD C,(HL)", "LD (PTR),A", "LD HL,COUNT", "JR NZ,COUNT"}));
}

TEST(TasmFrontend_Test, MacrosLocalsAndRedefinitions)
{
    // \0 and /0 are parameters; ...labels in a macro are local to each expansion, after .LOCAL to the region
    const std::vector<std::string> lines = ToSjasmplus("        DEFMAC  MOVE\n        LD      HL,/0\n        LD      DE,\\1\n...1    DJNZ ...1\n        ENDMAC\n"
                                                       "        .LOCAL\n...1    NOP\n        JP ...1\n        MOVE    1,2\n        DEFMAC  HL*8\n        ENDMAC\n        HL*8");
    EXPECT_TRUE(Contains(lines, "MACRO MOVE _arg0,_arg1"));
    EXPECT_TRUE(Contains(lines, "LD HL,_arg0"));
    EXPECT_TRUE(Contains(lines, ".local_1 DJNZ .local_1"));
    EXPECT_TRUE(Contains(lines, "__local1_1 NOP"));
    EXPECT_TRUE(Contains(lines, "JP __local1_1"));
    EXPECT_TRUE(Contains(lines, "MOVE 1,2"));
    EXPECT_TRUE(Contains(lines, "MACRO L_HL_8"));   // a name sjasmplus rejects, renamed at the definition and the call
    EXPECT_TRUE(Contains(lines, "L_HL_8"));
    // A name assigned with "=" is redefinable, its EQU too
    EXPECT_EQ(ToSjasmplus("X       EQU 1\nX       =   X+1"), (std::vector<std::string>{"X=1", "X=X+1"}));
}

TEST(TasmFrontend_Test, AnIndentedWordUsedAsALabelIsOne)
{
    EXPECT_EQ(ToSjasmplus(" ?ASK\n        JR NZ,?ASK"), (std::vector<std::string>{"L__ASK", "JR NZ,L__ASK"}));
    EXPECT_EQ(ToSjasmplus("        fSIZE   EQU 4"), std::vector<std::string>{"fSIZE   EQU 4"});
}

TEST(TasmFrontend_Test, KeywordsFollowTheVersion)
{
    // TASM 3 has no DM / INF keywords (its table ends at INCBIN): they are labels there; a keyword in column 0 is a command
    EXPECT_EQ(ToSjasmplus("DM      NOP\n        JP DM", "3"), (std::vector<std::string>{"DM      NOP", "JP DM"}));
    EXPECT_EQ(ToSjasmplus("DEFB    1,2", "4.0"), std::vector<std::string>{"DB 1,2"});
}

TEST(TasmFrontend_Test, GsRomConvertsWithoutLosses)
{
    const ProjectResult r = GsRom();
    ASSERT_TRUE(r.ok);
    for (const Diagnostic& d : r.diagnostics)
        EXPECT_EQ(d.message.find("not converted"), std::string::npos) << d.message;
    // The converted file survives the sjasmplus frontend + backend unchanged (written, read back, converted, written)
    const codecs::SjasmplusCodec codec;
    for (const ProjectFile& file : r.files)
    {
        const std::vector<uint8_t> bytes = codec.Encode(file.document, {}).bytes;
        DecodeOptions options;
        options.codePage = file.document.codePage;
        const ConvertResult again = Convert(codec.Decode(bytes, options).document, "sjasmplus");
        EXPECT_EQ(codec.Encode(again.document, {}).bytes, bytes) << file.name;
    }
}

TEST(TasmFrontend_Test, GsRomAssemblesToTheRomTasmBuilt)
{
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    std::random_device random;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    const codecs::SjasmplusCodec codec;
    for (const ProjectFile& file : GsRom().files)
        WriteBytes(dir / (file.name + ".asm"), codec.Encode(file.document, {}).bytes);
    for (const char* binary : {"STUFF", "BPM", "SGEN"})
    {
        const containers::TrdosFile file = Hobeta(std::string("dialects/gs104/") + binary + ".$C");
        WriteBytes(dir / binary, file.data);
        WriteBytes(dir / (std::string(binary) + ".slack"), file.tail);
    }
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"MAIN.asm\"\n        SAVEBIN \"gs.bin\",#8000,#8000\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    EXPECT_EQ(std::system(command.c_str()), 0);
    std::vector<uint8_t> rom = ReadBytes(dir / "gs.bin");
    ASSERT_EQ(rom.size(), 32768u);
    // The build's last step (CRC.A, run in TASM) stores the sum of the words from #0008 at ROMCRC (#0006)
    uint16_t sum = 0;
    for (size_t k = 8; k < rom.size(); k += 2)
        sum = static_cast<uint16_t>(sum + rom[k] + 256 * rom[k + 1]);
    rom[6] = static_cast<uint8_t>(sum);
    rom[7] = static_cast<uint8_t>(sum >> 8);
    EXPECT_EQ(rom, Hobeta("dialects/gs104/GS.$C").data);
    std::filesystem::remove_all(dir);
}

TEST(TasmFrontend_Test, SinusTableAssemblesToWhatTasm412Built)
{
    // TASM 4.12's SINUS example (ORG moved to #7000, clear of TASM's overlay at #8000): a macro that calls itself 64
    // times, values carried from pass 1 into pass 2 (.IF PASS), 16-bit division. SIN7.bin is what TASM 4.12 built
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    const containers::TrdosFile file = Hobeta("dialects/tasm412/SIN7.$A");
    const codecs::TasmCodec tasm;
    DecodeOptions options;
    options.catalog = file.Hints();
    const ConvertResult r = Convert(tasm.Decode(file.data, options).document, "sjasmplus");
    ASSERT_TRUE(r.ok);
    std::random_device random;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    WriteBytes(dir / "SIN7.asm", codecs::SjasmplusCodec().Encode(r.document, {}).bytes);
    const std::string harness = "        DEVICE ZXSPECTRUM128\n        INCLUDE \"SIN7.asm\"\n        SAVEBIN \"out.bin\",#7000,256\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    EXPECT_EQ(std::system(command.c_str()), 0);
    EXPECT_EQ(ReadBytes(dir / "out.bin"), ReadTestData("dialects/tasm412/SIN7.bin"));
    std::filesystem::remove_all(dir);
}

TEST(TasmFrontend_Test, TextsBitOperandsAndTypedNames)
{
    // """ is one quote (TASM 4.0, the GENS form); a text may run to the end of the line
    EXPECT_EQ(ToSjasmplus("        LD A,\"\"\"\n        DB \"Hello world", "4.0"), (std::vector<std::string>{"LD A,'\"'", "DB 'Hello world'"}));
    // BIT 3D: number and register without a comma; Z12_ is a name, not Z + 12
    EXPECT_EQ(ToSjasmplus("        BIT 3D\n        CALL Z12_", "4.0"), (std::vector<std::string>{"BIT 3,D", "CALL Z12_"}));
    // DM /text/: any delimiter; LOOP: is LOOP; INCLUDE names lose the blanks TR-DOS pads them with
    EXPECT_EQ(ToSjasmplus("LOOP:   DM /\"/\n        INCLUDE PARTS ", "4.0"), (std::vector<std::string>{"LOOP    DB '\"'", "INCLUDE \"PARTS.asm\""}));
    // @ is a character of TASM names (sjasmplus would read @VAL as VAL)
    EXPECT_EQ(ToSjasmplus("@VAL    EQU #11\nVAL     NOP", "4.0"), (std::vector<std::string>{"L__VAL  EQU #11", "VAL     NOP"}));
}

TEST(TasmFrontend_Test, Tasm50ProgramAssemblesToWhatTasm50Built)
{
    // Typed into TASM 5.0 beta in unreal-ng, saved and assembled there at #7000 (45 bytes): left-to-right arithmetic,
    // ^, a string fill in DS, PUSH with two registers, structural lines without commas
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    const containers::TrdosFile file = Hobeta("tasm/T50PROG.$A");
    const codecs::TasmCodec tasm;
    DecodeOptions options;
    options.catalog = file.Hints();
    const ConvertResult r = Convert(tasm.Decode(file.data, options).document, "sjasmplus");
    ASSERT_TRUE(r.ok);
    std::random_device random;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    WriteBytes(dir / "PROG.asm", codecs::SjasmplusCodec().Encode(r.document, {}).bytes);
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"PROG.asm\"\n        SAVEBIN \"out.bin\",#7000,45\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    EXPECT_EQ(std::system(command.c_str()), 0);
    EXPECT_EQ(ReadBytes(dir / "out.bin"), ReadTestData("dialects/tasm50/T50PROG.bin"));
    std::filesystem::remove_all(dir);
}
