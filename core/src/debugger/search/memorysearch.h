#pragma once

// Byte-pattern search over emulated memory, for every automation surface (WebAPI POST /memory/find, CLI find,
// Lua / Python mem_find, MCP find_bytes): one implementation, one pattern syntax, one set of address spaces.
//
// Pattern text: hex bytes, separated or not ("CD 16 00", "cd1600"); "??" matches any byte, "A?" / "?5" any
// value of the wildcard nibble. A mask can also be given as bytes (1 bits must match).
// Spaces:
//   cpu            the CPU's view, addresses 0..#FFFF (what is paged in now)
//   ram5 / rom2 / cache0   one physical page, offsets 0..#3FFF (what the CPU does not see now as well)
//   ram            every RAM page of the machine, in page order; a match is reported as page + offset

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/memory/memory.h"
#include "emulator/state/statenode.h"

class EmulatorContext;

struct MemorySearchRequest
{
    std::vector<uint8_t> pattern;
    std::vector<uint8_t> mask;  // same length as pattern; empty = every bit must match

    enum class Space : uint8_t
    {
        Cpu,
        Page,
        AllRam
    } space = Space::Cpu;
    MemoryBankModeEnum pageType = BANK_RAM;  // Space::Page
    uint8_t page = 0;

    uint32_t start = 0;            // in the space: CPU address, page offset; AllRam: linear offset over all pages
    uint32_t end = 0xFFFFFFFF;     // inclusive; clamped to the space
    unsigned max = 64;             // matches reported (the rest set `truncated`)
    unsigned alignment = 1;        // 1 or 2
    unsigned contextBefore = 4;    // bytes before the match returned with each match (clamped at the space's start)
    unsigned contextAfter = 4;     // bytes after the pattern
};

struct MemorySearchMatch
{
    uint32_t address = 0;   // Cpu: the CPU address; Page / AllRam: the offset in `page`
    int pageType = -1;      // Page / AllRam: MemoryBankModeEnum of the page; -1 for Cpu
    int page = -1;
    uint32_t contextStart = 0;   // where `bytes` starts (the same space as `address`)
    std::vector<uint8_t> bytes;  // contextBefore bytes, the matched bytes, contextAfter bytes
};

struct MemorySearchResult
{
    std::vector<MemorySearchMatch> matches;
    bool truncated = false;
    std::string error;  // non-empty: the request was refused, nothing searched
};

namespace MemorySearch
{
/// Pattern text into bytes and mask ("??" / "A?" wildcards). False with the reason in `error`
bool ParsePattern(const std::string& text, std::vector<uint8_t>& pattern, std::vector<uint8_t>& mask, std::string& error);
/// A number as a pattern, its hex digits as written: 0xAF3C -> "AF3C", 0xF -> "0F"
std::string NumberPattern(uint64_t value);
/// "cpu", "ram" or a page ("ram5", "rom2", "cache0") into the request's space. False with the reason
bool ParseSpace(const std::string& text, MemorySearchRequest& request, std::string& error);
/// "cpu", "ram", or the page name: the request's space as the surfaces name it
std::string SpaceName(const MemorySearchRequest& request);
/// Runs the search on the machine's memory (side-effect free reads)
MemorySearchResult Search(EmulatorContext* context, const MemorySearchRequest& request);
/// A request from what the surfaces take: pattern text (or `patternBytes` when not null), optional mask text,
/// space text, range, max, alignment. False with the reason in `error`
bool BuildRequest(const std::string& pattern, const std::vector<uint8_t>* patternBytes, const std::string& mask,
                  const std::string& space, uint32_t start, uint32_t end, unsigned max, unsigned alignment,
                  MemorySearchRequest& out, std::string& error);
/// The result as the scripting surfaces return it: {space, count, truncated, matches: [{address | page {kind,
/// page} + offset, context_start, context}]}, numbers as numbers; {error} when refused
StateNode ToState(const MemorySearchRequest& request, const MemorySearchResult& result);
}  // namespace MemorySearch
