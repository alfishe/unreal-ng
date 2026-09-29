#pragma once

/// @file gshostclock.h
/// @brief Host (ZX) time -> card time conversion shared by the General Sound
/// slot cards (neogs-tdd.md §5.2).
///
/// A card counts time in its own unit (`unitsPerSecond`: 12e6 for the classic
/// GS cycle, 120e6 for the NeoGS base tick). The conversion is the classic
/// card's historical one, written once: the ZX base clock comes from the
/// configured frame geometry, only the HOST speed multiplier stretches the ZX
/// tact domain (hardware turbo is already descaled by AudioTstate), and one ZX
/// frame is therefore a turbo-invariant number of card units.

#include <cstdint>

class EmulatorContext;

/// Classic General Sound board facts (12 MHz crystal, 37.5 kHz interrupt
/// divider). The classic card and the lightweight player share them; NeoGS
/// has its own clocks (neogs-tdd.md §3.3, §3.5).
struct GSClassicTiming
{
    static constexpr uint32_t CLOCK_HZ = 12000000;
    static constexpr uint32_t INT_FREQUENCY_HZ = 37500;
    static constexpr int CYCLES_PER_INT = static_cast<int>(CLOCK_HZ / INT_FREQUENCY_HZ); // 320
};

struct GSHostClock
{
    /// Card units per ZX tact at the current configuration
    static double unitsPerZxTact(const EmulatorContext* context, double unitsPerSecond);

    /// Current ZX time (AudioTstate domain: hardware turbo descaled), or
    /// `fallback` when no host CPU is attached (unit tests, a card on its own)
    static uint64_t currentZxTacts(const EmulatorContext* context, uint64_t fallback);

    /// One ZX frame in card units
    static int64_t frameUnits(const EmulatorContext* context, double unitsPerSecond);

    /// Card target time for the host's current tact, relative to the frame
    /// bases taken at handleFrameStart. Returns false when the ZX clock was
    /// rewound (a ZX reset) - the caller waits for the next frame base.
    static bool targetUnits(const EmulatorContext* context, double unitsPerSecond, uint64_t frameStartZxTacts,
                            int64_t frameStartUnits, int64_t& target);
};
