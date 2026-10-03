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

    /// Card time at which the host frame that starts now begins, in card units.
    ///
    /// A card runs its CPU in whole instructions, so a frame ends a little PAST
    /// its nominal end (the overshoot: 0 up to one instruction, or one
    /// 320-cycle quantum for the lightweight player). The next frame must not
    /// take the card's actual time as its base - that would add the overshoot
    /// to every frame and let the card run ahead of the machine without bound.
    /// The base follows the NOMINAL timeline instead, so the overshoot is paid
    /// back by the next frame's catch-up and the card's time stays within one
    /// instruction of `frames * frameUnits`.
    ///
    /// `previousBase` / `previousFrameUnits`: the previous frame's base and
    /// length. `cardNow`: the card's actual time. `zxElapsed`: host tacts since
    /// the previous frame's anchor (negative: the ZX clock was rewound).
    /// `unitsPerZxTact`: the host -> card conversion.
    ///
    /// - The card ran the previous frame to its end (it stands at or just past
    ///   the nominal end, by less than a frame): the base is the nominal end.
    /// - The card did NOT reach the nominal end: that frame was abandoned or is
    ///   being started a second time (a pause and resume, a state restored in
    ///   the middle of a frame and resumed). The card then keeps the lead it has
    ///   over the host: the base is the nominal card time of the host's current
    ///   tact, `previousBase + zxElapsed * unitsPerZxTact`. A repeated start in
    ///   the same tact leaves the base where it was.
    /// - Anything else means the timeline was broken (first frame, reset, the
    ///   ZX clock rewound, a state from another timeline): the base restarts
    ///   from the card's actual time.
    static int64_t nextFrameBase(int64_t previousBase, int64_t previousFrameUnits, int64_t cardNow,
                                 int64_t zxElapsed, double unitsPerZxTact);

    /// Host tacts since `anchorZxTacts` (the previous frame's ZX base), or -1
    /// when the ZX clock was rewound (a ZX reset)
    static int64_t zxElapsedSince(const EmulatorContext* context, uint64_t anchorZxTacts);

    /// Card target time for the host's current tact, relative to the frame
    /// bases taken at handleFrameStart. Returns false when the ZX clock was
    /// rewound (a ZX reset) - the caller waits for the next frame base.
    static bool targetUnits(const EmulatorContext* context, double unitsPerSecond, uint64_t frameStartZxTacts,
                            int64_t frameStartUnits, int64_t& target);
};
