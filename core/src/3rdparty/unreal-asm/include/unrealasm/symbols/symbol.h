#pragma once

// The symbol model (symbols/architecture.md §3, symbols/tdd.md §2): one record for a name from any source - an
// assembler's symbol file, another tool's label file, a decoded source, a ROM bundle - with where it is (address space
// + offset), what it is, and where it came from. Symbols live in sets; a file holds sets.

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace unrealasm::symbols
{
enum class SpaceKind : uint8_t
{
    CpuView,    ///< whatever the CPU sees at the address now (a symbol valid in any page)
    Rom,        ///< a ROM page, wherever it is mapped
    Ram,        ///< a RAM page
    Cache,      ///< a cache RAM page (ZX-Evo, TS-Conf)
    Device,     ///< memory a device owns ("vram", "cram", "eeprom", ...)
    Constant,   ///< a number, not a place (an EQU used as a size)
    Port,       ///< an I/O port
};

/// Where a location is. Written (architecture.md §3.1): "cpu:main", "rom2", "ram3", "cache0", "vram", "const", "port";
/// another CPU's spaces carry its name: "cpu:gs", "gs.rom0", "gs.vram"
struct AddressSpace
{
    std::string cpu = "main";
    SpaceKind kind = SpaceKind::CpuView;
    uint16_t page = 0;          ///< Rom / Ram / Cache
    std::string region;         ///< Device

    std::string Format() const;
    /// false when the text is no space spelling
    static bool Parse(std::string_view text, AddressSpace& out);
    /// The largest offset + 1 the space takes (pages 16 KB, a CPU view 64 KB, ports 64 K, others unbounded: 0)
    uint32_t Extent() const;

    auto operator<=>(const AddressSpace&) const = default;
    bool operator==(const AddressSpace&) const = default;
};

struct Location
{
    AddressSpace space;
    uint32_t offset = 0;

    auto operator<=>(const Location&) const = default;
    bool operator==(const Location&) const = default;
};

enum class SymbolKind : uint8_t
{
    Unknown,
    Code,       ///< a label on an instruction
    Data,       ///< a label on DB / DW / DS
    Const,      ///< EQU / = of a number
    Port,
    Entry,      ///< a documented entry point (ROM maps)
    Local,      ///< a local label (.loop, @1, 1$); its parent is in `parent`
};

std::string_view KindName(SymbolKind kind);
/// "code", "data", ... (case-insensitive); false when unknown
bool ParseKind(std::string_view text, SymbolKind& out);

struct SourceRef
{
    std::string file;
    uint32_t line = 0;          ///< 1-based; 0 = unknown
    uint32_t column = 0;        ///< 1-based; 0 = unknown

    bool operator==(const SourceRef&) const = default;
};

struct Provenance
{
    std::string importer;       ///< the codec that read it ("sjasmplus-sym", "native", ...)
    std::string raw;            ///< the original line or entry
    uint32_t line = 0;          ///< the line of the imported file (diagnostics, conflicts)

    bool operator==(const Provenance&) const = default;
};

/// A missing field means "unknown" (size 0, line 0, an empty string)
struct Symbol
{
    std::string name;
    Location location;
    SymbolKind kind = SymbolKind::Unknown;
    uint32_t size = 0;
    std::string parent;         ///< the scope a local symbol belongs to ("" = global)
    std::string module;
    SourceRef source;
    std::string comment;
    std::vector<std::string> aliases;
    bool enabled = true;
    Provenance provenance;
    std::string extra;          ///< members of the native file this version does not know (a JSON object text), written back

    bool operator==(const Symbol&) const = default;
};

enum class CaseRule : uint8_t
{
    Exact,      ///< names compare byte-wise
    Fold,       ///< ASCII letters compare without case (tools that ignore case)
};

struct Origin
{
    std::string kind;           ///< "file", "live", "bundle", "user"
    std::string where;          ///< a path, "ram6:#C000", a bundle id
    std::string sha256;

    bool operator==(const Origin&) const = default;
};

struct SymbolSet
{
    std::string id;
    std::string title;
    Origin origin;
    int priority = 100;         ///< higher wins in the resolved view
    bool enabled = true;
    CaseRule caseRule = CaseRule::Exact;
    std::vector<Symbol> symbols;
    std::string extra;          ///< unknown members of the native file (a JSON object text)

    bool operator==(const SymbolSet&) const = default;
};

/// What a symbol file holds: the native file several sets, every other format one
struct SymbolFile
{
    std::vector<SymbolSet> sets;
    std::string extra;          ///< unknown top-level members of the native file

    bool operator==(const SymbolFile&) const = default;
};
}  // namespace unrealasm::symbols
