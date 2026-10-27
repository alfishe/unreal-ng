// platform-debugging P6: the TS-Conf DMA through the page watchpoints, the FMaps register window through the port
// breakpoints, the beam on the TS-Conf raster, the machine's events (docs/inprogress/2026-10-27-platform-debugging/README.md)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

namespace
{
constexpr uint16_t kCode = 0x8000;
constexpr uint32_t kSource = 0x40000;       // RAM page #10
constexpr uint32_t kDestination = 0x50000;  // RAM page #14
constexpr uint8_t kSourcePage = kSource >> 14;
constexpr uint8_t kDestinationPage = kDestination >> 14;
constexpr uint32_t kBytes = 16;  // LEN 3 (4 words) x NUM 1 (2 blocks)
}  // namespace

class TsConfMachineEvents_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_TSConf* _ports = nullptr;
    BreakpointManager* _manager = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("TSL", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _ports = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _manager = _emulator->GetBreakpointManager();
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
    uint16_t AddEvent(GSCardEvent kind, int value = -1)
    {
        BreakpointSpec spec;
        spec.type = BRK_EVENT;
        spec.address = static_cast<uint16_t>(kind);
        if (value >= 0)
        {
            spec.nrValue = static_cast<uint8_t>(value);
            spec.nrMask = 0xFF;
        }
        return Add(spec);
    }
    uint16_t AddPageWatch(uint8_t access, uint8_t page, uint16_t offset)
    {
        BreakpointSpec spec;
        spec.access = access;
        spec.address = offset;
        spec.hasPage = true;
        spec.page = page;
        spec.pageType = BANK_RAM;
        return Add(spec);
    }
    uint32_t Hits(uint16_t id) { return _manager->GetBreakpointById(id)->hitCount; }
    uint8_t& Ram(uint32_t physical) { return _context->pMemory->RAMBase()[physical]; }

    /// The code at kCode: DI, then OUT (reg << 8 | #AF) for every pair, then JR $
    void LoadProgram(const std::vector<std::pair<uint8_t, uint8_t>>& registers)
    {
        std::vector<uint8_t> code = {0xF3};
        for (const auto& [reg, value] : registers)
            code.insert(code.end(), {0x01, 0xAF, reg, 0x3E, value, 0xED, 0x79});  // LD BC,reg<<8|#AF : LD A,v : OUT (C),A
        code.insert(code.end(), {0x18, 0xFE});
        for (size_t i = 0; i < code.size(); i++)
            _z80->DirectWrite(static_cast<uint16_t>(kCode + i), code[i]);
        _z80->pc = kCode;
        _z80->sp = 0xBF00;
        _z80->iff1 = _z80->iff2 = 0;
    }
    /// A RAM -> RAM copy of kBytes from kSource to kDestination
    void LoadDmaCopy()
    {
        for (uint32_t i = 0; i < kBytes; i++)
        {
            Ram(kSource + i) = static_cast<uint8_t>(0x80 + i);
            Ram(kDestination + i) = 0;
        }
        LoadProgram({{TsConfReg::DmaSAl, kSource & 0xFF},
                     {TsConfReg::DmaSAh, (kSource >> 8) & 0x3F},
                     {TsConfReg::DmaSAx, kSourcePage},
                     {TsConfReg::DmaDAl, kDestination & 0xFF},
                     {TsConfReg::DmaDAh, (kDestination >> 8) & 0x3F},
                     {TsConfReg::DmaDAx, kDestinationPage},
                     {TsConfReg::DmaLen, 3},
                     {TsConfReg::DmaNum, 1},
                     {TsConfReg::DmaCtrl, 0x01}});
    }
};

// The DMA writes physical RAM: a write watchpoint on that page stops after the DMA's byte, a dma_write stop at the offset
TEST_F(TsConfMachineEvents_Test, TheDmasWriteHitsTheWatchpointOfItsPage)
{
    LoadDmaCopy();
    const uint16_t id = AddPageWatch(BRK_MEM_WRITE, kDestinationPage, 5);
    _emulator->RunNFrames(1, false);
    const Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.reason, Emulator::DebugStop::Reason::Breakpoint);
    EXPECT_EQ(stop.breakpoint.breakpointId, id);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::DmaWrite);
    EXPECT_EQ(stop.breakpoint.address, 5);
    EXPECT_EQ(Ram(kDestination + 5), 0x85) << "the byte is written first";
}

// A read watchpoint on the source page sees the DMA's read; the CPU's own reads of the address do not reach it
TEST_F(TsConfMachineEvents_Test, TheDmasReadHitsAReadWatchpointOfItsPage)
{
    LoadDmaCopy();
    const uint16_t id = AddPageWatch(BRK_MEM_READ, kSourcePage, 2);
    _emulator->RunNFrames(1, false);
    const Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.breakpoint.breakpointId, id);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::DmaRead);
    EXPECT_EQ(stop.breakpoint.address, 2);
    EXPECT_EQ(Hits(id), 1u);
}

// tsdma_start with the device when DMA_CTRL starts the transfer, tsdma_done (1: the end) when it ends
TEST_F(TsConfMachineEvents_Test, TheDmasStartAndEndAreEvents)
{
    LoadDmaCopy();
    const uint16_t start = AddEvent(GSCardEvent::TsDmaStart, 0x01);
    const uint16_t done = AddEvent(GSCardEvent::TsDmaDone, 1);
    _emulator->RunNFrames(1, false);
    EXPECT_EQ(_emulator->LastStop().breakpoint.breakpointId, start);
    EXPECT_EQ(_emulator->LastStop().breakpoint.kind, BreakpointHitKind::Event);
    _emulator->RunNFrames(1, false);
    EXPECT_EQ(Hits(done), 1u);
    EXPECT_EQ(Ram(kDestination + kBytes - 1), 0x80 + kBytes - 1);
}

// The register window of FMaps: a write to +#400+n is OUT (n << 8 | #AF), a port breakpoint on that port stops
TEST_F(TsConfMachineEvents_Test, AnFMapsRegisterWriteHitsThePortBreakpoint)
{
    // FMaps at #C000 (bit 4: on), then LD (#C40F),A: the border register through the window
    std::vector<uint8_t> code = {0xF3, 0x01, 0xAF, TsConfReg::FMaps, 0x3E, 0x1C, 0xED, 0x79,
                                 0x3E, 0x42, 0x32, 0x0F, 0xC4, 0x18, 0xFE};
    for (size_t i = 0; i < code.size(); i++)
        _z80->DirectWrite(static_cast<uint16_t>(kCode + i), code[i]);
    _z80->pc = kCode;
    BreakpointSpec spec;
    spec.type = BRK_IO;
    spec.access = BRK_IO_OUT;
    spec.address = static_cast<uint16_t>(TsConfReg::Border << 8 | 0xAF);
    const uint16_t id = Add(spec);
    _emulator->RunNFrames(1, false);
    const Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.breakpoint.breakpointId, id);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::PortOut);
    EXPECT_EQ(stop.breakpoint.address, TsConfReg::Border << 8 | 0xAF);
    EXPECT_EQ(_ports->GetState().regs[TsConfReg::Border], 0x42);
}

// The frame interrupt the CPU takes: cpu_int with int_sel 0 and the vector #FF
TEST_F(TsConfMachineEvents_Test, TheFrameInterruptIsACpuIntOfTheFrame)
{
    // IM 2 : EI : HALT : JR $-1
    const uint8_t program[] = {0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD};
    for (size_t i = 0; i < sizeof(program); i++)
        _z80->DirectWrite(static_cast<uint16_t>(kCode + i), program[i]);
    _z80->pc = kCode;
    _z80->sp = 0xBF00;
    const uint16_t frame = AddEvent(GSCardEvent::CpuInt, 0);
    const uint16_t line = AddEvent(GSCardEvent::CpuInt, 1);
    _emulator->RunNFrames(2, false);
    EXPECT_EQ(_emulator->LastStop().breakpoint.breakpointId, frame);
    EXPECT_EQ(_emulator->LastStop().breakpoint.address, static_cast<uint16_t>(GSCardEvent::CpuInt));
    EXPECT_EQ(Hits(line), 0u) << "no line interrupt is enabled";
}

// A beam breakpoint on TS-Conf: line of the 320-line raster, hc the CPU tact in the 224-tact line
TEST_F(TsConfMachineEvents_Test, TheBeamStopsAtItsPlaceInTheRaster)
{
    LoadProgram({});
    BreakpointSpec spec;
    spec.type = BRK_BEAM;
    spec.address = 100;
    spec.beamHc = 50;
    const uint16_t id = Add(spec);
    EXPECT_TRUE(_context->beamWatch);
    _emulator->RunNFrames(2, false);
    EXPECT_EQ(_emulator->LastStop().breakpoint.breakpointId, id);
    EXPECT_EQ(_emulator->LastStop().breakpoint.kind, BreakpointHitKind::Beam);
    const uint32_t raster = _ports->GetEngine().StepEndRaster();
    const uint32_t place = 100 * 224 + 50;
    EXPECT_GT(raster, place) << "the step passed the place";
    EXPECT_LE(raster, place + 32) << "within the instruction that passed it";
    _manager->ClearUserBreakpoints();
    EXPECT_FALSE(_context->beamWatch);
}

TEST(TsConfMachineEventNames_Test, TheDmasEventsAreTheMainCpus)
{
    for (const char* name : {"tsdma_start", "tsdma_done"})
    {
        GSCardEvent kind = GSCardEvent::Count;
        ASSERT_TRUE(ParseGSCardEvent(name, kind)) << name;
        EXPECT_TRUE(GSCardEventIsHostSide(kind)) << name;
        EXPECT_STREQ(GSCardEventName(kind), name);
    }
}
