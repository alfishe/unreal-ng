/// @file v2_index_overhead.cpp
/// @brief Analyze v2 format index overhead for hot buffer + file stream
///
/// v2 format needs to track:
/// - Page slot indices for fast dereferencing
/// - Frame index → file offset mapping
/// - Hot buffer page addresses
///
/// This POC calculates memory overhead for various hot buffer sizes.

#include <cstdint>
#include <cstddef>
#include <iostream>
#include <iomanip>

// ============================================================================
// v2 Index Structures
// ============================================================================

// Per-page slot entry (in hot buffer index)
struct PageSlotEntry {
    uint32_t slotId;          // Unique slot ID
    uint32_t hotBufferOffset; // Offset in hot buffer (0 = not in hot buffer)
    uint64_t fileOffset;      // Offset in file stream
    uint16_t pageSize;        // 4KB = 4096
    uint16_t flags;           // Dirty, compressed, etc.
};  // 20 bytes per slot

// Per-frame checkpoint index entry
struct FrameIndexEntry {
    uint64_t frameNumber;
    uint64_t globalTStates;
    uint64_t fileOffset;       // Where this frame's data starts in file
    uint32_t compressedSize;   // Bytes in file
    uint32_t uncompressedSize;
    uint32_t pageCount;        // Number of pages referenced
    uint32_t* pageSlots;       // Variable-length array of slot IDs
};  // 36 bytes base + 4 bytes per page

// Hot buffer metadata
struct HotBufferMeta {
    uint8_t* baseAddress;
    size_t totalSize;
    size_t usedSize;
    uint32_t pageCapacity;     // How many 4KB pages fit
    uint32_t pagesInUse;
};

// ============================================================================
// Overhead Calculations
// ============================================================================

struct OverheadResult {
    size_t hotBufferMB;
    size_t pageCapacity;
    size_t pageIndexBytes;
    size_t frameIndexBytesPerFrame;
    size_t totalIndexForSession;
    double overheadPercent;
};

OverheadResult CalculateOverhead(size_t hotBufferMB, size_t framesIn5Min,
                                  size_t pagesPerFrame, size_t totalRamPages)
{
    OverheadResult r{};
    r.hotBufferMB = hotBufferMB;

    const size_t pageSize = 4096;
    r.pageCapacity = (hotBufferMB * 1024 * 1024) / pageSize;

    // Page slot index: one entry per possible slot in hot buffer
    // Plus some overhead for hash table / lookup structure
    r.pageIndexBytes = r.pageCapacity * sizeof(PageSlotEntry) * 1.3;  // 30% hash overhead

    // Frame index: base entry + page slot refs per frame
    // Typical: 1-5 dirty pages per frame for ZX
    r.frameIndexBytesPerFrame = 36 + pagesPerFrame * 4;

    // Total index for 5-minute session
    r.totalIndexForSession = r.pageIndexBytes + framesIn5Min * r.frameIndexBytesPerFrame;

    // Overhead as percentage of hot buffer
    r.overheadPercent = 100.0 * r.totalIndexForSession / (hotBufferMB * 1024 * 1024);

    return r;
}

void PrintOverheadTable()
{
    std::cout << "# v2 Format Index Overhead Analysis\n\n";

    std::cout << "## Assumptions\n\n";
    std::cout << "- Page size: 4KB\n";
    std::cout << "- Session: 5 minutes @ 50fps = 15000 frames\n";
    std::cout << "- Dirty pages/frame: ~2 (typical ZX workload)\n";
    std::cout << "- PageSlotEntry: 20 bytes\n";
    std::cout << "- FrameIndexEntry: 36 bytes base + 4 bytes per page ref\n\n";

    const size_t framesIn5Min = 15000;
    const size_t dirtyPagesPerFrame = 2;
    const size_t totalRamPages = 256;  // 4MB / 16KB

    std::cout << "## Hot Buffer Sizing\n\n";
    std::cout << "| Hot Buffer | Page Capacity | Page Index | Frame Index | Total Index | Overhead |\n";
    std::cout << "|------------|---------------|------------|-------------|-------------|----------|\n";

    for (size_t mb : {64, 128, 256, 512, 1024, 2048}) {
        auto r = CalculateOverhead(mb, framesIn5Min, dirtyPagesPerFrame, totalRamPages);

        std::cout << "| " << std::setw(8) << mb << "MB"
                  << " | " << std::setw(13) << r.pageCapacity
                  << " | " << std::setw(8) << r.pageIndexBytes / (1024 * 1024) << "MB"
                  << " | " << std::setw(9) << (framesIn5Min * r.frameIndexBytesPerFrame) / 1024 << "KB"
                  << " | " << std::setw(9) << r.totalIndexForSession / (1024 * 1024) << "MB"
                  << " | " << std::setw(8) << std::fixed << std::setprecision(1) << r.overheadPercent << "% |\n";
    }

    std::cout << "\n## Scaling Analysis\n\n";

    // Per-machine breakdown
    struct MachineConfig {
        const char* name;
        size_t ramKB;
        size_t dirtyPagesPerFrame;
        size_t peripheralKB;
    };

    MachineConfig machines[] = {
        {"ZX-48K",        48,   1,    0},
        {"Pentagon-128K", 128,  1,    0},
        {"ZX-Evo",        4096, 1,    0},
        {"ZX-Evo + GS",   4096, 1,  512},
    };

    std::cout << "| Machine | RAM Pages | Periph Pages | Index/Frame | 5min Index |\n";
    std::cout << "|---------|-----------|--------------|-------------|------------|\n";

    for (const auto& m : machines) {
        size_t ramPages = (m.ramKB * 1024) / 4096;
        size_t periphPages = (m.peripheralKB * 1024) / 4096;
        size_t totalPages = ramPages + periphPages;

        // Frame index entry size
        size_t frameIndexBytes = 36 + m.dirtyPagesPerFrame * 4;
        size_t sessionIndex = framesIn5Min * frameIndexBytes;

        std::cout << "| " << std::setw(13) << std::left << m.name
                  << " | " << std::setw(9) << std::right << ramPages
                  << " | " << std::setw(12) << periphPages
                  << " | " << std::setw(11) << frameIndexBytes << "B"
                  << " | " << std::setw(10) << sessionIndex / 1024 << "KB |\n";
    }

    std::cout << "\n## Hot Buffer Eviction Strategy\n\n";
    std::cout << "When hot buffer fills:\n";
    std::cout << "1. Find oldest pages not referenced by recent N frames\n";
    std::cout << "2. Flush to disk, keep file offset in PageSlotEntry\n";
    std::cout << "3. On SeekTo: check if page in hot buffer, else read from disk\n\n";

    std::cout << "## Recommended Configuration\n\n";
    std::cout << "| Use Case | Hot Buffer | Reasoning |\n";
    std::cout << "|----------|------------|----------|\n";
    std::cout << "| 48K/128K casual | 64MB | 15000+ pages, no eviction needed |\n";
    std::cout << "| ZX-Evo gaming | 256MB | ~60000 pages, covers 5+ minutes |\n";
    std::cout << "| ZX-Evo + GS | 512MB | 512KB GS pages need space |\n";
    std::cout << "| Extended session | 1024MB+ | 10+ minute sessions |\n\n";

    std::cout << "## Index Memory Formula\n\n";
    std::cout << "```\n";
    std::cout << "PageIndex = (HotBufferMB * 256) * 26 bytes  // 256 pages/MB, 26B/entry with overhead\n";
    std::cout << "FrameIndex = NumFrames * (36 + 4 * DirtyPages) bytes\n";
    std::cout << "TotalIndex = PageIndex + FrameIndex\n";
    std::cout << "```\n\n";

    // Show formula results
    std::cout << "**Example: 256MB buffer, 5 min session, 2 dirty pages/frame:**\n";
    std::cout << "- PageIndex = 256 * 256 * 26 = 1.7MB\n";
    std::cout << "- FrameIndex = 15000 * 44 = 660KB\n";
    std::cout << "- Total = ~2.3MB (0.9% overhead)\n";
}

int main()
{
    PrintOverheadTable();
    return 0;
}
