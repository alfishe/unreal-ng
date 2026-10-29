// NextREG breakpoints (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P3)
// Source: zxnext.vhd. Every register write passes nr_wr_en (cpu_req or copper_req: the NEXTREG instructions and OUT #253B on the CPU
// side, a copper MOVE on the other), a read is port_253b_rd. The line interrupt and the copper share one vertical counter (cvc of
// zxula_timing.vhd) and no signal: int_line fires at hc_ula = 255 of the row before the line, a WAIT is true from hc >= 8 * h + 12
// (copper.vhd) whatever the CPU is doing. CSpect (owner's report) loses the copper's WAIT when both fall on the same place.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/breakpoints/breakpointcondition.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/io/z80n/nextcopper.h"
#include "emulator/io/z80n/nextregjournal.h"
#include "emulator/io/z80n/nexttiming.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
// Registers used (NextREG numbers from the register map, nextregtable.inc)
constexpr uint8_t kRegCpuSpeed = NextBoard::kRegCpuSpeed;        // NR #07
constexpr uint8_t kRegPeripheral2 = NextBoard::kRegPeripheral2;  // NR #08
constexpr uint8_t kRegUnwatched = 0x0F;                          // NR #0F, a plain register
constexpr uint8_t kRegGlobalTransparency = 0x14;                 // NR #14 and NR #4A only store their byte: the copper's MOVE shows in Stored()
constexpr uint8_t kRegSpriteLayers = 0x15;                       // NR #15
constexpr uint8_t kRegHandlerScratch = 0x16;                     // NR #16: what the interrupt handler of the test writes
constexpr uint8_t kRegLineInterruptControl = 0x22;               // NR #22 bit 1: the line interrupt on (bit 0 is line bit 8)
constexpr uint8_t kRegLineInterruptValue = 0x23;                 // NR #23: the line
constexpr uint8_t kRegPaletteIndex = 0x40;                       // NR #40..#44: the palette registers
constexpr uint8_t kRegPaletteFormat = 0x42;                      // NR #42
constexpr uint8_t kRegPaletteIndexLast = 0x44;
constexpr uint8_t kRegFallbackColour = 0x4A;                     // NR #4A
constexpr uint16_t kRegCount = 256;                              // NextREG numbers are 8 bits

// Ports (the NEXTREG pair)
constexpr uint16_t kPortRegSelect = 0x243B;
constexpr uint16_t kPortRegData = 0x253B;
// Payloads of the writes (any bytes: only their identity matters)
constexpr uint8_t kByteA = 0x11;
constexpr uint8_t kByteB = 0x22;

// The raster, from the platform constants. A 7 MHz pixel clock is two T-states of the 3.5 MHz CPU clock; the copper and the video count
// in 28 MHz clocks, four to a pixel (copper.vhd runs on i_CLK_28, the pixel counters on i_CLK_7)
constexpr unsigned kPixelsPerTState = 2;       // 7 MHz / 3.5 MHz
constexpr unsigned kCopperClocksPerPixel = NextCopper::kClocksPerPixel;  // 28 MHz / 7 MHz
// copper.vhd: a WAIT with horizontal h is true from hc >= (h << 3) + kWaitHcBase
constexpr unsigned kWaitHcBase = 12;
constexpr unsigned kWaitHcStep = 8;
constexpr unsigned kWaitHorizontalMax = 63;  // the horizontal field of a WAIT is 6 bits
// zxula_timing.vhd: int_line fires when hc_ula = 255, on the row before the line it belongs to ("before the line is drawn")
constexpr unsigned kLineInterruptHc = 255;
// The paper is 192 lines; the line counter (cvc, NR #1E / #1F) is 0 on its first
constexpr unsigned kPaperLines = 192;

struct Raster
{
    NextTiming timing;
    unsigned hcPerLine;  // pixel clocks per row
    unsigned lines;      // rows per frame
    explicit Raster(uint8_t family)
    {
        EXPECT_TRUE(NextTimingFor(family, timing));
        hcPerLine = timing.line * kPixelsPerTState;
        lines = timing.frame / timing.line;
    }
    /// Copper time (28 MHz clocks from line 0 / hc 0) of a place in the raster
    uint64_t At(unsigned line, unsigned hc) const { return (static_cast<uint64_t>(line) * hcPerLine + hc) * kCopperClocksPerPixel; }
    static unsigned WaitThreshold(unsigned h) { return h * kWaitHcStep + kWaitHcBase; }
    /// The largest horizontal whose WAIT is true at pixel clock `hc`
    static unsigned LastWaitHorizontalAt(unsigned hc) { return (hc - kWaitHcBase) / kWaitHcStep; }
};

BreakpointSpec NrSpec(uint16_t reg, uint8_t access)
{
    BreakpointSpec spec;
    spec.type = BRK_NEXTREG;
    spec.access = access;
    spec.address = reg;
    return spec;
}
}  // namespace

TEST(NextRegBreakpointManager_Test, TheSpecIsCheckedAndTheHotPathAnswersByRegisterAccessDoorAndValue)
{
    EmulatorContext context(LoggerLevel::LogError);
    BreakpointManager manager(&context);
    std::string error;

    BreakpointSpec bad = NrSpec(kRegCount, BRK_IO_OUT);
    EXPECT_EQ(manager.AddBreakpoint(bad, error), BRK_INVALID);
    EXPECT_NE(error.find("0..255"), std::string::npos) << error;
    bad = NrSpec(kRegCpuSpeed, BRK_IO_IN);
    bad.nrDoors = BRK_NR_DOOR_COPPER;
    EXPECT_EQ(manager.AddBreakpoint(bad, error), BRK_INVALID) << "a read comes through #253B only";
    bad = NrSpec(kRegCpuSpeed, BRK_IO_OUT);
    bad.portMask = 0x00FF;
    EXPECT_EQ(manager.AddBreakpoint(bad, error), BRK_INVALID);
    bad = NrSpec(kRegCpuSpeed, BRK_IO_OUT);
    bad.nrDoors = 0;
    EXPECT_EQ(manager.AddBreakpoint(bad, error), BRK_INVALID);
    BreakpointSpec memory;
    memory.nrMask = 0xFF;
    EXPECT_EQ(manager.AddBreakpoint(memory, error), BRK_INVALID) << "a value belongs to NextREG breakpoints";

    // write of #07 from any door
    const uint16_t any = manager.AddBreakpoint(NrSpec(kRegCpuSpeed, BRK_IO_OUT), error);
    ASSERT_NE(any, BRK_INVALID) << error;
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegCpuSpeed, 0x00, BRK_NR_DOOR_NEXTREG), any);
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegCpuSpeed, 0x00, BRK_NR_DOOR_COPPER), any);
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_READ, kRegCpuSpeed, 0x00, BRK_NR_DOOR_PORT), BRK_INVALID) << "a write breakpoint";
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegPeripheral2, 0x00, BRK_NR_DOOR_NEXTREG), BRK_INVALID) << "another register";
    EXPECT_EQ(manager.GetBreakpointById(any)->hitCount, 2u);

    // the copper's writes of a range, with a masked value
    BreakpointSpec range = NrSpec(kRegPaletteIndex, BRK_IO_OUT);
    range.hasEnd = true;
    range.addressEnd = kRegPaletteIndexLast;
    range.nrDoors = BRK_NR_DOOR_COPPER;
    range.nrValue = 0x01;
    range.nrMask = 0x03;
    const uint16_t copper = manager.AddBreakpoint(range, error);
    ASSERT_NE(copper, BRK_INVALID) << error;
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegPaletteFormat, 0xFD, BRK_NR_DOOR_COPPER), copper) << "bits 1:0 = 01";
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegPaletteFormat, 0xFE, BRK_NR_DOOR_COPPER), BRK_INVALID) << "bits 1:0 = 10";
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegPaletteFormat, 0x01, BRK_NR_DOOR_NEXTREG), BRK_INVALID) << "not by the CPU";
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegPaletteIndexLast + 1, 0x01, BRK_NR_DOOR_COPPER), BRK_INVALID) << "outside the range";

    // the listing: padded numbers (the columns before them are left-justified)
    const std::string line = manager.FormatBreakpointInfo(copper);
    EXPECT_NE(line.find("NextREG"), std::string::npos) << line;
    EXPECT_NE(line.find("NR 0x40"), std::string::npos) << line;
    EXPECT_NE(line.find("to 0x0044"), std::string::npos) << line;
    EXPECT_NE(line.find("via copper"), std::string::npos) << line;
    EXPECT_NE(line.find("value 0x01 mask 0x03"), std::string::npos) << line;
    BreakpointSpec place;
    place.type = BRK_BEAM;
    place.address = 99;
    place.beamHc = 255;
    const uint16_t beamId = manager.AddBreakpoint(place, error);
    ASSERT_NE(beamId, BRK_INVALID) << error;
    EXPECT_NE(manager.FormatBreakpointInfo(beamId).find("hc 255"), std::string::npos);
    BreakpointSpec instructions;
    instructions.type = BRK_COPPER;
    instructions.address = 12;
    instructions.hasEnd = true;
    instructions.addressEnd = 14;
    const uint16_t copperId = manager.AddBreakpoint(instructions, error);
    ASSERT_NE(copperId, BRK_INVALID) << error;
    EXPECT_NE(manager.FormatBreakpointInfo(copperId).find("to 0x000E"), std::string::npos) << manager.FormatBreakpointInfo(copperId);
    EXPECT_EQ(manager.HandleCopperPc(13), copperId);
    EXPECT_EQ(manager.HandleCopperPc(15), BRK_INVALID);
    EXPECT_EQ(manager.HandleBeam([](uint16_t line, uint16_t hc) { return line == 99 && hc == 255; }), beamId);
    EXPECT_EQ(manager.HandleBeam([](uint16_t, uint16_t) { return false; }), BRK_INVALID);
    BreakpointSpec badBeam = place;
    badBeam.beamHc = BRK_BEAM_MAX_HC + 1;
    EXPECT_EQ(manager.AddBreakpoint(badBeam, error), BRK_INVALID);
    badBeam = place;
    badBeam.hasEnd = true;
    badBeam.addressEnd = place.address + 1;
    EXPECT_EQ(manager.AddBreakpoint(badBeam, error), BRK_INVALID) << "a beam place is one place";
    ASSERT_TRUE(manager.RemoveBreakpointByID(beamId));
    ASSERT_TRUE(manager.RemoveBreakpointByID(copperId));
    // a hit policy counts every match and stops on the chosen ones
    BreakpointSpec third = NrSpec(kRegSpriteLayers, BRK_IO_IN | BRK_IO_OUT);
    third.hitMode = BRK_HIT_EQUAL;
    third.hitTarget = 3;
    const uint16_t counted = manager.AddBreakpoint(third, error);
    ASSERT_NE(counted, BRK_INVALID) << error;
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegSpriteLayers, 0, BRK_NR_DOOR_PORT), BRK_INVALID);
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_READ, kRegSpriteLayers, 0, BRK_NR_DOOR_PORT), BRK_INVALID);
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegSpriteLayers, 0, BRK_NR_DOOR_PORT), counted) << "the 3rd access";
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegSpriteLayers, 0, BRK_NR_DOOR_PORT), BRK_INVALID);

    // off, removed: the gate and the filter follow
    ASSERT_TRUE(manager.DeactivateBreakpoint(any));
    EXPECT_EQ(manager.HandleNextReg(BRK_NR_WRITE, kRegCpuSpeed, 0x00, BRK_NR_DOOR_NEXTREG), BRK_INVALID);
    ASSERT_TRUE(manager.RemoveBreakpointByID(copper));
    ASSERT_TRUE(manager.RemoveBreakpointByID(counted));
    EXPECT_FALSE(manager.GetHotState()->hasNextRegRead);
    EXPECT_FALSE(manager.GetHotState()->hasNextRegWrite);
    EXPECT_FALSE(manager.GetHotState()->copperWatch) << "nothing on the copper is left: the machine pays nothing";

    // the surfaces' words
    uint8_t doors = 0;
    ASSERT_TRUE(BreakpointManager::ParseNextRegDoors("Copper, port", doors, error)) << error;
    EXPECT_EQ(doors, BRK_NR_DOOR_COPPER | BRK_NR_DOOR_PORT);
    EXPECT_EQ(BreakpointManager::NextRegDoorsName(doors), "port,copper");
    ASSERT_TRUE(BreakpointManager::ParseNextRegDoors("", doors, error));
    EXPECT_EQ(doors, BRK_NR_DOOR_ALL);
    EXPECT_FALSE(BreakpointManager::ParseNextRegDoors("dma", doors, error));
}

class NextRegBreakpointMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;
    BreakpointManager* _manager = nullptr;
    Raster _raster{2};  // 128K family, the machine's default
    uint8_t _transparencyAtStart = 0;  // the registers the copper writes hold their reset value, not 0
    uint8_t _fallbackAtStart = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _manager = _emulator->GetBreakpointManager();
        ASSERT_NE(_manager, nullptr);
        _raster = Raster(_ports->Board().Timing());
        _transparencyAtStart = _ports->Board().Stored(kRegGlobalTransparency);
        _fallbackAtStart = _ports->Board().Stored(kRegFallbackColour);
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
    uint16_t Last() { return _manager->GetLastTriggeredBreakpointID(); }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    void Nr(uint8_t reg, uint8_t value) { Out(kPortRegSelect, reg); Out(kPortRegData, value); }
    uint8_t In(uint16_t port) { return _ports->DecodePortIn(port, 0); }
    NextBoard& Board() { return _ports->Board(); }
    /// The copper list from instruction 0 through NR #60 and the mode in NR #62 (via the board: no port trace, no breakpoint of ours)
    void CopperList(const std::vector<uint16_t>& words, uint8_t mode)
    {
        _beamClock = 0;  // the copper follows the test's beam from the first write (the machine's own clock would run it to a moment of its own)
        BindBeam();
        Board().Copper().Reset();
        Board().Copper().SetGeometry(_raster.hcPerLine, _raster.lines);
        Board().Copper().WriteAddressLow(0);
        Board().Copper().WriteControl(0);
        std::vector<uint16_t> list = words;
        list.push_back(Wait(BRK_BEAM_MAX_LINE, kWaitHorizontalMax));  // the usual halt: the rest of the list is NOPs that would run on and loop
        for (uint16_t w : list)
        {
            Board().Copper().WriteData(static_cast<uint8_t>(w >> 8));
            Board().Copper().WriteData(static_cast<uint8_t>(w & 0xFF));
        }
        Board().Copper().WriteControl(static_cast<uint8_t>(mode << 6));
    }
    static uint16_t Wait(unsigned line, unsigned h) { return static_cast<uint16_t>(0x8000 | (h << 9) | line); }
    static uint16_t Move(unsigned reg, unsigned value) { return static_cast<uint16_t>((reg << 8) | value); }
    uint64_t At(unsigned line, unsigned hc) const { return _raster.At(line, hc); }
    /// The beam the copper follows (the machine binds the raster's own); the picture is not drawn in these tests
    uint64_t _beamClock = 0;
    void BindBeam()
    {
        Board().Copper().SetNow([this]() { return _beamClock; });
        Board().Copper().SetBeforeWrite(nullptr);
    }
    /// The machine runs an instruction ending with the beam at (line, hc): the board runs the copper to it, then the watchers look
    void BeamAt(unsigned line, unsigned hc)
    {
        _beamClock = At(line, hc);
        Board().AfterInstruction();
    }
};

TEST_F(NextRegBreakpointMachine_Test, EachDoorOfAWriteAndTheReadDoorAreTold)
{
    const uint16_t nextreg = Add(NrSpec(kRegCpuSpeed, BRK_IO_OUT));
    Board().WriteNextReg(kRegPeripheral2, kByteA);
    EXPECT_EQ(Last(), BRK_INVALID) << "another register";
    Board().WriteNextReg(kRegCpuSpeed, kByteA);
    EXPECT_EQ(Last(), nextreg) << "NEXTREG n,v";
    _manager->ClearLastTriggeredBreakpoint();

    Out(kPortRegSelect, kRegCpuSpeed);
    EXPECT_EQ(Last(), BRK_INVALID) << "the select latch is not an access of the register";
    Out(kPortRegData, kByteB);
    EXPECT_EQ(Last(), nextreg) << "OUT (#253B)";
    _manager->ClearLastTriggeredBreakpoint();

    ASSERT_TRUE(_manager->RemoveBreakpointByID(nextreg));
    BreakpointSpec copperOnly = NrSpec(kRegGlobalTransparency, BRK_IO_OUT);
    copperOnly.nrDoors = BRK_NR_DOOR_COPPER;
    const uint16_t copper = Add(copperOnly);
    Board().WriteNextReg(kRegGlobalTransparency, kByteA);
    EXPECT_EQ(Last(), BRK_INVALID) << "the CPU's door is not watched";

    // WAIT line 5, horizontal 0 (true from hc = kWaitHcBase), then a MOVE. The copper runs with the machine: the stop comes at the first
    // instruction boundary after the beam reached the MOVE, and the MOVE is held back until the machine runs on
    constexpr unsigned kWaitLine = 5;
    CopperList({Wait(kWaitLine, 0), Move(kRegGlobalTransparency, kByteB)}, 1);
    BindBeam();
    BeamAt(kWaitLine, Raster::WaitThreshold(0) - 1);  // the first look only notes where the beam is
    EXPECT_EQ(Last(), BRK_INVALID) << "the beam is not there yet";
    BeamAt(kWaitLine, Raster::WaitThreshold(0) + 1);
    EXPECT_EQ(Last(), copper);
    EXPECT_EQ(Board().Stored(kRegGlobalTransparency), kByteA) << "stopped before the write";
    EXPECT_EQ(_manager->GetBreakpointById(copper)->hitCount, 1u);
    Board().AfterInstruction();  // the machine runs on: the held MOVE writes, and is not counted or stopped again
    EXPECT_EQ(Board().Stored(kRegGlobalTransparency), kByteB);
    EXPECT_EQ(_manager->GetBreakpointById(copper)->hitCount, 1u);

    // a read: a program's IN (#253B) is told, the board's own read (the state reports) never is
    ASSERT_TRUE(_manager->RemoveBreakpointByID(copper));
    _manager->ClearLastTriggeredBreakpoint();
    const uint16_t reading = Add(NrSpec(kRegUnwatched, BRK_IO_IN));
    (void)Board().Read(kRegUnwatched);
    EXPECT_EQ(Last(), BRK_INVALID) << "the board's own read is no access of the program";
    Out(kPortRegSelect, kRegUnwatched);
    (void)In(kPortRegData);
    EXPECT_EQ(Last(), reading) << "IN (#253B)";
}

TEST_F(NextRegBreakpointMachine_Test, ATestToolsWriteIsNotAnAccessOfTheProgram)
{
    const uint16_t id = Add(NrSpec(kRegCpuSpeed, BRK_IO_OUT));
    _context->toolAccessActive = true;  // what NextRegWriteControl::Write holds around the board write
    Board().WriteNextReg(kRegCpuSpeed, kByteA);
    _context->toolAccessActive = false;
    EXPECT_EQ(Last(), BRK_INVALID);
    EXPECT_EQ(_manager->GetBreakpointById(id)->hitCount, 0u);
}

// The coexistence the owner was told CSpect gets wrong (the line interrupt at the right border after the paper, the copper's WAIT at the
// same place). Both are free-running counters of one vertical count; the copper's MOVE must happen whatever the interrupt does.
TEST_F(NextRegBreakpointMachine_Test, ALineInterruptAndACopperWaitAtTheSamePlaceBothHappen)
{
    // The interrupt of line N fires on the row before it, so the copper looks at row N - 1 (the same cvc)
    constexpr unsigned kLine = kPaperLines / 2;
    constexpr unsigned kRow = kLine - 1;
    Nr(kRegLineInterruptControl, 0x02);  // bit 1: on; bit 0 is line bit 8
    Nr(kRegLineInterruptValue, kLine);
    NextInterruptSource& interrupts = _ports->Interrupts();
    Z80& z80 = *_context->pCore->GetZ80();

    // Where the interrupt source puts the pulse: scan the frame after the ULA interrupt of the frame start
    uint32_t interruptT = 0;
    bool found = false;
    for (uint32_t t = _raster.timing.intStart + _raster.timing.intLength; t < _raster.timing.frame && !found; t++)
    {
        z80.t = t;
        if (interrupts.IsIntAsserted(t))
        {
            interruptT = t;
            found = true;
        }
    }
    ASSERT_TRUE(found);
    z80.t = interruptT;
    EXPECT_EQ(interrupts.CurrentLine(), kRow) << "int_line fires on cvc = N - 1";

    // The WAITs on the interrupt's row: the last horizontal true at the interrupt's pixel clock, and the next one (true only after it)
    const unsigned before = Raster::LastWaitHorizontalAt(kLineInterruptHc);
    const unsigned after = before + 1;
    ASSERT_LE(Raster::WaitThreshold(before), kLineInterruptHc);
    ASSERT_GT(Raster::WaitThreshold(after), kLineInterruptHc);
    const uint16_t beforeBp = Add(NrSpec(kRegGlobalTransparency, BRK_IO_OUT));
    const uint16_t afterBp = Add(NrSpec(kRegFallbackColour, BRK_IO_OUT));
    CopperList({Wait(kRow, before), Move(kRegGlobalTransparency, kByteA), Wait(kRow, after), Move(kRegFallbackColour, kByteB)}, 1);

    BindBeam();
    // The beam moves; the board runs the copper to it after each instruction. A MOVE under a breakpoint stops the machine before it writes
    // (the copper holds it), and writes when the machine runs on
    auto beam = [&](unsigned hc) { BeamAt(kRow, hc); };
    beam(Raster::WaitThreshold(before) - 1);
    EXPECT_EQ(Board().Stored(kRegGlobalTransparency), _transparencyAtStart) << "one clock before the first WAIT is true";
    EXPECT_EQ(Last(), BRK_INVALID);
    beam(kLineInterruptHc);
    EXPECT_EQ(Last(), beforeBp) << "the first MOVE, before the interrupt's pixel clock: the breakpoint holds it";
    EXPECT_EQ(Board().Stored(kRegGlobalTransparency), _transparencyAtStart);
    beam(kLineInterruptHc);
    EXPECT_EQ(Board().Stored(kRegGlobalTransparency), kByteA) << "run on: the MOVE writes";
    EXPECT_EQ(Board().Stored(kRegFallbackColour), _fallbackAtStart);

    // The CPU takes the interrupt now (acknowledge, a handler writing a register), the copper goes on
    z80.t = interruptT;
    EXPECT_TRUE(interrupts.IsIntAsserted(interruptT));
    (void)interrupts.AcknowledgeInterrupt(interruptT);
    Board().WriteNextReg(kRegHandlerScratch, kByteA);
    beam(Raster::WaitThreshold(after) - 1);
    EXPECT_EQ(Board().Stored(kRegFallbackColour), _fallbackAtStart) << "one clock before the second WAIT is true";
    EXPECT_EQ(Last(), beforeBp);
    beam(Raster::WaitThreshold(after) + 1);
    EXPECT_EQ(Last(), afterBp) << "the second MOVE, after the interrupt's place";
    EXPECT_EQ(Board().Stored(kRegFallbackColour), _fallbackAtStart) << "held";
    beam(Raster::WaitThreshold(after) + 1);
    EXPECT_EQ(Board().Stored(kRegFallbackColour), kByteB);
    EXPECT_EQ(_manager->GetBreakpointById(beforeBp)->hitCount, 1u);
    EXPECT_EQ(_manager->GetBreakpointById(afterBp)->hitCount, 1u);
}

TEST_F(NextRegBreakpointMachine_Test, ABeamBreakpointStopsWhenTheBeamPassesThePlaceAndACopperOneWhenTheInstructionIsDone)
{
    constexpr unsigned kRow = kPaperLines / 4;
    constexpr unsigned kHc = kLineInterruptHc;
    BreakpointSpec beam;
    beam.type = BRK_BEAM;
    beam.address = kRow;
    beam.beamHc = kHc;
    const uint16_t beamBp = Add(beam);
    BreakpointSpec instruction;
    instruction.type = BRK_COPPER;
    instruction.address = 1;  // the MOVE: the second instruction of the list
    const uint16_t copperBp = Add(instruction);

    CopperList({Wait(kRow, 0), Move(kRegGlobalTransparency, kByteB)}, 1);
    BindBeam();
    BeamAt(kRow, 0);  // the first look only notes where the beam is
    EXPECT_EQ(Last(), BRK_INVALID);
    BeamAt(kRow, Raster::WaitThreshold(0) + 1);  // the WAIT is true from hc = kWaitHcBase and the MOVE (index 1) follows at once
    EXPECT_EQ(Last(), copperBp) << "the copper completed the instruction at index 1";
    EXPECT_EQ(Board().Stored(kRegGlobalTransparency), _transparencyAtStart) << "held before the MOVE writes";
    _manager->ClearLastTriggeredBreakpoint();
    BeamAt(kRow, kHc - 1);
    EXPECT_EQ(Last(), BRK_INVALID);
    BeamAt(kRow, kHc + 1);
    EXPECT_EQ(Last(), beamBp) << "the beam passed (row, hc)";
    _manager->ClearLastTriggeredBreakpoint();
    BeamAt(kRow, kHc + 2);
    EXPECT_EQ(Last(), BRK_INVALID) << "once";

    // across the end of the frame: a place early in the row 0
    ASSERT_TRUE(_manager->RemoveBreakpointByID(beamBp));
    beam.address = 0;
    beam.beamHc = kWaitHcBase;
    const uint16_t wrapBp = Add(beam);
    BeamAt(_raster.lines - 1, _raster.hcPerLine - 1);
    EXPECT_EQ(Last(), BRK_INVALID);
    BeamAt(0, kWaitHcBase + 1);
    EXPECT_EQ(Last(), wrapBp) << "the beam wrapped into the next frame and passed the place";
}

TEST_F(NextRegBreakpointMachine_Test, TheJournalStampsACopperWriteWithTheBeamOfTheMoveNotTheCpuTimeOfTheCatchUp)
{
    Board().Journal().SetEnabled(true);
    constexpr unsigned kRow = kPaperLines / 4;
    CopperList({Wait(kRow, 0), Move(kRegGlobalTransparency, kByteB)}, 1);
    Board().Copper().RunTo(At(kRow + 3, 0));  // the catch-up is three rows late
    const std::vector<NextRegWriteEvent> events = Board().Journal().Query(NextRegJournalQuery{});
    ASSERT_FALSE(events.empty());
    const NextRegWriteEvent& move = events.back();
    EXPECT_EQ(move.source, NextRegSource::Copper);
    EXPECT_EQ(move.beamLine, kRow);
    EXPECT_EQ(move.beamHc, Raster::WaitThreshold(0)) << "the WAIT is true on the clock hc = kWaitHcBase; the MOVE follows within that pixel";
}

// Magic-opcode breakpoints (P6, G9): ED FF (jnext, ZEsarUX) and DD 01 (CSpect) are debug traps in other emulators. On the Z80N they are
// what the opcode table says (ED FF is a NOP of two bytes, DD 01 is LD BC,nn with the prefix ignored); the breakpoint only notices them
namespace
{
constexpr uint16_t kProgram = 0x8000;
constexpr uint16_t kEdFfLength = 2;           // ED, FF
constexpr uint16_t kDd01Length = 4;           // DD, 01, nn, nn
constexpr uint16_t kLoadedBc = 0x1234;        // the operand of DD 01
constexpr uint8_t kPrefixEd = 0xED, kPrefixDd = 0xDD, kOpcodeFf = 0xFF, kOpcodeLdBc = 0x01;
}  // namespace

TEST_F(NextRegBreakpointMachine_Test, AMagicOpcodeStopsAfterItRanAndExecutesAsOnTheHardware)
{
    Memory* memory = _context->pMemory;
    Z80* z80 = _context->pCore->GetZ80();
    const uint8_t program[] = {kPrefixEd, kOpcodeFf, kPrefixDd, kOpcodeLdBc, static_cast<uint8_t>(kLoadedBc & 0xFF),
                               static_cast<uint8_t>(kLoadedBc >> 8)};
    for (size_t i = 0; i < sizeof(program); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kProgram + i), program[i]);

    // Without a breakpoint: the instruction runs, nothing is told
    z80->pc = kProgram;
    const uint32_t first = z80->tt;
    z80->EngineStep();
    EXPECT_EQ(z80->pc, kProgram + kEdFfLength);
    EXPECT_EQ(Last(), BRK_INVALID);
    const uint32_t edFfTime = z80->tt - first;

    BreakpointSpec edff;
    edff.type = BRK_MAGIC;
    edff.access = BRK_IO_OUT;
    edff.address = BRK_MAGIC_ED_FF;
    const uint16_t edffId = Add(edff);
    BreakpointSpec dd01 = edff;
    dd01.address = BRK_MAGIC_DD_01;
    const uint16_t dd01Id = Add(dd01);
    EXPECT_NE(_manager->FormatBreakpointInfo(edffId).find("Magic"), std::string::npos);
    EXPECT_NE(_manager->FormatBreakpointInfo(edffId).find("ED FF"), std::string::npos);

    // The same opcode again with the breakpoint: told after it ran, with the time the table gives
    z80->pc = kProgram;
    const uint32_t before = z80->tt;
    z80->EngineStep();
    EXPECT_EQ(Last(), edffId);
    EXPECT_EQ(z80->pc, kProgram + kEdFfLength) << "the stop is after the instruction";
    EXPECT_EQ(z80->tt - before, edFfTime) << "an armed breakpoint costs no T-states";

    _manager->ClearLastTriggeredBreakpoint();
    z80->bc = 0;
    z80->EngineStep();  // the CPU library runs DD 01 nn nn as one instruction
    EXPECT_EQ(Last(), dd01Id);
    EXPECT_EQ(z80->pc, kProgram + kEdFfLength + kDd01Length);
    EXPECT_EQ(z80->bc, kLoadedBc) << "DD 01 is LD BC,nn on the hardware";

    // A hit policy and the gate follow the usual rules; off or removed, the opcode runs unnoticed
    ASSERT_TRUE(_manager->DeactivateBreakpoint(edffId));
    ASSERT_TRUE(_manager->RemoveBreakpointByID(dd01Id));
    EXPECT_FALSE(_manager->GetHotState()->hasMagic) << "no active magic breakpoint is left: one branch, no resolve";
    _manager->ClearLastTriggeredBreakpoint();
    z80->pc = kProgram;
    z80->EngineStep();
    EXPECT_EQ(Last(), BRK_INVALID);
}

TEST(NextRegBreakpointManager_Test, AMagicBreakpointTakesOnlyTheTwoOpcodeWords)
{
    EmulatorContext context(LoggerLevel::LogError);
    BreakpointManager manager(&context);
    std::string error;
    BreakpointSpec spec;
    spec.type = BRK_MAGIC;
    spec.access = BRK_IO_OUT;
    spec.address = 0x1234;
    EXPECT_EQ(manager.AddBreakpoint(spec, error), BRK_INVALID);
    EXPECT_NE(error.find("ED FF"), std::string::npos) << error;
    spec.address = BRK_MAGIC_DD_01;
    spec.hasEnd = true;
    spec.addressEnd = BRK_MAGIC_ED_FF;
    EXPECT_EQ(manager.AddBreakpoint(spec, error), BRK_INVALID) << "one opcode, no range";
    spec.hasEnd = false;
    const uint16_t id = manager.AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;
    EXPECT_EQ(manager.HandleMagic(BRK_MAGIC_ED_FF), BRK_INVALID);
    EXPECT_EQ(manager.HandleMagic(BRK_MAGIC_DD_01), id);
}

// The copper is a bus master of the machine (the design of automation-gaps-2026-10-11 section "copper"): the board runs it after every
// instruction, so what its MOVE writes is in place in machine time, with no picture involved and in order with the program's own writes
#include "emulator/memory/memory.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/video/next/screennext.h"

namespace
{
constexpr uint8_t kRegMmuSlot2 = NextBoard::kRegMmu0 + 2;  // NR #52: the 8K slot at #4000
constexpr uint8_t kMmuBankWritten = 0x2F;                  // any RAM bank
constexpr uint8_t kRegJoystickScratch = 0x16;              // NR #16 stores its byte
constexpr uint8_t kByteFromCopper = 0xC1, kByteFromCpu = 0x5E;
constexpr uint16_t kJrSelf = 0xFE18;                       // JR $ (18 FE): 12 T-states, never leaves
constexpr uint32_t kJrSelfStates = 12;
}  // namespace

TEST_F(NextRegBreakpointMachine_Test, TheCopperRunsWithTheInstructionsNotWithThePicture)
{
    ScreenNext* screen = dynamic_cast<ScreenNext*>(_context->pScreen);
    ASSERT_NE(screen, nullptr);
    Z80& z80 = *_context->pCore->GetZ80();
    constexpr unsigned kLine = kPaperLines / 3;
    CopperList({Wait(kLine, 0), Move(kRegMmuSlot2, kMmuBankWritten)}, 1);
    screen->BindCopper(Board().Copper());  // the machine's own clock and picture hook instead of the test's beam
    Board().Copper().SetGeometry(_raster.hcPerLine, _raster.lines);

    // The first T-state of the frame at which the beam is at the WAIT's place (the map is linear inside the frame)
    const uint64_t due = At(kLine, Raster::WaitThreshold(0));
    uint32_t dueT = 0;
    for (uint32_t t = 1; t < _raster.timing.frame && dueT == 0; t++)  // the copper's frame wraps inside the CPU's: take the upward crossing
        if (screen->CopperClock(t - 1) < due && screen->CopperClock(t) >= due)
            dueT = t;
    ASSERT_GT(dueT, 10 * kJrSelfStates);

    Memory* memory = _context->pMemory;
    memory->DirectWriteToZ80Memory(kProgram, kJrSelf & 0xFF);
    memory->DirectWriteToZ80Memory(kProgram + 1, kJrSelf >> 8);
    z80.pc = kProgram;
    z80.iff1 = z80.iff2 = 0;
    const uint8_t before = static_cast<NextMemory*>(memory)->GetMmu(2);
    ASSERT_NE(before, kMmuBankWritten);

    // Instruction by instruction through the real engine, from well before the beam gets there: nothing draws, nothing asks the screen
    z80.t = dueT - 10 * kJrSelfStates;
    unsigned steps = 0;
    while (z80.t < dueT && steps++ < 100)
    {
        EXPECT_EQ(static_cast<NextMemory*>(memory)->GetMmu(2), before) << "T " << z80.t << ": the WAIT is not true yet";
        z80.EngineStep();
    }
    // The instruction that ran past the due time has finished: the copper has done its MOVE, in this instruction's boundary
    EXPECT_EQ(static_cast<NextMemory*>(memory)->GetMmu(2), kMmuBankWritten) << "the MOVE reached the MMU register with the instruction that crossed T " << dueT;
    EXPECT_LE(z80.t, dueT + kJrSelfStates);
}

TEST_F(NextRegBreakpointMachine_Test, AMoveThatIsDueComesBeforeTheProgramsOwnWriteOfTheSameRegister)
{
    Board().Journal().SetEnabled(true);
    constexpr unsigned kLine = kPaperLines / 5;
    CopperList({Wait(kLine, 0), Move(kRegJoystickScratch, kByteFromCopper)}, 1);
    _beamClock = At(kLine, Raster::WaitThreshold(0) + 1);  // the beam is past the WAIT; no instruction boundary has come since
    // The program writes the register now: the board gives the copper its clock first (the bus gives the copper priority), the program's
    // value is the last
    Board().WriteNextReg(kRegJoystickScratch, kByteFromCpu);
    EXPECT_EQ(Board().Stored(kRegJoystickScratch), kByteFromCpu) << "the program's write came after the copper's";
    const std::vector<NextRegWriteEvent> events = Board().Journal().Query(NextRegJournalQuery{});
    ASSERT_GE(events.size(), 2u);
    EXPECT_EQ(events[events.size() - 2].source, NextRegSource::Copper);
    EXPECT_EQ(events[events.size() - 2].value, kByteFromCopper);
    EXPECT_EQ(events.back().source, NextRegSource::NextReg);
    EXPECT_EQ(events.back().value, kByteFromCpu);
}

// platform-debugging P4: a condition on a NextREG breakpoint sees the byte written (VAL) and the register before it
// (NR(x)); on the Next the machine answers the MMU slots, the registers, the beam, the copper and the speed
TEST_F(NextRegBreakpointMachine_Test, AConditionReadsTheBoardsRegistersSlotsBeamAndSpeed)
{
    ASSERT_NE(_transparencyAtStart, kByteA);
    BreakpointSpec change = NrSpec(kRegGlobalTransparency, BRK_IO_OUT);
    change.condition = "VAL != NR(#14)";
    const uint16_t id = Add(change);
    Board().WriteNextReg(kRegGlobalTransparency, _transparencyAtStart);
    EXPECT_EQ(Last(), BRK_INVALID) << "the byte it holds";
    Board().WriteNextReg(kRegGlobalTransparency, kByteA);
    EXPECT_EQ(Last(), id) << "a change";
    _manager->ClearLastTriggeredBreakpoint();
    Out(kPortRegSelect, kRegGlobalTransparency);
    Out(kPortRegData, kByteA);
    EXPECT_EQ(Last(), BRK_INVALID) << "the same byte again, through #253B";
    const BreakpointSnapshot bp = _manager->GetBreakpointById(id);
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(bp->matchCount, 3u);
    EXPECT_EQ(bp->hitCount, 1u);

    ASSERT_EQ(_context->pConditionMachine, static_cast<const IBreakpointConditionMachine*>(_ports));
    constexpr uint8_t kPage = 0x21;  // an 8K RAM page: the upper half of 16K page #10
    constexpr unsigned kLine = 5;
    constexpr unsigned kHc = 100;
    Board().WriteNextReg(NextBoard::kRegMmu0 + 7, kPage);
    Board().WriteNextReg(kRegCpuSpeed, 3);  // 28 MHz: eight times the base clock
    CopperList({Wait(kLine, 0)}, 1);
    _beamClock = At(kLine, kHc);
    BreakpointConditionContext x;
    x.cpu = _context->pCore->GetZ80();
    x.memory = _context->pMemory;
    x.state = &_context->emulatorState;
    x.machine = _context->pConditionMachine;
    auto holds = [&](const std::string& text) {
        std::string error;
        BreakpointConditionPtr program = BreakpointConditionProgram::Compile(text, BRK_COND_ANY, nullptr, error);
        EXPECT_TRUE(program) << text << ": " << error;
        std::string why;
        const BreakpointConditionResult result = program ? program->Evaluate(x, &why) : BreakpointConditionResult::Error;
        EXPECT_NE(result, BreakpointConditionResult::Error) << text << ": " << why;
        return result == BreakpointConditionResult::True;
    };
    EXPECT_TRUE(holds("MMU7 == 21 && NR(57) == 21 && MMU0 == 0FF && NR(50) == 0FF && MMU2 == 0A"))
        << "slot 7 shows the upper half of page #10";
    EXPECT_TRUE(holds("CPUSPEED == 8"));
    EXPECT_TRUE(holds("VC == 5 && HC == 100d"));
    EXPECT_TRUE(holds("CPC == " + std::to_string(Board().Copper().Pc()) + "d"));
}

// A machine without a NEXTREG file refuses NR(x) when the breakpoint is set, with the reason
TEST(NextRegBreakpointManager_Test, AConditionOnAMachineWithoutTheOperandIsRefusedWhenSet)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EXPECT_EQ(emulator->GetContext()->pConditionMachine, nullptr);
    BreakpointManager* manager = emulator->GetBreakpointManager();
    BreakpointSpec spec;
    spec.address = 0x8000;
    spec.condition = "NR(7) == 0";
    std::string error;
    EXPECT_EQ(manager->AddBreakpoint(spec, error), BRK_INVALID);
    EXPECT_NE(error.find("no NR here"), std::string::npos) << error;
    spec.condition = "MMU7 == 1 && CPUSPEED == 1";
    EXPECT_NE(manager->AddBreakpoint(spec, error), BRK_INVALID) << error;
    EmulatorTestHelper::CleanupEmulator(emulator);
}
