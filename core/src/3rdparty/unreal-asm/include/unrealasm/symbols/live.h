#pragma once

// Label tables of assemblers in the machine's RAM (symbols/formats.md §4, symbols/tdd.md §4.3,
// research-labeltables.md): a scanner looks for its assembler's table in a copy of the RAM pages and reads it into a
// symbol set. The caller copies the pages at a coherent moment (the emulator's adapter, or dump files for symconv);
// the scan runs on the copy and touches nothing.

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/symbols/symbol.h"

namespace unrealasm::symbols
{
/// One RAM page (16 KB) by its number
struct MemoryPage
{
    uint16_t page = 0;
    std::span<const uint8_t> bytes;
};

/// The pages a scan may look at; pages not given are not scanned
struct MemoryView
{
    std::vector<MemoryPage> pages;
};

/// A place that holds a label table
struct LiveCandidate
{
    std::string scanner;     ///< the scanner's id ("alasm-table", "xas-table")
    std::string version;     ///< which layout ("5.0x", "4.4x", "7.x", "4.x")
    uint16_t page = 0;
    uint32_t offset = 0;     ///< the first byte of the table in the page
    uint32_t end = 0;        ///< the byte after it (the terminator)
    size_t count = 0;        ///< entries, symbols and others
    int score = 0;           ///< higher = more certain (the entries, plus the place the assembler is known to use)
};

struct LiveReadResult
{
    SymbolSet set;
    Diagnostics diagnostics;
    bool ok = false;
};

class ILiveScanner
{
public:
    virtual ~ILiveScanner() = default;
    virtual std::string_view Id() const = 0;
    virtual std::string_view Title() const = 0;
    /// Every table of this assembler found in the pages, best first
    virtual std::vector<LiveCandidate> Find(const MemoryView& memory) const = 0;
    /// The defined labels of a table Find gave
    virtual LiveReadResult Read(const MemoryView& memory, const LiveCandidate& candidate) const = 0;
};

/// Every built-in scanner (ALASM 4.4x / 5.0x, XAS 4.x / 7.x)
const std::vector<std::unique_ptr<ILiveScanner>>& LiveScanners();

/// The candidates of every scanner, best first
std::vector<LiveCandidate> FindLabelTables(const MemoryView& memory);

/// The labels of a candidate, read by the scanner that found it
LiveReadResult ReadLabelTable(const MemoryView& memory, const LiveCandidate& candidate);
}  // namespace unrealasm::symbols
