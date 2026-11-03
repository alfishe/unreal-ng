#pragma once

/// @file nextintjournal.h
/// @brief The Next's interrupt, NMI and reset journal (docs/inprogress/2026-10-07-zx-next/automation-gaps-2026-10-11.md, P4): a bounded
/// ring of what the CPU was interrupted by and how the machine was reset - the maskable interrupt accepted (which source, which
/// vector), the RETI that ended it, an NMI request from a button (accepted, or refused and why), the RETN that ended an NMI session,
/// a reset (soft or hard, with NR #02's reset bits after it). "Which interrupt did the CPU take at this PC" and "why did the M1 button do
/// nothing" are one query. Off by default; one branch when off.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/z80n/nextjournal.h"

enum class NextIntKind : uint8_t
{
    IntAck = 0,      ///< a maskable interrupt accepted: `source` and, in hardware IM2 mode, the `vector`
    Reti = 1,        ///< RETI: the hardware IM2 source in service is done (`source`)
    NmiRequest = 2,  ///< a button or a tool asked for an NMI: `accepted`, else `reason`
    Retn = 3,        ///< RETN: `source` names the NMI session it ended (multiface, divmmc, none)
    Reset = 4,       ///< `source` soft / hard; `value` = NR #02 bits 1:0 after it
};

const char* NextIntKindName(NextIntKind kind);

struct NextIntEvent
{
    uint64_t seq = 0;
    uint32_t frame = 0;
    uint32_t t = 0;
    uint16_t pc = 0;
    NextIntKind kind = NextIntKind::IntAck;
    const char* source = "";  ///< a static string: line, ula, ctc0..ctc7, uart0_rx ...; m1_button, drive_button, pulse; soft, hard
    bool hasVector = false;   ///< hardware IM2 mode: the vector was placed on the bus by the controller
    uint8_t vector = 0;
    bool accepted = false;    ///< NmiRequest
    const char* reason = "";  ///< NmiRequest refused: why
    uint8_t value = 0;        ///< Reset: NR #02 bits 1:0 after

    /// The record of the history and the journal reports (portdecoder_next_state.cpp)
    void ToState(StateNode& node) const;
};

struct NextIntJournalQuery
{
    std::vector<NextIntKind> kinds;
    uint64_t since = 0;
    int64_t frameFrom = -1;
    int64_t frameTo = -1;
    size_t limit = 200;
};

bool NextIntJournalQueryFromStrings(const std::string& kinds, const std::string& since, const std::string& from, const std::string& to,
                                    const std::string& limit, NextIntJournalQuery& query, std::string& error);

class NextIntJournal : public NextJournal<NextIntEvent>
{
public:
    static constexpr size_t kDefaultCapacity = 16384;

    explicit NextIntJournal(size_t capacity = kDefaultCapacity)
        : NextJournal("next-int", "interrupts, NMIs, RETNs and resets", capacity)
    {
    }

    /// The matches, oldest first, the newest `limit` of them
    std::vector<NextIntEvent> Query(const NextIntJournalQuery& query) const;
};
