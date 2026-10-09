// DivMmcPaging (core/src/emulator/io/divmmc/divmmcpaging.h): the DivMMC / DivIDE board on a classic machine -
// #E3 paging with the EEPROM and banked RAM at #0000-#3FFF, MAPRAM, the automap on M1 fetches at the entry
// points, the SPI pair. Rules: docs/inprogress/2026-09-28-storage-controllers-survey/divide-divmmc-esxdos.md §2.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/divmmc/divmmcpaging.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
#include "emulator/io/storage/memorydisk.h"
#include "_helpers/testpathhelper.h"
#include "emulator/memory/memory.h"

class DivMmcPaging_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    std::unique_ptr<DivMmcPaging> _div;
    uint8_t _firmware[DivMmcPaging::kRomSize];

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        for (uint32_t i = 0; i < sizeof _firmware; i++)
            _firmware[i] = static_cast<uint8_t>(0xA0 ^ i ^ (i >> 8));
        _div = std::make_unique<DivMmcPaging>(_context);
        ASSERT_TRUE(_div->LoadRom(_firmware, sizeof _firmware));
        ASSERT_TRUE(_div->Attach());
    }

    void TearDown() override
    {
        _div.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Load(uint16_t addr, std::initializer_list<uint8_t> code)
    {
        for (uint8_t b : code)
            _memory->DirectWriteToZ80Memory(addr++, b);
    }
};

TEST_F(DivMmcPaging_Test, UnmappedBoardIsInvisible)
{
    EXPECT_FALSE(_div->Mapped());
    EXPECT_EQ(_z80->rd(0x0000), _memory->DirectReadFromZ80Memory(0x0000)) << "the Spectrum ROM";
    EXPECT_NE(_z80->rd(0x0100), _firmware[0x100]);
}

TEST_F(DivMmcPaging_Test, ConmemMapsTheEepromAndABankAndWritesGoToTheBank)
{
    _z80->out(0x00E3, 0x80 | 5);
    EXPECT_TRUE(_div->Mapped());
    for (uint16_t a : {0x0000, 0x0123, 0x1FFF})
        EXPECT_EQ(_z80->rd(a), _firmware[a]) << a;
    _z80->wd(0x2010, 0x5A);  // bank 5
    EXPECT_EQ(_z80->rd(0x2010), 0x5A);
    EXPECT_EQ(_div->RamBank(5)[0x10], 0x5A);
    _z80->wd(0x0010, 0x77);  // the EEPROM is read-only
    EXPECT_EQ(_z80->rd(0x0010), _firmware[0x10]);
    _z80->out(0x00E3, 0x80 | 6);
    EXPECT_NE(_z80->rd(0x2010), 0x5A) << "bank 6 is another 8K";
    _z80->out(0x00E3, 0x00);
    EXPECT_FALSE(_div->Mapped());
    EXPECT_EQ(_z80->rd(0x2010), _memory->DirectReadFromZ80Memory(0x2010)) << "the Spectrum ROM is back";
}

TEST_F(DivMmcPaging_Test, MapramIsStickyAndShowsBank3ReadOnlyAtZero)
{
    _div->RamBank(3)[0x20] = 0xC3;
    _z80->out(0x00E3, 0x40 | 0x03);  // MAPRAM + bank 3
    _z80->out(0x00E3, 0x80 | 0x03);  // CONMEM now: the EEPROM wins
    EXPECT_EQ(_z80->rd(0x0020), _firmware[0x20]);
    _z80->out(0x00E3, 0x00);         // MAPRAM stays although the write cleared it
    EXPECT_TRUE(_div->Mapram());
    // mapped by the trap, MAPRAM on: bank 3 shows at #0000
    _div->SetAutomapEnabled(true);
    Load(0x8000, {0xCF});            // RST 8: the fetch at #0008 maps the board
    _z80->pc = 0x8000;
    _z80->sp = 0xFF00;
    _z80->Z80Step();
    EXPECT_EQ(_z80->pc, 0x0008);
    _z80->Z80Step();              // the opcode at #0008 (it came from the Spectrum ROM)
    EXPECT_TRUE(_div->Automapped());
    EXPECT_EQ(_z80->rd(0x0020), 0xC3) << "bank 3 at #0000";
    _z80->wd(0x0020, 0x00);
    EXPECT_EQ(_z80->rd(0x0020), 0xC3) << "read-only";
    _z80->wd(0x2020, 0x11);          // bank 3 at #2000 is protected as well while MAPRAM is set
    EXPECT_EQ(_div->RamBank(3)[0x20], 0xC3);
}

TEST_F(DivMmcPaging_Test, AutomapEntriesMapInAfterTheFetchAndOffAreaMapsOut)
{
    // RST 8: the fetch at #0008 maps in after the opcode read
    Load(0x8000, {0xCF});
    _z80->pc = 0x8000;
    _z80->sp = 0xFF00;
    _z80->Z80Step();
    EXPECT_EQ(_z80->pc, 0x0008);
    EXPECT_FALSE(_div->Automapped()) << "not yet: the fetch has not happened";
    _z80->Z80Step();
    EXPECT_TRUE(_div->Automapped());
    EXPECT_EQ(_z80->rd(0x0100), _firmware[0x100]);
    // a fetch in #1FF8-#1FFF maps out
    _z80->pc = 0x1FF8;
    _z80->Z80Step();
    EXPECT_FALSE(_div->Automapped());
}

TEST_F(DivMmcPaging_Test, TrDosEntryMapsInInstantly)
{
    // the firmware byte at #3D00 is the opcode the CPU executes: a NOP
    _firmware[0x3D00] = 0x00;
    ASSERT_TRUE(_div->LoadRom(_firmware, sizeof _firmware));
    _z80->pc = 0x3D00;
    _z80->Z80Step();
    EXPECT_EQ(_z80->pc, 0x3D01) << "the opcode came from the board (the Spectrum ROM has no NOP there)";
    EXPECT_TRUE(_div->Automapped());
}

TEST_F(DivMmcPaging_Test, SpiPairTalksToACard)
{
    ASSERT_TRUE(_div->Card(0).insert(std::make_unique<MemoryDisk>(4096), SdCardSpi::WriteMode::Session));
    _z80->out(0x00E7, 0xFE);  // card 0
    const uint8_t cmd0[6] = {0x40, 0, 0, 0, 0, 0x95};
    for (uint8_t b : cmd0)
        _z80->out(0x00EB, b);
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 16 && r1 == 0xFF; i++)
        r1 = _z80->in(0x00EB);
    EXPECT_EQ(r1, 0x01) << "idle after CMD0";
    _z80->out(0x00E7, 0xFF);
}

TEST_F(DivMmcPaging_Test, DetachRestoresTheMachine)
{
    _z80->out(0x00E3, 0x80);
    EXPECT_EQ(_z80->rd(0x0000), _firmware[0]);
    _div->Detach();
    EXPECT_EQ(_z80->rd(0x0000), _memory->DirectReadFromZ80Memory(0x0000));
    EXPECT_EQ(_z80->machineM1Hook, nullptr);
}

// UnoDOS 3 (Source Solutions, GPLv3; testdata/storage/divmmc/unodos): the firmware boots on a 48K machine through the
// automap, initializes the SD card over SPI, mounts the FAT16 card, loads /dos/unodos.sys into the board's RAM and
// leaves the machine at the BASIC prompt. Boot-bound: a ROM cold start plus the card initialization
class DivMmcUnoDos_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    std::unique_ptr<DivMmcPaging> _div;

    void SetUp() override
    {
        const std::filesystem::path root = TestPathHelper::FindProjectRoot() / "testdata/storage/divmmc/unodos";
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _div = std::make_unique<DivMmcPaging>(_context);
        ASSERT_TRUE(_div->LoadRomFile((root / "unodos.rom").string()));
        ASSERT_TRUE(_div->Attach());

        FolderSnapshot snapshot;
        FolderScanOptions scan;
        std::string error;
        ASSERT_TRUE(FolderSnapshot::Scan(root / "card", scan, snapshot, &error)) << error;
        FatVolumeOptions options;
        std::vector<std::string> report;
        auto card = HostFolderFat::Build(snapshot, options, &error, &report);
        ASSERT_NE(card, nullptr) << error;
        ASSERT_TRUE(_div->Card(0).insert(std::move(card), SdCardSpi::WriteMode::Session));
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        _div.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

TEST_F(DivMmcUnoDos_Test, BootsMountsTheCardAndReachesBasic)
{
    _div->Reset();
    _emulator->Reset();
    int frames = 0;
    bool ready = EmulatorTestHelper::RunUntilBASICReady(_emulator, 1500, &frames);
    EXPECT_TRUE(ready);
    EXPECT_EQ(_div->RamBank(0)[0x0D42], 0xAA) << "UnoDOS initialized";
    EXPECT_GT(_div->Card(0).blocksRead(), 0u);
}

// A guest program lists the card's root folder through the esxDOS-compatible API: RST 8 with F_OPENDIR (#A3) and
// F_READDIR (#A4), the whole way over the automap, the board's RAM, the SPI port and the SD card in SPI mode
TEST_F(DivMmcUnoDos_Test, GuestListsTheRootFolderThroughRst8)
{
    _div->Reset();
    _emulator->Reset();
    ASSERT_TRUE(EmulatorTestHelper::RunUntilBASICReady(_emulator, 1500));
    Memory* memory = _context->pMemory;
    const std::vector<uint8_t> program = {
        0xF3,                          // 8000 DI
        0x3E, 0x2A,                    // 8001 LD A,'*'            default drive
        0xDD, 0x21, 0x40, 0x80,        // 8003 LD IX,#8040         path
        0x06, 0x00,                    // 8007 LD B,0
        0xCF, 0xA3,                    // 8009 RST 8 : F_OPENDIR
        0x38, 0x18,                    // 800B JR C,fail
        0x32, 0x3F, 0x80,              // 800D LD (#803F),A        handle
        0xDD, 0x21, 0x00, 0x90,        // 8010 LD IX,#9000
        0x3A, 0x3F, 0x80,              // 8014 loop: LD A,(#803F)
        0xCF, 0xA4,                    // 8017 RST 8 : F_READDIR
        0x38, 0x0A,                    // 8019 JR C,fail
        0xB7,                          // 801B OR A                A = 1: more entries (IX is left after the entry)
        0x20, 0xF6,                    // 801C JR NZ,loop
        0x3E, 0x01,                    // 801E LD A,1
        0x32, 0x3E, 0x80,              // 8020 LD (#803E),A        done
        0x18, 0xFE,                    // 8023 JR $
        0x32, 0x3C, 0x80,              // 8025 fail: LD (#803C),A  error code
        0x3E, 0xFF,                    // 8028 LD A,#FF
        0x32, 0x3E, 0x80,              // 802A LD (#803E),A
        0x18, 0xFE,                    // 802D JR $
    };
    for (size_t i = 0; i < program.size(); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    memory->DirectWriteToZ80Memory(0x8040, '/');
    memory->DirectWriteToZ80Memory(0x8041, 0);
    memory->DirectWriteToZ80Memory(0x803E, 0);
    _z80->pc = 0x8000;
    _z80->sp = 0xBF00;
    for (int i = 0; i < 300 && memory->DirectReadFromZ80Memory(0x803E) == 0; i++)
        _emulator->RunFrame(true);
    ASSERT_EQ(memory->DirectReadFromZ80Memory(0x803E), 1) << "error code " << int(memory->DirectReadFromZ80Memory(0x803C));

    // Entries follow each other: attributes, ASCIZ name, 4 bytes date, 4 bytes size
    std::set<std::string> names;
    uint16_t at = 0x9000;
    for (int entry = 0; entry < 8 && memory->DirectReadFromZ80Memory(at) != 0; entry++)
    {
        std::string name;
        uint16_t a = static_cast<uint16_t>(at + 1);
        for (; memory->DirectReadFromZ80Memory(a) != 0 && name.size() < 16; a++)
            name += static_cast<char>(memory->DirectReadFromZ80Memory(a));
        names.insert(name);
        at = static_cast<uint16_t>(a + 1 + 8);
    }
    std::string all;
    for (const std::string& n : names)
        all += n + " ";
    EXPECT_TRUE(names.count("HELLO.TXT")) << "names: " << all;
    EXPECT_TRUE(names.count("DOS") || names.count("dos"));
    EXPECT_TRUE(names.count("PROGRAMS") || names.count("programs"));
}
