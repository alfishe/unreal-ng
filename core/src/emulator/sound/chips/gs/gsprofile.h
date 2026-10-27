#pragma once

/// @file gsprofile.h
/// @brief Board profile of the LLE General Sound card (SoundChip_GeneralSound).
///
/// One card implementation serves every board that carries the General Sound
/// design; what differs between boards is configuration, not code (owner rule:
/// no forks of chip modules). The classic card is the default profile and keeps
/// today's numbers exactly. The ZX-MultiSound (UzixLS) GS is the second profile
/// (docs/inprogress/2026-10-03-zx-multisound/architecture.md §4.2, hardware
/// reference §4.2, RTL findings F5 / F6 / F8 / F10 in tdd-card-logic.md §7).
///
/// Time: the card counts in its own unit, the least common multiple of the CPU
/// clock and the INT divider clock, so both are whole numbers of units. Classic:
/// one unit = one 12 MHz cycle (the CPU and the divider share the crystal), so
/// the TTD blob, the port trace and the golden digests keep their values.
/// MultiSound: 48 MHz units, a CPU cycle (16 MHz) is 3 units and an INT period
/// (321 clocks of 12 MHz) is 1284 units.

#include <cstddef>
#include <cstdint>
#include <numeric>

#include "emulator/sound/chips/gs/gsdacsink.h"
#include "emulator/sound/chips/gs/gshostclock.h" // GSClassicTiming

/// Host time supplied by the board instead of the emulator's CPU clock
/// (GSProfile::hostClock). A board that runs on its own time axis (the
/// ZX-MultiSound card, driven through explicit port and frame calls) gives
/// the GS the axis it is driven on; without it the card reads the machine's
/// Z80 (GSHostClock). All three values are on the board's axis, frame-relative
/// like the emulator's AudioTstate: the board rebases it at every frame start.
class IGSHostClock
{
public:
    virtual ~IGSHostClock() = default;

    /// Host time now (ticks since the board's current frame start)
    virtual uint64_t GsHostTacts() const = 0;
    /// Host ticks per second
    virtual uint32_t GsHostTickRate() const = 0;
    /// Length of the current host frame in ticks
    virtual uint64_t GsHostFrameTacts() const = 0;
};

/// How the card CPU's address space reaches ROM and RAM
enum class GSMemoryMap : uint8_t
{
    /// Classic GS: page 0 = ROM pair, page N = RAM pair N-1 (masked to the
    /// installed RAM); #4000-#7FFF = the upper half of RAM chip 1
    Classic,
    /// MultiSound CPLD bus controller (MultiSoundLogic::GsMemoryMapFor): two
    /// 512 KB chips, page bits 0-4, page bit 5 ignored
    MultiSound1Mb,
    /// MultiSound 2 MB firmware (GS_RAM_2MB): four 512 KB chips, page bits 0-5
    MultiSound2Mb
};

/// GS-side port decode and the flag rules
enum class GSPortRules : uint8_t
{
    /// Classic card as the emulator has always modeled it (Unreal gsz80.cpp /
    /// ZXMAK2): ports decoded on the low byte, a port 3 read sets the reply
    /// to #FF, port 4 returns the raw flags, port #0B copies volume 0 bit 5
    Classic,
    /// MultiSound CPLD (top.v, MultiSoundLogic::GsPortRead / GsPortWrite):
    /// A3-A0 only (ports mirror every 16), a port 3 read sets the data flag
    /// and leaves the reply alone, port 4 = {data flag, 111111, command flag},
    /// port #0B copies volume 3 bit 5 (the register the SounDrive shares)
    MultiSound
};

struct GSProfile
{
    /// Short name for automation and logs ("classic", "multisound")
    const char* name = "classic";

    /// Card CPU clock
    uint32_t cpuClockHz = GSClassicTiming::CLOCK_HZ;
    /// Clock the INT divider counts
    uint32_t intClockHz = GSClassicTiming::CLOCK_HZ;
    /// INT period in divider clocks
    uint32_t intPeriodClocks = GSClassicTiming::CYCLES_PER_INT;
    /// INT low time in divider clocks; 0 = the request is held until the CPU
    /// accepts it (the classic model, gs-tdd §2.4). A pulse that ends before
    /// the CPU accepts it is lost (counted in interruptsCoalesced)
    uint32_t intLowClocks = 0;

    /// RAM range; the constructor's size is clamped into it
    size_t minRamKB = 128;
    size_t maxRamKB = 512;

    /// Host port #33 (reset / NMI) is fitted
    bool controlPort = true;

    GSMemoryMap memoryMap = GSMemoryMap::Classic;
    GSPortRules portRules = GSPortRules::Classic;

    /// A GS-side read of a port nothing drives
    uint8_t undecodedPortRead = 0xFF;

    /// Firmware the board ships with (relative ROM path as [ROM] entries use);
    /// empty = the configured [ROM] GS
    const char* romPath = "";

    /// Optional DAC sink (not owned); nullptr = the card's own stereo mix
    IGSDacSink* dacSink = nullptr;

    /// Optional host time source (not owned); nullptr = the machine's Z80
    /// clock and frame geometry (GSHostClock)
    const IGSHostClock* hostClock = nullptr;

    /// Time-travel identity: the blob id (ttd::PeripheralId), the RAM region
    /// id (ttd::TTDRegionId), the device and region names. A board GS next to
    /// a GS card needs its own (the engine binds devices and regions by id)
    uint8_t ttdPeripheralId = 5;            // PeripheralId::GeneralSound
    uint16_t ttdRegionId = 1;               // TTDRegionId::GeneralSoundRam
    const char* ttdDeviceName = "GeneralSound";
    const char* ttdRegionName = "gs.ram";

    /// region <Derived timing (card units)>
    uint64_t UnitsPerSecond() const { return std::lcm(static_cast<uint64_t>(cpuClockHz), static_cast<uint64_t>(intClockHz)); }
    int64_t UnitsPerCpuCycle() const { return static_cast<int64_t>(UnitsPerSecond() / cpuClockHz); }
    int64_t UnitsPerIntClock() const { return static_cast<int64_t>(UnitsPerSecond() / intClockHz); }
    int64_t IntPeriodUnits() const { return static_cast<int64_t>(intPeriodClocks) * UnitsPerIntClock(); }
    int64_t IntLowUnits() const { return static_cast<int64_t>(intLowClocks) * UnitsPerIntClock(); }
    /// endregion </Derived timing>

    /// The classic General Sound card (12 MHz, INT every 320 cycles held until
    /// accepted, 128-512 KB, #B3 / #BB / #33, own stereo mix)
    static GSProfile Classic() { return GSProfile{}; }

    /// The ZX-MultiSound GS: 16 MHz CPU, INT from the 12 MHz DDS / 321 low for
    /// 33 clocks (2.75 us), 1 MB (1024) or 2 MB (2048), #B3 / #BB only, GS
    /// 1.05b firmware, DACs shared through `sink`, host time from `clock`
    /// (the card's own axis; nullptr = the machine's Z80)
    static GSProfile MultiSound(size_t ramKB, IGSDacSink* sink = nullptr, const IGSHostClock* clock = nullptr)
    {
        GSProfile p;
        p.name = "multisound";
        p.cpuClockHz = kMultiSoundCpuClockHz;
        p.intClockHz = kMultiSoundIntClockHz;
        p.intPeriodClocks = kMultiSoundIntPeriodClocks;
        p.intLowClocks = kMultiSoundIntLowClocks;
        const bool twoMb = ramKB >= 2048;
        p.minRamKB = p.maxRamKB = twoMb ? 2048 : 1024;
        p.controlPort = false;
        p.memoryMap = twoMb ? GSMemoryMap::MultiSound2Mb : GSMemoryMap::MultiSound1Mb;
        p.portRules = GSPortRules::MultiSound;
        p.undecodedPortRead = 0xFF;
        p.romPath = kMultiSoundRomPath;
        p.dacSink = sink;
        p.hostClock = clock;
        p.ttdPeripheralId = 60;             // PeripheralId::MultiSoundGs
        p.ttdRegionId = 17;                 // TTDRegionId::MultiSoundGsRam
        p.ttdDeviceName = "MultiSoundGs";
        p.ttdRegionName = "multisound.gs.ram";
        return p;
    }

    /// MultiSound board facts (hardware-reference.md §4.2, RTL top.v)
    static constexpr uint32_t kMultiSoundCpuClockHz = 16000000;   // gclk = clk16
    static constexpr uint32_t kMultiSoundIntClockHz = 12000000;   // g_int_cnt counts clk12
    static constexpr uint32_t kMultiSoundIntPeriodClocks = 321;   // 0..320, reload at 320
    static constexpr uint32_t kMultiSoundIntLowClocks = 33;       // gint_n low until g_int_cnt[5]
    /// GS 1.05b (data/rom/README-ROMS.md): the card repository's gs105b.32K.rom
    static constexpr const char* kMultiSoundRomPath = "rom/gs105b.rom";
};
