#pragma once

/// @file ttdfindevent.h
/// @brief TTD find event (docs/inprogress/2026-11-02-ttd-find-event/design.md): the moment something happened.
///
/// A query describes an event by kind and condition (a memory value decreased or became V, the screen changed as a
/// whole, an address ran, a port was written, the border changed, a key was pressed); the answer is the first (or
/// last) frames in a window where it happened, each with its exact time, the instruction, the values before and after
/// and a one-line why. TimeTravelController::QueryFindEvent filters the frames from what the session stores (piece
/// versions, the coverage index, the port and input journals) and replays only the hit frames.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/ttdcheckpoint.h"   // TTDTimePoint
#include "debugger/ttd/ttdphyspage.h"

namespace ttd
{

enum class TTDFindEventKind : uint8_t
{
    Mem,      ///< a byte or word of memory meets a condition
    Screen,   ///< the picture changed as a whole (or was cleared)
    Exec,     ///< an address (range) ran
    Port,     ///< an IN / OUT matching port and value
    Border,   ///< the border colour changed (or became a colour)
    Key,      ///< a key pressed or released (the input journal)
};

enum class TTDFindEventCond : uint8_t
{
    Changed,    ///< mem: differs from the frame before
    Increased,
    Decreased,
    Equals,     ///< mem: is V (already at the window's start counts)
    Becomes,    ///< mem: is V and was not
    Differs,    ///< mem: stops being V
    BitSet,     ///< mem: bit B goes 0 -> 1
    BitClear,   ///< mem: bit B goes 1 -> 0
    Change,     ///< screen: at least minChanged % of the picture's bytes differ from the frame before
    Cleared,    ///< screen: the pixel bytes all one value, and they were not
};

const char* TTDFindEventKindName(TTDFindEventKind kind);
const char* TTDFindEventCondName(TTDFindEventCond cond);
/// By name ("mem", "screen", ...; "decreased", "bit_set", ...); false for an unknown name
bool TTDFindEventKindByName(const std::string& name, TTDFindEventKind& out);
bool TTDFindEventCondByName(const std::string& name, TTDFindEventCond& out);

struct TTDFindEventQuery
{
    TTDFindEventKind kind = TTDFindEventKind::Mem;
    TTDFindEventCond cond = TTDFindEventCond::Changed;
    bool last = false;                 ///< the newest hits instead of the oldest
    uint64_t fromFrame = 0;            ///< the window, inclusive (clamped to the history)
    uint64_t toFrame = UINT64_MAX;
    uint32_t limit = 1;

    // mem / exec
    uint16_t address = 0;
    uint16_t addressTo = 0;            ///< exec: the range's last address (= address for one)
    bool hasPage = false;              ///< the physical page given (else the one mapped at the current position)
    PhysPage page = kPhysPageNone;
    uint8_t size = 1;                  ///< mem: 1 or 2 bytes (little endian)
    uint16_t value = 0;                ///< mem: V of equals / becomes / differs; border: the colour; port: the value
    uint8_t bit = 0;                   ///< mem: B of bit_set / bit_clear
    bool exact = false;                ///< mem: every CPU write, not frame boundaries

    // screen
    uint32_t minChanged = 50;          ///< percent

    // port / border
    bool portOut = true;
    uint16_t port = 0;
    uint16_t portMask = 0xFFFF;
    bool hasValue = false;
    uint8_t valueMask = 0xFF;

    // key
    std::string key;                   ///< a ZX key name; empty: any key
    bool pressed = true;

    // exec without the coverage index: frames replayed at most
    uint32_t maxFrames = 1000;

    static constexpr uint32_t kMaxLimit = 100;
    static constexpr uint32_t kMaxFramesReplayed = 100000;
};

struct TTDFindEventHit
{
    TTDTimePoint time;                 ///< the write / the instruction start / the journal record; screen: the frame start
    bool hasPc = false;
    uint16_t pc = 0;
    PhysPage codePage = kPhysPageNone;
    std::string text;                  ///< the instruction, when the hit frame was replayed
    bool hasValues = false;
    uint32_t before = 0;               ///< mem: the value before; screen: changed bytes; border: the colour before
    uint32_t after = 0;
    bool device = false;               ///< mem: no CPU write explains the change (a device, DMA, a ROM trap)
    std::string why;
};

struct TTDFindEventResult
{
    std::string error;
    uint64_t fromFrame = 0;            ///< the window searched
    uint64_t toFrame = 0;
    PhysPage page = kPhysPageNone;     ///< mem / exec: the page searched
    std::vector<TTDFindEventHit> hits;
    bool more = false;                 ///< more hits than `limit` in the window
    uint64_t nextFromFrame = 0;        ///< more: continue here (first) ...
    uint64_t nextToFrame = 0;          ///< ... or here (last)
    std::vector<std::string> barriers; ///< markers the comparison did not cross ("f120 snapshot load")
    std::vector<std::string> missing;  ///< sources the session lacks and what that changed
    uint64_t framesScanned = 0;
    uint64_t framesReplayed = 0;
    double ms = 0.0;
};

/// The answer as text, one line per hit and a summary line (the MCP and CLI form)
std::string TTDFindEventText(const TTDFindEventQuery& query, const TTDFindEventResult& result);

}  // namespace ttd
