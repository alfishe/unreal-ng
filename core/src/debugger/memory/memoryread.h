#pragma once

// Raw memory reads for every automation surface (WebAPI format=binary, Lua / Python mem_read_bytes, CLI memory save,
// the debugger snapshot's memory windows): one implementation, the address spaces of MemorySearch
// (docs/inprogress/2026-10-04-debugger-snapshot/tdd.md §3).
//
// Spaces:
//   cpu                    the CPU's view, addresses 0..#FFFF, wrapping at #FFFF (what is paged in now)
//   ram5 / rom2 / cache0   one physical page, offsets 0..#3FFF, mapped or not
//   ram                    every RAM page back to back (page n at n * #4000)
// Reads are side-effect free: no contention, no memory-mapped device sees them.

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;

namespace MemoryRead
{
constexpr uint32_t kMaxLength = 65536;

/// A resolved read: the space as named, the start and the byte count actually read
struct Result
{
    std::string space;   // "cpu", "ram", "ram5", ...
    uint32_t address = 0;
    std::vector<uint8_t> bytes;
    std::string error;   // non-empty: nothing was read
};

/// Read `length` bytes (1..kMaxLength) at `address` of `space` ("" = cpu). A page read stops at the page's end, an
/// all-RAM read at the last page; a CPU read wraps at #FFFF. An error names the reason
Result Bytes(EmulatorContext* context, const std::string& space, uint32_t address, uint32_t length);

/// A number as the debugger surfaces take it: decimal, 0x13AF, #13AF, $13AF or 13AFh; false when it is none of them
/// or above #FFFFFFFF
bool ParseNumber(const std::string& text, uint32_t& out);

/// "<space>:<address>:<length>" as the snapshot and the CLI take it ("cpu:0x8000:256", "ram5:0:6912"); numbers in
/// decimal, 0x.., #.., $.. or ..h (ParseNumber)
bool ParseWindow(const std::string& text, std::string& space, uint32_t& address, uint32_t& length, std::string& error);
}  // namespace MemoryRead
