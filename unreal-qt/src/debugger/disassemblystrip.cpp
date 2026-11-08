#include "disassemblystrip.h"

#include <algorithm>

#include "emulator/cpu/z80.h"

namespace
{
/// Rows fetched beyond what a move needs: fewer, larger extensions while scrolling on
constexpr size_t kChunk = 64;

/// FNV-1a, 64 bits
void Mix(uint64_t& hash, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
    {
        hash ^= (value >> (i * 8)) & 0xFF;
        hash *= 0x100000001B3ull;
    }
}
}  // namespace

void DisassemblyStrip::Clear()
{
    _rows.clear();
    _top = 0;
    _hash = 0;
    _pc = -1;
}

uint64_t DisassemblyStrip::Fingerprint(const disasm::Source& source, disasm::Marks& marks, int32_t& pc) const
{
    const uint16_t from = _rows.front().address;
    const uint16_t to = _rows.back().End();
    source.MarksOf(from, to, marks);
    const Z80Registers* registers = source.Registers();
    pc = registers && registers->pc >= from && registers->pc <= to ? registers->pc : -1;

    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint32_t address = from; address <= to; ++address)
        hash = (hash ^ source.Read(static_cast<uint16_t>(address))) * 0x100000001B3ull;
    for (const disasm::Range& range : marks.data)
        Mix(hash, static_cast<uint32_t>(range.lo) << 16 | range.hi);
    // PC moves on every step: it only has to start a row (Validate), its anchor would drop the strip each time
    for (uint16_t anchor : marks.anchors)
        if (anchor != pc)
            Mix(hash, anchor | 0x10000u);
    return hash;
}

bool DisassemblyStrip::Validate(const disasm::Source& source, uint64_t revision)
{
    if (_rows.empty())
        return false;
    disasm::Marks marks;
    int32_t pc = -1;
    bool valid = revision == _revision && Fingerprint(source, marks, pc) == _hash;
    if (valid && pc >= 0 && IndexOf(static_cast<uint16_t>(pc)) < 0)
        valid = false;  // PC runs mid-row: the rows there resync at it
    if (valid && _pc >= 0 && _pc != pc)
    {
        // The row the PC of the last check cut short is a whole instruction again
        const int index = IndexOf(static_cast<uint16_t>(_pc));
        if (index > 0 && _rows[static_cast<size_t>(index - 1)].cut)
            valid = false;
    }
    if (!valid)
    {
        Clear();
        return false;
    }
    _pc = pc;
    return true;
}

void DisassemblyStrip::ResetAt(disasm::Window& window, const disasm::Source& source, uint64_t revision,
                               uint16_t address, size_t count)
{
    Clear();
    _revision = revision;
    _rows = window.Rows(address, std::max<size_t>(count, 1));
    Settle(source);
}

void DisassemblyStrip::ResetAround(disasm::Window& window, const disasm::Source& source, uint64_t revision,
                                   uint16_t address, size_t before, size_t after)
{
    Clear();
    _revision = revision;
    _rows = window.RowsBefore(address, before);
    _top = _rows.size();
    std::vector<disasm::Row> forward = window.Rows(address, std::max<size_t>(after, 1));
    _rows.insert(_rows.end(), std::make_move_iterator(forward.begin()), std::make_move_iterator(forward.end()));
    _top = std::min(_top, _rows.size() - 1);
    Settle(source);
}

size_t DisassemblyStrip::ExtendUp(disasm::Window& window, size_t count)
{
    if (_rows.empty() || _rows.front().address == 0 || count == 0)
        return 0;
    std::vector<disasm::Row> above = window.RowsBefore(_rows.front().address, count);
    _rows.insert(_rows.begin(), std::make_move_iterator(above.begin()), std::make_move_iterator(above.end()));
    _top += above.size();
    return above.size();
}

size_t DisassemblyStrip::ExtendDown(disasm::Window& window, size_t count)
{
    if (_rows.empty() || count == 0)
        return 0;
    const uint32_t next = static_cast<uint32_t>(_rows.back().address) + _rows.back().length;
    if (next > 0xFFFF)
        return 0;
    std::vector<disasm::Row> below = window.Rows(static_cast<uint16_t>(next), count);
    _rows.insert(_rows.end(), std::make_move_iterator(below.begin()), std::make_move_iterator(below.end()));
    return below.size();
}

void DisassemblyStrip::Settle(const disasm::Source& source)
{
    if (_rows.empty())
        return;
    if (_rows.size() > kMaxRows)
    {
        // Half of what is kept above the top, the rest below it
        const size_t front = _top > kMaxRows / 2 ? _top - kMaxRows / 2 : 0;
        _rows.erase(_rows.begin(), _rows.begin() + static_cast<std::ptrdiff_t>(front));
        _top -= front;
        if (_rows.size() > kMaxRows)
            _rows.resize(kMaxRows);
    }
    disasm::Marks marks;
    _hash = Fingerprint(source, marks, _pc);
}

void DisassemblyStrip::Ensure(disasm::Window& window, const disasm::Source& source, size_t above, size_t below)
{
    if (_rows.empty())
        return;
    bool grown = false;
    if (_top < above)
        grown |= ExtendUp(window, above - _top + kChunk) > 0;
    if (_rows.size() - _top <= below)
        grown |= ExtendDown(window, _top + below + 1 - _rows.size() + kChunk) > 0;
    if (grown)
        Settle(source);
}

int DisassemblyStrip::Scroll(disasm::Window& window, const disasm::Source& source, int delta, size_t visible)
{
    if (_rows.empty() || delta == 0)
        return 0;
    if (delta < 0)
    {
        const auto need = static_cast<size_t>(-delta);
        if (_top < need)
            ExtendUp(window, need - _top + kChunk);
    }
    else if (_top + static_cast<size_t>(delta) + visible > _rows.size())
        ExtendDown(window, _top + static_cast<size_t>(delta) + visible - _rows.size() + kChunk);
    const auto from = static_cast<int64_t>(_top);
    const int64_t to = std::clamp<int64_t>(from + delta, 0, static_cast<int64_t>(_rows.size()) - 1);
    _top = static_cast<size_t>(to);
    Settle(source);
    return static_cast<int>(to - from);
}

int DisassemblyStrip::Step(disasm::Window& window, const disasm::Source& source, int index, int delta)
{
    if (_rows.empty())
        return index;
    int64_t target = static_cast<int64_t>(index) + delta;
    if (target < 0)
        target += static_cast<int64_t>(ExtendUp(window, static_cast<size_t>(-target) + kChunk));
    if (target >= static_cast<int64_t>(_rows.size()))
        ExtendDown(window, static_cast<size_t>(target) + 1 - _rows.size() + kChunk);
    target = std::clamp<int64_t>(target, 0, static_cast<int64_t>(_rows.size()) - 1);

    // Trimming keeps the rows around the top: the target is near it (a cursor row on the screen)
    const uint16_t address = _rows[static_cast<size_t>(target)].address;
    Settle(source);
    const int found = IndexOf(address);
    return found >= 0 ? found : static_cast<int>(_top);
}

int DisassemblyStrip::IndexOf(uint16_t address) const
{
    const auto it = std::lower_bound(_rows.begin(), _rows.end(), address,
                                     [](const disasm::Row& row, uint16_t value) { return row.address < value; });
    return it != _rows.end() && it->address == address ? static_cast<int>(it - _rows.begin()) : -1;
}

void DisassemblyStrip::SetTop(size_t index)
{
    if (!_rows.empty())
        _top = std::min(index, _rows.size() - 1);
}
