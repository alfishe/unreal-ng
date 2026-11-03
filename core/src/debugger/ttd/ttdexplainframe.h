#pragma once

/// @file ttdexplainframe.h
/// @brief TTD explain frame (docs/inprogress/2026-11-02-ttd-explain-frame/design.md): what one frame did.
///
/// One replay of the frame with the instruction and bus hooks (the cause chain's frame trace) gives the frame's time
/// split, the routines that ran by time (from the call structure), the interrupts, the paging, the port activity per
/// device, the memory changed per region with its writers, the screen and the border; the input journal gives the
/// keys and the external events journal the markers. TimeTravelController::QueryExplainFrame builds it and puts the
/// machine back where it stood.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/ttdcheckpoint.h"   // TTDTimePoint
#include "debugger/ttd/ttdphyspage.h"

namespace ttd
{

/// The answer's sections, a bitmask
enum TTDExplainSection : uint32_t
{
    kExplainTime = 1u << 0,
    kExplainRoutines = 1u << 1,
    kExplainInterrupt = 1u << 2,
    kExplainPaging = 1u << 3,
    kExplainPorts = 1u << 4,
    kExplainMemory = 1u << 5,
    kExplainScreen = 1u << 6,
    kExplainInput = 1u << 7,
    kExplainMarkers = 1u << 8,
    kExplainAll = (1u << 9) - 1,
};

/// "time", "routines", ... ("all" for every one); false for an unknown name
bool TTDExplainSectionByName(const std::string& name, uint32_t& out);
const char* TTDExplainSectionName(uint32_t section);

struct TTDExplainFrameQuery
{
    bool hasFrame = false;             ///< else the current position's frame
    uint64_t frame = 0;
    uint32_t sections = kExplainAll;
    uint32_t limit = 8;                ///< per list (routines, paging, ports, memory ranges, writers of a range)

    static constexpr uint32_t kMaxLimit = 100;
};

struct TTDExplainRoutine
{
    uint16_t pc = 0;                   ///< the entry
    PhysPage codePage = kPhysPageNone;
    bool isr = false;                  ///< entered by an interrupt acknowledge
    bool start = false;                ///< the code running at the frame's start (entry unknown: pc is where it was)
    uint32_t calls = 0;
    uint64_t t = 0;                    ///< inclusive
    uint64_t ownT = 0;
    uint64_t insns = 0;                ///< inclusive
    bool hasCaller = false;
    uint16_t caller = 0;               ///< the PC of the first call (the CALL / RST; the interrupted PC for an ISR)
    std::string label;                 ///< the debugger's label at the entry, when loaded
};

struct TTDExplainInterrupt
{
    uint32_t tInFrame = 0;             ///< the acknowledge
    uint8_t mode = 0;                  ///< 0, 1, 2
    bool hasVector = false;
    uint16_t vectorAddress = 0;        ///< IM 2: where the routine's address was read
    uint16_t entry = 0;
    uint64_t t = 0;                    ///< until its return (or the frame end)
    bool returned = false;
    uint16_t returnedTo = 0;           ///< the interrupted PC
    bool beforeFrame = false;          ///< acknowledged in the frame before's last step: the frame starts at its entry
};

struct TTDExplainPaging
{
    uint32_t tInFrame = 0;
    uint16_t pc = 0;                   ///< the instruction that switched (the record before the change)
    uint8_t bank = 0;                  ///< 0..3: #0000, #4000, #8000, #C000
    PhysPage page = kPhysPageNone;
    PhysPage was = kPhysPageNone;
};

struct TTDExplainPort
{
    bool out = false;
    std::string device;                ///< the port map's device ("" when no row decodes it)
    uint16_t port = 0;                 ///< the first port seen (a device decodes many)
    bool manyPorts = false;            ///< more than one port of the device
    uint32_t count = 0;
    uint32_t tInFrame = 0;             ///< the first access
    uint16_t pc = 0;
    std::vector<uint8_t> values;       ///< up to four distinct values in order
    bool moreValues = false;
    uint8_t rows = 0;                  ///< a ULA read: the keyboard half-rows read (bit n = row n)
    std::vector<std::string> keys;     ///< a ULA read: the keys seen down, once each
};

struct TTDExplainWriter
{
    uint16_t pc = 0;
    PhysPage codePage = kPhysPageNone;
    std::string text;
    uint32_t count = 0;                ///< bytes written by it into the range
};

enum class TTDExplainRegion : uint8_t
{
    Ram,
    ScreenPixels,
    ScreenAttributes,
    Stack,
};
const char* TTDExplainRegionName(TTDExplainRegion region);

struct TTDExplainRange
{
    TTDExplainRegion region = TTDExplainRegion::Ram;
    PhysPage page = kPhysPageNone;
    uint32_t offset = 0;               ///< inside the page
    bool hasAddress = false;
    uint16_t address = 0;              ///< the Z80 address of offset at the first write (or where the page was mapped)
    uint32_t length = 0;               ///< offset span (first .. last byte touched)
    uint32_t changed = 0;
    uint32_t written = 0;
    bool hasValues = false;            ///< a range of one or two bytes: the value at the frame start and end
    uint16_t before = 0;
    uint16_t after = 0;
    bool device = false;               ///< changed with no CPU write
    std::vector<TTDExplainWriter> writers;
    bool moreWriters = false;
};

struct TTDExplainBorder
{
    uint8_t colour = 0;
    uint32_t count = 0;
};

struct TTDExplainKey
{
    uint32_t tInFrame = 0;
    std::string key;
    bool pressed = true;
};

struct TTDExplainFrameResult
{
    std::string error;
    uint64_t frame = 0;
    uint32_t frameT = 0;               ///< the frame's length (T at the base clock)
    uint32_t endT = 0;                 ///< explained up to here (< frameT: partial or a barrier)
    bool partial = false;              ///< the session ends inside the frame
    std::string barrier;               ///< a marker inside the frame ended the replay there ("f120+300 snapshot load")
    uint64_t instructions = 0;

    // time
    uint64_t isrT = 0;
    uint64_t haltT = 0;
    uint64_t codeT = 0;

    std::vector<TTDExplainRoutine> routines;
    uint32_t moreRoutines = 0;
    bool interruptsEnabled = true;     ///< IFF1 at the frame start
    std::vector<TTDExplainInterrupt> interrupts;
    std::vector<TTDExplainPaging> paging;
    uint32_t morePaging = 0;
    std::vector<TTDExplainPort> ports;
    uint32_t morePorts = 0;
    std::vector<TTDExplainRange> memory;
    uint32_t moreMemory = 0;

    // screen
    bool hasScreen = false;
    uint32_t screenBytes = 0;          ///< the picture's size
    uint32_t screenChanged = 0;
    uint32_t pixelsChanged = 0;
    uint32_t attributesChanged = 0;
    bool screenSwitched = false;       ///< the shown page changed
    std::vector<TTDExplainBorder> border;

    // input
    std::vector<std::string> keysHeld;
    std::vector<TTDExplainKey> keys;

    std::vector<std::string> markers;  ///< external events and bookmarks inside the frame

    uint64_t framesReplayed = 0;
    double ms = 0.0;
};

/// The answer as text, one block per section (the MCP and CLI form)
std::string TTDExplainFrameText(const TTDExplainFrameQuery& query, const TTDExplainFrameResult& result);

}  // namespace ttd
