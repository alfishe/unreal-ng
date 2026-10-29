// platform-debugging P5: the ZX Next's DMA through the main CPU's watchpoints, the machine's events as event
// breakpoints (docs/inprogress/2026-10-27-platform-debugging/README.md)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

namespace
{
constexpr uint16_t kCode = 0xA000;
constexpr uint16_t kTable = 0xA100;
constexpr uint16_t kSource = 0x8000;  // the DMA's fixed port A byte
constexpr uint16_t kDestination = 0xE000;
constexpr uint8_t kLength = 16;
}  // namespace

class NextMachineEvents_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Next* _ports = nullptr;
    BreakpointManager* _manager = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _manager = _emulator->GetBreakpointManager();
        _emulator->GetFeatureManager()->setFeature(Features::kBreakpoints, true);
        _emulator->DebugOn();
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint16_t Add(const BreakpointSpec& spec)
    {
        std::string error;
        const uint16_t id = _manager->AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return id;
    }
    uint16_t AddEvent(GSCardEvent kind, int value = -1, uint8_t mask = 0xFF)
    {
        BreakpointSpec spec;
        spec.type = BRK_EVENT;
        spec.address = static_cast<uint16_t>(kind);
        if (value >= 0)
        {
            spec.nrValue = static_cast<uint8_t>(value);
            spec.nrMask = mask;
        }
        return Add(spec);
    }
    uint32_t Hits(uint16_t id) { return _manager->GetBreakpointById(id)->hitCount; }

    /// LD HL,table : LD BC,#0D6B : OTIR (a continuous fill of kLength bytes from kSource to kDestination) : JR $
    void LoadDmaFill()
    {
        const uint8_t program[] = {0xF3, 0x21, kTable & 0xFF, kTable >> 8, 0x01, 0x6B, 0x0D, 0xED, 0xB3, 0x18, 0xFE};
        const uint8_t table[] = {0x83, 0x7D, kSource & 0xFF, kSource >> 8, kLength, 0x00, 0x24, 0x10, 0xAD,
                                 kDestination & 0xFF, kDestination >> 8, 0xCF, 0x87};
        for (size_t i = 0; i < sizeof(program); i++)
            _z80->DirectWrite(static_cast<uint16_t>(kCode + i), program[i]);
        for (size_t i = 0; i < sizeof(table); i++)
            _z80->DirectWrite(static_cast<uint16_t>(kTable + i), table[i]);
        _z80->DirectWrite(kSource, 0xAA);
        _z80->pc = kCode;
        _z80->iff1 = _z80->iff2 = 0;
    }
};

// The DMA drives the CPU's address bus: a write watchpoint there stops after the DMA's byte, a dma_write stop
TEST_F(NextMachineEvents_Test, TheDmasWriteHitsTheWatchpointOfItsAddress)
{
    LoadDmaFill();
    BreakpointSpec spec;
    spec.access = BRK_MEM_WRITE;
    spec.address = kDestination + 3;
    const uint16_t id = Add(spec);
    _emulator->RunNFrames(1, false);
    const Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.reason, Emulator::DebugStop::Reason::Breakpoint);
    EXPECT_EQ(stop.breakpoint.breakpointId, id);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::DmaWrite);
    EXPECT_STREQ(BreakpointHitKindName(stop.breakpoint.kind), "dma_write");
    EXPECT_EQ(stop.breakpoint.address, kDestination + 3);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(kDestination + 3), 0xAA) << "the byte is written first";
}

// 'breakpoints' off: the DMA's accesses are not checked, as the CPU's are not
TEST_F(NextMachineEvents_Test, BreakpointsOffTheDmaDoesNotStop)
{
    LoadDmaFill();
    BreakpointSpec spec;
    spec.access = BRK_MEM_WRITE;
    spec.address = kDestination + 3;
    const uint16_t id = Add(spec);
    _emulator->GetFeatureManager()->setFeature(Features::kBreakpoints, false);
    _emulator->RunNFrames(1, false);
    EXPECT_NE(_emulator->LastStop().reason, Emulator::DebugStop::Reason::Breakpoint);
    EXPECT_EQ(Hits(id), 0u);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(kDestination + kLength - 1), 0xAA) << "the fill ran";
}

// A read watchpoint on the source sees every DMA read of it; the program's own memory is not touched
TEST_F(NextMachineEvents_Test, TheDmasReadsHitAReadWatchpointEveryByte)
{
    LoadDmaFill();
    BreakpointSpec spec;
    spec.access = BRK_MEM_READ;
    spec.address = kSource;
    const uint16_t id = Add(spec);
    _emulator->RunNFrames(1, false);
    const Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.breakpoint.breakpointId, id);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::DmaRead);
    EXPECT_STREQ(BreakpointHitKindName(stop.breakpoint.kind), "dma_read");
    EXPECT_EQ(Hits(id), kLength) << "a direct run ends after the instruction: the transfer went on, each byte counted";
}

// zxndma_start when the transfer begins, zxndma_done (1: the end of the block) when it stops
TEST_F(NextMachineEvents_Test, TheDmasStartAndEndAreEvents)
{
    LoadDmaFill();
    const uint16_t start = AddEvent(GSCardEvent::ZxnDmaStart);
    const uint16_t done = AddEvent(GSCardEvent::ZxnDmaDone, 1);
    EXPECT_TRUE(_context->machineEventWatch);
    _emulator->RunNFrames(1, false);
    EXPECT_EQ(_emulator->LastStop().breakpoint.breakpointId, start);
    EXPECT_EQ(_emulator->LastStop().breakpoint.kind, BreakpointHitKind::Event);
    EXPECT_EQ(Hits(start), 1u);
    _emulator->RunNFrames(1, false);
    EXPECT_EQ(Hits(done), 1u);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(kDestination + kLength - 1), 0xAA);
    _manager->ClearUserBreakpoints();
    EXPECT_FALSE(_context->machineEventWatch) << "the machine stops telling its events";
}

// The maskable interrupt the CPU takes: cpu_int with its source (11: the ULA's frame interrupt)
TEST_F(NextMachineEvents_Test, TheFrameInterruptIsACpuIntOfTheUla)
{
    // IM 1 : EI : HALT : JR $-1
    const uint8_t program[] = {0xED, 0x56, 0xFB, 0x76, 0x18, 0xFD};
    for (size_t i = 0; i < sizeof(program); i++)
        _z80->DirectWrite(static_cast<uint16_t>(kCode + i), program[i]);
    _z80->pc = kCode;
    _z80->sp = 0xBF00;
    const uint16_t line = AddEvent(GSCardEvent::CpuInt, 0);
    const uint16_t ula = AddEvent(GSCardEvent::CpuInt, 11);
    _emulator->RunNFrames(2, false);
    EXPECT_EQ(_emulator->LastStop().breakpoint.breakpointId, ula);
    EXPECT_EQ(_emulator->LastStop().breakpoint.address, static_cast<uint16_t>(GSCardEvent::CpuInt));
    EXPECT_EQ(Hits(line), 0u) << "no line interrupt is enabled";
}

// The DivMMC: the automap entry, esxDOS's call byte, the exit
TEST_F(NextMachineEvents_Test, TheDivMmcsAutomapAndTheEsxDosCallAreEvents)
{
    NextBoard& board = _ports->Board();
    board.WriteNextReg(0x83, 0xFF);  // the DivMMC port and memory enabled
    board.WriteNextReg(0x0A, 0x10);  // the automap
    board.WriteNextReg(0xB8, 0x82);  // #0008 only
    board.WriteNextReg(0xB9, 0xFF);
    board.WriteNextReg(0xBA, 0x00);
    board.WriteNextReg(0xBB, 0x40);  // the #1FF8 exit
    const uint16_t map = AddEvent(GSCardEvent::DivMmcMap, 0);
    const uint16_t open = AddEvent(GSCardEvent::EsxDosCall, 0x9A);
    const uint16_t any = AddEvent(GSCardEvent::EsxDosCall);
    const uint16_t unmap = AddEvent(GSCardEvent::DivMmcUnmap, 1);
    // RST $08 / DEFB $9D: the RST pushed #C000, the address of the call byte
    _z80->sp = 0x8000;
    _z80->DirectWrite(0x8000, 0x00);
    _z80->DirectWrite(0x8001, 0xC0);
    _z80->DirectWrite(0xC000, 0x9D);
    _ports->DivMmc().OnMachineM1(0x0008);
    EXPECT_EQ(Hits(map), 1u);
    EXPECT_EQ(Hits(open), 0u) << "F_READ is not F_OPEN";
    EXPECT_EQ(Hits(any), 1u);
    _ports->DivMmc().OnMachineM1(0x1FF8);
    EXPECT_EQ(Hits(unmap), 1u);
}

// An NMI request: cpu_nmi with its source (0: the M1 button), bit 7 when the machine refused it
TEST_F(NextMachineEvents_Test, AnNmiRequestIsACpuNmiOfItsSource)
{
    const uint16_t m1 = AddEvent(GSCardEvent::CpuNmi, 0, 0x7F);
    const uint16_t drive = AddEvent(GSCardEvent::CpuNmi, 1, 0x7F);
    _ports->GenerateMultifaceNmi();
    EXPECT_EQ(Hits(m1), 1u);
    EXPECT_EQ(Hits(drive), 0u);
    EXPECT_EQ(_manager->GetLastTriggeredBreakpointID(), m1);
}

// The DivMMC's RAM bank by its name: a write through slot 1 with CONMEM and bank 3 stops a watchpoint on divmmc3,
// not on another bank that shares the address
TEST_F(NextMachineEvents_Test, TheDivMmcsRamBankIsAPageByItsName)
{
    // LD A,#83 : OUT (#E3),A (CONMEM, bank 3) : LD A,#5A : LD (#2005),A : JR $
    const uint8_t program[] = {0xF3, 0x3E, 0x83, 0xD3, 0xE3, 0x3E, 0x5A, 0x32, 0x05, 0x20, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(program); i++)
        _z80->DirectWrite(static_cast<uint16_t>(kCode + i), program[i]);
    _z80->pc = kCode;
    auto watch = [&](const char* page) {
        BreakpointSpec spec;
        spec.access = BRK_MEM_WRITE;
        spec.address = 0x2005;
        std::string error;
        EXPECT_TRUE(BreakpointManager::ParsePageInto(page, spec, error)) << error;
        return Add(spec);
    };
    const uint16_t bank3 = watch("divmmc3");
    const uint16_t bank2 = watch("divmmc2");
    EXPECT_EQ(BreakpointManager::PageSpecName(*_manager->GetBreakpointById(bank3)), "divmmc3");
    EXPECT_EQ(_manager->GetBreakpointById(bank3)->page, 9) << "bank 3: the upper half of system page 9";
    _emulator->RunNFrames(1, false);
    const Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.breakpoint.breakpointId, bank3);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::MemoryWrite);
    EXPECT_EQ(Hits(bank2), 0u);
}

// The names: divmmcrom, divmmc0..15, mfrom, mfram; a bank past 15 is refused, and so is any of them off the Next
TEST(NextSystemPages_Test, TheNamesAndWhereTheyAreRefused)
{
    struct Case
    {
        const char* name;
        uint8_t page;
        uint16_t half;
    };
    for (const Case c : {Case{"divmmcrom", 4, 0}, Case{"divmmc0", 8, 0}, Case{"DIVMMC15", 15, 1}, Case{"mfrom", 5, 0},
                         Case{"mfram", 5, 1}})
    {
        BreakpointSpec spec;
        std::string error;
        ASSERT_TRUE(BreakpointManager::ParsePageInto(c.name, spec, error)) << c.name << ": " << error;
        EXPECT_EQ(spec.page, c.page) << c.name;
        EXPECT_EQ(spec.pageType, BANK_ROM) << c.name;
        EXPECT_EQ(spec.bank8k, c.half) << c.name;
        EXPECT_TRUE(spec.nextSystemPage) << c.name;
    }
    BreakpointSpec spec;
    std::string error;
    EXPECT_FALSE(BreakpointManager::ParsePageInto("divmmc16", spec, error));

    EmulatorContext pentagon(LoggerLevel::LogError);
    pentagon.config.mem_model = MM_PENTAGON;
    BreakpointManager manager(&pentagon);
    BreakpointSpec other;
    other.access = BRK_MEM_READ;
    ASSERT_TRUE(BreakpointManager::ParsePageInto("divmmcrom", other, error));
    EXPECT_EQ(manager.AddBreakpoint(other, error), BRK_INVALID);
    EXPECT_NE(error.find("ZX Next"), std::string::npos) << error;
}

// The event names on the surfaces: the machine's are the main CPU's
TEST(NextMachineEventNames_Test, TheMachinesEventsAreTheMainCpus)
{
    for (const char* name : {"cpu_int", "cpu_reti", "cpu_nmi", "cpu_retn", "machine_reset", "divmmc_map",
                             "divmmc_unmap", "esxdos_call", "zxndma_start", "zxndma_done"})
    {
        GSCardEvent kind = GSCardEvent::Count;
        ASSERT_TRUE(ParseGSCardEvent(name, kind)) << name;
        EXPECT_TRUE(GSCardEventIsHostSide(kind)) << name;
        EXPECT_STREQ(GSCardEventName(kind), name);
    }
    GSCardEvent kind = GSCardEvent::Count;
    ASSERT_TRUE(ParseGSCardEvent("dma_start", kind));
    EXPECT_FALSE(GSCardEventIsHostSide(kind)) << "the NeoGS DMA stays the card's";
}
