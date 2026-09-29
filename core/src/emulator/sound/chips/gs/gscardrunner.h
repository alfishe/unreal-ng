#pragma once

/// @file gscardrunner.h
/// @brief Catch-up loop shared by the General Sound slot cards that run a real
/// Z80 (classic GS, NeoGS) - neogs-tdd.md §5.3.
///
/// The runner steps the card CPU from its current time to a target time. Time
/// is counted in the card's own unit (classic GS: 12 MHz cycles; NeoGS: 120 MHz
/// base ticks, §5.2), so each card keeps its numbers - and the classic card its
/// TTD blobs and traces - unchanged.
///
/// It is a template over the card type, not a virtual interface: every call
/// from the loop to the card is direct and inlinable, so the classic card pays
/// nothing for the extraction. The card keeps binding the CPU's memory and port
/// bus to its own trampolines; the runner never sits between the CPU and memory.
///
/// Each round, in this order:
///  1. a DMA stall (NeoGS): time advances without instructions, no interrupt or
///     NMI is accepted, due events still fire;
///  2. a pending NMI is delivered; if accepted the round ends;
///  3. the INT line is sampled; an accepted interrupt adds its cycles but does
///     not end the round;
///  4. a due event (interrupt period, DAC frame, SPI byte, ...) fires and the
///     round ends;
///  5. otherwise one instruction runs.
/// Steps 2-5 are the classic card's historical order (Unreal z80loop,
/// gsz80.inl:39-82), which its golden tests pin.
///
/// The next event time lives in the runner (setNextEvent): the card updates it
/// whenever its schedule changes - in runEvents, and from a port or memory
/// handler that starts a timed operation. Keeping it next to `now` makes the
/// per-instruction check one compare of two adjacent members.
///
/// The card provides (all non-virtual):
///   bool     nmiPending() const;       void onNmiAccepted();
///   bool     intLine() const;          void onIntAccepted();
///   void     runEvents(int64_t now);   // must call setNextEvent() before returning
///   int64_t  unitsPerCycle() const;    void onStep();
///   static constexpr bool kCanStall;   // false compiles the stall check out

#include <algorithm>
#include <cstdint>
#include <limits>

#include "3rdparty/unreal-z80/z80cpu.h"

template <class Card>
class GSCardRunner
{
public:
    void bind(Z80CPU* cpu, Card* card)
    {
        _cpu = cpu;
        _card = card;
    }

    /// Card time now, in the card's unit
    int64_t now() const { return _now; }
    void setNow(int64_t now) { _now = now; }

    /// DMA: the CPU loses `units` of time from now (or from the end of an
    /// already running stall), without executing instructions
    void stall(int64_t units) { _stallUntil = std::max(_stallUntil, _now) + units; }
    int64_t stallUntil() const { return _stallUntil; }
    void setStallUntil(int64_t until) { _stallUntil = until; }
    bool stalled() const { return _stallUntil > _now; }

    /// Earliest time at which runEvents must run (std::numeric_limits<int64_t>::max(): nothing scheduled)
    void setNextEvent(int64_t when) { _nextEvent = when; }
    int64_t nextEvent() const { return _nextEvent; }

    void reset()
    {
        _now = 0;
        _stallUntil = 0;
        _nextEvent = std::numeric_limits<int64_t>::max();
    }

    void runTo(int64_t target)
    {
        // Locals, not members, across the opaque CPU calls: the compiler must
        // otherwise reload them after every call
        Card* const card = _card;
        Z80CPU* const cpu = _cpu;
        while (_now < target)
        {
            if constexpr (Card::kCanStall)
            {
                if (_stallUntil > _now)
                {
                    if (_now >= _nextEvent)
                    {
                        card->runEvents(_now);
                        continue;
                    }
                    _now = std::min({_stallUntil, target, _nextEvent});
                    continue;
                }
            }

            if (card->nmiPending())
            {
                const int t = Z80CpuNmi(cpu);
                if (t > 0)
                {
                    _now += static_cast<int64_t>(t) * card->unitsPerCycle();
                    card->onNmiAccepted();
                    continue;
                }
            }

            if (card->intLine())
            {
                const int t = Z80CpuInt(cpu);
                if (t > 0)
                {
                    _now += static_cast<int64_t>(t) * card->unitsPerCycle();
                    card->onIntAccepted();
                }
            }

            if (_now >= _nextEvent)
            {
                card->runEvents(_now);
                continue;
            }

            const int t = Z80CpuStep(cpu);
            if (t <= 0)
                break; // defensive: a stuck core must not hang the emulator
            _now += static_cast<int64_t>(t) * card->unitsPerCycle();
            card->onStep();
        }
    }

private:
    Z80CPU* _cpu = nullptr;
    Card* _card = nullptr;
    int64_t _now = 0;
    int64_t _nextEvent = std::numeric_limits<int64_t>::max();
    int64_t _stallUntil = 0;
};
