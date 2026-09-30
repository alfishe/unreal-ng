#include "stdafx.h"
#include "pch.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/zxprogramfiles.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/assembler/z80textassembler.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

/// snowtest (tools/verification/contention/snowtest; design docs/inprogress/2026-09-29-ula-snow/tdd.md): the
/// ULA snow test program. This suite
/// - builds it with the in-tree assembler and fills in the EXPECTED band and its text from the snow model:
///   a refresh whose T3 falls on the ULA's pixel byte 1 fetch shows, in that cell, the cell R (before its
///   increment) points to; on pixel byte 2's fetch the second cell repeats the first. The model's tick and R
///   were fixed on Snow Hold's photos of three real 48K machines (UlaSnowRender_Test);
/// - runs it on the 48K and the 128K and checks that the LIVE band renders exactly as the EXPECTED band, and on
///   the +3 and the Pentagon that it renders plain;
/// - keeps the committed snowtest.tap / .trd / .sym equal to the source.

namespace
{
constexpr uint16_t kOrg = 36000;
const std::string kPlain = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

std::string SnowPath(const std::string& file)
{
    return (TestPathHelper::FindProjectRoot() / "tools" / "verification" / "contention" / "snowtest" / file)
        .make_preferred()
        .string();
}

std::string EnginePath()
{
    return (TestPathHelper::FindProjectRoot() / "tools" / "verification" / "contention" / "ctprobe" / "engine.asm")
        .make_preferred()
        .string();
}

/// The prediction for one band line (every line of the band is the same, see snowtest.asm): the chain of 32
/// x LD A,0 starts 96 T before the line's first fetch (pixel byte 1 of cell 0), so instruction i's refresh
/// T3 is 7 i + 2 - 96 T from it. In the 16-pixel group g the pixel byte 1 fetch is at 8 g, pixel byte 2 at
/// 8 g + 2. The refresh of chain instruction i puts R0 + 3 + i on the bus (LD A,#40 and the two M1s of
/// LD I,A come between LD R,A and the chain). Line length and the 128K's OUT (#FF),A do not change a line's
/// own ticks
struct Prediction
{
    std::string chars = kPlain;
    std::string text;
};

Prediction Predict(uint8_t r0)
{
    Prediction p;
    std::string snow, doubled;
    for (int i = 0; i < 32; i++)
    {
        const int f = 7 * i + 2 - 96;
        if (f < 0 || f >= 128 || f % 8 != 0)
            continue;
        const int cell = f / 4;
        const int column = (r0 + 3 + i) & 31;
        p.chars[static_cast<size_t>(cell)] = kPlain[static_cast<size_t>(column)];
        snow += " " + std::to_string(cell) + "=" + kPlain[static_cast<size_t>(column)];
    }
    for (int i = 0; i < 32; i++)
    {
        const int f = 7 * i + 2 - 96;
        if (f < 0 || f >= 128 || f % 8 != 2)
            continue;
        const int cell = (f - 2) / 4 + 1;
        p.chars[static_cast<size_t>(cell)] = p.chars[static_cast<size_t>(cell - 1)];
        doubled += " " + std::to_string(cell) + "=" + p.chars[static_cast<size_t>(cell - 1)];
    }
    p.text = "Snow at column" + snow + "\rDouble at column" + doubled;
    return p;
}

struct SnowProgram
{
    AsmResult asmResult;
    std::vector<uint8_t> bytes;
    Prediction prediction;

    uint16_t Sym(const char* name) const
    {
        auto it = asmResult.symbols.find(name);
        EXPECT_NE(it, asmResult.symbols.end()) << name;
        return it == asmResult.symbols.end() ? 0 : static_cast<uint16_t>(it->second);
    }
};

SnowProgram BuildSnowTest()
{
    SnowProgram p;
    Z80TextAssembler assembler;
    p.asmResult = assembler.Assemble(
        ZxProgramFiles::ReadText(SnowPath("snowtest.asm")) + "\n" + ZxProgramFiles::ReadText(EnginePath()), kOrg);
    if (!p.asmResult.ok)
    {
        ADD_FAILURE() << p.asmResult.error.line << ": " << p.asmResult.error.message << " | "
                      << p.asmResult.error.sourceLine;
        return p;
    }
    p.bytes = p.asmResult.bytes;
    p.prediction = Predict(p.bytes[p.Sym("R0") - kOrg]);
    const uint16_t chars = p.Sym("EXPCHARS");
    for (size_t i = 0; i < 32; i++)
        p.bytes[chars - kOrg + i] = static_cast<uint8_t>(p.prediction.chars[i]);
    const uint16_t text = p.Sym("EXPTEXT");
    EXPECT_LE(p.prediction.text.size(), 64u);
    for (size_t i = 0; i < p.prediction.text.size() && i < 64; i++)
        p.bytes[text - kOrg + i] = static_cast<uint8_t>(p.prediction.text[i]);
    return p;
}
}  // namespace

class SnowTest_Test : public RomEditorFixture
{
protected:
    SnowProgram _program;

    /// Boots `editor`, runs snowtest from its host entry until the band loop has run a few frames, then renders
    /// every tick of one frame
    void Run(const char* editor)
    {
        BootEditor(editor);
        ASSERT_FALSE(HasFatalFailure());
        _program = BuildSnowTest();
        ASSERT_FALSE(HasFailure());
        ASSERT_LT(kOrg + _program.bytes.size(), 0xBE00u) << "the program runs into the engine's IM2 table";
        for (size_t i = 0; i < _program.bytes.size(); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kOrg + i), _program.bytes[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = _program.Sym("HOSTENTRY");
        z80->sp = 0xBDFE;
        z80->iff1 = z80->iff2 = 1;
        WaitForLoopAndRender();
    }

    /// Waits until the band loop runs (the delay measured, ONEPASS back to 0), then renders every tick of a frame
    void WaitForLoopAndRender()
    {
        Z80* z80 = _context->pCore->GetZ80();
        auto peek = [&](uint16_t a) { return _context->pMemory->DirectReadFromZ80Memory(a); };
        const uint16_t delay = _program.Sym("DELAYLEN");
        ASSERT_TRUE(RunUntil([&] { return (peek(delay) | peek(delay + 1)) != 0 && peek(_program.Sym("ONEPASS")) == 0; },
                             3000))
            << "the band loop never started: PC #" << std::hex << z80->pc << std::dec << ", frame "
            << (peek(_program.Sym("FRAMELEN")) | (peek(_program.Sym("FRAMELEN") + 1) << 8)) << ", body "
            << (peek(_program.Sym("BODYLEN")) | (peek(_program.Sym("BODYLEN") + 1) << 8)) << ", line "
            << int(peek(_program.Sym("LINELEN"))) << "\n" << Screen();

        RunFrames(5);
        _emulator->DisableTurboMode();
        _context->pFeatureManager->setFeature(Features::kScreenHQ, true);  // every tick rendered
        RunFrames(2);
        if (const char* dump = std::getenv("SNOWTEST_DUMP"))  // raw RGBA frame for a look (scratch)
        {
            std::ofstream f(dump, std::ios::binary);
            const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
            const uint32_t w = fb.width, h = fb.height;
            f.write(reinterpret_cast<const char*>(&w), 4);
            f.write(reinterpret_cast<const char*>(&h), 4);
            f.write(reinterpret_cast<const char*>(fb.memoryBuffer), static_cast<std::streamsize>(fb.memoryBufferSize));
        }
    }

    /// The 8 pixels of character row `row`, scan line `line`, column `cell`, as rendered
    std::vector<uint32_t> Cell(uint32_t row, uint32_t line, uint32_t cell) const
    {
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        const RasterDescriptor& rd = _context->pScreen->GetTimingDescriptor(_context->pScreen->GetVideoMode());
        const uint32_t* px = reinterpret_cast<const uint32_t*>(fb.memoryBuffer) +
                             (rd.screenOffsetTop + row * 8 + line) * fb.width + rd.screenOffsetLeft + cell * 8;
        return std::vector<uint32_t>(px, px + 8);
    }

    /// Columns where the LIVE band (rows 16-19) renders differently from the EXPECTED band (rows 8-11)
    std::string LiveDifferences() const
    {
        std::string differ;
        for (uint32_t cell = 0; cell < 32; cell++)
        {
            bool same = true;
            for (uint32_t row = 0; row < 4 && same; row++)
                for (uint32_t line = 0; line < 8 && same; line++)
                    same = Cell(16 + row, line, cell) == Cell(8 + row, line, cell);
            if (!same)
                differ += " " + std::to_string(cell);
        }
        return differ;
    }
};

TEST_F(SnowTest_Test, LiveMatchesExpectedOn48K)
{
    Run("48K");
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(LiveDifferences(), "") << "LIVE differs from EXPECTED in these columns (prediction: "
                                     << _program.prediction.text << ")";
    RunFrames(1);  // the loop lasts exactly one frame: the next frame is the same
    EXPECT_EQ(LiveDifferences(), "") << "the next frame differs: the loop drifts";
}

TEST_F(SnowTest_Test, LiveMatchesExpectedOn128K)
{
    Run("128K-48BASIC");
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(LiveDifferences(), "") << "LIVE differs from EXPECTED in these columns (prediction: "
                                     << _program.prediction.text << ")";
    RunFrames(1);  // the loop lasts exactly one frame: the next frame is the same
    EXPECT_EQ(LiveDifferences(), "") << "the next frame differs: the loop drifts";
}

/// No snow on the gate array and the clones: LIVE renders plain, so it differs from EXPECTED exactly in the
/// predicted columns
TEST_F(SnowTest_Test, LiveStaysPlainWithoutAFerrantiUla)
{
    std::string predicted;
    for (const char* editor : { "Plus3-48BASIC", "Pentagon-48BASIC" })
    {
        Run(editor);
        ASSERT_FALSE(HasFatalFailure());
        if (predicted.empty())
            for (size_t i = 0; i < 32; i++)
                if (_program.prediction.chars[i] != kPlain[i])
                    predicted += " " + std::to_string(i);
        EXPECT_EQ(LiveDifferences(), predicted) << editor;
        TearDown();
    }
}

/// The committed reference files are what the source builds (no drift)
TEST(SnowTestFiles_Test, CommittedFilesMatchTheSource)
{
    const SnowProgram p = BuildSnowTest();
    ASSERT_FALSE(p.bytes.empty());
    const std::string heading = "; snowtest symbols (generated by snowtest_test.cpp)";
    EXPECT_TRUE(ZxProgramFiles::ReadBinary(SnowPath("snowtest.tap")) == ZxProgramFiles::BuildTap("snowtest", kOrg, p.bytes))
        << "rebuild with UNREAL_SNOWTEST_EXPORT=1";
    EXPECT_TRUE(ZxProgramFiles::ReadBinary(SnowPath("snowtest.trd")) == ZxProgramFiles::BuildTrd("snowtest", kOrg, p.bytes))
        << "rebuild with UNREAL_SNOWTEST_EXPORT=1";
    EXPECT_TRUE(ZxProgramFiles::ReadBinary(SnowPath("snowtest.sym")) ==
                ZxProgramFiles::BuildSym(heading, p.asmResult.symbols))
        << "rebuild with UNREAL_SNOWTEST_EXPORT=1";
}

/// UNREAL_SNOWTEST_EXPORT=1 writes the reference files from the source
TEST(SnowTestFiles_Test, Export)
{
    if (!std::getenv("UNREAL_SNOWTEST_EXPORT"))
        GTEST_SKIP() << "set UNREAL_SNOWTEST_EXPORT=1 to write snowtest.tap / .trd / .sym";
    const SnowProgram p = BuildSnowTest();
    ASSERT_FALSE(p.bytes.empty());
    const std::string heading = "; snowtest symbols (generated by snowtest_test.cpp)";
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "snowtest.tap", ZxProgramFiles::BuildTap("snowtest", kOrg, p.bytes) },
        { "snowtest.trd", ZxProgramFiles::BuildTrd("snowtest", kOrg, p.bytes) },
        { "snowtest.sym", ZxProgramFiles::BuildSym(heading, p.asmResult.symbols) }
    };
    for (const auto& [file, data] : files)
    {
        std::ofstream out(SnowPath(file), std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        ASSERT_TRUE(out.good()) << file;
    }
}

/// The reference files loaded the way a user would (opt-in with UNREAL_TIMING_SUITES=1: a load and a run per
/// machine): the LIVE band as EXPECTED on the 48K and the 128K, plain on the Pentagon
struct SnowLoading
{
    const char* editor;
    const char* file;
    std::vector<std::string> commands;
    bool snows;
};

void PrintTo(const SnowLoading& l, std::ostream* os)
{
    *os << l.editor;
}

class SnowTestLoad_Test : public SnowTest_Test, public ::testing::WithParamInterface<SnowLoading>
{
};

TEST_P(SnowTestLoad_Test, LiveBandAsPredicted)
{
    const SnowLoading& l = GetParam();
    BootEditor(l.editor);
    ASSERT_FALSE(HasFatalFailure());
    _program = BuildSnowTest();
    _context->pFeatureManager->setFeature(Features::kFastTape, true);
    const std::string path = SnowPath(l.file);
    if (std::string(l.file).find(".tap") != std::string::npos)
        ASSERT_TRUE(_emulator->LoadTape(path));
    else
        ASSERT_TRUE(_emulator->LoadDisk(path));
    CommandTyper* typer = _context->pDebugManager->GetCommandTyper();
    for (const std::string& command : l.commands)
    {
        ASSERT_TRUE(typer->Request(command, CommandTyper::Options{})) << command;
        ASSERT_TRUE(RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000)) << command;
    }
    WaitForLoopAndRender();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(ScreenHas("EXPECTED") && ScreenHas("LIVE")) << Screen();
    std::string predicted;
    for (size_t i = 0; i < 32; i++)
        if (_program.prediction.chars[i] != kPlain[i])
            predicted += " " + std::to_string(i);
    EXPECT_EQ(LiveDifferences(), l.snows ? "" : predicted) << l.editor;
}

std::vector<SnowLoading> SnowLoadings()
{
    return {
        { "48K", "snowtest.tap", { "LOAD \"\"" }, true },
        { "128K-128BASIC", "snowtest.tap", { "LOAD \"\"" }, true },
        { "Pentagon-TRDOS", "snowtest.trd", { "RUN" }, false },
    };
}

std::string SnowLoadingName(const ::testing::TestParamInfo<SnowLoading>& info)
{
    std::string name = info.param.editor;
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = '_';
    return name;
}

INSTANTIATE_TEST_SUITE_P(Opt, SnowTestLoad_Test,
                         ::testing::ValuesIn(std::getenv("UNREAL_TIMING_SUITES") ? SnowLoadings() : std::vector<SnowLoading>{}),
                         SnowLoadingName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(SnowTestLoad_Test);
