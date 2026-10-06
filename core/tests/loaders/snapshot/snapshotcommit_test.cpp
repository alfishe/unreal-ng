/// @file snapshotcommit_test.cpp
/// @brief The legacy commits read the SnapshotImage (snapshot pipeline P9, PLAN #84): what the machine ends up with is what
/// the image says, not what the loader's private staging holds. Each test stages a real fixture, changes the IMAGE the plan
/// left, commits, and checks the machine follows the image. Plus the cases the image had to learn to carry: a 48K SNA whose
/// stack is not in the file's RAM, and the Z80 files no machine can take.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/snapshot/loader_sna.h"
#include "loaders/snapshot/loader_z80.h"
#include "loaders/snapshot/szx/loaderszx.h"
#include "loaders/snapshot/szx/szxreader.h"

namespace
{
std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class Commit_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // The runner leaves the TurboSound slot empty; the AY registers of a snapshot need the chip
        _sound = std::make_unique<SoundCardScope>(TestSound::TurboSound);
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }

    Z80& Cpu() { return *_context->pCore->GetZ80(); }
    Memory& Mem() { return *_context->pMemory; }

    std::unique_ptr<SoundCardScope> _sound;
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
};
}  // namespace

// SNA 128: registers, a bank's bytes, #7FFD, the TR-DOS flag and the border come from the image
TEST_F(Commit_Test, ASna128CommitFollowsTheImage)
{
    LoaderSNACUT loader(_context, TestPathHelper::GetTestDataPath("loaders/sna/action.sna"));
    ASSERT_TRUE(loader.validate());
    ASSERT_TRUE(loader.loadToStaging());
    ASSERT_TRUE(loader.planSnapshot());

    snapshot::Image& image = loader._image;
    ASSERT_EQ(image.memoryModel, snapshot::MemoryModel::Mem128k);
    image.cpu.pc = 0x1234;
    image.cpu.sp = 0xBEEF;
    image.cpu.hl = 0xCAFE;
    image.cpu.af2 = 0x5A5A;
    image.cpu.im = 2;
    image.cpu.iff1 = image.cpu.iff2 = true;
    image.cpu.r = 0xA7;
    image.border = 5;
    image.paging.p7FFD = 0x13;                 // bank 3 on top, ROM 0
    image.banks.at(5)[0x0100] = 0xE7;          // a screen byte
    image.banks.at(3)[0x0000] = 0x3C;          // the top bank

    ASSERT_TRUE(loader.applySnapshotFromStaging());

    EXPECT_EQ(Cpu().pc, 0x1234);
    EXPECT_EQ(Cpu().sp, 0xBEEF);
    EXPECT_EQ(Cpu().hl, 0xCAFE);
    EXPECT_EQ(Cpu().alt.af, 0x5A5A);
    EXPECT_EQ(Cpu().im, 2);
    EXPECT_EQ(Cpu().iff1, 1);
    EXPECT_EQ(Cpu().r_low, 0xA7);
    EXPECT_EQ(Cpu().r_hi, 0x80);
    EXPECT_EQ(_context->emulatorState.border_attr, 5);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x13);
    EXPECT_EQ(Mem().GetRAMPageForBank(3), 3u);
    EXPECT_EQ(Mem().DirectReadFromZ80Memory(0x4100), 0xE7);
    EXPECT_EQ(Mem().DirectReadFromZ80Memory(0xC000), 0x3C);
}

// SNA 48: the commit takes the PC the image read off the stack, and the stack pointer past it
TEST_F(Commit_Test, ASna48CommitFollowsTheImage)
{
    LoaderSNACUT loader(_context, TestPathHelper::GetTestDataPath("loaders/sna/z80full.sna"));
    ASSERT_TRUE(loader.validate());
    ASSERT_TRUE(loader.loadToStaging());
    ASSERT_TRUE(loader.planSnapshot());

    snapshot::Image& image = loader._image;
    ASSERT_EQ(image.memoryModel, snapshot::MemoryModel::Mem48k);
    ASSERT_FALSE(image.cpu.pcOnMachineStack);
    const uint16_t sp = image.cpu.sp;
    image.cpu.pc = 0x8765;
    ASSERT_TRUE(loader.applySnapshotFromStaging());
    EXPECT_EQ(Cpu().pc, 0x8765) << "the image's PC, not the word on the stack";
    EXPECT_EQ(Cpu().sp, sp) << "the image's SP (already past the popped word)";
}

// A 48K SNA whose stack is in the ROM or at the top of memory: the image cannot read the PC, the machine's memory at SP
// decides, as the commit always did
TEST_F(Commit_Test, ASna48WithTheStackOutsideTheFilesRamPopsFromTheMachine)
{
    for (uint16_t sp : {static_cast<uint16_t>(0x1000), static_cast<uint16_t>(0xFFFF)})
    {
        std::vector<uint8_t> file = ReadFile(TestPathHelper::GetTestDataPath("loaders/sna/z80full.sna"));
        file[23] = static_cast<uint8_t>(sp & 0xFF);
        file[24] = static_cast<uint8_t>(sp >> 8);
        const std::string path = TestPathHelper::GetUniqueTestScratchPath("sna-stack.sna");
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        }

        LoaderSNACUT loader(_context, path);
        ASSERT_TRUE(loader.validate());
        ASSERT_TRUE(loader.loadToStaging());
        ASSERT_TRUE(loader.planSnapshot());
        EXPECT_TRUE(loader._image.cpu.pcOnMachineStack) << std::hex << sp;
        EXPECT_EQ(loader._image.cpu.sp, sp) << "raw: the commit pops it";
        ASSERT_TRUE(loader.applySnapshotFromStaging());

        const uint16_t low = Mem().DirectReadFromZ80Memory(sp);
        const uint16_t high = Mem().DirectReadFromZ80Memory(static_cast<uint16_t>(sp + 1));
        EXPECT_EQ(Cpu().pc, static_cast<uint16_t>(high << 8 | low)) << std::hex << sp;
        EXPECT_EQ(Cpu().sp, static_cast<uint16_t>(sp + 2)) << std::hex << sp;
        std::remove(path.c_str());
    }
}

// Z80 v3 on the Pentagon: registers (R's two halves), #7FFD, the frame position, the AY registers and a bank's bytes
TEST_F(Commit_Test, AZ80CommitFollowsTheImage)
{
    LoaderZ80CUT loader(_context, TestPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80"));
    ASSERT_TRUE(loader.validate());
    ASSERT_TRUE(loader.stageLoad());
    ASSERT_TRUE(loader.planSnapshot());

    snapshot::Image& image = loader._image;
    ASSERT_EQ(image.memoryModel, snapshot::MemoryModel::Mem128k);
    ASSERT_FALSE(image.ay.empty());
    image.cpu.pc = 0x4321;
    image.cpu.sp = 0xF00D;
    image.cpu.de = 0x1357;
    image.cpu.hl2 = 0x2468;
    image.cpu.r = 0xD5;
    image.cpu.im = 1;
    image.cpu.iff1 = true;
    image.cpu.iff2 = false;
    image.border = 6;
    image.paging.p7FFD = 0x14;
    image.framePosition = 1000;
    image.ay[0].registers[7] = 0x3E;
    image.ay[0].registers[8] = 0x0B;
    image.ay[0].selected = 9;
    image.banks.at(5)[0x0200] = 0x99;

    loader.commitFromStage();

    EXPECT_EQ(Cpu().pc, 0x4321);
    EXPECT_EQ(Cpu().sp, 0xF00D);
    EXPECT_EQ(Cpu().de, 0x1357);
    EXPECT_EQ(Cpu().alt.hl, 0x2468);
    EXPECT_EQ(Cpu().r_low, 0x55);
    EXPECT_EQ(Cpu().r_hi, 0x80);
    EXPECT_EQ(Cpu().im, 1);
    EXPECT_EQ(Cpu().iff1, 1);
    EXPECT_EQ(Cpu().iff2, 0);
    EXPECT_EQ(_context->emulatorState.border_attr, 6);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x14);
    EXPECT_EQ(Mem().GetRAMPageForBank(3), 4u);
    EXPECT_EQ(Mem().DirectReadFromZ80Memory(0x4200), 0x99);
    SoundChip_AY8910* ay = _context->pSoundManager->getAYChip(0);
    ASSERT_NE(ay, nullptr);
    EXPECT_EQ(ay->readRegister(7), 0x3E);
    EXPECT_EQ(ay->readRegister(8), 0x0B);
}

// SZX: the same, with the machine state from the image and the media / devices from the stage
TEST_F(Commit_Test, AnSzxCommitFollowsTheImage)
{
    const std::string path = TestPathHelper::GetTestDataPath("loaders/szx/libspectrum/synth-pentagon.szx");
    const std::vector<uint8_t> bytes = ReadFile(path);
    szx::Stage stage;
    std::string error;
    ASSERT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), stage, error)) << error;
    snapshot::Image image = LoaderSZX::BuildImage(stage, path);

    image.cpu.pc = 0x2222;
    image.cpu.bc = 0x3333;
    image.cpu.af2 = 0x4444;
    image.cpu.memptr = 0x5555;
    image.cpu.q = 0x66;
    image.cpu.halted = false;
    image.cpu.eiShadow = false;
    image.border = 4;
    image.portFE = 0xFC;
    image.paging.p7FFD = 0x15;
    ASSERT_FALSE(image.ay.empty());
    image.ay[0].registers[1] = 0x0F;
    image.banks.at(2)[0x0010] = 0xAB;

    szx::Report report;
    ASSERT_TRUE(LoaderSZX::CommitImage(_context, image, stage, report, error)) << error;

    EXPECT_EQ(Cpu().pc, 0x2222);
    EXPECT_EQ(Cpu().bc, 0x3333);
    EXPECT_EQ(Cpu().alt.af, 0x4444);
    EXPECT_EQ(Cpu().memptr, 0x5555);
    EXPECT_EQ(Cpu().q, 0x66);
    EXPECT_EQ(Cpu().halted, 0);
    EXPECT_EQ(Cpu().boundary, Z80_BOUNDARY_NONE);
    EXPECT_EQ(_context->emulatorState.border_attr, 4);
    EXPECT_EQ(_context->emulatorState.pFE, 0xFC);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x15);
    EXPECT_EQ(Mem().DirectReadFromZ80Memory(0x8010), 0xAB);
    EXPECT_EQ(_context->pSoundManager->getAYChip(0)->readRegister(1), 0x0F);
}

// The Z80 files no machine can take are refused by the plan, before the machine is touched (the commit used to throw an
// uncaught std::logic_error after the reset)
TEST_F(Commit_Test, AZ80NoMachineCanTakeIsRefusedBeforeAnythingIsWritten)
{
    const auto run = [&](const char* what, std::vector<uint8_t> file, const char* expectedInReason) {
        SCOPED_TRACE(what);
        const uint16_t pcBefore = Cpu().pc;
        const uint8_t byteBefore = Mem().DirectReadFromZ80Memory(0x4000);
        EXPECT_FALSE(_emulator->LoadSnapshotData(file, "z80", "synthetic.z80"));
        const snapshot::Report& report = _emulator->LastSnapshotReport();
        EXPECT_TRUE(report.refused);
        EXPECT_EQ(report.needs, "format:unsupported");
        EXPECT_NE(report.reason.find(expectedInReason), std::string::npos) << report.reason;
        EXPECT_EQ(Cpu().pc, pcBefore) << "the machine was not reset or written";
        EXPECT_EQ(Mem().DirectReadFromZ80Memory(0x4000), byteBefore);
    };

    // SamRam: v2, hardware byte 2
    std::vector<uint8_t> samram = ReadFile(TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80"));
    ASSERT_GT(samram.size(), 60u);
    samram[34] = 2;
    run("SamRam", samram, "SamRam");

    // A ROM block: a 128K file whose first memory block names page 0 (ROM 0)
    std::vector<uint8_t> romBlock = ReadFile(TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80"));
    const size_t firstBlock = 30 + 2 + (romBlock[30] | romBlock[31] << 8);
    ASSERT_LT(firstBlock + 3, romBlock.size());
    romBlock[firstBlock + 2] = 0;
    run("a ROM block", romBlock, "ROM block");
}
