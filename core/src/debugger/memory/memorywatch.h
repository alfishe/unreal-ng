#pragma once

// Watches over memory regions and ports for every automation surface (WebAPI /profiler/memory/watches, MCP
// analyze_performance memory_watch, CLI profiler memory watch, Lua / Python memory_watch_*): one implementation over
// MemoryAccessTracker's monitored regions and ports.
//
// A watch counts reads, writes and executes (a region: CPU addresses) or reads and writes (a port: the address lines
// in `mask`, so 0x00FF with port 0x1F catches every Kempston read), and optionally the PCs that made them (callers)
// and the values moved (values), each map up to its own limit of distinct keys. Watches count while a memory session
// captures (tracecontrol "memory" start) and clear with its counters. The emulation thread updates them without a
// lock, so every call here runs at a coherent moment (paused, stopped, or between two frames).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Emulator;

namespace MemoryWatch
{
struct Options
{
    bool callers = false;     // count the PC of every access
    bool values = false;      // count the values read and written
    uint32_t maxCallers = 100;
    uint32_t maxValues = 256;
};

struct Watch
{
    std::string name;
    bool port = false;
    uint16_t start = 0;       // region: the first CPU address; port: the port
    uint32_t length = 0;      // region: bytes (start + length <= #10000); port: 0
    uint16_t mask = 0xFFFF;   // port: the address lines compared
    Options options;
    uint32_t reads = 0;
    uint32_t writes = 0;
    uint32_t executes = 0;
    std::map<uint16_t, uint32_t> callers;   // PC -> accesses
    std::map<uint8_t, uint32_t> values;     // value -> accesses
};

struct Result
{
    bool ok = false;
    bool busy = false;             // no coherent moment within the timeout
    std::string moment;            // "paused", "stopped" or "frame" when ok
    std::vector<Watch> watches;    // List, and the watch added; by name
    std::string error;
};

/// Watch `length` bytes from CPU address `start`. A name is 1..64 of letters, digits, '_', '-', '.' and is unique
/// over regions and ports
Result AddRegion(Emulator* emulator, const std::string& name, uint32_t start, uint32_t length, const Options& options);

/// Watch the ports whose address lines in `mask` equal `port`'s
Result AddPort(Emulator* emulator, const std::string& name, uint32_t port, uint32_t mask, const Options& options);

/// Remove the region or port of that name
Result Remove(Emulator* emulator, const std::string& name);

/// Remove every watch
Result Clear(Emulator* emulator);

/// Every watch with its counts
Result List(Emulator* emulator);

/// The watch's name as AddRegion / AddPort take it; false with the reason
bool ValidName(const std::string& name, std::string& error);
}  // namespace MemoryWatch
