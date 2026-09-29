#pragma once

/// @file ttdportsearch.h
/// @brief "When did the program ..." queries over the port journals
/// (ttd-port-read-journal.md §10): when it first saw key A pressed, when it
/// saw the tape signal change, when it wrote AY register 7, when the border
/// changed. A scan of the recorded IN / OUT records - nothing is replayed, so
/// it works the same on a live session and on a loaded file.
///
/// A query is either a named event (BuildPortEventQuery) or a raw filter:
/// port mask/value, value test, trigger. Every surface (WebAPI, MCP, CLI, Lua,
/// Python) builds its queries through BuildPortEventQuery so an event means
/// the same thing everywhere.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ttdcheckpoint.h"
#include "ttdportjournal.h"

namespace ttd
{

/// How a record's value is tested (after `valueMask`)
enum class TTDValueMatch : uint8_t
{
    Any,          ///< every value
    Equals,       ///< (value & valueMask) == value
    AnyBitClear,  ///< some bit of valueMask is 0 (keyboard: a key is down)
    AnyBitSet     ///< some bit of valueMask is 1
};

/// Which matching records are hits. Rising and Change compare with the
/// previous record of the same stream - the same port address under
/// TTDPortQuery::streamMask - so interleaved polls of different half-rows do
/// not trigger each other, while writes to the ULA, which decodes A0 alone,
/// form one stream whatever the high byte
enum class TTDPortTrigger : uint8_t
{
    Every,   ///< every record whose value passes the test
    Rising,  ///< the test passes now and did not at the previous read of this port (first read: counts)
    Change   ///< the masked value differs from the previous record of this port (first record: not a hit)
};

struct TTDPortQuery
{
    TTDPortJournal::Direction direction = TTDPortJournal::Direction::Read;
    /// A record's port matches when (port & portMask) == portValue
    uint16_t portMask = 0;
    uint16_t portValue = 0;
    TTDValueMatch valueMatch = TTDValueMatch::Any;
    uint8_t valueMask = 0xFF;
    uint8_t value = 0;
    TTDPortTrigger trigger = TTDPortTrigger::Every;
    /// Address bits that tell one stream from another for Rising / Change:
    /// #FFFF = every port address is its own stream (keyboard half-rows);
    /// #0001 = one stream for the ULA's port whatever the high byte (border,
    /// beeper, tape signal)
    uint16_t streamMask = 0xFFFF;
    /// Only accesses made while this AY register was selected (-1: any). The
    /// selection is followed through the OUT journal (#FFFD writes)
    int ayRegister = -1;
    /// Time window [from, to]
    TTDTimePoint from{0, 0};
    TTDTimePoint to{UINT64_MAX, UINT32_MAX};
    size_t limit = 100;
    bool newestFirst = false;  ///< the last `limit` hits instead of the first
};

struct TTDPortHit
{
    TTDPortRecord record;  ///< time, PC, port, value
    uint64_t index = 0;    ///< position in its journal
    int ayRegister = -1;   ///< the AY register selected at the access (-1: none known)
};

struct TTDPortSearchResult
{
    bool ok = false;
    std::string error;
    std::vector<TTDPortHit> hits;  ///< in time order (newest first when asked)
    bool truncated = false;        ///< more hits than `limit`
    uint64_t scanned = 0;          ///< records examined
};

/// Named events:
///   key [KEY]       IN: the program saw a key down, once per press. KEY: a
///                   matrix key name as the keyboard API takes it - "a",
///                   "enter", "space", "caps", "symbol"... - and only reads of
///                   that key's half-row alone count (reading several rows at
///                   once, a program cannot tell A from Q). No KEY: any key
///                   down in any read of the keyboard
///   ear             IN: the program saw the tape (EAR) bit change
///   ay-read [REG]   IN from the AY (#FFFD): a register read
///   ay-write [REG]  OUT to the AY data port (#BFFD)
///   ay-select [REG] OUT to the AY register port (#FFFD)
///   border          OUT #FE: the border color changed
///   beeper          OUT #FE: the beeper bit changed
///   in / out        every IN / OUT (narrow it with the raw fields)
/// AY ports use the 128K decoding (A15, A14, A1). Returns false with `err` for
/// an unknown event or argument. The raw fields of `q` not set by the event
/// are left as the caller set them
bool BuildPortEventQuery(const std::string& event, const std::string& arg, TTDPortQuery& q, std::string& err);

/// The named events, for help texts and schemas
const std::vector<std::string>& PortEventNames();

/// One option of a query given as text, the same on every surface:
///   limit=N            hits to return (default 100)
///   newest=true|false  the last hits instead of the first
///   from=F[:T] to=F[:T] time window (frame, optional TTD T-state in it)
///   port=P port_mask=M  the port matches when (port & M) == P (P alone: M = #FFFF)
///   value=V value_mask=M  (value & M) == V (V alone: M = #FF, match=equals)
///   match=any|equals|any-clear|any-set  the value test
///   trigger=every|rising|change
///   stream_mask=M      address bits that separate streams for rising/change
///   ay_register=R      only while AY register R is selected
/// Numbers: decimal, 0x.., #.. or $... Apply after BuildPortEventQuery: an
/// option overrides what the event set. False with `err` for a bad option
bool ApplyPortQueryOption(TTDPortQuery& q, const std::string& name, const std::string& value, std::string& err);

/// Short text names of the enums (reports, schemas)
const char* PortDirectionName(TTDPortJournal::Direction d);

/// Scan the journals. `writes` is also used for the AY register selection
/// when the query asks for one
TTDPortSearchResult SearchPortEvents(const TTDPortJournal& reads, const TTDPortJournal& writes,
                                     const TTDPortQuery& q);

}  // namespace ttd
