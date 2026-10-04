#pragma once

// Access traces of real programs (recorder/) replayed through every matcher. One event per CPU access the
// emulator's debug memory / port paths check, plus the slot remaps the paging port causes, so a matcher sees
// what BreakpointManager sees: the Z80 address, and the physical page through the current slot table.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace poc
{

/// What an event is. Exec: one per instruction, at its PC (BreakpointManager::HandlePCChange). Fetch: the
/// opcode / operand bytes, which the debug read path checks as reads too. Remap: slot `addr` now shows the
/// physical page `value`.
enum Kind : uint8_t
{
    KExec = 0,
    KFetch,
    KRead,
    KWrite,
    KIn,
    KOut,
    KRemap,
    KCount
};

/// Physical page id: kind in the high byte (0 ROM, 1 RAM, 2 cache), page number in the low byte
using PhysPage = uint16_t;
constexpr PhysPage MakePhys(uint8_t type, uint8_t page) { return static_cast<PhysPage>((type << 8) | page); }
constexpr uint8_t kRom = 0, kRam = 1, kCache = 2;

struct Event
{
    uint16_t addr;   // Z80 address or port; the slot for KRemap
    uint16_t value;  // the byte read / written; the new PhysPage for KRemap
    uint8_t kind;
    uint8_t pad = 0;
};
static_assert(sizeof(Event) == 6, "packed trace event");

struct Trace
{
    std::string name;
    PhysPage initialSlots[4] = {};
    std::vector<Event> events;

    bool Save(const std::string& path) const
    {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
            return false;
        const uint32_t magic = 0x32324250;  // "PB22"
        const uint64_t count = events.size();
        std::fwrite(&magic, 4, 1, f);
        std::fwrite(initialSlots, sizeof(initialSlots), 1, f);
        std::fwrite(&count, 8, 1, f);
        std::fwrite(events.data(), sizeof(Event), events.size(), f);
        return std::fclose(f) == 0;
    }

    bool Load(const std::string& path)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return false;
        uint32_t magic = 0;
        uint64_t count = 0;
        bool ok = std::fread(&magic, 4, 1, f) == 1 && magic == 0x32324250 &&
                  std::fread(initialSlots, sizeof(initialSlots), 1, f) == 1 && std::fread(&count, 8, 1, f) == 1;
        if (ok)
        {
            events.resize(count);
            ok = std::fread(events.data(), sizeof(Event), count, f) == count;
        }
        std::fclose(f);
        const size_t slash = path.find_last_of('/');
        name = path.substr(slash == std::string::npos ? 0 : slash + 1);
        return ok;
    }
};

/// The traces every experiment runs on (recorded by recorder/, kept out of the repository)
inline std::string TraceDir()
{
    const char* dir = std::getenv("POC022_TRACES");
    return dir ? dir : "scratch/poc-022/traces";
}

}  // namespace poc
