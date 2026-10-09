// The ZX-Evo's ROM chip as a flash (evoflash.h, docs/inprogress/2026-09-27-tsconf/tdd-evo-flash.md): CPU writes reach
// the 29F040 on TS-Conf (MEM_CONFIG.W0_WE, window 0) and on the ATM3 / BaseConf (#BF bit 1, any ROM window). The
// chip's command set is Flash29F040B's (flash29f040b_test.cpp); here the machines' routing, the status reads, the
// overlay's life cycle, the TTD blob, and two real flashers' routines run by the emulated CPU.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/assembler/z80textassembler.h"
#include "debugger/ttd/atm/ttdevoflash.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/machines/tsconf/tsconffixture.h"
#include "emulator/memory/atm/evoflash.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"

namespace
{
/// Base clock t-states (3.5 MHz) of the datasheet-typical times the chip model uses
constexpr uint64_t kProgramT = 35;           // 10 us
constexpr uint64_t kSectorEraseT = 3'500'000;  // 1 s
constexpr uint64_t kEraseWindowT = 175;      // 50 us

std::vector<uint8_t> Assemble(const std::string& source, uint16_t org, AsmResult* out = nullptr)
{
    Z80TextAssembler assembler;
    AsmResult result = assembler.Assemble(source, org);
    if (!result.ok)
        ADD_FAILURE() << result.error.line << ": " << result.error.message << " | " << result.error.sourceLine;
    if (out)
        *out = result;
    return result.bytes;
}

/// Steps the CPU from @p pc until it reaches @p end. A chip operation that keeps the program polling for more than
/// @p pollSteps instructions has the clock moved to its end (a sector erase is 3.5 M t-states), so the program sees
/// the status bits for a while and then the finished operation
::testing::AssertionResult RunUntil(EmulatorContext* context, EvoFlash& flash, uint16_t pc, uint16_t end,
                                    int maxSteps = 2'000'000, int pollSteps = 300, int* busySteps = nullptr)
{
    Z80* z80 = context->pCore->GetZ80();
    z80->pc = pc;
    int busy = 0;
    for (int step = 0; step < maxSteps; ++step)
    {
        if (z80->pc == end)
        {
            if (busySteps)
                *busySteps = busy;
            return ::testing::AssertionSuccess();
        }
        z80->Z80Step();
        const int64_t until = flash.Chip().busyUntil();
        if (until != INT64_MAX && until > flash.Now())
        {
            if (++busy % pollSteps == 0)
                context->emulatorState.t_states += static_cast<uint64_t>(until - flash.Now());
        }
    }
    return ::testing::AssertionFailure() << "PC " << z80->pc << " did not reach " << end;
}
}  // namespace

/// region <TS-Conf>

class EvoFlashTsConf_Test : public TsConfFixture
{
protected:
    EvoFlash& Flash() { return _decoder->GetFlash(); }
    uint8_t& Rom(uint32_t page, uint32_t offset) { return _memory->ROMBase()[page * PAGE_SIZE + offset]; }

    void Unlock()
    {
        Reg(TsConfReg::Page0, 0x00);
        Poke(0x0555, 0xAA);
        Poke(0x02AA, 0x55);
    }
    void Command(uint8_t command)
    {
        Unlock();
        Poke(0x0555, command);
    }
    void WriteEnable(bool on)
    {
        Reg(TsConfReg::MemConfig, static_cast<uint8_t>(TsConfMemConfig::W0NoMap | (on ? TsConfMemConfig::W0We : 0)));
    }
    void Pass(uint64_t t) { _context->emulatorState.t_states += t; }
};

/// The chip works on the machine's ROM pages: no copy of its own
TEST_F(EvoFlashTsConf_Test, TheChipsArrayIsTheMachinesRom)
{
    EXPECT_EQ(Flash().Chip().data(), _memory->ROMBase());
    EXPECT_TRUE(Flash().Chip().arrayMode());
}

/// The overlay is on the bus only while W0_WE lets writes through or the chip answers status
TEST_F(EvoFlashTsConf_Test, OverlayInstalledOnlyWhileNeeded)
{
    EXPECT_FALSE(_core->IsBusOverlayInstalled(&Flash())) << "reset: W0_WE = 0";

    WriteEnable(true);
    EXPECT_TRUE(_core->IsBusOverlayInstalled(&Flash()));
    EXPECT_EQ(Flash().WriteWindows(), 0x01);

    Reg(TsConfReg::MemConfig, TsConfMemConfig::W0NoMap | TsConfMemConfig::W0We | TsConfMemConfig::W0Ram);
    EXPECT_EQ(Flash().WriteWindows(), 0x00) << "W0_RAM: /CSROM is off, the write goes to RAM";
    EXPECT_FALSE(_core->IsBusOverlayInstalled(&Flash()));

    // A program left running when W0_WE is cleared: status reads still come from the chip until it is done
    WriteEnable(true);
    Command(0xA0);
    Reg(TsConfReg::Page0, 0x07);
    Poke(0x0100, 0x00);
    WriteEnable(false);
    EXPECT_TRUE(_core->IsBusOverlayInstalled(&Flash())) << "busy: reads give status";
    Pass(kProgramT);
    _decoder->OnFrameEnd();
    EXPECT_FALSE(_core->IsBusOverlayInstalled(&Flash())) << "done: the frame end takes the overlay away";
    EXPECT_EQ(Rom(7, 0x0100), 0x00);
}

/// Status during a byte program: DQ7 = complement of the data's bit 7, DQ6 toggles on every read; then the data
TEST_F(EvoFlashTsConf_Test, ProgramStatusPollingThenData)
{
    WriteEnable(true);
    Command(0xA0);
    Reg(TsConfReg::Page0, 0x07);  // tag #07
    Poke(0x2000, 0x05);

    const uint8_t s1 = Peek(0x2000);
    const uint8_t s2 = Peek(0x2000);
    EXPECT_EQ(s1 & 0x80, 0x80) << "DQ7 = !D7 (D7 of #05 is 0)";
    EXPECT_NE(s1 & 0x40, s2 & 0x40) << "DQ6 toggles";
    EXPECT_EQ(s1 & 0x20, 0x00) << "DQ5: no failure";
    EXPECT_EQ(Rom(7, 0x2000), 0x07) << "not programmed yet";

    Pass(kProgramT);
    EXPECT_EQ(Peek(0x2000), 0x05);
    EXPECT_EQ(Peek(0x2000), 0x05) << "no toggle: the operation is over";
}

/// Programming a 1 over a 0 fails: DQ5 set, the byte keeps its zeros, only F0 returns to array mode
TEST_F(EvoFlashTsConf_Test, ProgrammingAOneOverAZeroFails)
{
    WriteEnable(true);
    Command(0xA0);
    Reg(TsConfReg::Page0, 0x04);  // tag #04
    Poke(0x0010, 0x03);
    Pass(kProgramT);
    EXPECT_EQ(Peek(0x0010) & 0x20, 0x20) << "DQ5";
    EXPECT_EQ(Rom(4, 0x0010), 0x00) << "#04 & #03";
    Poke(0x0000, 0xF0);
    EXPECT_EQ(Peek(0x0010), 0x00);
    EXPECT_TRUE(Flash().Chip().arrayMode());
}

/// Autoselect: manufacturer and device of the Am29F040B at XX00 / XX01, sector protection 0 at XX02; F0 leaves
TEST_F(EvoFlashTsConf_Test, AutoselectReadsTheChipId)
{
    WriteEnable(true);
    Command(0x90);
    Reg(TsConfReg::Page0, 0x05);
    EXPECT_EQ(Peek(0x0000), 0x01) << "AMD";
    EXPECT_EQ(Peek(0x0001), 0xA4) << "Am29F040B";
    EXPECT_EQ(Peek(0x0002), 0x00) << "not protected";
    EXPECT_EQ(Peek(0x0100), 0x01) << "any address with A7..A0 = 0";

    // The ID reads do not depend on W0_WE: /OE alone
    WriteEnable(false);
    EXPECT_EQ(Peek(0x0001), 0xA4);
    WriteEnable(true);
    Poke(0x0000, 0xF0);
    EXPECT_EQ(Peek(0x0001), 0x05) << "back to the array";
}

/// Sector erase: the 64 KB sector of the addressed page (pages 4n..4n+3) becomes #FF after the erase time; reads
/// give status meanwhile (DQ7 = 0, DQ3 = 1 once the 50 us command window closed)
TEST_F(EvoFlashTsConf_Test, SectorEraseClearsFourPages)
{
    WriteEnable(true);
    Command(0x80);
    Unlock();
    Reg(TsConfReg::Page0, 0x09);  // sector 2 = pages 8..11
    Poke(0x0000, 0x30);

    EXPECT_EQ(Peek(0x0000) & 0x88, 0x00) << "in the command window: DQ7 = 0, DQ3 = 0";
    Pass(kEraseWindowT);
    EXPECT_EQ(Peek(0x0000) & 0x88, 0x08) << "erasing: DQ7 = 0, DQ3 = 1";
    EXPECT_EQ(Rom(8, 0), 0x08);

    Pass(kSectorEraseT);
    EXPECT_EQ(Peek(0x0000), 0xFF);
    for (uint32_t page = 8; page < 12; ++page)
    {
        EXPECT_EQ(Rom(page, 0x0000), 0xFF) << page;
        EXPECT_EQ(Rom(page, 0x3FFF), 0xFF) << page;
    }
    EXPECT_EQ(Rom(7, 0x3FFF), 0x07) << "the sector before";
    EXPECT_EQ(Rom(12, 0x0000), 0x0C) << "the sector after";
}

/// Mapped mode: the write goes to the page window 0 shows, {PAGE0[7:2], ~DOS, ROM128}
TEST_F(EvoFlashTsConf_Test, MappedModeWritesThePageWindowZeroShows)
{
    Reg(TsConfReg::Page0, 0x14);
    Reg(TsConfReg::MemConfig, TsConfMemConfig::W0We | TsConfMemConfig::Rom128);  // mapped, ROM128 = 1: page #17
    ASSERT_EQ(Tag(0x0000), 0x17);
    // The unlock cycles compare A10..A0 only: any page
    Poke(0x0555, 0xAA);
    Poke(0x02AA, 0x55);
    Poke(0x0555, 0xA0);
    Poke(0x3000, 0x01);
    Pass(kProgramT);
    // The chip completes an operation when it is next accessed (or at the frame end): read first
    EXPECT_EQ(Peek(0x3000), 0x01);
    EXPECT_EQ(Rom(0x17, 0x3000), 0x01);
}

/// TTD blob: a state saved mid-erase restores the running erase (status reads, the sector cleared at its time)
TEST_F(EvoFlashTsConf_Test, StateRoundTripMidErase)
{
    WriteEnable(true);
    Command(0x80);
    Unlock();
    Reg(TsConfReg::Page0, 0x10);
    Poke(0x0000, 0x30);
    Pass(kEraseWindowT + 1000);
    Flash().Sync();  // the closed command window becomes the running erase (as a status read would do)

    ttd::TTDEvoFlash serializer(Flash());
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    ASSERT_EQ(blob.size(), EvoFlash::kStateSize);
    serializer.TTDSaveState(blob.data());
    const uint64_t savedAt = _context->emulatorState.t_states;
    const uint64_t hash = serializer.TTDHashState();

    Pass(kSectorEraseT);
    EXPECT_EQ(Peek(0x0000), 0xFF) << "erased";
    std::memset(_memory->ROMBase() + 0x10 * PAGE_SIZE, 0x10, 4 * PAGE_SIZE);  // the engine region restores the array

    _context->emulatorState.t_states = savedAt;
    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(serializer.TTDHashState(), hash);
    EXPECT_TRUE(_core->IsBusOverlayInstalled(&Flash())) << "the restored state needs the overlay";
    EXPECT_EQ(Peek(0x0000) & 0x88, 0x08) << "erasing again";
    EXPECT_EQ(Rom(0x10, 0), 0x10);
    Pass(kSectorEraseT);
    EXPECT_EQ(Peek(0x0000), 0xFF);
}

/// The machine registers the flash's TTD state (id 61) and its array as the engine region "evo.flash" (18)
TEST_F(EvoFlashTsConf_Test, TtdRegistersTheBlobAndTheRegion)
{
    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    ASSERT_TRUE(ttd::RegisterMachinePeripherals(_context, registry, owned, &error)) << error;
    EXPECT_TRUE(registry.IsRegistered(ttd::PeripheralId::EvoFlash));

    std::vector<ttd::TTDDeviceRegion> regions;
    for (ttd::ITTDRegionSource* source : registry.RegionSources())
        source->TTDRegions(regions);
    bool found = false;
    for (const ttd::TTDDeviceRegion& r : regions)
        if (r.desc.id == ttd::TTDRegionId::EvoFlash)
        {
            found = true;
            EXPECT_EQ(r.desc.name, "evo.flash");
            EXPECT_EQ(r.desc.memory, _memory->ROMBase());
            EXPECT_EQ(r.desc.bytes, 512u * 1024u);
        }
    EXPECT_TRUE(found);
}

/// A reset restarts the time base: a running operation completes at the reset (the chip has no reset pin)
TEST_F(EvoFlashTsConf_Test, AResetFinishesARunningOperation)
{
    WriteEnable(true);
    Command(0xA0);
    Reg(TsConfReg::Page0, 0x07);
    Poke(0x0200, 0x01);
    _decoder->reset();
    EXPECT_TRUE(Flash().Chip().arrayMode());
    EXPECT_EQ(Rom(7, 0x0200), 0x01);
    EXPECT_FALSE(_core->IsBusOverlayInstalled(&Flash()));
}

/// End to end: the ROM writer plugin of Wild Commander (pentevo `soft/WC/source/plugins/rom_writer/PLUG04.ASM`,
/// "ROM WRITER v0.3", KOSHI/MGN 2025, ZBOSZOR 2015) - its erase-block, program and toggle-wait routines as written,
/// with MEM_CONFIG #06 / #0E around them, run by the emulated CPU: erase pages 8..11, program bytes, read back
TEST_F(EvoFlashTsConf_Test, WildCommanderRomWriterErasesAndPrograms)
{
    // The plugin's routines (sjasmplus multi-statement lines split, `LD BC,HL` written out)
    const std::string source = R"(
PW0     EQU #10AF
PE0     EQU #21AF
WRE     EQU %00000110
WRD     EQU %00001110
MAIN    DI
        LD BC,PE0
        LD A,WRE
        OUT (C),A
        LD E,8
        CALL ERASBLK
        LD E,9
        LD HL,#C123
        LD A,#5A
        CALL PROG
        LD E,11
        LD HL,#3FFF
        LD A,#A5
        CALL PROG
        LD BC,PE0
        LD A,WRD
        OUT (C),A
        LD BC,PW0
        LD A,9
        OUT (C),A
        LD A,(#0123)
        LD (RESULT),A
        LD BC,PE0
        LD A,#04
        OUT (C),A
        LD A,(#0123)
        LD (RESULT+1),A
DONE    JR DONE
RESULT  DW 0
PROG    PUSH HL
        LD BC,PW0
        LD L,0
        OUT (C),L
        LD HL,#0555
        LD (HL),#AA
        LD HL,#02AA
        LD (HL),#55
        LD HL,#0555
        LD (HL),#A0
        LD BC,PW0
        OUT (C),E
        POP HL
        LD C,L
        LD B,H
        RES 7,B
        RES 6,B
        LD (BC),A
        JR WAITROM
ERASBLK LD BC,PW0
        XOR A
        OUT (C),A
        LD HL,#0555
        LD (HL),#AA
        LD HL,#02AA
        LD (HL),#55
        LD HL,#0555
        LD (HL),#80
        LD (HL),#AA
        LD HL,#02AA
        LD (HL),#55
        LD BC,PW0
        OUT (C),E
        LD H,A
        LD L,A
        LD (HL),#30
        LD B,H
        LD C,L
        LD A,87
D1      DEC A
        JR NZ,D1
WAITROM CALL WAIT
        RET NC
        LD A,#F0
        CALL WRBYTE
        LD A,9
D2      DEC A
        JR NZ,D2
        SCF
        RET
WAIT    CALL RDBYTE
        LD D,A
        CALL RDBYTE
        XOR D
        BIT 6,A
        RET Z
        BIT 5,A
        JR NZ,WAIT
        BIT 5,D
        JR Z,WAIT
        SCF
        RET
RDBYTE  PUSH BC
        LD BC,PW0
        OUT (C),E
        POP BC
        LD A,(BC)
        RET
WRBYTE  PUSH BC
        LD BC,PW0
        OUT (C),E
        POP BC
        LD (BC),A
        RET
)";
    AsmResult listing;
    const std::vector<uint8_t> code = Assemble(source, 0x8000, &listing);
    ASSERT_FALSE(code.empty());
    for (size_t i = 0; i < code.size(); ++i)
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    _z80->sp = 0xFF00;

    int busySteps = 0;
    ASSERT_TRUE(RunUntil(_context, Flash(), 0x8000, static_cast<uint16_t>(listing.symbols.at("DONE")), 2'000'000, 300,
                         &busySteps));
    EXPECT_GT(busySteps, 300) << "the plugin polled the toggle bit while the chip worked";

    const uint16_t result = static_cast<uint16_t>(listing.symbols.at("RESULT"));
    for (uint32_t page = 8; page < 12; ++page)
        EXPECT_EQ(Rom(page, 0x2000), 0xFF) << "erased page " << page;
    EXPECT_EQ(Rom(9, 0x0123), 0x5A) << "programmed through window 0 at #0123 (HL #C123 masked)";
    EXPECT_EQ(Rom(11, 0x3FFF), 0xA5);
    EXPECT_EQ(Rom(12, 0x0000), 0x0C) << "the next sector untouched";
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(result), 0x09) << "WRD (#0E) maps RAM page 9 into window 0";
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(static_cast<uint16_t>(result + 1)), 0x5A)
        << "MEM_CONFIG #04: the CPU reads the programmed ROM byte";
    EXPECT_EQ(Flash().WriteWindows(), 0x00);
    EXPECT_FALSE(_core->IsBusOverlayInstalled(&Flash()));
}

/// endregion </TS-Conf>

/// region <ATM3 (BaseConf)>

class EvoFlashAtm3_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_ATM3* _decoder = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        _decoder = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        // The pager on, window 0 ROM, RAM 5 / 2 / 0 in windows 1-3; the shadow ports open (#BF bit 0)
        _decoder->ApplyBootROMDefaults(RM_DOS);
        Out(0x00BF, 0x01);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Out(uint16_t port, uint8_t value) { _decoder->DecodePortOut(port, value, 0x0000); }
    uint8_t Peek(uint16_t addr) { return (_memory->*(_z80->MemIf->MemoryRead))(addr, false); }
    void Poke(uint16_t addr, uint8_t value) { (_memory->*(_z80->MemIf->MemoryWrite))(addr, value); }
    EvoFlash& Flash() { return _decoder->GetFlash(); }
    uint8_t& Rom(uint32_t page, uint32_t offset) { return _memory->ROMBase()[page * PAGE_SIZE + offset]; }
    /// Window @p window (0-3) shows ROM page @p page (#xFF7, type 11 = ROM page from the register, active low)
    void RomWindow(uint8_t window, uint8_t page)
    {
        Out(static_cast<uint16_t>((window << 14) | 0x3FF7), static_cast<uint8_t>(~page & 0x3F));
    }
    /// ... RAM page @p page (type 01: RAM page from the register)
    void RamWindow(uint8_t window, uint8_t page)
    {
        Out(static_cast<uint16_t>((window << 14) | 0x3FF7), static_cast<uint8_t>(0x40 | (~page & 0x3F)));
    }
};

/// #BF bit 1 (romrw_en) lets writes to every ROM window through; bit 1 clear, a RAM window or #xBF7 write
/// protection keeps them away (fpga/base/z80/zmem.v:193)
TEST_F(EvoFlashAtm3_Test, RomWriteEnableFollowsPortBfAndTheWindows)
{
    EXPECT_EQ(Flash().WriteWindows(), 0x00);
    EXPECT_FALSE(_context->pCore->IsBusOverlayInstalled(&Flash()));

    RomWindow(3, 0x05);
    Out(0x00BF, 0x03);
    EXPECT_EQ(Flash().WriteWindows(), 0x09) << "windows 0 and 3 show ROM";
    EXPECT_TRUE(_context->pCore->IsBusOverlayInstalled(&Flash()));

    // #xBF7 write protect on window 3 (shadow, A8 = 1, A11:A10 = 10)
    Out(0xFBF7, 0x01);
    EXPECT_EQ(Flash().WriteWindows(), 0x01) << "window 3 protected (wrdisable)";
    Out(0xFBF7, 0x00);

    Out(0x00BF, 0x01);
    EXPECT_EQ(Flash().WriteWindows(), 0x00);
    EXPECT_FALSE(_context->pCore->IsBusOverlayInstalled(&Flash()));

    // A reset clears #BF: no write window is left over
    Out(0x00BF, 0x03);
    ASSERT_NE(Flash().WriteWindows(), 0x00);
    _decoder->reset();
    EXPECT_EQ(Flash().WriteWindows(), 0x00);
    EXPECT_FALSE(_context->pCore->IsBusOverlayInstalled(&Flash()));
}

/// Programming through window 3 with #BF bit 1 set changes the ROM page; with it clear the same writes are lost
TEST_F(EvoFlashAtm3_Test, ProgramThroughAnyRomWindow)
{
    // A byte with a bit to clear: programming clears its lowest set bit
    uint16_t offset = 0x1000;
    while (Rom(6, offset) == 0x00)
        ++offset;
    const uint8_t before = Rom(6, offset);
    const uint8_t value = static_cast<uint8_t>(before & (before - 1));
    const uint16_t addr = static_cast<uint16_t>(0xC000 + offset);
    auto program = [&]() {
        RomWindow(3, 0x00);
        Poke(0xC555, 0xAA);
        Poke(0xC2AA, 0x55);
        Poke(0xC555, 0xA0);
        RomWindow(3, 0x06);
        Poke(addr, value);
        _context->emulatorState.t_states += kProgramT;
    };
    program();
    EXPECT_EQ(Peek(addr), before) << "#BF bit 1 = 0";
    EXPECT_EQ(Rom(6, offset), before);
    Out(0x00BF, 0x03);
    program();
    EXPECT_EQ(Peek(addr), value) << "the CPU reads the programmed byte (the chip completes on this access)";
    EXPECT_EQ(Rom(6, offset), value);
}

/// End to end: the routines of NedoOS's evoflash.com (DimkaM / NedoPC 2023; release bin/evoflash.com, disassembled:
/// the command helper at #09BC and the 16 KB page writer at #09DC, as written) and its ID read and 64 KB erase
/// sequences, run by the emulated CPU on the ATM3: ID, erase sector 1 (pages 4..7), program page 5 from RAM
TEST_F(EvoFlashAtm3_Test, NedoOsEvoflashRoutinesIdEraseAndProgram)
{
    const std::string source = R"(
        ORG #09BC
CMD     PUSH BC
        LD BC,#FFF7
        LD A,#3E
        OUT (C),A
        LD A,#AA
        LD (#D555),A
        LD A,#3F
        OUT (C),A
        LD A,#55
        LD (#EAAA),A
        LD A,#3E
        OUT (C),A
        LD A,E
        LD (#D555),A
        POP BC
        RET
WRPAGE  PUSH BC
        DI
        LD A,3
        OUT (#BF),A
        LD A,E
        CPL
        AND #3F
        LD BC,#7FF7
        OUT (C),A
        LD DE,#8000
        LD HL,#4000
        LD BC,#FFF7
        LD A,#3E
        OUT (C),A
NEXT    LD A,(DE)
        CP (HL)
        JP Z,SKIP
        LD A,#AA
        LD (#D555),A
        LD A,#3F
        OUT (C),A
        LD A,#55
        LD (#EAAA),A
        LD A,#3E
        OUT (C),A
        LD A,#A0
        LD (#D555),A
        LD A,(DE)
        LD (HL),A
POLL    LD A,(HL)
        XOR (HL)
        BIT 6,A
        JP NZ,POLL
SKIP    INC DE
        INC HL
        BIT 7,H
        JP Z,NEXT
        LD A,1
        OUT (#BF),A
        POP BC
        RET
; The test's main program: evoflash's ID read (#0346) and 64 KB erase (#0427) sequences with fixed operands
MAIN    LD SP,#3F00
        DI
        LD A,3
        OUT (#BF),A
        LD E,#F0
        CALL CMD
        LD A,#3F
        LD BC,#FFF7
        OUT (C),A
        LD E,#90
        CALL CMD
        LD HL,(#C000)
        LD (CHIPID),HL
        LD E,#F0
        CALL CMD
        LD E,#80
        CALL CMD
        LD A,#AA
        LD (#D555),A
        LD A,#3F
        LD BC,#FFF7
        OUT (C),A
        LD A,#55
        LD (#EAAA),A
        LD A,#3B
        OUT (C),A
        LD HL,#C000
        LD (HL),#30
WAITE   LD A,(HL)
        XOR (HL)
        BIT 6,A
        JR NZ,WAITE
        LD E,#F0
        CALL CMD
        LD A,1
        OUT (#BF),A
        LD E,5
        CALL WRPAGE
DONE    JR DONE
CHIPID  DW 0
)";
    AsmResult listing;
    const std::vector<uint8_t> code = Assemble(source, 0x09BC, &listing);
    ASSERT_FALSE(code.empty());
    ASSERT_EQ(listing.symbols.at("WRPAGE"), 0x09DCu) << "the page writer sits where evoflash.com has it";

    // Window 0: RAM page 4 holding the code and the stack; window 2: RAM page 2 = the data for ROM page 5
    RamWindow(0, 0x04);
    for (size_t i = 0; i < code.size(); ++i)
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x09BC + i), code[i]);
    std::vector<uint8_t> data(PAGE_SIZE, 0xFF);
    for (uint16_t i = 0; i < 64; ++i)
        data[0x0100 + i * 0x80] = static_cast<uint8_t>(i * 3);
    std::memcpy(_memory->RAMPageAddress(2), data.data(), data.size());
    const uint8_t page3 = Rom(3, 0x3FFF);
    const uint8_t page8 = Rom(8, 0x0000);

    ASSERT_TRUE(RunUntil(_context, Flash(), static_cast<uint16_t>(listing.symbols.at("MAIN")),
                         static_cast<uint16_t>(listing.symbols.at("DONE"))));

    const uint16_t chipId = static_cast<uint16_t>(_memory->DirectReadFromZ80Memory(static_cast<uint16_t>(listing.symbols.at("CHIPID"))) |
                                                  (_memory->DirectReadFromZ80Memory(static_cast<uint16_t>(listing.symbols.at("CHIPID") + 1)) << 8));
    EXPECT_EQ(chipId, 0xA401) << "evoflash's table: #A401 = AMD Am29F040B";
    EXPECT_EQ(std::memcmp(_memory->ROMBase() + 5 * PAGE_SIZE, data.data(), PAGE_SIZE), 0) << "page 5 = the data";
    for (uint32_t page : {4u, 6u, 7u})
        EXPECT_EQ(Rom(page, 0x2345), 0xFF) << "erased page " << page;
    EXPECT_EQ(Rom(3, 0x3FFF), page3) << "the sector before";
    EXPECT_EQ(Rom(8, 0x0000), page8) << "the sector after";
    EXPECT_EQ(_context->emulatorState.evo.pBF & 0x02, 0) << "evoflash closes the write enable";
    EXPECT_FALSE(_context->pCore->IsBusOverlayInstalled(&Flash()));
}

/// endregion </ATM3 (BaseConf)>
