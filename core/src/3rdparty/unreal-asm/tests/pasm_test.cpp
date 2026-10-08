// Power Assembler (PASM 3.0): the codec (a CR LF text with the pasm dialect, detected by DUP, ITXT / IBIN, ENT, SLI),
// the frontend (PASM -> sjasmplus) construct by construct, and with UNREAL_ASM_SJASMPLUS probes assembled to the bytes
// PASM 3.0 built from them in unreal-ng. research-power-assembler.md.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "codecs/pasm/pasmcodec.h"
#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
std::vector<std::string> ToSjasmplus(const std::string& pasm)
{
    const ConvertResult r = Convert(SourceDocument::FromText(pasm, "pasm"), "sjasmplus");
    EXPECT_TRUE(r.ok);
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
        const size_t gap = text.find("  ");
        if (gap != std::string::npos)
            text = text.substr(0, gap) + " " + text.substr(text.find_first_not_of(' ', gap));
        out.push_back(text);
    }
    return out;
}

using Lines = std::vector<std::string>;

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

SourceDocument Pasm(const std::string& relative)
{
    return codecs::PasmCodec().Decode(ReadTestData(relative), {}).document;
}

/// The bytes sjasmplus builds at #6000 from a converted project (its first file the main one)
std::vector<uint8_t> AssembleAt6000(const char* sjasmplus, const std::vector<ProjectFile>& project, const std::vector<std::pair<std::string, std::string>>& binaries,
                                    size_t size)
{
    std::random_device random;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("unreal-asm-tests-pasm-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                                       std::to_string(random()));
    std::filesystem::create_directories(dir);
    const ProjectResult converted = ConvertProject(project, "sjasmplus");
    EXPECT_TRUE(converted.ok);
    for (const ProjectFile& f : converted.files)
        WriteBytes(dir / (f.name + ".asm"), codecs::SjasmplusCodec().Encode(f.document, {}).bytes);
    for (const auto& [name, path] : binaries)
        WriteBytes(dir / name, ReadTestData(path));
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        INCLUDE \"" + project[0].name + ".asm\"\n        SAVEBIN \"out.bin\",#6000," +
                                std::to_string(size) + "\n";
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

TEST(PasmCodec_Test, DetectionAndRoundTrip)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    // The probe and a file PASM 3.0 saved (type C at 31151): CR LF text with DUP / ENT / SLI
    const std::vector<uint8_t> probe = ReadTestData("pasm/PROBE.txt");
    DetectResult r = registry.Detect(probe, {});
    ASSERT_NE(r.chosen, nullptr) << r.reason;
    EXPECT_EQ(r.chosen->Info().id, "pasm");
    containers::TrdosFile saved;
    std::string error;
    ASSERT_TRUE(containers::ReadHobeta(ReadTestData("pasm/PSAVE.$C"), saved, error)) << error;
    r = registry.Detect(saved.data, saved.Hints());
    ASSERT_NE(r.chosen, nullptr) << r.reason;
    EXPECT_EQ(r.chosen->Info().id, "pasm");
    const codecs::PasmCodec codec;
    for (const std::vector<uint8_t>& bytes : {probe, saved.data})
    {
        const DecodeResult d = codec.Decode(bytes, {});
        ASSERT_TRUE(d.ok);
        EXPECT_EQ(d.document.dialect, "pasm");
        EXPECT_EQ(codec.Encode(d.document, {}).bytes, bytes);
    }
    // A text without PASM's words is not taken for PASM
    const std::string plain = "        LD A,1\r\n        RET\r\n";
    EXPECT_EQ(codec.Detect(std::vector<uint8_t>(plain.begin(), plain.end()), {}), 0);
}

TEST(PasmFrontend_Test, Constructs)
{
    // Left to right (1+2*255 = 765), DUP fills, ' strings, $ of a DW item its own address, ENT a comment, SLI
    EXPECT_EQ(ToSjasmplus("        ORG #6000\nLAB     LD HL,1+2*255\n        DB 'AB',1 DUP 2,'C\n        DW 1,$\n        DW #A0ED DUP 2\n"
                          "        ENT\n        SLI B\n        ITXT LIB1\n        IBIN DAT\n"),
              (Lines{"ORG #6000", "LAB LD HL,+((1+2)*255)", "DB 'AB'", "DS 2,1", "DB 'C'", "DW 1,($+2)", "DUP 2",
                     "DB (#A0ED)&#FF,((#A0ED)&#FFFF)>>8", "EDUP", "SLI B", "INCLUDE \"LIB1.asm\"", "INCBIN \"DAT\""}));
    // $ in an instruction: the bytes put before the operand is read (the first of two operands: before the opcode)
    EXPECT_EQ(ToSjasmplus("        ORG 0\n        JP $\n        LD ($),A\n        LD BC,($)\n        DJNZ $\n"),
              (Lines{"ORG 0", "JP +($+1)", "LD ($),A", "LD BC,(($+2))", "DJNZ +($+1)"}));
    // Without ORG the code goes to 24576 (in the main text only)
    EXPECT_EQ(ToSjasmplus("        NOP\n"), (Lines{"IF $==0", "ORG 24576", "ENDIF", "NOP"}));
}

// --- Assembled by sjasmplus (opt-in: UNREAL_ASM_SJASMPLUS) ----------------------------------------------------------

TEST(PasmFrontend_Test, ProbesAssembleToWhatPasmBuilt)
{
    // PASM 3.0 (Pentagon 128) compiled each probe; the bytes were read from its page 4 (#E000 there), where it puts
    // the object code
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    std::vector<uint8_t> expected = ReadTestData("dialects/pasm/PROBE.bin");
    EXPECT_EQ(AssembleAt6000(sjasmplus, {{"PROBE", Pasm("pasm/PROBE.txt")}}, {}, expected.size()), expected);
    expected = ReadTestData("dialects/pasm/CURRENT.bin");
    EXPECT_EQ(AssembleAt6000(sjasmplus, {{"CURRENT", SourceDocument::FromText(ReadTestText("dialects/pasm/CURRENT.txt"), "pasm")}}, {}, expected.size()), expected);
    // ITXT LIB1 and IBIN DAT
    expected = ReadTestData("dialects/pasm/MAIN.bin");
    EXPECT_EQ(AssembleAt6000(sjasmplus, {{"MAIN", Pasm("dialects/pasm/MAIN.txt")}, {"LIB1", SourceDocument::FromText(ReadTestText("dialects/pasm/LIB1.txt"), "pasm")}},
                             {{"DAT", "dialects/pasm/DAT"}}, expected.size()),
              expected);
}
