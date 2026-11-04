#pragma once

/// @file ttddiffmoments.h
/// @brief TTD diff of two moments (docs/inprogress/2026-11-03-ttd-diff-moments/design.md): what differs between two
/// points of the recorded history.
///
/// The machine is replayed to each moment (any T inside a frame) and its whole state taken there: the CPU, the port
/// latches, the slot mapping, every RAM byte and every registered device's state. The answer lists what differs: the
/// registers, the latches and the mapping, the RAM changed in ranges per region with their values and the last write
/// in between (the write journal), the screen and border, the devices, and what happened in between (time, keys,
/// markers). TimeTravelController::QueryDiffMoments builds it and puts the machine back where it stood.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/ttdcheckpoint.h"   // TTDTimePoint
#include "debugger/ttd/ttdphyspage.h"

namespace ttd
{

/// The answer's sections, a bitmask
enum TTDDiffSection : uint32_t
{
    kDiffCpu = 1u << 0,
    kDiffPaging = 1u << 1,
    kDiffMemory = 1u << 2,
    kDiffScreen = 1u << 3,
    kDiffDevices = 1u << 4,
    kDiffBetween = 1u << 5,
    kDiffAll = (1u << 6) - 1,
};

/// "cpu", "paging", ... ("all" for every one); false for an unknown name
bool TTDDiffSectionByName(const std::string& name, uint32_t& out);
const char* TTDDiffSectionName(uint32_t section);

struct TTDDiffMomentsQuery
{
    TTDTimePoint from{};
    bool hasTo = false;                ///< else the current position
    TTDTimePoint to{};
    uint32_t sections = kDiffAll;
    uint32_t limit = 16;               ///< per list (memory ranges, devices, keys)
    bool writers = true;               ///< the last write in between per memory range (the write journal)

    static constexpr uint32_t kMaxLimit = 200;
};

/// One value that differs (a register, a latch)
struct TTDDiffValue
{
    std::string name;
    uint32_t before = 0;
    uint32_t after = 0;
    uint8_t digits = 2;                ///< hex digits to print
};

/// An 8K slot whose mapping differs
struct TTDDiffSlot
{
    uint8_t slot = 0;                  ///< 0..7: #0000, #2000, ... #E000
    PhysPage before = kPhysPageNone;   ///< kPhysPageNone: ROM (or nothing known)
    uint8_t beforeHalf = 0;            ///< which 8K half of the 16K page the slot shows (0: the slot's own parity)
    PhysPage after = kPhysPageNone;
    uint8_t afterHalf = 0;
};

enum class TTDDiffRegion : uint8_t
{
    Ram,
    ScreenPixels,
    ScreenAttributes,
};
const char* TTDDiffRegionName(TTDDiffRegion region);

struct TTDDiffRange
{
    TTDDiffRegion region = TTDDiffRegion::Ram;
    PhysPage page = 0;
    uint32_t offset = 0;               ///< inside the page (a screen range: its first changed byte)
    bool hasAddress = false;
    uint16_t address = 0;              ///< the Z80 address of offset at the second moment (else at the first)
    uint32_t length = 0;               ///< first .. last changed byte
    uint32_t changed = 0;
    std::vector<uint8_t> before;       ///< a range of 8 bytes or less: its bytes at both moments
    std::vector<uint8_t> after;

    // The last write in between (TTDDiffMomentsQuery::writers)
    bool hasWriter = false;
    TTDTimePoint writerTime{};
    uint16_t writerPc = 0;
    uint16_t writerAddress = 0;
    std::string writerText;
    bool writerUnknown = false;        ///< searched, none found in between (a device, DMA, or a barrier stopped it)
};

struct TTDDiffDevice
{
    std::string name;                  ///< the device's instance name ("ay-socket.ts")
    uint32_t bytes = 0;                ///< its state's size
    uint32_t changed = 0;              ///< bytes that differ
};

struct TTDDiffKey
{
    TTDTimePoint time{};
    std::string key;
    bool pressed = true;
};

struct TTDDiffMomentsResult
{
    std::string error;
    TTDTimePoint from{};
    TTDTimePoint to{};
    bool backwards = false;            ///< to is before from (the answer still reads from -> to)

    std::vector<TTDDiffValue> registers;
    std::vector<TTDDiffValue> latches;
    std::vector<TTDDiffSlot> slotMap;

    std::vector<TTDDiffRange> memory;
    uint32_t moreMemory = 0;
    uint32_t ramChanged = 0;           ///< every RAM byte that differs
    uint32_t pagesChanged = 0;         ///< 16K pages with a byte that differs

    bool hasScreen = false;
    uint32_t screenBytes = 0;
    uint32_t pixelsChanged = 0;
    uint32_t attributesChanged = 0;
    bool screenSwitched = false;       ///< the shown screen is another page
    uint8_t borderBefore = 0;
    uint8_t borderAfter = 0;

    std::vector<TTDDiffDevice> devices;
    uint32_t moreDevices = 0;
    uint32_t devicesSame = 0;

    // between
    uint64_t frames = 0;
    uint64_t t = 0;                    ///< T between the moments (TTD units: the top clock's T-states)
    std::vector<std::string> keysHeld; ///< down at the first moment
    std::vector<TTDDiffKey> keys;
    uint32_t moreKeys = 0;
    std::vector<std::string> markers;  ///< external events and bookmarks in between
    std::string barrier;               ///< a replay barrier in between: the writers search stops there

    uint32_t writerSearches = 0;
    double ms = 0.0;
};

/// The answer as text, one block per section (the MCP and CLI form)
std::string TTDDiffMomentsText(const TTDDiffMomentsQuery& query, const TTDDiffMomentsResult& result);

}  // namespace ttd
