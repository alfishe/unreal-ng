// The ZX-ASM frontend (A6): ZX-ASM / ZAsm sources into the IR, written by the sjasmplus backend. Construct by
// construct, and four programs written for the test, saved as ZAsm 3.15 files and assembled by ZAsm 3.15 in unreal-ng
// (testdata/dialects/zasm315: ZXT1..ZXT4 and the bytes ZAsm saved with SAVEOBJ from #8000; inc1 is the file ZXT4
// INCLUDEs, ins.C the file it INSERTs). With UNREAL_ASM_SJASMPLUS=<path to sjasmplus> the converted programs are
// assembled and compared with those bytes.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "codecs/zxasm/zxasmcodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;

namespace
{
/// The sjasmplus statements (blank lines and comment-only lines dropped, leading blanks trimmed) of a ZX-ASM source
std::vector<std::string> ToSjasmplus(const std::string& zxasm, Diagnostics* diagnostics = nullptr)
{
    const ConvertResult r = Convert(SourceDocument::FromText(zxasm, "zxasm"), "sjasmplus");
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

/// The statements from the first ORG on (a file's first ORG gets a conditional ENT before it)
std::vector<std::string> FromOrg(const std::string& zxasm)
{
    std::vector<std::string> lines = ToSjasmplus(zxasm);
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

/// Converts the program with inc1 as one project, assembles it with sjasmplus, returns `length` bytes from #8000
std::vector<uint8_t> AssembleConverted(const char* sjasmplus, const std::string& name, size_t length)
{
    const codecs::ZxasmCodec zxasm;
    std::vector<ProjectFile> project;
    for (const std::string& file : {name, std::string("inc1")})
    {
        const containers::TrdosFile hobeta = Hobeta("dialects/zasm315/" + file + ".$a");
        DecodeOptions options;
        options.catalog = hobeta.Hints();
        options.subversion = "3.15";
        project.push_back({file, zxasm.Decode(hobeta.data, options).document});
    }
    const ProjectResult r = ConvertProject(project, "sjasmplus");
    std::random_device random;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("unreal-asm-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    for (const ProjectFile& f : r.files)
        WriteBytes(dir / (f.name + ".asm"), codecs::SjasmplusCodec().Encode(f.document, {}).bytes);
    WriteBytes(dir / "ins.C", Hobeta("dialects/zasm315/ins.$C").data);
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"" + name + ".asm\"\n        SAVEBIN \"cmp.bin\",#8000," + std::to_string(length) + "\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    EXPECT_EQ(std::system(command.c_str()), 0);
    std::vector<uint8_t> out = ReadBytes(dir / "cmp.bin");
    std::filesystem::remove_all(dir);
    return out;
}
}  // namespace

TEST(ZxasmFrontend_Test, ExpressionsLeftToRightWithPostfixFunctions)
{
    // No priorities: 10+20*2 is 60; parentheses first
    EXPECT_EQ(ToSjasmplus("        dw 10+20*2,10+(20*2)"), (std::vector<std::string>{"DW (10+20)*2,10+(20*2)"}));
    // ! is XOR, | OR, \ the remainder; IBM numbers
    EXPECT_EQ(ToSjasmplus("        dw 6!3,6|3,7\\3,12h,0ffh,101b"), (std::vector<std::string>{"DW 6^3,6|3,7%3,#12,#0FF,%101"}));
    // A function acts on the operand before it: #1200+#34.b is #1234
    EXPECT_EQ(ToSjasmplus("        dw #1200+#34.b,#1234.h"), (std::vector<std::string>{"DW #1200+(#34&#FF),high #1234"}));
    // .m: the word at that address while assembling (sjasmplus needs a device for it)
    EXPECT_EQ(ToSjasmplus("        dw X.m"), (std::vector<std::string>{"DEVICE ZXSPECTRUM4096", "DW {X}"}));
}

TEST(ZxasmFrontend_Test, InstructionForms)
{
    // jrz / jpnc / callnz / retc; PUSH / INC lists; EXA; (BC) as the port; Russian text is a comment without ";"
    EXPECT_EQ(ToSjasmplus("        jrz $:callnz X:retc"), (std::vector<std::string>{"JR Z,$", "CALL NZ,X", "RET C"}));
    EXPECT_EQ(ToSjasmplus("        push af,bc:inc a,(ix-3)"), (std::vector<std::string>{"PUSH AF", "PUSH BC", "INC A", "INC (IX-3)"}));
    EXPECT_EQ(ToSjasmplus("        exa:in a,(bc):out (bc),e"), (std::vector<std::string>{"EX AF,AF'", "IN A,(C)", "OUT (C),E"}));
    EXPECT_EQ(ToSjasmplus("Print   nop  \xD0\x92\xD1\x85\xD0\xBE\xD0\xB4: A"), (std::vector<std::string>{"Print   NOP"}));
    // The index offset is a byte: (IY+#FE) is (IY-2)
    EXPECT_EQ(ToSjasmplus("        ld (iy+#fe),a"), (std::vector<std::string>{"LD (IY-#02),A"}));
    // Column 0 holding no label is documentation typed in the editor, not source
    Diagnostics diagnostics;
    ToSjasmplus("-  a new command", &diagnostics);
    EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.message.find("not a label") != std::string::npos; }));
    // Label: is the label (marked for MAKELAB)
    EXPECT_EQ(ToSjasmplus("Lab:    nop"), (std::vector<std::string>{"Lab     NOP"}));
}

TEST(ZxasmFrontend_Test, Data)
{
    // DC sets bit 7 of each text's last character; DS repeats the fill sequence n times; DBW (3.3) is DB + DW
    EXPECT_EQ(ToSjasmplus("        dc \"RND\",\"$\""), (std::vector<std::string>{"DB 'RN',#C4,#A4"}));
    EXPECT_EQ(ToSjasmplus("        ds 3,1,2"), (std::vector<std::string>{"DUP 3", "DB 1,2", "EDUP"}));
    EXPECT_EQ(ToSjasmplus("        dbw 3,#c000"), (std::vector<std::string>{"DB 3", "DW #C000"}));
    EXPECT_EQ(ToSjasmplus("        db 'AB',\"C\""), (std::vector<std::string>{"DB 'AB','C'"}));
}

TEST(ZxasmFrontend_Test, MacrosAndRepeats)
{
    // Parameters =1..=n; a call leaving one out keeps the previous call's value (help: FILL ,,6912,0)
    EXPECT_EQ(ToSjasmplus("MAC     macro:ld a,=1:ld b,=2:endm\n        MAC 5,6\n        MAC ,7"),
              (std::vector<std::string>{"LD A,5", "LD B,6", "LD A,5", "LD B,7"}));
    // Labels inside a macro are local to each call (checked)
    EXPECT_EQ(ToSjasmplus("LBL     macro\nLL1     djnz LL1\n        endm\n        LBL\n        LBL"),
              (std::vector<std::string>{"LL1__M1 DJNZ LL1__M1", "LL1__M2 DJNZ LL1__M2"}));
    // IFP: a parameter passed; a parameter glued to a command word (rept=1)
    EXPECT_EQ(ToSjasmplus("MOV     macro:ifp:ld a,(=1):else:ld a,(hl):endif:endm\n        MOV #5C08\n        MOV"),
              (std::vector<std::string>{"IF 1", "LD A,(#5C08)", "ELSE", "LD A,(HL)", "ENDIF", "IF 0", "LD A,(#5C08)", "ELSE", "LD A,(HL)", "ENDIF"}));
    EXPECT_EQ(ToSjasmplus("LDI_    macro:rept=1:ldi:endr:endm\n        LDI_ 4"), (std::vector<std::string>{"DUP 4", "LDI", "EDUP"}));
    // REPT with labels: each pass gets its own (checked); REPL repeats the rest of the line
    EXPECT_EQ(ToSjasmplus("        rept 2\nRL1     djnz RL1\n        endr"), (std::vector<std::string>{"RL1__R1 DJNZ RL1__R1", "RL1__R2 DJNZ RL1__R2"}));
    EXPECT_EQ(ToSjasmplus("        repl 2:inc a:inc b"), (std::vector<std::string>{"DUP 2", "INC A", "INC B", "EDUP"}));
}

TEST(ZxasmFrontend_Test, PhaseNestsAndConditions)
{
    // UNPHASE returns to the outer PHASE where it would be now (checked: #C006 after a 3-byte inner part)
    const std::vector<std::string> phase = FromOrg("        org #8000\n        phase #c000\n        nop\n        phase #d000\n        nop\n        unphase\n        unphase");
    EXPECT_EQ(phase, (std::vector<std::string>{"ORG #8000", "DISP #C000", "DEFINE __UNREALASM_DISP", "NOP", "__UNREALASM_PH1=$", "__UNREALASM_PP1=$$$", "ENT",
                                               "UNDEFINE __UNREALASM_DISP", "DISP #D000", "DEFINE __UNREALASM_DISP", "NOP", "ENT", "UNDEFINE __UNREALASM_DISP",
                                               "DISP __UNREALASM_PH1+($-__UNREALASM_PP1)", "DEFINE __UNREALASM_DISP", "ENT", "UNDEFINE __UNREALASM_DISP"}));
    // IFUSED X: used and not defined so far (a label file's EQU keeps the library's copy out)
    EXPECT_EQ(ToSjasmplus("X       equ 1\n        call X\n        ifused X\nX       nop\n        endif"),
              (std::vector<std::string>{"X       EQU 1", "DEFINE __UNREALASM_DEF_X", "CALL X", "__UNREALASM_IFU=0", "IFUSED X", "IFNDEF __UNREALASM_DEF_X",
                                        "__UNREALASM_IFU=1", "ENDIF", "ENDIF", "IF __UNREALASM_IFU", "X       NOP", "DEFINE __UNREALASM_DEF_X", "ENDIF"}));
    EXPECT_EQ(ToSjasmplus("        ifdef X:db 3:endif"), (std::vector<std::string>{"IF exist X", "DB 3", "ENDIF"}));
}

TEST(ZxasmFrontend_Test, Files)
{
    // INCLUDE names the project file (ZAsm's ".asm" / ".lbl" extension is the type); INSERT and SAVEOBJ keep the name
    EXPECT_EQ(ToSjasmplus("        include \"s:ovldef\",\"A315.lbl\"\n        insert \"a:FONT.fn1\""),
              (std::vector<std::string>{"INCLUDE \"ovldef.asm\"", "INCLUDE \"A315.asm\"", "INCBIN \"FONT.fn1\""}));
    EXPECT_EQ(FromOrg("        org #c000\n        nop\n        saveobj \"E:level  1.C\""),
              (std::vector<std::string>{"ORG #C000", "NOP", "SAVEBIN \"level  1.C\",#C000,$-(#C000)"}));
}

TEST(ZxasmFrontend_Test, ProgramsAssembleToWhatZasm315Built)
{
    // Written for the test, saved as ZAsm 3.15 files and assembled by ZAsm 3.15 in unreal-ng; the bytes it saved with
    // SAVEOBJ. ZXT1: postfix functions, 16-bit unsigned division, IBM numbers, DC, DS, lists, jrz forms, (BC) ports,
    // EXA, SLI, XH / YL, REPT, REPL, a macro reusing a parameter, nested PHASE. ZXT2: labels in a macro called twice.
    // ZXT3: labels in REPT passes. ZXT4: IF / IFDEF / IFNDEF / IFUSED / IFNUSED, .m, comments without ";", $ per
    // statement, INCLUDE, INSERT of a 300-byte file
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    for (const std::string name : {"ZXT1", "ZXT2", "ZXT3", "ZXT4"})
    {
        const std::vector<uint8_t> expected = ReadTestData("dialects/zasm315/" + name + ".bin");
        EXPECT_EQ(AssembleConverted(sjasmplus, name, expected.size()), expected) << name;
    }
}
