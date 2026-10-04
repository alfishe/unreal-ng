#pragma once

// ZX-MultiSound card logic scenarios (.msc), shared by core-tests (multisoundlogic_test.cpp) and the RTL
// co-simulation tool (tools/verification/multisound/). One line = one bus cycle; both sides turn every line into a
// MultiSoundCycleRecord with the same fields, so the RTL run and the MultiSoundLogic run compare line for line.
// Format reference: tools/verification/multisound/README.md.
//
// No gtest and no emulator dependencies: the tool compiles this file stand-alone with multisoundlogic.cpp.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "emulator/slots/cards/multisound/multisoundlogic.h"

struct MultiSoundCycle
{
    enum class Op : uint8_t
    {
        M1,             // host opcode fetch (sets the ROM lock)
        MemRead,        // host memory read
        MemWrite,       // host memory write
        Out,            // host I/O write
        In,             // host I/O read
        GsOut,          // GS Z80 I/O write
        GsIn,           // GS Z80 I/O read
        GsMemRead,      // GS Z80 memory read, value = what the RAM / ROM returns
        GsMemWrite,     // GS Z80 memory write
        Reset,          // bus /RESET
        Dip,            // DIP switch change (value = cfg[3:0])
        Par             // a host cycle and a GS cycle overlapping in time (DAC arbitration)
    };

    Op op = Op::Reset;
    uint16_t address = 0;
    uint8_t value = 0;

    // Op::Par only: the host part is hostOp / address / value; the GS part starts gsOffset ticks (1 tick = 1/64 us,
    // half a 32 MHz period) after it
    Op hostOp = Op::Out;
    Op gsOp = Op::GsMemRead;
    uint16_t gsAddress = 0;
    uint8_t gsValue = 0;
    uint32_t gsOffset = 0;

    int sourceLine = 0;
};

struct MultiSoundScenario
{
    MultiSoundOptions options;      // header directives: dip (initial), mask, ram
    double cpuMHz = 3.5;            // header directive cpu
    std::vector<MultiSoundCycle> cycles;
};

/// Everything one bus cycle shows, as both the RTL testbench and MultiSoundLogic report it.
struct MultiSoundCycleRecord
{
    // Events of this cycle
    uint8_t iorqge = 2;         // 0 / 1 for a host I/O cycle, 2 = not a host I/O cycle
    uint8_t ymWrite = 0;        // bit 0 = YM chip 0 (ym1, U4) latched a write, bit 1 = chip 1 (ym2, U10)
    uint8_t ymA0 = 0;
    uint8_t ymValue = 0;
    uint8_t saaWrite = 0;
    uint8_t saaA0 = 0;
    uint8_t saaValue = 0;
    uint8_t sdWrite = 0;        // bit per SounDrive channel written
    uint8_t readDriven = 0;     // host IN: the card drove the data bus; GS IN: the CPLD drove gd
    uint8_t readValue = 0;
    uint8_t gsMap = 0xFF;       // GS memory cycle: chip << 4 | gma (chip: 0 ROM, 1-4 RAM1-4, 7 none); #FF otherwise

    // State when the cycle ends
    uint8_t ymChip = 0;
    uint8_t ymReadStatus = 0;
    uint8_t fmMuted = 0;
    uint8_t saaClock = 0;
    uint8_t romLock = 0;
    uint8_t gsData = 0;
    uint8_t gsCommand = 0;
    uint8_t gsPage = 0;
    uint8_t gsOutput = 0;
    uint8_t dataFlag = 0;
    uint8_t commandFlag = 0;
    std::array<uint8_t, 4> dacSample{};
    std::array<uint8_t, 4> dacVolume{};

    static constexpr size_t ByteCount = 30;
    std::array<uint8_t, ByteCount> Bytes() const;
};

/// The YM2203 stand-in of the testbench drives #C0 | chip << 4 | A0 when it is read; the logic side maps its read
/// source to the same value.
constexpr uint8_t MultiSoundFakeYmValue(uint8_t chip, uint8_t a0) { return static_cast<uint8_t>(0xC0 | (chip << 4) | a0); }

namespace MultiSoundTiming
{
    /// Ticks (half periods of the 32 MHz clock) from the start of a host cycle to its half T-state 'halfT'.
    uint32_t HostTick(int halfT, double cpuMHz);
    /// Same for the GS Z80 (16 MHz: one half T-state = two ticks).
    constexpr uint32_t GsTick(int halfT) { return static_cast<uint32_t>(halfT) * 2u; }

    int HalfStates(MultiSoundCycle::Op op);      // cycle length in half T-states
    uint32_t StrobeEnd(MultiSoundCycle::Op op);  // half T-state at which the cycle's RD / WR strobe rises
}

/// Parses a .msc text. Returns false and fills 'error' (with the line number) on a syntax error.
bool ParseMultiSoundScenario(const std::string& text, MultiSoundScenario& scenario, std::string& error);

std::string FormatMultiSoundCycle(const MultiSoundCycle& cycle);
std::string FormatMultiSoundRecord(const MultiSoundCycleRecord& record);
std::string FormatMultiSoundDip(const MultiSoundOptions& options);
uint8_t MultiSoundDipBits(const MultiSoundOptions& options);
void ApplyMultiSoundDipBits(MultiSoundOptions& options, uint8_t bits);

/// FNV-1a 64 over the record bytes, chained.
uint64_t HashMultiSoundRecord(uint64_t hash, const MultiSoundCycleRecord& record);
constexpr uint64_t MultiSoundHashSeed = 0xCBF29CE484222325ull;

/// Event summary of a record (no values): bit 0 IORQGE, 1-2 YM chip writes, 3 YM A0, 4 SAA write, 5 SAA A0,
/// 6-9 SounDrive channels, 10 read driven.
uint16_t MultiSoundEventCode(const MultiSoundCycleRecord& record);

/// Plays cycles into MultiSoundLogic.
class MultiSoundLogicBus
{
public:
    explicit MultiSoundLogicBus(const MultiSoundScenario& scenario);

    MultiSoundCycleRecord Execute(const MultiSoundCycle& cycle);
    MultiSoundLogic& Logic() { return _logic; }

private:
    void ExecuteOne(MultiSoundCycle::Op op, uint16_t address, uint8_t value, MultiSoundCycleRecord& record);
    void CaptureState(MultiSoundCycleRecord& record) const;

    MultiSoundLogic _logic;
    MultiSoundScenario _header;
};

/// The decode sweep for one DIP setting: a reset, the DIP setting, then for ROM lock off / on (M1 at #8000 / #0000) every port
/// 0-#FFFF written (value = low byte ^ #5A, which includes control bytes at #xxAD) and then read.
/// 4 + 4 * 65536 cycles.
template <typename Fn>
void ForEachMultiSoundSweepCycle(uint8_t dipBits, Fn&& fn)
{
    MultiSoundCycle cycle;
    cycle.op = MultiSoundCycle::Op::Reset;      // first: the run starts from the reset state whatever ran before
    fn(cycle);
    cycle.op = MultiSoundCycle::Op::Dip;
    cycle.value = dipBits;
    fn(cycle);
    for (int lock = 0; lock < 2; lock++)
    {
        cycle.op = MultiSoundCycle::Op::M1;
        cycle.address = lock ? 0x0000 : 0x8000;
        fn(cycle);
        for (int dir = 0; dir < 2; dir++)
        {
            cycle.op = dir == 0 ? MultiSoundCycle::Op::Out : MultiSoundCycle::Op::In;
            for (uint32_t port = 0; port < 0x10000; port++)
            {
                cycle.address = static_cast<uint16_t>(port);
                cycle.value = static_cast<uint8_t>((port & 0xFF) ^ 0x5A);
                fn(cycle);
            }
        }
    }
}

/// Witness ports of the sweep: A12-A9 = 0 (the RTL decodes none of them; the full-range hash proves the rest).
constexpr bool IsMultiSoundWitnessPort(uint16_t port) { return (port & 0x1E00) == 0; }

/// The GS memory map sweep: page register 0-127, ten addresses per page.
template <typename Fn>
void ForEachMultiSoundGsMapCycle(Fn&& fn)
{
    static constexpr uint16_t addresses[] = { 0x0000, 0x3FFF, 0x4000, 0x5FFF, 0x6000, 0x7FFF, 0x8000, 0xBFFF, 0xC000, 0xFFFF };
    MultiSoundCycle cycle;
    cycle.op = MultiSoundCycle::Op::Reset;
    fn(cycle);
    for (int page = 0; page < 128; page++)
    {
        cycle.op = MultiSoundCycle::Op::GsOut;
        cycle.address = 0x0000;
        cycle.value = static_cast<uint8_t>(page);
        fn(cycle);
        cycle.op = MultiSoundCycle::Op::GsMemRead;
        for (uint16_t address : addresses)
        {
            cycle.address = address;
            cycle.value = static_cast<uint8_t>(page ^ address);
            fn(cycle);
        }
    }
}
