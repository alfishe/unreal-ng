#pragma once

/// @file exechistory.h
/// @brief The execution history of a processor on the device history pattern (devicehistory.h): one record per
/// instruction it starts, with the page its address showed. One history per processor: `cpu` the machine's main CPU
/// (PcHistory feeds it), `<slot>.cpu` a sound card's Z80. Fed at instruction rate, so through PushFast (no lock);
/// off, the processor's gate (a step-work bit, the card's debug loop) stays down and nothing runs.

#include <cstdint>

#include "debugger/history/devicehistory.h"

struct StateNode;

/// One record: an instruction the processor starts, or an event of its time line (an accepted INT / NMI, a NeoGS
/// clock change). 16 bytes (static_assert below): the ring of 65536 is 1 MiB.
///
/// `t` is the record's start within its frame, on the processor's own clock:
/// - the main CPU: its T in the frame (Z80::t; up to an instruction past the frame length before the frame end wraps
///   it, the record's `frame` still the one it ran in);
/// - a sound card: card units since the card's frame start (GeneralSoundCard::cardFrameTime: 12 MHz cycles x the
///   profile's units per cycle on GS, 120 MHz ticks on NeoGS), exactly main T x GSHostClock::unitsPerZxTact.
/// A record lasts until the next record of the same history starts (its frame and `t`): contention, /WAIT and the
/// NeoGS clock are in that duration as they happened.
///
/// INT and NMI are records of their own (`kind` kInt / kNmi, `pc` the interrupted address): the acknowledge takes
/// time on the bus between two instructions, and without a record of its own it would widen the instruction before
/// it. They reuse `kind` (whose bank-mode values are 0..2 and #FF) so the record keeps its 16 bytes and every reader
/// that filters by kind sees them; IsEvent tells them apart.
struct ExecRecord
{
    static constexpr uint8_t kInt = 0xF0;    ///< an INT acknowledge starts at `t`; `pc` the address it interrupted
    static constexpr uint8_t kNmi = 0xF1;    ///< an NMI acknowledge, likewise
    static constexpr uint8_t kClock = 0xF2;  ///< the card CPU's clock changes at `t` (NeoGS GSCFG0): `idle` the new
                                             ///< clock in Hz, `page` the old one in MHz; it takes no time itself

    uint32_t frame = 0;  ///< the frame it ran in (frame_counter, low 32 bits)
    uint32_t t = 0;      ///< its start in the frame (see above)
    uint16_t pc = 0;
    uint8_t kind = 0;    ///< MemoryBankModeEnum of the address's window: 0 ROM, 1 RAM, 2 cache, #FF none; or an event
    uint8_t page = 0;    ///< the physical page of that kind
    /// A sound card's idle poll loop slept through (GSCardRunner): the loop at `pc` ran this often more, its
    /// instructions as the iteration recorded before this one
    uint32_t idle = 0;

    bool IsEvent() const { return kind >= kInt && kind <= kClock; }
    /// The idle loop's iterations (0 for an event, whose `idle` means something else)
    uint32_t IdleIterations() const { return IsEvent() ? 0 : idle; }

    void ToState(StateNode& node) const;
};
static_assert(sizeof(ExecRecord) == 16, "ExecRecord keeps 16 bytes: the execution rings are sized by it");

using ExecHistory = history::Registered<ExecRecord>;

/// The ring of an execution history without a limit: a few frames of a busy program
constexpr size_t kExecHistoryRing = 65536;
