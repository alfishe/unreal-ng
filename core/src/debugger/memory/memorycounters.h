#pragma once

// The memory access counters (MemoryAccessTracker: reads, writes and executes of every address) for every automation
// surface (WebAPI GET /profiler/memory/counters, MCP analyze_performance memory_counters, CLI profiler memory
// counters, Lua / Python memory_counters): one implementation.
//
// Spaces:
//   cpu                    the CPU's view, addresses 0..#FFFF: what ran at an address whatever was paged in
//   ram5 / rom2 / cache0   one physical page, offsets 0..#3FFF, mapped or not
//   ram                    every RAM page back to back (page n at n * #4000)
//   phys                   the whole physical array: RAM 0..255, cache 0..3, misc 0, ROM 0..127, each #4000 cells
//                          (PhysPageName names the page of an offset)
// The counters run while a memory session captures (tracecontrol "memory" start; the feature memorytracking). The
// cpu counters only count in the Z80 address space mode (the default); the physical ones always. A read takes the
// counters' lock shared and copies; the emulation thread keeps counting meanwhile.

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;

namespace MemoryCounters
{
constexpr uint8_t kRead = 1;
constexpr uint8_t kWrite = 2;
constexpr uint8_t kExecute = 4;
constexpr uint8_t kAll = kRead | kWrite | kExecute;

constexpr uint32_t kMaxDense = 1u << 20;      // cells of one dense read
constexpr uint32_t kMaxLimit = 1u << 20;      // cells of one sparse / top read
constexpr uint32_t kMaxAddresses = 65536;     // cells named in one read

struct Cell
{
    uint32_t address = 0;   // in the space
    uint32_t reads = 0;
    uint32_t writes = 0;
    uint32_t executes = 0;
};

enum class Mode : uint8_t
{
    Dense,    // every cell of the window, zeros too
    Sparse,   // the cells of the window at or above `min`, in address order
    Top       // the `limit` heaviest cells of the window, heaviest first (ties: lower address first)
};

struct Request
{
    std::string space;                // "" = cpu
    uint32_t start = 0;
    uint32_t length = 0;              // 0: to the end of the space
    std::vector<uint32_t> addresses;  // non-empty: exactly these cells in this order, whatever their counts
    Mode mode = Mode::Sparse;
    uint8_t kinds = kAll;             // the counters a cell's weight sums: the sparse filter, the top order
    uint32_t min = 1;                 // sparse / top: cells whose weight is at least this (0 acts as 1)
    uint32_t limit = 4096;            // sparse / top: at most this many cells (more set `truncated`)
};

struct Result
{
    std::string space;            // as the surfaces name it: "cpu", "phys", "ram", "ram5"
    uint32_t spaceSize = 0;       // cells in the space
    uint32_t start = 0;           // the window read (addresses: 0 and the space's size)
    uint32_t length = 0;
    std::string session;          // "capturing", "paused" or "stopped"
    bool allocated = false;       // false: no session ever ran, every count is 0
    std::vector<Cell> cells;
    uint64_t reads = 0;           // the window's totals
    uint64_t writes = 0;
    uint64_t executes = 0;
    uint32_t matched = 0;         // sparse / top: cells at or above `min` in the window (cells holds up to `limit`)
    bool truncated = false;       // sparse / top: matched > limit
    std::string error;            // non-empty: nothing was read
};

/// Read the counters as asked. An error names the reason
Result Read(EmulatorContext* context, const Request& request);

/// One cell (an address of `space`); the counts are 0 while the counters were never allocated
Result ReadCell(EmulatorContext* context, const std::string& space, uint32_t address);

/// "r", "w", "x" or any mix ("rw", "rwx"), or "all"; false with the reason
bool ParseKinds(const std::string& text, uint8_t& kinds, std::string& error);

/// "dense", "sparse" or "top"; false with the reason
bool ParseMode(const std::string& text, Mode& mode, std::string& error);

/// A list of cells: "0x5C00,#5C01,23554", ranges "0x8000-0x80FF" (inclusive) too; at most kMaxAddresses cells.
/// False with the reason
bool ParseAddresses(const std::string& text, std::vector<uint32_t>& addresses, std::string& error);

/// "ram5", "cache0", "misc0" or "rom2": the page of an absolute page index (offset / #4000 in phys)
std::string PhysPageName(uint32_t absPage);

/// "r", "rw", "rwx": the kinds as ParseKinds takes them
std::string KindsName(uint8_t kinds);

/// "dense", "sparse", "top"
const char* ModeName(Mode mode);
}  // namespace MemoryCounters
