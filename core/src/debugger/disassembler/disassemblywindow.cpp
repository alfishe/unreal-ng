#include "disassemblywindow.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/markup/memorymarkup.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace disasm
{

/// region <Sources>

EmulatorSource::EmulatorSource(EmulatorContext* context, bool markup) : _context(context), _markup(markup) {}

uint8_t EmulatorSource::Read(uint16_t address) const
{
    // Direct reads: the view must not strobe the ProfROM quadrant machine on #0000-#0003
    Memory* memory = _context ? _context->pMemory : nullptr;
    return memory ? memory->DirectReadFromZ80Memory(address) : 0xFF;
}

void EmulatorSource::MarksOf(uint16_t from, uint16_t to, Marks& out) const
{
    out = {};
    DebugManager* debug = _context ? _context->pDebugManager : nullptr;
    if (!debug)
        return;
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    const int32_t pc = z80 ? z80->pc : -1;
    const auto inStretch = [&](uint32_t address) { return address >= from && address <= to; };

    if (inStretch(pc))
        out.anchors.push_back(static_cast<uint16_t>(pc));
    if (LabelManager* labels = debug->GetLabelManager())
    {
        for (const auto& label : labels->GetLabelsInRange(from, to))
        {
            // A data or constant label names no instruction; a label of a page not mapped there now is not here
            if (!label || label->type == "data" || label->type == "const")
                continue;
            if (labels->GetLabelByZ80Address(label->address) == label)
                out.anchors.push_back(label->address);
        }
    }
    if (BreakpointManager* breakpoints = debug->GetBreakpointsManager())
    {
        for (const auto& pair : breakpoints->GetAllBreakpoints())
        {
            const BreakpointDescriptor* bp = pair.second.get();
            if (bp->type == BRK_MEMORY && (bp->memoryType & BRK_MEM_EXECUTE) && bp->active && !bp->isRange &&
                inStretch(bp->z80address))
                out.anchors.push_back(bp->z80address);
        }
    }
    if (markup::MemoryMarkup* markup = _markup ? debug->GetMemoryMarkup() : nullptr)
    {
        for (uint16_t anchor : markup->Anchors(from, to))
            out.anchors.push_back(anchor);
        for (const markup::Span& span : markup->Ranges(from, to))
        {
            if (span.kind == analysis::RegionKind::Code)
                continue;
            Range range{span.lo, span.hi};
            if (pc >= range.lo && pc <= range.hi)
            {
                // PC runs there: the part from PC on is code whatever a claim says
                if (pc == range.lo)
                    continue;
                range.hi = static_cast<uint16_t>(pc - 1);
            }
            // Neighbour spans of two claims are one data range: no boundary between them
            if (!out.data.empty() && out.data.back().hi + 1 == range.lo)
                out.data.back().hi = range.hi;
            else
                out.data.push_back(range);
        }
    }
    std::sort(out.anchors.begin(), out.anchors.end());
    out.anchors.erase(std::unique(out.anchors.begin(), out.anchors.end()), out.anchors.end());
}

Z80Registers* EmulatorSource::Registers() const
{
    return _context && _context->pCore ? _context->pCore->GetZ80() : nullptr;
}

Memory* EmulatorSource::RuntimeMemory() const
{
    return _context ? _context->pMemory : nullptr;
}

BufferSource::BufferSource(std::vector<uint8_t> memory, Marks marks) : _memory(std::move(memory)), _marks(std::move(marks))
{
    _memory.resize(0x10000, 0x00);
}

uint8_t BufferSource::Read(uint16_t address) const
{
    return _memory[address];
}

void BufferSource::MarksOf(uint16_t from, uint16_t to, Marks& out) const
{
    out = {};
    for (uint16_t anchor : _marks.anchors)
        if (anchor >= from && anchor <= to)
            out.anchors.push_back(anchor);
    for (const Range& range : _marks.data)
        if (range.hi >= from && range.lo <= to)
            out.data.push_back({std::max(range.lo, from), std::min(range.hi, to)});  // clipped, as the markup's
}

/// endregion </Sources>

/// region <Stretch>

const Range* Window::Stretch::DataAt(uint16_t address) const
{
    const auto it = std::upper_bound(marks.data.begin(), marks.data.end(), address,
                                     [](uint16_t value, const Range& range) { return value < range.lo; });
    if (it == marks.data.begin())
        return nullptr;
    const Range& range = *std::prev(it);
    return address <= range.hi ? &range : nullptr;
}

uint32_t Window::Stretch::NextBoundary(uint16_t address) const
{
    uint32_t next = 0x10000;
    const auto anchor = std::upper_bound(marks.anchors.begin(), marks.anchors.end(), address);
    if (anchor != marks.anchors.end())
        next = *anchor;
    for (const Range& range : marks.data)
    {
        if (range.lo > address)
        {
            next = std::min<uint32_t>(next, range.lo);
            break;
        }
        if (range.hi >= address)
        {
            next = std::min<uint32_t>(next, static_cast<uint32_t>(range.hi) + 1);
            break;
        }
    }
    return next;
}

int32_t Window::Stretch::LastCertainStart(uint16_t address) const
{
    int32_t last = -1;
    const auto anchor = std::upper_bound(marks.anchors.begin(), marks.anchors.end(), address);
    if (anchor != marks.anchors.begin())
        last = *std::prev(anchor);
    for (const Range& range : marks.data)
    {
        if (range.lo > address)
            break;
        last = std::max<int32_t>(last, range.lo);
        if (static_cast<uint32_t>(range.hi) + 1 <= address)
            last = std::max<int32_t>(last, range.hi + 1);
    }
    return last;
}

/// endregion </Stretch>

Window::Window(Z80Disassembler& disassembler, const Source& source) : _disassembler(disassembler), _source(source) {}

Window::Stretch Window::StretchOf(uint16_t from, uint16_t to) const
{
    Stretch stretch;
    stretch.from = from;
    stretch.to = to;
    _source.MarksOf(from, to, stretch.marks);
    stretch.lengths.assign(static_cast<size_t>(to - from) + 1, 0);
    return stretch;
}

uint8_t Window::LengthAt(const Stretch& stretch, uint16_t address)
{
    if (address < stretch.from || address > stretch.to)
        return RowAt(stretch, address, false).length;
    uint8_t& length = stretch.lengths[address - stretch.from];
    if (length == 0)
        length = RowAt(stretch, address, false).length;
    return length;
}

Row Window::RowAt(const Stretch& stretch, uint16_t address, bool full)
{
    Row row;
    row.address = address;
    const uint32_t boundary = stretch.NextBoundary(address);
    if (const Range* data = stretch.DataAt(address))
    {
        const uint32_t end = std::min<uint32_t>({data->hi, address + kDataRowBytes - 1u, boundary - 1});
        row.kind = RowKind::Data;
        row.length = static_cast<uint8_t>(end - address + 1);
    }
    else
    {
        std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
        for (size_t i = 0; i < buffer.size(); ++i)
            buffer[i] = _source.Read(static_cast<uint16_t>(address + i));
        uint8_t decodedLength = 0;
        _disassembler.disassembleSingleCommand(buffer, address, &decodedLength);
        const uint32_t length = decodedLength ? decodedLength : 1;
        const uint32_t limit = std::min<uint32_t>(boundary, 0x10000);
        if (address + length > limit)
        {
            // Runs across a boundary (or past #FFFF): its bytes up to there are data, decoding resyncs at it
            row.kind = RowKind::Data;
            row.cut = true;
            row.length = static_cast<uint8_t>(limit - address);
        }
        else
        {
            row.length = static_cast<uint8_t>(length);
            if (full)
            {
                uint8_t ignored = 0;
                row.mnemonic = _disassembler.disassembleSingleCommandWithRuntime(
                    buffer, address, &ignored, _source.Registers(), _source.RuntimeMemory(), &row.decoded);
            }
        }
    }
    for (uint8_t i = 0; i < row.length; ++i)
        row.bytes[i] = _source.Read(static_cast<uint16_t>(address + i));
    if (full && row.kind == RowKind::Data)
    {
        row.mnemonic = "db ";
        for (uint8_t i = 0; i < row.length; ++i)
        {
            char byte[5];
            std::snprintf(byte, sizeof(byte), i ? ",#%02X" : "#%02X", row.bytes[i]);
            row.mnemonic += byte;
        }
    }
    return row;
}

bool Window::Chain(const Stretch& stretch, uint16_t start, uint16_t until, std::vector<uint16_t>* starts)
{
    uint32_t address = start;
    while (address < until)
    {
        if (starts)
            starts->push_back(static_cast<uint16_t>(address));
        address += LengthAt(stretch, static_cast<uint16_t>(address));
    }
    return address == until;
}

uint16_t Window::BestStart(const Stretch& stretch, uint16_t address, bool& lands)
{
    lands = true;
    const uint16_t last = static_cast<uint16_t>(address - 1);
    const int32_t certain = stretch.LastCertainStart(last);

    // A data range: its rows count from the last boundary in it. The row of that count when it ends right before
    // `address`, else (entered mid-row, a go to address) the kDataRowBytes before it
    if (stretch.DataAt(last))
    {
        const auto aligned = static_cast<uint16_t>(certain + (last - certain) / kDataRowBytes * kDataRowBytes);
        if (aligned + LengthAt(stretch, aligned) == address)
            return aligned;
        return static_cast<uint16_t>(std::max<int32_t>(certain, address - kDataRowBytes));
    }
    if (certain >= stretch.from && Chain(stretch, static_cast<uint16_t>(certain), address, nullptr))
        return static_cast<uint16_t>(certain);

    // No certain start leads there: every start offset below votes for the row before `address`; Z80 chains
    // converge within a few instructions, so most agree. A start below a certain one would pass through it
    const int32_t lowest = std::max<int32_t>({certain + 1, stretch.from, address - kSyncSpan});
    std::map<uint16_t, std::pair<int, uint16_t>> votes;  // the row before address -> (votes, the lowest start)
    std::vector<uint16_t> starts;
    for (int32_t start = lowest; start < address; ++start)
    {
        starts.clear();
        if (!Chain(stretch, static_cast<uint16_t>(start), address, &starts))
            continue;
        auto& vote = votes[starts.back()];
        if (vote.first++ == 0)
            vote.second = static_cast<uint16_t>(start);
    }
    if (votes.empty())
    {
        lands = false;
        return last;
    }
    const auto best = std::max_element(votes.begin(), votes.end(), [](const auto& a, const auto& b) {
        return a.second.first < b.second.first;  // ties: the lower row (the first one found)
    });
    return best->second.second;
}

std::vector<Row> Window::Rows(uint16_t address, size_t count)
{
    std::vector<Row> rows;
    if (count == 0)
        return rows;
    const uint16_t to = static_cast<uint16_t>(std::min<size_t>(0xFFFF, address + (count + 1) * kDataRowBytes));
    const Stretch stretch = StretchOf(address, to);
    uint32_t current = address;
    while (rows.size() < count && current <= 0xFFFF)
    {
        rows.push_back(RowAt(stretch, static_cast<uint16_t>(current), true));
        current += rows.back().length;
    }
    return rows;
}

std::vector<Row> Window::RowsBefore(uint16_t address, size_t count)
{
    std::vector<Row> reversed;
    uint16_t current = address;
    while (reversed.size() < count && current > 0)
    {
        // The stretch reaches a little above `current`: an anchor there cuts the rows that lead to it
        const uint16_t from = current > kSyncReach ? static_cast<uint16_t>(current - kSyncReach) : 0;
        const uint16_t to = static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, current + kDataRowBytes));
        const Stretch stretch = StretchOf(from, to);
        bool lands = false;
        const uint16_t start = BestStart(stretch, current, lands);
        if (!lands)
        {
            // Mid-row whichever way: the byte before is a row of its own
            Row row;
            row.address = start;
            row.kind = RowKind::Data;
            row.cut = true;
            row.bytes[0] = _source.Read(start);
            char byte[8];
            std::snprintf(byte, sizeof(byte), "db #%02X", row.bytes[0]);
            row.mnemonic = byte;
            reversed.push_back(row);
            current = start;
            continue;
        }
        std::vector<uint16_t> starts;
        Chain(stretch, start, current, &starts);
        for (auto it = starts.rbegin(); it != starts.rend() && reversed.size() < count; ++it)
            reversed.push_back(RowAt(stretch, *it, true));
        current = starts.front();
    }
    return {reversed.rbegin(), reversed.rend()};
}

std::vector<Row> Window::Around(uint16_t address, size_t before, size_t after)
{
    std::vector<Row> rows = RowsBefore(address, before);
    std::vector<Row> forward = Rows(address, after);
    rows.insert(rows.end(), forward.begin(), forward.end());
    return rows;
}

uint16_t Window::SnapToInstruction(uint16_t address)
{
    const uint16_t from = address > kSyncReach ? static_cast<uint16_t>(address - kSyncReach) : 0;
    const uint16_t to = static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, address + kDataRowBytes));
    const Stretch stretch = StretchOf(from, to);
    const int32_t certain = stretch.LastCertainStart(address);
    if (certain == address)
        return address;
    if (stretch.DataAt(address))
    {
        // The row of the data range's chunks from its last boundary
        const int32_t base = std::max<int32_t>(certain, stretch.from);
        return static_cast<uint16_t>(base + (address - base) / kDataRowBytes * kDataRowBytes);
    }

    // The row holding `address` on the chain from each start below; the certain start's chain decides
    const auto holding = [&](uint16_t start) {
        uint32_t row = start;
        while (true)
        {
            const uint32_t next = row + LengthAt(stretch, static_cast<uint16_t>(row));
            if (next > address)
                return static_cast<uint16_t>(row);
            row = next;
        }
    };
    if (certain >= stretch.from)
        return holding(static_cast<uint16_t>(certain));
    std::map<uint16_t, int> votes;
    for (int32_t start = std::max<int32_t>(stretch.from, address - kSyncSpan); start <= address; ++start)
        ++votes[holding(static_cast<uint16_t>(start))];
    return std::max_element(votes.begin(), votes.end(),
                            [](const auto& a, const auto& b) { return a.second < b.second; })
        ->first;
}

}  // namespace disasm
