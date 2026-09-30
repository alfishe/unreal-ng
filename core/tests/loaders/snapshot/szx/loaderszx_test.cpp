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

#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/corestate.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/upd765.h"
#include "emulator/io/tape/tape.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/covox.h"
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
    EXPECT_EQ(loader.GetError(),
              "the snapshot was saved on a ZX-Spectrum 128k, the running machine is a ZX-Spectrum 48k: "
              "create a ZX-Spectrum 128k to load it");

    Machine machine;
    std::string error;
    ASSERT_TRUE(LoaderSZX::ProbeMachine(Fixture("libspectrum/synth-pentagon512.szx"), machine, error)) << error;
    EXPECT_EQ(machine.model, MM_PENTAGON);
    EXPECT_EQ(machine.ramKb, 512u);
    EXPECT_EQ(DescribeModel(machine.model, machine.ramKb), "Pentagon 512K");
    EXPECT_FALSE(LoaderSZX::ProbeMachine(Fixture("libspectrum/synth-48.libspectrum.txt"), machine, error));
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

/// HALT: PC stays on the HALT while halted (here and in Fuse); a snapshot
/// taken there round-trips, and the INT that ends it returns past the HALT.
/// A file whose PC is already past the HALT is moved back onto it
TEST(LoaderSZXHalt_Test, HaltedStateRoundTripsAndTheIntReturnsPastTheHalt)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("szx-halt", "48K", 48, LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    Z80& cpu = *context->pCore->GetZ80();
    Memory& memory = *context->pMemory;

    // #8000: HALT, IM 1, interrupts on, 100 T-states after the INT (window over)
    Stage stage = Parse(Fixture("libspectrum/synth-48.szx"));
    stage.pages[2][0] = 0x76;
    stage.pages[2][1] = 0x00;
    stage.z80->pc = 0x8000;
    stage.z80->sp = 0xBF00;
    stage.z80->im = 1;
    stage.z80->iff1 = stage.z80->iff2 = 1;
    stage.z80->flags = 0;
    stage.z80->cyclesStart = 100;
    Report report;
    std::string error;
    ASSERT_TRUE(LoaderSZX::Commit(context, stage, report, error)) << error;

    // Execute the HALT: the snapshot now says HALTED with PC on the HALT
    emulator->RunSingleCPUCycle();
    ASSERT_EQ(cpu.halted, 1);
    ASSERT_EQ(cpu.pc, 0x8000);
    Stage saved;
    ASSERT_TRUE(LoaderSZX::Capture(context, saved, error)) << error;
    EXPECT_EQ(saved.z80->pc, 0x8000);
    EXPECT_TRUE(saved.z80->flags & kHalted);

    // Load it back and let the next INT end the HALT
    report = Report{};
    ASSERT_TRUE(LoaderSZX::Commit(context, saved, report, error)) << error;
    EXPECT_EQ(cpu.halted, 1);
    EXPECT_EQ(cpu.pc, 0x8000);
    for (int step = 0; step < 40000 && cpu.pc != 0x0038; step++)
        emulator->RunSingleCPUCycle();
    ASSERT_EQ(cpu.pc, 0x0038) << "the INT was not taken";
    EXPECT_EQ(cpu.halted, 0);
    const uint16_t returnAddress =
        static_cast<uint16_t>(memory.DirectReadFromZ80Memory(cpu.sp) | (memory.DirectReadFromZ80Memory(cpu.sp + 1) << 8));
    EXPECT_EQ(returnAddress, 0x8001) << "the INT returns past the HALT";

    // Another writer's convention: PC stored past the HALT
    saved.z80->pc = 0x8001;
    report = Report{};
    ASSERT_TRUE(LoaderSZX::Commit(context, saved, report, error)) << error;
    EXPECT_EQ(cpu.pc, 0x8000) << "moved back onto the HALT";
    EXPECT_NE(report.ToText().find("moved back onto the HALT"), std::string::npos) << report.ToText();
    manager->RemoveEmulator(emulator->GetId());
}

namespace
{
    std::string TestFile(const std::string& relative)
    {
        return (TestPathHelper::FindProjectRoot() / "testdata" / relative).string();
    }

    MediaResult InsertFile(EmulatorContext* context, const std::string& slot, const std::string& path)
    {
        MediaSource source;
        source.path = path;
        InsertOptions options;
        options.immediate = true;
        return context->pMediaManager->Insert(slot, source, options);
    }

    std::shared_ptr<Emulator> CreateWithSound(const char* id, const char* model, uint32_t ramKb)
    {
        SoundCardScope sound(TestSound::GeneralSound | TestSound::TurboSound);
        return EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM(id, model, ramKb, LoggerLevel::LogError);
    }
}  // namespace

/// Media and devices round-trip on a Pentagon: a linked TRD on drive A with
/// its head cylinder, a linked tape at block 2, the classic GS card (CPU,
/// page, volumes, DAC, RAM) and the Covox level come back on a fresh machine
TEST(LoaderSZXDevices_Test, MediaAndDevicesRoundTripOnAPentagon)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto source = CreateWithSound("szx-dev-a", "PENTAGON", 128);
    auto target = CreateWithSound("szx-dev-b", "PENTAGON", 128);
    ASSERT_TRUE(source && target);
    EmulatorContext* a = source->GetContext();
    EmulatorContext* b = target->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(a->pSoundManager, GSTypeKind::Z80));
    ASSERT_TRUE(FitGeneralSoundCard(b->pSoundManager, GSTypeKind::Z80));

    const std::string trd = TestFile("loaders/trd/zx-format8.trd");
    const std::string tap = TestFile("loaders/tap/aydetect.tap");
    ASSERT_TRUE(InsertFile(a, "fdd.a", trd).Ok());
    ASSERT_TRUE(InsertFile(a, "tape", tap).Ok());
    ASSERT_TRUE(a->pTape->EnsureImageLoaded());
    ASSERT_TRUE(a->pTape->SeekToBlock(2));
    a->coreState.diskDrives[0]->setTrack(5);
    ASSERT_TRUE(a->pSoundManager->hasCovox());
    a->pSoundManager->getCovox()->portDeviceOutMethod(0x00FB, 0x9C);
    source->RunNFrames(3, true);  // the GS card runs its firmware

    Stage saved;
    std::string error;
    ASSERT_TRUE(LoaderSZX::Capture(a, saved, error)) << error;
    ASSERT_EQ(saved.betaDisks.size(), 1u);
    EXPECT_EQ(saved.betaDisks[0].type, DiskTrd);
    EXPECT_EQ(saved.betaDisks[0].cylinder, 5);
    ASSERT_TRUE(saved.tape);
    EXPECT_EQ(saved.tape->block, 2);
    ASSERT_TRUE(saved.gs);
    // GS128: four 32 KB pages; GS512: fifteen (the format's 0..14)
    const size_t ramKb = a->pSoundManager->getGeneralSound()->getRamSizeKB();
    EXPECT_EQ(saved.gs->model, ramKb >= 512 ? 1 : 0);
    EXPECT_EQ(saved.gsPages.size(), ramKb >= 512 ? 15u : 4u);
    ASSERT_TRUE(saved.covox);
    EXPECT_EQ(*saved.covox, 0x9C);

    // Through the bytes, then onto the other machine
    const std::vector<uint8_t> bytes = SzxWriter::Write(saved);
    Stage parsed;
    ASSERT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), parsed, error)) << error;
    Report report;
    ASSERT_TRUE(LoaderSZX::Commit(b, parsed, report, error)) << error;

    EXPECT_EQ(b->pMediaManager->Info("fdd.a")->source, trd);
    EXPECT_EQ(b->pMediaManager->Info("fdd.a")->access, AccessMode::Session) << "linked media never write through";
    EXPECT_EQ(b->coreState.diskDrives[0]->getTrack(), 5);
    EXPECT_EQ(b->pMediaManager->Info("tape")->source, tap);
    ASSERT_TRUE(b->pTape->GetPosition());
    EXPECT_EQ(b->pTape->GetPosition()->blockIndex, 2u);
    uint8_t latches[4] = {};
    b->pSoundManager->getCovox()->getDacLatches(latches);
    EXPECT_EQ(latches[3], 0x9C);

    GeneralSoundCard& gsA = *a->pSoundManager->getGeneralSound();
    GeneralSoundCard& gsB = *b->pSoundManager->getGeneralSound();
    for (GSCpuRegister reg : {GSCpuRegister::AF, GSCpuRegister::BC, GSCpuRegister::HL, GSCpuRegister::SP, GSCpuRegister::PC,
                              GSCpuRegister::IX, GSCpuRegister::I, GSCpuRegister::IM, GSCpuRegister::IFF1})
        EXPECT_EQ(gsA.getCPUReg(reg), gsB.getCPUReg(reg)) << "GS register " << static_cast<int>(reg);
    EXPECT_EQ(gsA.getMPAG(), gsB.getMPAG());
    for (int channel = 0; channel < 4; channel++)
    {
        EXPECT_EQ(gsA.getChannelVolume(channel), gsB.getChannelVolume(channel));
        EXPECT_EQ(gsA.getChannelSample(channel), gsB.getChannelSample(channel));
    }
    Stage again;
    ASSERT_TRUE(LoaderSZX::Capture(b, again, error)) << error;
    EXPECT_EQ(again.gsPages, saved.gsPages) << "GS RAM";
    EXPECT_NE(report.ToText().find("BDSK A: applied"), std::string::npos) << report.ToText();

    manager->RemoveEmulator(source->GetId());
    manager->RemoveEmulator(target->GetId());
}

/// Embedded images: a BDSK disk and a TAPE image inside the file are staged in
/// temporary files that go with their media; a relative link is found next
/// to the snapshot
TEST(LoaderSZXDevices_Test, EmbeddedImagesAndRelativeLinks)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("szx-embed", "PENTAGON", 128, LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();

    auto read = [](const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    };
    Stage stage = Parse(Fixture("libspectrum/synth-pentagon.szx"));
    BetaDisk disk;
    disk.drive = 1;
    disk.type = DiskTrd;
    disk.cylinder = 3;
    disk.image = read(TestFile("loaders/trd/zx-format8.trd"));
    stage.betaDisks.push_back(disk);
    szx::Tape tape;
    tape.extension = "tap";
    tape.block = 1;
    tape.image = read(TestFile("loaders/tap/aydetect.tap"));
    stage.tape = tape;

    // Through the bytes (compressed payloads), then committed
    const std::vector<uint8_t> bytes = SzxWriter::Write(stage);
    Stage parsed;
    std::string error;
    ASSERT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), parsed, error)) << error;
    EXPECT_EQ(parsed.betaDisks.at(0).image, disk.image);
    EXPECT_EQ(parsed.tape->image, tape.image);
    Report report;
    ASSERT_TRUE(LoaderSZX::Commit(context, parsed, report, error)) << error;

    const std::optional<SlotInfo> drive = context->pMediaManager->Info("fdd.b");
    ASSERT_TRUE(drive && drive->present) << report.ToText();
    const std::string staged = drive->source;
    EXPECT_TRUE(FileHelper::FileExists(staged));
    EXPECT_EQ(context->coreState.diskDrives[1]->getTrack(), 3);
    ASSERT_TRUE(context->pTape->GetPosition());
    EXPECT_EQ(context->pTape->GetPosition()->blockIndex, 1u);
    ASSERT_TRUE(context->pMediaManager->Eject("fdd.b").Ok());
    EXPECT_FALSE(FileHelper::FileExists(staged)) << "the staged image goes with its medium";

    // A relative link, found in the snapshot's folder
    Stage linked = Parse(Fixture("libspectrum/synth-pentagon.szx"));
    linked.folder = (TestPathHelper::FindProjectRoot() / "testdata" / "loaders" / "trd").string();
    BetaDisk link;
    link.fileName = "zx-format8.trd";
    linked.betaDisks.push_back(link);
    report = Report{};
    ASSERT_TRUE(LoaderSZX::Commit(context, linked, report, error)) << error;
    EXPECT_EQ(context->pMediaManager->Info("fdd.a")->source, TestFile("loaders/trd/zx-format8.trd"));
    manager->RemoveEmulator(emulator->GetId());
}

/// +3: the motor and a linked DSK on drive A come back
TEST(LoaderSZXDevices_Test, Plus3DiskRoundTrip)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto source = manager->CreateEmulatorWithModelAndRAM("szx-p3-a", "PLUS3", 128, LoggerLevel::LogError);
    auto target = manager->CreateEmulatorWithModelAndRAM("szx-p3-b", "PLUS3", 128, LoggerLevel::LogError);
    ASSERT_TRUE(source && target);
    const std::string dsk = TestFile("loaders/dsk/plus3-blank.dsk");
    ASSERT_TRUE(InsertFile(source->GetContext(), "fdd.a", dsk).Ok());
    source->GetContext()->pUPD765->setMotor(true);
    Stage saved;
    std::string error;
    ASSERT_TRUE(LoaderSZX::Capture(source->GetContext(), saved, error)) << error;
    ASSERT_TRUE(saved.plus3);
    EXPECT_EQ(saved.plus3->motorOn, 1);
    ASSERT_EQ(saved.dskFiles.size(), 1u);
    EXPECT_TRUE(saved.betaDisks.empty()) << "the +3 has no Beta 128";

    const std::vector<uint8_t> bytes = SzxWriter::Write(saved);
    Stage parsed;
    ASSERT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), parsed, error)) << error;
    // SPCR #1FFD carries the motor bit too; the +3 block must agree with it
    parsed.spec->port1FFDorEFF7 |= 0x08;
    Report report;
    ASSERT_TRUE(LoaderSZX::Commit(target->GetContext(), parsed, report, error)) << error;
    EXPECT_TRUE(target->GetContext()->pUPD765->getMotor());
    EXPECT_EQ(target->GetContext()->pMediaManager->Info("fdd.a")->source, dsk);
    manager->RemoveEmulator(source->GetId());
    manager->RemoveEmulator(target->GetId());
}

/// Blocks for hardware we do not emulate are read and reported, never applied
TEST(LoaderSZXDevices_Test, UnemulatedHardwareIsReported)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModelAndRAM("szx-report", "PENTAGON", 128, LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    LoaderSZX loader(emulator->GetContext(), Fixture("other/spectaculator-pentagon-crazylove.szx"));
    ASSERT_TRUE(loader.load()) << loader.GetError();
    const std::string text = loader.GetReport().ToText();
    for (const char* line : {"IF1: ignored", "MFCE: ignored", "ZXPR: ignored", "JOY: ignored (joysticks are not emulated)"})
        EXPECT_NE(text.find(line), std::string::npos) << line << "\n" << text;
    EXPECT_NE(text.find("BDSK A: ignored (linked image not found"), std::string::npos) << text;
    manager->RemoveEmulator(emulator->GetId());
}

/// TurboSound FM replaces TurboSound: the AY block goes into the SSG half of
/// YM2203 chip 1, fully applied (never an error), with the YM2203's own
/// address latch on the selected register
TEST(LoaderSZXSound_Test, TurboSoundFmTakesTheAyBlock)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator;
    {
        SoundCardScope sound(TestSound::TurboSound);
        emulator = manager->CreateEmulatorWithModelAndRAM("szx-tsfm", "PENTAGON", 128, LoggerLevel::LogError);
    }
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    ITurboSoundDevice* device = context->pSoundManager->getTurboSound();
    ASSERT_TRUE(device && device->hasFm()) << "the shipped Pentagon config fits TurboSound FM";

    LoaderSZX loader(context, Fixture("libspectrum/synth-pentagon.szx"));
    ASSERT_TRUE(loader.load()) << loader.GetError();
    EXPECT_NE(loader.GetReport().ToText().find("AY: applied (into the SSG half"), std::string::npos) << loader.GetReport().ToText();
    SoundChip_AY8910* ssg = context->pSoundManager->getAYChip(0);
    for (uint8_t reg = 0; reg < 16; reg++)
        EXPECT_EQ(ssg->readRegister(reg), reg == 7 ? 0x38 : reg * 3 + 1) << "SSG register " << int(reg);
    // The selected register (7) reaches the chip's own latch: a data write
    // with no address write before it lands there
    device->portDeviceOutMethod(0xBFFD, 0x3F);
    EXPECT_EQ(ssg->readRegister(7), 0x3F);

    Stage saved;
    std::string error;
    ASSERT_TRUE(LoaderSZX::Capture(context, saved, error)) << error;
    ASSERT_TRUE(saved.ay) << "a TSFM machine saves the AY block";
    EXPECT_EQ(saved.ay->currentRegister, 7);
    manager->RemoveEmulator(emulator->GetId());
}

/// NeoGS replaces the General Sound: a GS block loads without an error (the
/// card keeps its own firmware), and saving leaves the classic-card block out
TEST(LoaderSZXSound_Test, NeoGsAcceptsTheGsBlock)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator;
    {
        SoundCardScope sound(TestSound::GeneralSound);
        emulator = manager->CreateEmulatorWithModelAndRAM("szx-ngs", "PENTAGON", 128, LoggerLevel::LogError);
    }
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS));

    Stage stage = Parse(Fixture("libspectrum/synth-pentagon.szx"));
    stage.gs = GeneralSound{};
    stage.gsPages[0] = std::vector<uint8_t>(kGsPageSize, 0x55);
    Report report;
    std::string error;
    ASSERT_TRUE(LoaderSZX::Commit(context, stage, report, error)) << error;
    const std::string text = report.ToText();
    EXPECT_NE(text.find("GS: approximated (NeoGS replaces the GS"), std::string::npos) << text;
    EXPECT_EQ(text.find("GS: ignored"), std::string::npos) << text;

    Stage saved;
    ASSERT_TRUE(LoaderSZX::Capture(context, saved, error)) << error;
    EXPECT_FALSE(saved.gs) << "the GS block describes the classic card";
    EXPECT_FALSE(saved.warnings.empty());
    manager->RemoveEmulator(emulator->GetId());
}
