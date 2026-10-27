#pragma once

// ZX-MultiSound bus trace (docs/inprogress/2026-10-03-zx-multisound/tdd-card-logic.md §8): every bus cycle the card's
// CPLD sees, from both buses, in the order the emulator produced them. A receiver set with
// MultiSoundCard::SetBusTrace gets one event per cycle; with none set (the default) the card pays one pointer test per
// host port cycle and the GS one per GS port cycle and DAC fetch.
//
// Order. Events arrive in causal order, not sorted by time: the card runs its GS up to a host cycle's time before
// that cycle touches state the GS shares (the mailbox, the shared DACs), so every GS cycle that came before such a host
// cycle arrives before it. Host cycles on the YM2203 / SAA ports do not wait for the GS: their order against GS cycles
// is the emulator's, harmless because they share no state.
//
// The text form of a trace is the card logic scenario format (.msc, tools/verification/multisound/README.md), written
// by the test helper MultiSoundTraceWriter (core/tests/_helpers/multisoundscenario.h).

#include <cstdint>

struct MultiSoundBusEvent
{
    enum class Kind : uint8_t
    {
        HostOut,        ///< host I/O write on a port the card was given
        HostIn,         ///< host I/O read; `drives` = the card drove the data bus, `value` its byte (#FF otherwise)
        BusReset,       ///< bus /RESET
        GsOut,          ///< GS CPU OUT (port = A7-A0)
        GsIn,           ///< GS CPU IN, `value` = the byte the GS CPU got
        GsDacFetch,     ///< GS CPU memory read at #6000-#7FFF, `value` = the byte read
        FrameStart,     ///< a host frame starts at `time` (MultiSoundCard::FrameStart)
        FrameEnd,       ///< the host frame ends (MultiSoundCard::FrameEnd, before the card runs its modules to `time`).
                        ///< Both mark where the frame calls fell between the cycles: a cycle's time does not always
                        ///< tell (the CPU ends a frame up to one instruction past its end; a machine reset comes
                        ///< between two frames)
    };

    Kind kind = Kind::HostOut;
    bool drives = false;
    uint8_t value = 0;
    uint16_t address = 0;   ///< host port, GS port or GS memory address
    uint16_t m1 = 0;        ///< host cycles: the last M1 address the card saw (the IN / OUT instruction's fetch)
    uint64_t time = 0;      ///< the card axis (MultiSoundCard: host ticks); GS cycles: their instruction's start
};

class IMultiSoundBusTrace
{
public:
    virtual ~IMultiSoundBusTrace() = default;
    virtual void OnMultiSoundBus(const MultiSoundBusEvent& event) = 0;
};
