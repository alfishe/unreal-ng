// platform-debugging P7: the Sprinter's accelerator through the CPU's watchpoints, the Z84C15's interrupts, the PLD's
// internal codes and journal events as event breakpoints (docs/inprogress/2026-10-27-platform-debugging/README.md)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/breakpoints/chipregister.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/ports/models/sprinter/sprinterpldstate.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

namespace
{
constexpr uint16_t kCode = 0x8000;
constexpr uint16_t kData = 0x9000;
constexpr uint8_t kLength = 8;
constexpr uint16_t kAllModePort = 0x00C3;  // any port the Z84C15 does not own: the test's table entry
constexpr uint16_t kCblControlPort = 0x00C5;
constexpr uint16_t kCblDataPort = 0x00C7;
}  // namespace

class SprinterMachineEvents_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Sprinter* _ports = nullptr;
    BreakpointManager* _manager = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _ports = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _manager = _emulator->GetBreakpointManager();
        // The PLD configured at once (no loader run), the decoder open, the accelerator on, no original waits
        _context->config.sprinter.fast_start = 1;
        _ports->PowerCycle();
        _ports->DecodePortIn(0x00FE, 0x0000);
        _ports->GetPldState().allMode = 0x05;
        _ports->ApplyOrigWaits();
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
    uint16_t AddWatch(uint8_t access, uint16_t address)
    {
        BreakpointSpec spec;
        spec.access = access;
        spec.address = address;
        return Add(spec);
    }
    uint32_t Hits(uint16_t id) { return _manager->GetBreakpointById(id)->hitCount; }
    uint8_t Peek(uint16_t address) { return _context->pMemory->DirectReadFromZ80Memory(address); }

    /// `code` at kCode, then JR $
    void Load(std::vector<uint8_t> code)
    {
        code.insert(code.end(), {0x18, 0xFE});
        for (size_t i = 0; i < code.size(); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kCode + i), code[i]);
        _z80->pc = kCode;
        _z80->sp = 0xBF00;
        _z80->iff1 = _z80->iff2 = 0;
    }
};

// LD C,C (fill) : LD (HL),A: the CPU stores the first byte, the accelerator the others; a write watchpoint on one of
// them stops after its byte with accel_write
TEST_F(SprinterMachineEvents_Test, TheAcceleratorsFillHitsTheWriteWatchpoint)
{
    // LD HL,kData : LD A,#AA : LD D,D : LD E,kLength (the length) : LD C,C : LD (HL),A : LD B,B
    Load({0x21, kData & 0xFF, kData >> 8, 0x3E, 0xAA, 0x52, 0x1E, kLength, 0x49, 0x77, 0x40});
    const uint16_t id = AddWatch(BRK_MEM_WRITE, kData + 3);
    _emulator->RunNFrames(1, false);
    const Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.reason, Emulator::DebugStop::Reason::Breakpoint);
    EXPECT_EQ(stop.breakpoint.breakpointId, id);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::AccelWrite);
    EXPECT_STREQ(BreakpointHitKindName(stop.breakpoint.kind), "accel_write");
    EXPECT_EQ(stop.breakpoint.address, kData + 3);
    EXPECT_EQ(Peek(kData + kLength - 1), 0xAA) << "the fill went on to its end";
}

// LD L,L (copy) : LD A,(HL): the accelerator's repeated reads go through the read watchpoints (accel_read)
TEST_F(SprinterMachineEvents_Test, TheAcceleratorsCopyReadHitsTheReadWatchpoint)
{
    // LD HL,kData : LD D,D : LD E,kLength : LD L,L : LD A,(HL) : LD B,B
    Load({0x21, kData & 0xFF, kData >> 8, 0x52, 0x1E, kLength, 0x6D, 0x7E, 0x40});
    const uint16_t first = AddWatch(BRK_MEM_READ, kData);
    const uint16_t id = AddWatch(BRK_MEM_READ, kData + 2);
    _emulator->RunNFrames(1, false);
    const Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.breakpoint.breakpointId, first) << "the CPU's own read comes first";
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::MemoryRead);
    EXPECT_EQ(Hits(id), 1u);
    EXPECT_STREQ(BreakpointHitKindName(BreakpointHitKind::AccelRead), "accel_read");
}

// CTC channel 0 counts down and interrupts: cpu_int with the source 0 and the vector the chip gave
TEST_F(SprinterMachineEvents_Test, TheCtcsInterruptIsACpuIntOfItsChannel)
{
    // A vector table at #8100 to a handler at #8282 (EI : RETI), whoever interrupts
    for (uint16_t a = 0x8100; a <= 0x8200; a++)
        _context->pMemory->DirectWriteToZ80Memory(a, 0x82);
    for (uint8_t b : {0xFB, 0xED, 0x4D})
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8282 + (b == 0xFB ? 0 : b == 0xED ? 1 : 2)), b);
    // CTC 0: vector #00, timer, prescaler 16, time constant #10, interrupt on; I = #81, IM 2, EI, HALT
    Load({0x3E, 0x00, 0xD3, 0x10, 0x3E, 0x87, 0xD3, 0x10, 0x3E, 0x10, 0xD3, 0x10, 0x3E, 0x81, 0xED, 0x47,
          0xED, 0x5E, 0xFB, 0x76});
    const uint16_t ctc = AddEvent(GSCardEvent::CpuInt, 0);
    const uint16_t reti = AddEvent(GSCardEvent::CpuReti, 0);
    _emulator->RunNFrames(1, false);
    EXPECT_EQ(_emulator->LastStop().breakpoint.breakpointId, ctc);
    EXPECT_EQ(_ports->GetZ84().LastAcknowledgedSource(), 0);
    _emulator->RunNFrames(1, false);
    EXPECT_EQ(_emulator->LastStop().breakpoint.breakpointId, reti) << "the handler's RETI ends channel 0's service";
}

// A port the PLD decodes: pld_write with its internal code; the code's journal event: pld_event (9: all_mode)
TEST_F(SprinterMachineEvents_Test, ThePldsCodeAndItsJournalEventAreEvents)
{
    uint8_t* table = _context->pMemory->RAMPageAddress(SprinterMemory::kPortTablePage);
    table[_ports->LookupIndex(kAllModePort, false)] = SprinterCode::AllMode;
    // LD BC,kAllModePort : LD A,#01 : OUT (C),A
    Load({0x01, kAllModePort & 0xFF, kAllModePort >> 8, 0x3E, 0x01, 0xED, 0x79});
    const uint16_t code = AddEvent(GSCardEvent::PldWrite, SprinterCode::AllMode);
    const uint16_t other = AddEvent(GSCardEvent::PldWrite, SprinterCode::Border);
    const uint16_t allMode = AddEvent(GSCardEvent::PldEvent, 9);
    _emulator->RunNFrames(1, false);
    EXPECT_EQ(Hits(code), 1u);
    EXPECT_EQ(Hits(other), 0u);
    EXPECT_EQ(Hits(allMode), 1u) << "ALL_MODE #05 -> #01 is in the journal's terms an all_mode event";
    EXPECT_EQ(_ports->GetPldState().allMode, 0x01);
}

// The Covox Blaster as a sound chip (bchip cbl): register 1 the control, 0 the data; each stops after its OUT
TEST_F(SprinterMachineEvents_Test, TheCovoxBlastersControlAndDataAreChipRegisters)
{
    uint8_t* table = _context->pMemory->RAMPageAddress(SprinterMemory::kPortTablePage);
    table[_ports->LookupIndex(kCblControlPort, false)] = SprinterCode::CovoxBlaster;
    table[_ports->LookupIndex(kCblDataPort, false)] = SprinterCode::Covox;
    // LD BC,kCblControlPort : LD A,#80 : OUT (C),A (the ring on) : LD C,kCblDataPort : LD A,#55 : OUT (C),A
    Load({0x01, kCblControlPort & 0xFF, kCblControlPort >> 8, 0x3E, 0x80, 0xED, 0x79, 0x0E, kCblDataPort & 0xFF,
          0x3E, 0x55, 0xED, 0x79});
    auto chip = [&](uint16_t reg, int value, uint8_t mask) {
        BreakpointSpec spec;
        spec.type = BRK_CHIPREG;
        spec.access = 0;
        spec.chip = static_cast<uint8_t>(SoundChipKind::Cbl);
        spec.address = reg;
        spec.nrValue = static_cast<uint8_t>(value);
        spec.nrMask = mask;
        return Add(spec);
    };
    const uint16_t ringOn = chip(1, 0x80, 0x80);
    const uint16_t data = chip(0, 0x55, 0xFF);
    _emulator->RunNFrames(1, false);
    Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.breakpoint.breakpointId, ringOn);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::ChipRegister);
    EXPECT_EQ(stop.breakpoint.address, 1);
    EXPECT_EQ(Hits(data), 0u) << "the stop comes after the control's OUT, before the data's";
    _emulator->RunNFrames(1, false);
    stop = _emulator->LastStop();
    EXPECT_EQ(stop.breakpoint.breakpointId, data);
    EXPECT_EQ(stop.breakpoint.address, 0);
    EXPECT_STREQ(SoundChipKindName(SoundChipKind::Cbl), "cbl");
}

TEST(SprinterMachineEventNames_Test, ThePldsEventsAreTheMainCpus)
{
    for (const char* name : {"pld_read", "pld_write", "pld_event"})
    {
        GSCardEvent kind = GSCardEvent::Count;
        ASSERT_TRUE(ParseGSCardEvent(name, kind)) << name;
        EXPECT_TRUE(GSCardEventIsHostSide(kind)) << name;
        EXPECT_STREQ(GSCardEventName(kind), name);
    }
}
