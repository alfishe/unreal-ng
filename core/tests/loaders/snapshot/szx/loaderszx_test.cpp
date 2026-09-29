// LoaderSZX on real machines: the libspectrum-written reference files
// (testdata/loaders/szx/libspectrum/synth-*.szx, known values, see
// tools/verification/szx) load on their model with the right registers,
// paging, frame position, MEMPTR / Q / interrupt shadow and border; saving
// gives the same state back; a libspectrum SZX of an SNA equals our SNA load;
// the model must match; the INT window survives a mid-frame restore.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/snapshot/szx/loaderszx.h"
#include "loaders/snapshot/szx/szxreader.h"
#include "loaders/snapshot/szx/szxwriter.h"

using namespace szx;

namespace
{
    std::string Fixture(const std::string& name)
    {
        return (TestPathHelper::FindProjectRoot() / "testdata" / "loaders" / "szx" / name).string();
    }

    Stage Parse(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Stage stage;
        std::string error;
        EXPECT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), stage, error)) << error;
        return stage;
    }

    struct SynthCase
    {
        const char* file;
        const char* model;
        uint32_t ramKb;
    };

    class LoaderSZX_Test : public ::testing::TestWithParam<SynthCase>
    {
    protected:
        std::shared_ptr<Emulator> _emulator;
        EmulatorContext* _context = nullptr;

        void Create(const char* model, uint32_t ramKb)
        {
            SoundCardScope sound(TestSound::TurboSound);  // the AY block needs the chip
            _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM("szx-test", model, ramKb, LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr) << model;
            _context = _emulator->GetContext();
        }

        void TearDown() override
        {
            if (_emulator)
                EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
        }
    };
}  // namespace

/// Values set by `szxtool synth`: registers #1122..., IM 2, MEMPTR #4321,
/// 12345 T-states after the INT, EI shadow and FSET, border 5, #7FFD = #13
/// (page 3 at #C000), page n filled with n * 16 + offset / 1024
TEST_P(LoaderSZX_Test, LoadsTheReferenceFileAndSavesItBack)
{
    const SynthCase& param = GetParam();
    Create(param.model, param.ramKb);
    LoaderSZX loader(_context, Fixture(param.file));
    ASSERT_TRUE(loader.load()) << loader.GetError();

    Z80& cpu = *_context->pCore->GetZ80();
    EXPECT_EQ(cpu.a, 0x11);
    EXPECT_EQ(cpu.f, 0x22);
    EXPECT_EQ(cpu.bc, 0x3344);
    EXPECT_EQ(cpu.hl, 0x7788);
    EXPECT_EQ(cpu.alt.hl, 0xF001);
    EXPECT_EQ(cpu.ix, 0x1234);
    EXPECT_EQ(cpu.iy, 0x5C3A);
    EXPECT_EQ(cpu.sp, 0xBEEF);
    EXPECT_EQ(cpu.pc, 0x8000);
    EXPECT_EQ(cpu.i, 0x3F);
    EXPECT_EQ(cpu.r_low, 0x85);
    EXPECT_EQ(cpu.r_hi, 0x80);
    EXPECT_EQ(cpu.im, 2);
    EXPECT_EQ(cpu.iff1, 1);
    EXPECT_EQ(cpu.memptr, 0x4321);
    EXPECT_EQ(cpu.q, 0x22) << "FSET: Q holds F";
    EXPECT_EQ(cpu.boundary, Z80_BOUNDARY_INT_SHADOW);
    EXPECT_EQ(cpu.halted, 0);
    EXPECT_EQ(cpu.t, LoaderSZX::FramePositionFromIntCount(_context, 12345));
    EXPECT_EQ(LoaderSZX::IntCountFromFramePosition(_context, cpu.t), 12345u);
    EXPECT_EQ(_context->emulatorState.border_attr, 5);

    Memory& memory = *_context->pMemory;
    const bool is48 = std::string(param.model) == "48K";
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x4000), 5 * 16) << "page 5 at #4000";
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0x8000), 2 * 16) << "page 2 at #8000";
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0xC000), is48 ? 0 : 3 * 16) << "the top page";
    EXPECT_EQ(memory.DirectReadFromZ80Memory(0xFFFF), is48 ? 15 : 3 * 16 + 15);
    if (!is48)
    {
        EXPECT_EQ(_context->emulatorState.p7FFD, 0x13);
        EXPECT_EQ(memory.GetRAMPageForBank3(), 3);
    }

    // Save: the same stage comes back (pages, registers, frame position)
    const Stage original = Parse(Fixture(param.file));
    Stage saved;
    std::string error;
    ASSERT_TRUE(LoaderSZX::Capture(_context, saved, error)) << error;
    EXPECT_EQ(saved.machineId, original.machineId);
    EXPECT_EQ(saved.pages, original.pages);
    const Z80Regs& a = *original.z80;
    const Z80Regs& b = *saved.z80;
    EXPECT_EQ(b.af, a.af);
    EXPECT_EQ(b.bc, a.bc);
    EXPECT_EQ(b.hl1, a.hl1);
    EXPECT_EQ(b.pc, a.pc);
    EXPECT_EQ(b.sp, a.sp);
    EXPECT_EQ(b.r, a.r);
    EXPECT_EQ(b.memptr, a.memptr);
    EXPECT_EQ(b.cyclesStart, a.cyclesStart);
    EXPECT_EQ(b.flags, a.flags);
    EXPECT_EQ(saved.spec->port7FFD, original.spec->port7FFD);
    EXPECT_EQ(saved.spec->port1FFDorEFF7, original.spec->port1FFDorEFF7);
    EXPECT_EQ(saved.spec->border, original.spec->border);
    // TurboSound FM configs (YM2203) have no AY-3-8910 to hold the block
    if (original.ay && _context->pSoundManager->getAYChip(0))
    {
        ASSERT_TRUE(saved.ay);
        EXPECT_EQ(saved.ay->registers, original.ay->registers);
        EXPECT_EQ(saved.ay->currentRegister, original.ay->currentRegister);
    }

    if (original.beta)
    {
        ASSERT_TRUE(saved.beta) << "a machine with a Beta 128 writes B128";
        EXPECT_EQ(saved.beta->system, original.beta->system);
        EXPECT_EQ(saved.beta->flags & kBetaPaged, original.beta->flags & kBetaPaged);
    }

    // And the written bytes parse to the same stage
    const std::vector<uint8_t> bytes = SzxWriter::Write(saved);
    Stage reparsed;
    ASSERT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), reparsed, error)) << error;
    EXPECT_EQ(reparsed.pages, saved.pages);
    EXPECT_EQ(reparsed.z80->cyclesStart, saved.z80->cyclesStart);
}

INSTANTIATE_TEST_SUITE_P(Models, LoaderSZX_Test,
                         ::testing::Values(SynthCase{"libspectrum/synth-48.szx", "48K", 48},
                                           SynthCase{"libspectrum/synth-128.szx", "128k", 128},
                                           SynthCase{"libspectrum/synth-plus2.szx", "PLUS2", 128},
                                           SynthCase{"libspectrum/synth-plus2a.szx", "PLUS2A", 128},
                                           SynthCase{"libspectrum/synth-plus3.szx", "PLUS3", 128},
                                           SynthCase{"libspectrum/synth-pentagon.szx", "PENTAGON", 128},
                                           SynthCase{"libspectrum/synth-pentagon512.szx", "PENTAGON", 512},
                                           SynthCase{"libspectrum/synth-pentagon1024.szx", "PENTAGON", 1024},
                                           SynthCase{"libspectrum/synth-scorpion.szx", "SCORPION", 256}),
                         [](const ::testing::TestParamInfo<SynthCase>& info) {
                             std::string name = std::filesystem::path(info.param.file).stem().string();
                             for (char& c : name)
                                 if (c == '-')
                                     c = '_';
                             return name;
                         });

/// libspectrum's SZX of z80full.sna (Patrik Rak, MIT) and our own SNA loader
/// give the same machine
TEST(LoaderSZXInterop_Test, LibspectrumSzxOfAnSnaEqualsOurSnaLoad)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto sna = manager->CreateEmulatorWithModelAndRAM("szx-sna", "48K", 48, LoggerLevel::LogError);
    auto szx = manager->CreateEmulatorWithModelAndRAM("szx-szx", "48K", 48, LoggerLevel::LogError);
    ASSERT_TRUE(sna && szx);
    ASSERT_TRUE(sna->LoadSnapshot((TestPathHelper::FindProjectRoot() / "testdata/loaders/sna/z80full.sna").string()));
    ASSERT_TRUE(szx->LoadSnapshot(Fixture("libspectrum/z80full-48k.szx")));

    const Z80& a = *sna->GetContext()->pCore->GetZ80();
    const Z80& b = *szx->GetContext()->pCore->GetZ80();
    EXPECT_EQ(a.af, b.af);
    EXPECT_EQ(a.bc, b.bc);
    EXPECT_EQ(a.de, b.de);
    EXPECT_EQ(a.hl, b.hl);
    EXPECT_EQ(a.alt.af, b.alt.af);
    EXPECT_EQ(a.ix, b.ix);
    EXPECT_EQ(a.iy, b.iy);
    EXPECT_EQ(a.sp, b.sp);
    EXPECT_EQ(a.pc, b.pc);
    EXPECT_EQ(a.i, b.i);
    EXPECT_EQ((a.r_low & 0x7F) | (a.r_hi & 0x80), (b.r_low & 0x7F) | (b.r_hi & 0x80));
    EXPECT_EQ(a.iff1, b.iff1);
    EXPECT_EQ(a.im, b.im);
    for (uint32_t address = 0x4000; address < 0x10000; address++)
        ASSERT_EQ(sna->GetContext()->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(address)),
                  szx->GetContext()->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(address)))
            << std::hex << address;
    EXPECT_EQ(sna->GetContext()->emulatorState.border_attr, szx->GetContext()->emulatorState.border_attr);
    manager->RemoveEmulator(sna->GetId());
    manager->RemoveEmulator(szx->GetId());
}

TEST(LoaderSZXModel_Test, AnotherModelIsRefused)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("szx-model", "48K", 48, LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    LoaderSZX loader(emulator->GetContext(), Fixture("libspectrum/synth-128.szx"));
    EXPECT_FALSE(loader.load());
    EXPECT_NE(loader.GetError().find("switch the model"), std::string::npos) << loader.GetError();
    manager->RemoveEmulator(emulator->GetId());
}

/// A restore inside the INT window: with T-states left (hold > 0) the INT is
/// still to come; with none left it was served and must not fire again
TEST(LoaderSZXFrame_Test, TheIntWindowSurvivesAMidFrameRestore)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("szx-int", "48K", 48, LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    Stage stage = Parse(Fixture("libspectrum/synth-48.szx"));
    stage.z80->cyclesStart = 10;

    Report report;
    std::string error;
    stage.z80->holdIntReqCycles = 0;
    ASSERT_TRUE(LoaderSZX::Commit(context, stage, report, error)) << error;
    Z80& cpu = *context->pCore->GetZ80();
    EXPECT_EQ(cpu.int_acked_in_pulse, 1) << "no T-states left: the INT was served";
    Stage saved;
    ASSERT_TRUE(LoaderSZX::Capture(context, saved, error)) << error;
    EXPECT_EQ(saved.z80->cyclesStart, 10u);
    EXPECT_EQ(saved.z80->holdIntReqCycles, 0);

    stage.z80->holdIntReqCycles = 20;
    report = Report{};
    ASSERT_TRUE(LoaderSZX::Commit(context, stage, report, error)) << error;
    EXPECT_EQ(cpu.int_acked_in_pulse, 0) << "T-states left: the INT is still to come";
    ASSERT_TRUE(LoaderSZX::Capture(context, saved, error)) << error;
    EXPECT_EQ(saved.z80->holdIntReqCycles, context->config.intlen - 1 - 10);
    manager->RemoveEmulator(emulator->GetId());
}

/// The Emulator entry points take .szx both ways
TEST(LoaderSZXEmulator_Test, SaveAndLoadThroughTheEmulator)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("szx-emu", "PENTAGON", 128, LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    ASSERT_TRUE(emulator->LoadSnapshot(Fixture("other/zxmak2-pentagon-cpd-test.szx")));
    const uint16_t pc = emulator->GetContext()->pCore->GetZ80()->pc;
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("loaderszx-roundtrip.szx");
    ASSERT_TRUE(emulator->SaveSnapshot(path));
    emulator->GetContext()->pCore->GetZ80()->pc = 0;
    ASSERT_TRUE(emulator->LoadSnapshot(path));
    EXPECT_EQ(emulator->GetContext()->pCore->GetZ80()->pc, pc);
    std::filesystem::remove(path);
    manager->RemoveEmulator(emulator->GetId());
}

/// Interop check with libspectrum (tools/verification/szx/check-interop.sh):
/// with UNREALNG_SZX_EXPORT_DIR set, each reference file is loaded and saved
/// again by us into that folder, for szxtool to read. Skipped otherwise
TEST_P(LoaderSZX_Test, ExportForTheLibspectrumCheck)
{
    const char* folder = std::getenv("UNREALNG_SZX_EXPORT_DIR");
    if (!folder || !*folder)
        GTEST_SKIP() << "UNREALNG_SZX_EXPORT_DIR not set (tools/verification/szx/check-interop.sh)";
    const SynthCase& param = GetParam();
    Create(param.model, param.ramKb);
    LoaderSZX loader(_context, Fixture(param.file));
    ASSERT_TRUE(loader.load()) << loader.GetError();
    const std::string name = std::filesystem::path(param.file).stem().string();
    LoaderSZX writer(_context, (std::filesystem::path(folder) / (name + ".szx")).string());
    ASSERT_TRUE(writer.save()) << writer.GetError();
}
