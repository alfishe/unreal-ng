#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Memory;
struct CONFIG;

/// region <Sparse memory map>

/// Default zero-run merge threshold: zero runs shorter than this stay inside
/// neighbouring data regions so a typical 64K map renders in a handful of
/// blocks (~200-500 tokens) instead of thousands of runs.
constexpr uint32_t kMemoryMapDefaultMinRun = 64;

/// Default block budget per map response.
constexpr uint32_t kMemoryMapDefaultMaxBlocks = 48;

/// Default fill-run threshold for sparse reads (filter=sparse / format=sparse).
constexpr uint32_t kSparseDefaultMinRun = 8;

/// One merged region of the sparse memory map. Single source for every
/// automation surface (WebAPI GET /memory/map, MCP inspect_state 'memory_map'
/// aspect, CLI 'memory map', Lua/Python memory_map()) - surfaces render these
/// fields verbatim and never recompute them (automation parity rule).
struct MemoryMapBlock
{
    uint32_t address = 0;   // Start address (CPU view) or page base (ram view)
    uint32_t size = 0;      // Region size in bytes
    uint8_t bank = 0xFF;    // CPU bank 0-3 (address view); 0xFF when N/A
    uint16_t page = 0;      // Physical RAM page (ROM page when isRom)
    bool isRom = false;     // Region is ROM-backed (address view, bank 0)
    uint32_t nonZero = 0;   // Bytes != 0x00 inside the region
    uint64_t hash = 0;      // FNV-1a 64 fingerprint of the region bytes (0 for zero fills)
    std::string typeName;   // Core-rendered label: "rom0", "ram5", "ram0-ram3" (merged zero span)

    bool IsZeroFill() const { return nonZero == 0; }
};

struct MemoryMapReport
{
    std::string model;          // Config::GetModelFullName of the mapped machine
    bool ramView = false;       // true = physical RAM pages, false = CPU address space
    uint32_t totalSize = 0;     // Bytes covered by the scan
    uint32_t nonZeroBytes = 0;  // Total bytes != 0x00
    uint32_t minRun = 0;        // Effective zero-run merge threshold used
    bool truncated = false;     // Block budget exhausted even at max granularity
    std::vector<MemoryMapBlock> blocks;
};

enum class MemoryMapView
{
    AddressSpace,   // 64 KB CPU view with current bank/page mapping
    RamPages        // Physical RAM pages (0 .. ramsize/16 - 1)
};

/// Builds the sparse non-zero overview of memory. Blocks never merge across
/// 16K bank/page boundaries (keeps the bank/page label unambiguous); zero
/// runs shorter than minRun stay folded inside data regions. Consecutive
/// all-zero RAM pages coalesce into one block in the ram view. If the block
/// budget overflows the scan retries with minRun * 4; truncated=true reports
/// a budget that even 16K granularity could not satisfy.
MemoryMapReport BuildMemoryMap(Memory& memory, const CONFIG& config,
                               MemoryMapView view = MemoryMapView::AddressSpace,
                               uint32_t minRun = kMemoryMapDefaultMinRun,
                               uint32_t maxBlocks = kMemoryMapDefaultMaxBlocks);

/// endregion

/// region <Compact read formats>

/// Classic 16-bytes-per-line dump with ASCII sidebar, the canonical
/// automation format for small in-context reads (format=hexdump):
///   "0x0060: 3E 21 F3 00 18 D3 3F 00  21 00 60 C3 00 60 00 C9  |..!....?..!...`..|"
std::string FormatHexDump(const uint8_t* data, size_t size, uint32_t baseAddress);

/// One run of the sparse read encoding: either a fill run (0x00 / 0xFF) or a
/// data run with lowercase hex. Single source for filter=sparse on every
/// automation surface.
struct MemorySparseSegment
{
    uint32_t offset = 0;    // Relative to the requested window start
    uint32_t length = 0;    // Run length in bytes
    bool isFill = false;
    uint8_t fill = 0;       // 0x00 or 0xFF (fill runs only)
    std::string hex;        // Lowercase hex, two chars per byte (data runs only)
};

/// Compresses runs of 0x00 / 0xFF of at least minRun bytes into fill
/// segments; everything between becomes data segments with lowercase hex.
std::vector<MemorySparseSegment> BuildSparseSegments(const uint8_t* data, size_t size,
                                                     uint32_t minRun = kSparseDefaultMinRun);

/// Counts bytes != 0x00 (summary field of sparse reads).
uint32_t CountNonZeroBytes(const uint8_t* data, size_t size);

/// endregion
