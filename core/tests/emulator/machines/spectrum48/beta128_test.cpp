// 48K-BETA128 (machinevariants.cpp): a ZX-Spectrum 48k with the Beta 128 disk interface on its edge connector.
// The interface's TR-DOS 5.03 ROM pages in on an M1 at #3Dxx and out on an M1 at #4000 and above; the WD1793
// ports #1F #3F #5F #7F and the system register #FF answer only while it is paged in; the 48K has no #7FFD.

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/zxprogramfiles.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/machinevariants.h"
#include "emulator/memory/rom.h"
#include "emulator/ports/portdecoder.h"

class Spectrum48Beta128_Test : public RomEditorFixture
{
protected:
    CommandTyper* _typer = nullptr;

    void Ready()
    {
        Boot("48K-BETA128", RM_SOS, "1982 Sinclair");
        ASSERT_FALSE(HasFatalFailure());
        _typer = _context->pDebugManager->GetCommandTyper();
        ASSERT_NE(_typer, nullptr);
    }

    CommandTyper::Result Type(const std::string& command)
    {
        CommandTyper::Options options;
        options.pressEnter = true;
        EXPECT_TRUE(_typer->Request(command, options));
        RunUntil([&] { return _typer->GetStatus() == CommandTyper::Status::Done; }, 4000);
        return _typer->GetResult();
    }

    bool InTrDos() const { return (_context->emulatorState.flags & CF_TRDOS) != 0; }
};

/// The variant by name and alias: the 48K with the Beta 128's ROM, reported as the variant
TEST(Spectrum48Beta128_Variant, FoundByNameAndReported)
{
    const MachineVariant* variant = MachineVariants::Find("48K-BETA128");
    ASSERT_NE(variant, nullptr);
    EXPECT_EQ(MachineVariants::Find("48k-trdos"), variant);
    EXPECT_STREQ(variant->baseModel, "48K");
    EXPECT_EQ(variant->ramKb, 48u);

    CONFIG config{};
    config.mem_model = MM_SPECTRUM48;
    config.trdos_present = true;  // the shipped spectrum48 ini says Beta128=1, but has no TR-DOS ROM
    EXPECT_EQ(MachineVariants::Of(config), nullptr) << "a plain 48K is not the variant";
    variant->apply(config);
    EXPECT_EQ(MachineVariants::Of(config), variant);
}

/// Boots to the 48K BASIC with the TR-DOS ROM in page 1, not paged in
TEST_F(Spectrum48Beta128_Test, BootsTo48Basic)
{
    Ready();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(_context->config.mem_model, MM_SPECTRUM48);
    EXPECT_TRUE(_context->pMemory->IsDosAvailable());
    EXPECT_FALSE(InTrDos());
    ROM* rom = _context->pCore->GetROM();
    EXPECT_EQ(rom->GetROMPageTitle(1), "TR-DOS v5.03 ROM");
    EXPECT_EQ(rom->GetROMPageRole(1), "TR-DOS ROM");
    EXPECT_EQ(EmulatorManager::GetMachineIdentity(*_emulator).Variant, "48K-BETA128");
}

/// RANDOMIZE USR 15616 enters TR-DOS: the A> prompt, the DOS ROM paged in while its editor runs
TEST_F(Spectrum48Beta128_Test, RandomizeUsr15616EntersTrDos)
{
    Ready();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd"), 0));

    Type("RANDOMIZE USR 15616");
    bool sawTrDos = false;
    EXPECT_TRUE(RunUntil([&] {
        sawTrDos = sawTrDos || InTrDos();
        return ScreenHas("A>");
    }, 300)) << "no TR-DOS prompt:\n" << Screen();
    EXPECT_TRUE(sawTrDos) << "the TR-DOS ROM never paged in";
}

/// A TR-DOS disk from testdata catalogues; a disk's BASIC file loads and runs (it loads and calls a CODE file)
TEST_F(Spectrum48Beta128_Test, CataloguesAndLoadsABasicFile)
{
    Ready();
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd"), 0));
    Type("RANDOMIZE USR 15619: REM : CAT");
    EXPECT_TRUE(RunUntil([&] { return ScreenHas("Free"); }, 400)) << "TR-DOS did not list the disk\n" << Screen();
    EXPECT_TRUE(ScreenHas("boot")) << Screen();

    // boot (BASIC, line 10 auto-run): CLEAR 32767, LOAD "probe" CODE, RANDOMIZE USR 32768; the code stores 42
    const std::vector<uint8_t> code = { 0x3E, 0x2A, 0x32, 0x30, 0x75, 0xC9 };  // LD A,42; LD (30000),A; RET
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("beta128-boot.trd");
    {
        const std::vector<uint8_t> trd = ZxProgramFiles::BuildTrd("probe", 32768, code);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(trd.data()), static_cast<std::streamsize>(trd.size()));
    }
    ASSERT_TRUE(_emulator->LoadDisk(path, 0));
    _context->pMemory->DirectWriteToZ80Memory(30000, 0);
    RunFrames(2);
    Type("RANDOMIZE USR 15619: REM : RUN \"boot\"");
    EXPECT_TRUE(RunUntil([&] { return _context->pMemory->DirectReadFromZ80Memory(30000) == 42; }, 600))
        << "the BASIC file never ran its CODE\n" << Screen();
}

/// No #7FFD on the 48K: a write there pages nothing (ROM, #C000 and the screen stay)
TEST_F(Spectrum48Beta128_Test, Port7FFDHasNoEffect)
{
    Ready();
    ASSERT_FALSE(HasFatalFailure());
    Memory* memory = _context->pMemory;
    const uint16_t rom = memory->GetROMPage();
    const uint16_t top = memory->GetRAMPageForBank3();
    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x07, 0);  // RAM 7 at #C000, ROM 0 (128 editor) on a 128K
    _context->pPortDecoder->DecodePortOut(0x7FFD, 0x1F, 0);
    EXPECT_EQ(memory->GetROMPage(), rom);
    EXPECT_EQ(memory->GetRAMPageForBank3(), top);
    EXPECT_EQ(memory->GetRAMPageForBank1(), 5);
    EXPECT_FALSE(InTrDos());
}

/// The FDC ports answer only while the TR-DOS ROM is paged in: outside it a write does not reach the WD1793 and a read
/// is not decoded; with it paged in both reach the controller, whatever the high byte (low-byte decode)
TEST_F(Spectrum48Beta128_Test, FdcPortsSilentWithTheTrDosRomOff)
{
    Ready();
    ASSERT_FALSE(HasFatalFailure());
    PortDecoder* decoder = _context->pPortDecoder;
    WD1793* fdc = _context->pBetaDisk;
    ASSERT_NE(fdc, nullptr);
    ASSERT_FALSE(InTrDos());

    const uint8_t track = fdc->getTrackRegister();
    decoder->DecodePortOut(0x003F, static_cast<uint8_t>(track ^ 0x55), 0);
    EXPECT_EQ(fdc->getTrackRegister(), track) << "#3F reached the WD1793 outside TR-DOS";
    for (const uint16_t port : { 0x001F, 0x003F, 0x005F, 0x007F, 0x00FF, 0x123F })
    {
        decoder->DecodePortIn(port, 0);
        EXPECT_FALSE(decoder->WasLastPortDecoded()) << std::hex << port << " answered outside TR-DOS";
    }

    _context->emulatorState.flags |= CF_TRDOS;  // what an M1 at #3Dxx does
    _context->pMemory->UpdateZ80Banks();
    decoder->DecodePortOut(0x423F, 0x21, 0);  // OUT (#3F),A with A = #42 on the high byte
    EXPECT_EQ(fdc->getTrackRegister(), 0x21);
    EXPECT_EQ(decoder->DecodePortIn(0x003F, 0), 0x21);
    EXPECT_TRUE(decoder->WasLastPortDecoded());
}

/// The TR-DOS ROM pages out on an M1 at #4000 and above
TEST_F(Spectrum48Beta128_Test, TrDosPagesOutAbove3FFF)
{
    Ready();
    ASSERT_FALSE(HasFatalFailure());
    Memory* memory = _context->pMemory;
    _context->emulatorState.flags |= CF_TRDOS;
    memory->UpdateZ80Banks();
    ASSERT_TRUE(_context->emulatorState.flags & CF_LEAVEDOSADR);
    ASSERT_EQ(memory->GetROMPage(), 1u) << "the TR-DOS ROM is page 1";

    // JR $ at #8000: the first fetch there closes the session
    memory->DirectWriteToZ80Memory(0x8000, 0x18);
    memory->DirectWriteToZ80Memory(0x8001, 0xFE);
    _context->pCore->GetZ80()->pc = 0x8000;
    RunFrames(1);
    EXPECT_FALSE(InTrDos());
    EXPECT_EQ(memory->GetROMPage(), 0u);
}
