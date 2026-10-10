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
///
/// A card may also let the runner sleep through its firmware's idle poll loop (sleepAllowed() and the rest below;
/// without them the code is compiled out). The loop is found by the CPU coming back to the head of a short
/// backward jump with every register as before except R and no bus side effect since (busEffects() did not move):
/// from then on every iteration is that one, until something outside the CPU changes. Sleeping, the runner adds whole iterations to the time, R and the card's step count, and runs
/// the card's quiet events (sleepHardStop: the ones that cannot change what the loop reads) at the instruction
/// boundary the loop would have reached them on - known from the iteration's instruction offsets. The loop is only
/// recognized inside one runTo (the host changes the card between runs: a mailbox write, a reset, a restore). It
/// wakes at the last iteration start before the target or a hard event and steps the rest for real, so the state
/// at the target (a host access, the frame end) is exactly the polling CPU's:
///   uint32_t busEffects() const;       // bumped by every bus access with a side effect or a value that may change
///   bool     sleepAllowed() const;     // no NMI, no INT the CPU could take, no DMA, reset, clock change, trace
///   int64_t  sleepHardStop() const;    // the earliest event that is not quiet
///   void     onStepsSkipped(uint64_t); // the steps the sleep stood for (counters)

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include "3rdparty/unreal-z80/z80cpu.h"

template <class Card>
class GSCardRunner
{
public:
    void bind(Z80CPU* cpu, Card* card)
    {
        _cpu = cpu;
        _card = card;
        _regs = cpu ? Z80CpuRegisterFilePtr(cpu) : nullptr;
        _idle.valid = false;
    }

    /// Card time now, in the card's unit
    int64_t now() const { return _now; }
    void setNow(int64_t now)
    {
        _now = now;
        _idle.valid = false;  // a restore: the loop reference is from another time
    }

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
        _idle.valid = false;
        _now = 0;
        _stallUntil = 0;
        _nextEvent = std::numeric_limits<int64_t>::max();
    }

    /// `Policy` answers the two per-instruction questions: the time an
    /// instruction took in card units (`static int64_t units(const Card*, int t)`)
    /// and the INT line (`static bool intLine(const Card*)`). `void` (default)
    /// asks the card (`unitsPerCycle()`, `intLine()`); a card whose answers
    /// are fixed for a whole run (the classic GS profile: 1 unit per cycle,
    /// INT held until accepted) passes its own policy so they compile to the
    /// pre-profile code
    template <class Policy = void>
    void runTo(int64_t target)
    {
        // Locals, not members, across the opaque CPU calls: the compiler must
        // otherwise reload them after every call
        Card* const card = _card;
        Z80CPU* const cpu = _cpu;
        if constexpr (kCanSleep)
        {
            _regs = Z80CpuRegisterFilePtr(cpu);  // the core's live register file (a host may attach its own)
            // Between two runs the host may have changed what the loop reads (a mailbox write, a reset, a restore,
            // a debugger's edit): the loop is found again inside this run, where only the card's CPU and events act
            _idle.valid = false;
        }
        auto units = [card](int t) -> int64_t {
            if constexpr (std::is_void_v<Policy>)
                return static_cast<int64_t>(t) * card->unitsPerCycle();
            else
                return Policy::units(card, t);
        };
        auto intLine = [card]() -> bool {
            if constexpr (std::is_void_v<Policy>)
                return card->intLine();
            else
                return Policy::intLine(card);
        };
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
                    _now += units(t);
                    card->onNmiAccepted();
                    continue;
                }
            }

            if (intLine())
            {
                const int t = Z80CpuInt(cpu);
                if (t > 0)
                {
                    _now += units(t);
                    card->onIntAccepted();
                }
            }

            if (_now >= _nextEvent)
            {
                card->runEvents(_now);
                continue;
            }

            if constexpr (kCanSleep)
            {
                const uint16_t pcBefore = _regs->pc;
                const int64_t stepStart = _now;
                const int t = Z80CpuStep(cpu);
                if (t <= 0)
                    break;  // defensive: a stuck core must not hang the emulator
                _now += units(t);
                card->onStep();
                if (_idle.steps < kMaxLoopSteps)
                    _idle.offsets[_idle.steps] = stepStart - _idle.at;
                _idle.steps++;
                // A short backward jump: the head of a loop candidate
                if (_regs->pc < pcBefore && pcBefore - _regs->pc <= kMaxLoopBytes) [[unlikely]]
                    onLoopHead(target);
                continue;
            }
            const int t = Z80CpuStep(cpu);
            if (t <= 0)
                break; // defensive: a stuck core must not hang the emulator
            _now += units(t);
            card->onStep();
        }
    }

    /// The firmware's idle loop is slept through (on by default; off steps it: comparison tests, diagnosis)
    void setSleepOn(bool on)
    {
        _sleepOn = on;
        _idle.valid = false;
    }
    /// Steps the sleeps stood for since bind (tests, diagnosis)
    uint64_t stepsSlept() const { return _stepsSlept; }

private:
    static constexpr bool kCanSleep = requires(Card& c) { c.sleepAllowed(); };
    static constexpr uint32_t kMaxLoopSteps = 32;
    static constexpr uint16_t kMaxLoopBytes = 64;

    /// The loop iteration the CPU last ran from its head: the state at the head, and each instruction's start
    struct LoopIteration
    {
        bool valid = false;
        uint16_t head = 0;
        Z80CpuRegisterFile regs{};   // rLow 0: R is compared apart
        uint32_t effects = 0;
        int64_t at = 0;              // the head's time
        uint32_t tstates = 0;        // the core's T at the head
        uint8_t rLow = 0;
        uint32_t steps = 0;          // instructions since the head
        int64_t offsets[kMaxLoopSteps] = {};
    };

    static Z80CpuRegisterFile WithoutR(const Z80CpuRegisterFile& r)
    {
        Z80CpuRegisterFile c = r;
        c.rLow = 0;
        return c;
    }

    /// The CPU stands at the head of a short backward jump: if the iteration from the last arrival here was the
    /// fixed point, sleep up to `target`; then take this arrival as the new reference
    void onLoopHead(int64_t target)
    {
        Card* const card = _card;
        const Z80CpuRegisterFile now = WithoutR(*_regs);
        const uint32_t effects = card->busEffects();
        LoopIteration& it = _idle;
        if (_sleepOn && it.valid && it.head == _regs->pc && it.effects == effects && it.steps <= kMaxLoopSteps &&
            std::memcmp(&it.regs, &now, sizeof(now)) == 0 && card->sleepAllowed() && !stalled())
        {
            const int64_t length = _now - it.at;
            if (length > 0)
                sleep(target, length, Z80CpuTstates(_cpu) - it.tstates, static_cast<uint8_t>((_regs->rLow - it.rLow) & 0x7F),
                      it.steps);
        }
        it.valid = true;
        it.head = _regs->pc;
        it.regs = WithoutR(*_regs);
        it.effects = card->busEffects();
        it.at = _now;
        it.tstates = Z80CpuTstates(_cpu);
        it.rLow = _regs->rLow;
        it.steps = 0;
    }

    /// Whole iterations from the head (now) up to the last one that ends by the target or the next hard event;
    /// the quiet events inside run at the boundary the loop reaches them on
    void sleep(int64_t target, int64_t length, uint32_t cycles, uint8_t rTicks, uint32_t steps)
    {
        Card* const card = _card;
        const int64_t start = _now;
        const int64_t stop = std::min(target, card->sleepHardStop());
        if (stop - start < length)
            return;
        const int64_t iterations = (stop - start) / length;
        const int64_t end = start + iterations * length;
        const int64_t* offsets = _idle.offsets;
        while (_nextEvent < end)
        {
            // The first instruction boundary at or after the event (a head counts: offset 0)
            int64_t boundary = start;
            if (_nextEvent > start)
            {
                const int64_t index = (_nextEvent - start) / length;
                const int64_t base = start + index * length;
                boundary = base + length;
                for (uint32_t i = 0; i < steps; i++)
                {
                    if (base + offsets[i] >= _nextEvent)
                    {
                        boundary = base + offsets[i];
                        break;
                    }
                }
            }
            _now = boundary;
            card->runEvents(boundary);  // calls setNextEvent
        }
        _now = end;
        _regs->rLow = static_cast<uint8_t>((_regs->rLow & 0x80) | ((_regs->rLow + iterations * rTicks) & 0x7F));
        Z80CpuSetTstates(_cpu, static_cast<uint32_t>(Z80CpuTstates(_cpu) + iterations * cycles));
        const uint64_t slept = static_cast<uint64_t>(iterations) * steps;
        card->onStepsSkipped(slept);
        _stepsSlept += slept;
    }

    Z80CPU* _cpu = nullptr;
    Card* _card = nullptr;
    Z80CpuRegisterFile* _regs = nullptr;
    LoopIteration _idle;
    bool _sleepOn = true;
    uint64_t _stepsSlept = 0;
    int64_t _now = 0;
    int64_t _nextEvent = std::numeric_limits<int64_t>::max();
    int64_t _stallUntil = 0;
};
