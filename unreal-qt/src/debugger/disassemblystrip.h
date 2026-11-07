#pragma once

/// @file disassemblystrip.h
/// @brief The rows a disassembly view keeps for reversible scrolling (docs/inprogress/2026-11-05-disasm-virtual-scroll
/// §3.4): a strip of a few screens of rows of `disasm::Window` and the index of the top row shown. Scrolling inside
/// the strip reuses its rows, so up N then down N shows the same rows; past its ends it grows by `Rows` /
/// `RowsBefore`. It is dropped when what it was decoded from changed (`Validate`).
///
/// Qt-free: the view paints the rows, the strip only keeps them.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "debugger/disassembler/disassemblywindow.h"

class DisassemblyStrip
{
public:
    /// The most rows kept: past it the end farther from the top is dropped
    static constexpr size_t kMaxRows = 768;

    /// Drops the strip when it no longer matches the machine: another markup @p revision, other bytes or data ranges
    /// or anchors (a 64-bit hash), PC inside it where no row starts, or a row cut by the PC of the last check that
    /// PC left. False when dropped (or empty)
    bool Validate(const disasm::Source& source, uint64_t revision);
    void Clear();
    bool Empty() const { return _rows.empty(); }

    /// A new strip of @p count rows from @p address on, the first one the top
    void ResetAt(disasm::Window& window, const disasm::Source& source, uint64_t revision, uint16_t address,
                 size_t count);
    /// A new strip of @p before rows above @p address and @p after rows from it; the top is the first of them
    void ResetAround(disasm::Window& window, const disasm::Source& source, uint64_t revision, uint16_t address,
                     size_t before, size_t after);

    /// Rows exist from @p above rows above the top to @p below rows past it (fewer at the ends of the 64K)
    void Ensure(disasm::Window& window, const disasm::Source& source, size_t above, size_t below);
    /// Moves the top by @p delta rows, @p visible rows kept below it; the rows moved
    int Scroll(disasm::Window& window, const disasm::Source& source, int delta, size_t visible);
    /// The index @p delta rows from @p index (rows added above shift every index: the one returned counts them)
    int Step(disasm::Window& window, const disasm::Source& source, int index, int delta);

    /// The index of the row at @p address, -1 when no row of the strip starts there
    int IndexOf(uint16_t address) const;
    size_t Top() const { return _top; }
    void SetTop(size_t index);
    uint16_t TopAddress() const { return _rows.empty() ? 0 : _rows[_top].address; }
    const std::vector<disasm::Row>& Rows() const { return _rows; }

private:
    /// Rows added above the first one / past the last one; the count added
    size_t ExtendUp(disasm::Window& window, size_t count);
    size_t ExtendDown(disasm::Window& window, size_t count);
    /// Keeps kMaxRows around the top, then takes the hash of what the strip covers now
    void Settle(const disasm::Source& source);
    /// The hash of the bytes, data ranges and anchors (PC aside) under the strip; the PC anchor found in @p pc
    uint64_t Fingerprint(const disasm::Source& source, disasm::Marks& marks, int32_t& pc) const;

    std::vector<disasm::Row> _rows;
    size_t _top = 0;
    uint64_t _hash = 0;
    uint64_t _revision = 0;
    int32_t _pc = -1;  ///< PC at the last check, -1: none or outside the strip
};
