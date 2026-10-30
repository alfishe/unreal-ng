#include "memorycounters.h"

#include <algorithm>
#include <cctype>
#include <queue>

#include "debugger/memory/memoryread.h"
#include "debugger/search/memorysearch.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/platform.h"

namespace MemoryCounters
{
namespace
{
constexpr uint32_t kFirstCachePage = MAX_RAM_PAGES;
constexpr uint32_t kFirstMiscPage = kFirstCachePage + MAX_CACHE_PAGES;
constexpr uint32_t kFirstRomPage = kFirstMiscPage + MAX_MISC_PAGES;

/// A space resolved into one counter array: the Z80's or a span of the physical one
struct Space
{
    std::string name;
    bool cpu = false;
    size_t base = 0;     // physical: the space's first cell in the physical counters
    uint32_t size = 0;   // cells
};

bool Resolve(EmulatorContext* context, const std::string& text, Space& space, std::string& error)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "phys")
    {
        space.name = "phys";
        space.size = static_cast<uint32_t>(PAGE_SIZE) * MAX_PAGES;
        return true;
    }
    MemorySearchRequest where;
    if (!MemorySearch::ParseSpace(text, where, error))
    {
        error = "space must be cpu, phys, ram or a page (ram5, rom2, cache0); " + error;
        return false;
    }
    space.name = MemorySearch::SpaceName(where);
    switch (where.space)
    {
        case MemorySearchRequest::Space::Cpu:
            space.cpu = true;
            space.size = 0x10000;
            return true;
        case MemorySearchRequest::Space::AllRam:
            space.size = MemorySearch::RamPageCount(context) * static_cast<uint32_t>(PAGE_SIZE);
            return true;
        case MemorySearchRequest::Space::Page:
        {
            if (!MemorySearch::PageHost(context, where.pageType, where.page))
            {
                error = "this machine has no page " + space.name;
                return false;
            }
            uint32_t absPage = where.page;
            if (where.pageType == BANK_CACHE)
                absPage += kFirstCachePage;
            else if (where.pageType == BANK_ROM)
                absPage += kFirstRomPage;
            space.base = static_cast<size_t>(absPage) * PAGE_SIZE;
            space.size = static_cast<uint32_t>(PAGE_SIZE);
            return true;
        }
        case MemorySearchRequest::Space::Region:
            break;
    }
    error = "space must be cpu, phys, ram or a page (ram5, rom2, cache0); device memories (" + text +
            ") have no access counters";
    return false;
}

const char* SessionName(ProfilerSessionState state)
{
    switch (state)
    {
        case ProfilerSessionState::Capturing: return "capturing";
        case ProfilerSessionState::Paused: return "paused";
        default: return "stopped";
    }
}

uint64_t Weight(const Cell& cell, uint8_t kinds)
{
    return ((kinds & kRead) ? uint64_t{cell.reads} : 0) + ((kinds & kWrite) ? uint64_t{cell.writes} : 0) +
           ((kinds & kExecute) ? uint64_t{cell.executes} : 0);
}

/// Lighter first: the top of the heap is the cell to drop
struct Lighter
{
    uint8_t kinds;
    bool operator()(const Cell& a, const Cell& b) const
    {
        const uint64_t wa = Weight(a, kinds);
        const uint64_t wb = Weight(b, kinds);
        return wa != wb ? wa > wb : a.address < b.address;
    }
};
}  // namespace

Result Read(EmulatorContext* context, const Request& request)
{
    Result result;
    Memory* memory = context ? context->pMemory : nullptr;
    if (!memory)
    {
        result.error = "memory not available";
        return result;
    }
    if (request.kinds == 0 || (request.kinds & ~kAll) != 0)
    {
        result.error = "kinds must be r, w, x or a mix of them";
        return result;
    }
    Space space;
    if (!Resolve(context, request.space, space, result.error))
        return result;
    result.space = space.name;
    result.spaceSize = space.size;

    if (request.addresses.empty())
    {
        if (request.start >= space.size)
        {
            result.error = "the start is past the end of " + space.name;
            return result;
        }
        result.start = request.start;
        result.length = request.length == 0 ? space.size - request.start
                                            : std::min<uint32_t>(request.length, space.size - request.start);
        if (request.mode == Mode::Dense && result.length > kMaxDense)
        {
            result.error = "a dense read is at most 1048576 cells; ask sparse or top, or a shorter window";
            return result;
        }
        if (request.mode != Mode::Dense && (request.limit == 0 || request.limit > kMaxLimit))
        {
            result.error = "the limit must be 1 to 1048576";
            return result;
        }
    }
    else
    {
        if (request.addresses.size() > kMaxAddresses)
        {
            result.error = "at most 65536 addresses in one read";
            return result;
        }
        for (uint32_t address : request.addresses)
        {
            if (address >= space.size)
            {
                result.error = "address " + std::to_string(address) + " is past the end of " + space.name;
                return result;
            }
        }
        result.length = space.size;
    }

    MemoryAccessTracker& tracker = memory->GetAccessTracker();
    result.session = SessionName(tracker.GetMemorySessionState());
    const MemoryAccessTracker::CountersView view = space.cpu ? tracker.Z80CountersView() : tracker.PhysCountersView();
    result.allocated = view.reads != nullptr;
    auto cellAt = [&](uint32_t address) {
        Cell cell;
        cell.address = address;
        const size_t at = space.base + address;
        if (result.allocated && at < view.size)
        {
            cell.reads = view.reads[at];
            cell.writes = view.writes[at];
            cell.executes = view.executes[at];
        }
        return cell;
    };
    auto add = [&](const Cell& cell) {
        result.reads += cell.reads;
        result.writes += cell.writes;
        result.executes += cell.executes;
    };

    if (!request.addresses.empty())
    {
        result.cells.reserve(request.addresses.size());
        for (uint32_t address : request.addresses)
        {
            result.cells.push_back(cellAt(address));
            add(result.cells.back());
        }
        return result;
    }

    const uint32_t end = result.start + result.length;
    if (request.mode == Mode::Dense)
    {
        result.cells.reserve(result.length);
        for (uint32_t address = result.start; address < end; address++)
        {
            result.cells.push_back(cellAt(address));
            add(result.cells.back());
        }
        return result;
    }
    if (!result.allocated)
        return result;

    const uint64_t min = std::max<uint32_t>(request.min, 1);
    if (request.mode == Mode::Sparse)
    {
        for (uint32_t address = result.start; address < end; address++)
        {
            const Cell cell = cellAt(address);
            add(cell);
            if (Weight(cell, request.kinds) < min)
                continue;
            result.matched++;
            if (result.cells.size() < request.limit)
                result.cells.push_back(cell);
        }
        result.truncated = result.matched > result.cells.size();
        return result;
    }

    // Top: a bounded heap of the heaviest
    std::priority_queue<Cell, std::vector<Cell>, Lighter> heap(Lighter{request.kinds});
    for (uint32_t address = result.start; address < end; address++)
    {
        const Cell cell = cellAt(address);
        add(cell);
        if (Weight(cell, request.kinds) < min)
            continue;
        result.matched++;
        heap.push(cell);
        if (heap.size() > request.limit)
            heap.pop();
    }
    result.cells.resize(heap.size());
    for (size_t i = heap.size(); i-- > 0;)
    {
        result.cells[i] = heap.top();
        heap.pop();
    }
    result.truncated = result.matched > result.cells.size();
    return result;
}

Result ReadCell(EmulatorContext* context, const std::string& space, uint32_t address)
{
    Request request;
    request.space = space;
    request.addresses = {address};
    return Read(context, request);
}

bool ParseKinds(const std::string& text, uint8_t& kinds, std::string& error)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.empty() || lower == "all")
    {
        kinds = kAll;
        return true;
    }
    kinds = 0;
    for (char c : lower)
    {
        if (c == 'r')
            kinds |= kRead;
        else if (c == 'w')
            kinds |= kWrite;
        else if (c == 'x')
            kinds |= kExecute;
        else if (c != ',' && c != '+')
        {
            error = "kinds must be r, w, x or a mix of them (rw, rwx), or all; got '" + text + "'";
            return false;
        }
    }
    if (kinds == 0)
    {
        error = "kinds must be r, w, x or a mix of them (rw, rwx), or all; got '" + text + "'";
        return false;
    }
    return true;
}

bool ParseMode(const std::string& text, Mode& mode, std::string& error)
{
    if (text.empty() || text == "sparse")
        mode = Mode::Sparse;
    else if (text == "dense")
        mode = Mode::Dense;
    else if (text == "top")
        mode = Mode::Top;
    else
    {
        error = "format must be dense, sparse or top; got '" + text + "'";
        return false;
    }
    return true;
}

bool ParseAddresses(const std::string& text, std::vector<uint32_t>& addresses, std::string& error)
{
    addresses.clear();
    size_t from = 0;
    while (from <= text.size())
    {
        size_t comma = text.find_first_of(", ", from);
        if (comma == std::string::npos)
            comma = text.size();
        const std::string item = text.substr(from, comma - from);
        from = comma + 1;
        if (item.empty())
            continue;
        const size_t dash = item.find('-');
        uint32_t first = 0;
        uint32_t last = 0;
        if (!MemoryRead::ParseNumber(item.substr(0, dash), first) ||
            (dash != std::string::npos && !MemoryRead::ParseNumber(item.substr(dash + 1), last)))
        {
            error = "bad address '" + item + "' (0x5C00, #5C00, 5C00h, 23552 or a range 0x8000-0x80FF)";
            return false;
        }
        if (dash == std::string::npos)
            last = first;
        if (last < first)
        {
            error = "a range runs up: '" + item + "'";
            return false;
        }
        if (addresses.size() + (uint64_t{last} - first + 1) > kMaxAddresses)
        {
            error = "at most 65536 addresses in one read";
            return false;
        }
        for (uint64_t address = first; address <= last; address++)
            addresses.push_back(static_cast<uint32_t>(address));
    }
    if (addresses.empty())
    {
        error = "no address in '" + text + "'";
        return false;
    }
    return true;
}

std::string PhysPageName(uint32_t absPage)
{
    if (absPage < kFirstCachePage)
        return "ram" + std::to_string(absPage);
    if (absPage < kFirstMiscPage)
        return "cache" + std::to_string(absPage - kFirstCachePage);
    if (absPage < kFirstRomPage)
        return "misc" + std::to_string(absPage - kFirstMiscPage);
    return "rom" + std::to_string(absPage - kFirstRomPage);
}

std::string KindsName(uint8_t kinds)
{
    std::string name;
    if (kinds & kRead)
        name += 'r';
    if (kinds & kWrite)
        name += 'w';
    if (kinds & kExecute)
        name += 'x';
    return name;
}

const char* ModeName(Mode mode)
{
    switch (mode)
    {
        case Mode::Dense: return "dense";
        case Mode::Top: return "top";
        default: return "sparse";
    }
}
}  // namespace MemoryCounters
