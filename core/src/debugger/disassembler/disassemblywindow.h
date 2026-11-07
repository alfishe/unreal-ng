#pragma once

/// @file disassemblywindow.h
/// @brief A virtual window over a CPU's 64K, disassembled on demand (docs/inprogress/2026-11-05-disasm-virtual-scroll,
/// §3): rows from an address forward, rows that end at an address (a local backward sync, never a global listing),
/// and the instruction start a scrollbar drag snaps to. Shared by the Qt view, the TUI, eCat and the planes.
///
/// What is decoded is a function of the bytes as mapped now and the marks of the stretch (anchors: addresses known
/// to start an instruction; data ranges: shown as `db` rows), so the same top address always gives the same rows:
///   - forward: one instruction after another; an instruction that would run across a boundary (an anchor, the
///     start or the end of a data range) is cut, its bytes up to the boundary a `db` row, and decoding resyncs there;
///     a data range gives `db` rows of at most kDataRowBytes, counted from its start or the last boundary in it;
///   - backward: forward from a certain start (a boundary at most kSyncReach bytes below), else from every start of
///     the kSyncSpan bytes below, keeping the chains that land on the address; the one most of them agree on wins.
///
/// @code
///   EmulatorDisassemblySource source(context);                  // the main CPU, its labels, breakpoints and markup
///   DisassemblyWindow window(*disassembler, source);
///   std::vector<disasm::Row> rows = window.Around(pc, 8, 24);  // the "follow PC" view
///   uint16_t top = window.SnapToInstruction(dragged);          // a scrollbar drag
/// @endcode
///
/// Stateless besides the source: a client keeps the strip of rows it shows (§3.4) for reversible scrolling.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/disassembler/z80disasm.h"

class EmulatorContext;
class Memory;
struct Z80Registers;

namespace disasm
{

enum class RowKind : uint8_t
{
    Instruction,
    Data,  ///< a data range's bytes, or an instruction's bytes cut by a boundary
};

constexpr uint8_t kDataRowBytes = 8;  ///< bytes per `db` row of a data range
constexpr uint16_t kSyncReach = 64;   ///< how far below an address a certain start is looked for
constexpr uint16_t kSyncSpan = 32;    ///< the start offsets tried below an address without one

struct Row
{
    uint16_t address = 0;
    uint8_t length = 1;  ///< 1-4 for an instruction, 1-kDataRowBytes for data
    RowKind kind = RowKind::Instruction;
    bool cut = false;    ///< a data row of an instruction cut short by a boundary
    uint8_t bytes[kDataRowBytes] = {};
    std::string mnemonic;         ///< "ld a,#01"; "db #3E,#01" for data
    DecodedInstruction decoded;   ///< instruction rows: jump targets, runtime hints

    uint16_t End() const { return static_cast<uint16_t>(address + length - 1); }
    bool operator==(const Row& other) const
    {
        return address == other.address && length == other.length && kind == other.kind && mnemonic == other.mnemonic;
    }
};

/// An inclusive range of CPU addresses
struct Range
{
    uint16_t lo = 0;
    uint16_t hi = 0;
};

/// What is known about a stretch of the 64K
struct Marks
{
    std::vector<uint16_t> anchors;  ///< instruction starts for certain, ascending
    std::vector<Range> data;        ///< data ranges overlapping the stretch, ascending, not overlapping
};

/// The bytes and marks a window reads: one CPU's 64K as mapped now
class Source
{
public:
    virtual ~Source() = default;
    virtual uint8_t Read(uint16_t address) const = 0;
    /// The marks of @p from..@p to (from <= to)
    virtual void MarksOf(uint16_t from, uint16_t to, Marks& out) const = 0;
    /// The CPU state the runtime hints read ("; HL=#4000"); null for none
    virtual Z80Registers* Registers() const { return nullptr; }
    virtual Memory* RuntimeMemory() const { return nullptr; }
};

/// The main CPU: bytes by direct reads (no side effects), anchors from PC, labels (code ones), execution breakpoints
/// and the memory markup's instruction starts, data ranges from the markup's non-code spans (the part from PC on
/// excluded: PC runs there). Without @p markup (the GUI's metadata switched off) the markup gives nothing: PC, labels
/// and breakpoints anchor alone, no data ranges
class EmulatorSource : public Source
{
public:
    explicit EmulatorSource(EmulatorContext* context, bool markup = true);
    uint8_t Read(uint16_t address) const override;
    void MarksOf(uint16_t from, uint16_t to, Marks& out) const override;
    Z80Registers* Registers() const override;
    Memory* RuntimeMemory() const override;

private:
    EmulatorContext* _context = nullptr;
    bool _markup = true;
};

/// Bytes and marks given directly (the tests, a card CPU's peeks)
class BufferSource : public Source
{
public:
    explicit BufferSource(std::vector<uint8_t> memory, Marks marks = {});
    uint8_t Read(uint16_t address) const override;
    void MarksOf(uint16_t from, uint16_t to, Marks& out) const override;

private:
    std::vector<uint8_t> _memory;  ///< 64K
    Marks _marks;
};

class Window
{
public:
    Window(Z80Disassembler& disassembler, const Source& source);

    /// Up to @p count rows from @p address forward; the rows stop at #FFFF (no wrap)
    std::vector<Row> Rows(uint16_t address, size_t count);
    /// Up to @p count rows that end right before @p address, in address order; fewer at #0000
    std::vector<Row> RowsBefore(uint16_t address, size_t count);
    /// @p before rows above @p address, then @p after rows from it
    std::vector<Row> Around(uint16_t address, size_t before, size_t after);
    /// The start of the row that holds @p address (the address itself when a row starts there)
    uint16_t SnapToInstruction(uint16_t address);

private:
    /// The marks of a stretch with the boundary lookups decoding needs
    struct Stretch
    {
        uint16_t from = 0;
        uint16_t to = 0;
        Marks marks;
        /// The row lengths decoded so far, by offset from `from` (0: not yet); chains converge, so most repeat
        mutable std::vector<uint8_t> lengths;
        /// The data range holding @p address, null outside one
        const Range* DataAt(uint16_t address) const;
        /// The first boundary above @p address (an anchor, a data range's start or the byte after its end), 0x10000
        /// when none in the stretch
        uint32_t NextBoundary(uint16_t address) const;
        /// The last certain start at or below @p address in the stretch, -1 when none
        int32_t LastCertainStart(uint16_t address) const;
    };

    Stretch StretchOf(uint16_t from, uint16_t to) const;
    /// The row at @p address; @p full: with the mnemonic (a chain search needs the length only)
    Row RowAt(const Stretch& stretch, uint16_t address, bool full);
    /// The length of the row at @p address (cached in the stretch)
    uint8_t LengthAt(const Stretch& stretch, uint16_t address);
    /// The rows of the chain from @p start up to (not including) the first one at or past @p until; true when it
    /// lands on @p until exactly
    bool Chain(const Stretch& stretch, uint16_t start, uint16_t until, std::vector<uint16_t>* starts);
    /// The start of the chain that best leads to @p address: a certain start's when it lands there, else the one
    /// most start offsets agree on; @p lands: false when none lands (the address is mid-row)
    uint16_t BestStart(const Stretch& stretch, uint16_t address, bool& lands);

    Z80Disassembler& _disassembler;
    const Source& _source;
};

}  // namespace disasm
