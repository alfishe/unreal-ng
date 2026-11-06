#include "debugger/markup/memorymarkup.h"

#include <algorithm>
#include <map>

#include "3rdparty/digestpp/digestpp.hpp"
#include "debugger/analyzers/analysis/analysisjobs.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/coverage/coverageanalyzer.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccessfacts.h"
#include "emulator/memory/memoryaccesstracker.h"

namespace markup
{

namespace
{
const char* const kLayerNames[kLayerCount] = {"bundle", "runtime", "analysis", "profile", "user"};

/// One claim cut to a window: the CPU range it covers there, who made it, where it starts in the CPU view
struct Piece
{
    uint16_t lo = 0;
    uint16_t hi = 0;
    int layer = 0;
    size_t index = 0;
    int32_t start = -1;  ///< the CPU address of the claim's first byte when it shows in this window, else -1
};

/// The page offsets of @p place minus @p cutLo..@p cutHi: none, one or two places
void Subtract(const Place& place, uint16_t cutLo, uint16_t cutHi, std::vector<Place>& out)
{
    if (cutHi < place.lo || cutLo > place.hi)
    {
        out.push_back(place);
        return;
    }
    if (cutLo > place.lo)
    {
        Place left = place;
        left.hi = static_cast<uint16_t>(cutLo - 1);
        out.push_back(left);
    }
    if (cutHi < place.hi)
    {
        Place right = place;
        right.lo = static_cast<uint16_t>(cutHi + 1);
        out.push_back(right);
    }
}

constexpr uint16_t kFirstCachePage = MAX_RAM_PAGES;
constexpr uint16_t kFirstMiscPage = MAX_RAM_PAGES + MAX_CACHE_PAGES;
constexpr uint16_t kFirstRomPage = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;

/// The absolute page (the tracker's, the analysis' PhysKey) of a RAM / ROM / cache page
uint16_t AbsolutePage(Space space, uint16_t page)
{
    return space == Space::Rom ? static_cast<uint16_t>(kFirstRomPage + page)
           : space == Space::Cache ? static_cast<uint16_t>(kFirstCachePage + page)
                                   : page;
}

/// The page of an absolute page; false for the misc page and past the table
bool PageOf(uint16_t absolute, Space& space, uint16_t& page)
{
    if (absolute < kFirstCachePage)
    {
        space = Space::Ram;
        page = absolute;
    }
    else if (absolute < kFirstMiscPage)
    {
        space = Space::Cache;
        page = static_cast<uint16_t>(absolute - kFirstCachePage);
    }
    else if (absolute >= kFirstRomPage && absolute < MAX_PAGES)
    {
        space = Space::Rom;
        page = static_cast<uint16_t>(absolute - kFirstRomPage);
    }
    else
        return false;
    return true;
}

constexpr uint8_t kCodeKinds = AccessKind::OpcodeFetch | AccessKind::OperandFetch;
constexpr uint8_t kDataKinds = AccessKind::DataRead | AccessKind::DataWrite | AccessKind::StackRead |
                               AccessKind::StackWrite | AccessKind::BlockAccess;
/// The longest Z80 instruction: executed addresses at most this far apart are one stretch of code
constexpr uint16_t kLongestInstruction = 4;
}  // namespace

LayerContent FromStructure(const analysis::L2Result& l2)
{
    LayerContent layer;
    layer.source = "analysis";
    for (const analysis::Region& region : l2.store.Regions())
    {
        Claim claim;
        if (region.kind == analysis::RegionKind::Untouched ||
            !PageOf(analysis::KeyPage(region.lo), claim.place.space, claim.place.page))
            continue;
        claim.place.lo = analysis::KeyOffset(region.lo);
        claim.place.hi = analysis::KeyOffset(region.hi);
        claim.kind = region.kind;
        claim.title = region.title.empty() ? region.label : region.title;
        claim.confidence = region.confidence;
        layer.claims.push_back(std::move(claim));
    }
    for (const analysis::CodeLine& line : l2.code)
    {
        Place place;
        if (!PageOf(analysis::KeyPage(line.key), place.space, place.page))
            continue;
        place.lo = place.hi = analysis::KeyOffset(line.key);
        layer.entries.push_back(place);
    }
    return layer;
}

const char* LayerName(Layer layer)
{
    const int i = static_cast<int>(layer);
    return i >= 0 && i < kLayerCount ? kLayerNames[i] : "?";
}

bool ParseLayer(const std::string& text, Layer& layer)
{
    for (int i = 0; i < kLayerCount; i++)
        if (text == kLayerNames[i])
        {
            layer = static_cast<Layer>(i);
            return true;
        }
    return false;
}

MemoryMarkup::MemoryMarkup(EmulatorContext* context) : _context(context)
{
}

void MemoryMarkup::SetLayer(Layer layer, LayerContent content)
{
    std::lock_guard<std::mutex> lock(_lock);
    _layers[static_cast<int>(layer)] = std::move(content);
    _revision++;
}

void MemoryMarkup::ClearLayer(Layer layer)
{
    SetLayer(layer, LayerContent{});
}

LayerContent MemoryMarkup::GetLayer(Layer layer) const
{
    std::lock_guard<std::mutex> lock(_lock);
    if (layer == Layer::Analysis)
    {
        RefreshStructureLocked();
        const LayerContent& set = _layers[static_cast<int>(Layer::Analysis)];
        return set.source.empty() ? _fromStructure : set;
    }
    return _layers[static_cast<int>(layer)];
}

uint64_t MemoryMarkup::Revision() const
{
    std::lock_guard<std::mutex> lock(_lock);
    RefreshStructureLocked();
    return _revision;
}

void MemoryMarkup::RefreshStructureLocked() const
{
    Emulator* emulator = _context ? _context->pEmulator : nullptr;
    if (!emulator)
        return;
    std::shared_ptr<const analysis::L2Result> l2 = analysis::L1Jobs::Instance().Structure(emulator->GetId());
    if (l2 == _structure)
        return;
    _structure = std::move(l2);
    _fromStructure = _structure ? FromStructure(*_structure) : LayerContent{};
    _revision++;
}

void MemoryMarkup::ViewOf(const std::vector<Window>& windows, bool derive, View& view) const
{
    RefreshStructureLocked();
    for (int i = 0; i < kLayerCount; i++)
        view.layers[i] = &_layers[i];
    const LayerContent& analysisSet = _layers[static_cast<int>(Layer::Analysis)];
    if (analysisSet.source.empty())
        view.layers[static_cast<int>(Layer::Analysis)] = &_fromStructure;
    if (derive)
    {
        view.runtime = _layers[static_cast<int>(Layer::Runtime)];
        if (view.runtime.source.empty())
            view.runtime.source = "runtime";
        DeriveRuntime(windows, view.runtime);
        view.layers[static_cast<int>(Layer::Runtime)] = &view.runtime;
    }
}

void MemoryMarkup::DeriveRuntime(const std::vector<Window>& windows, LayerContent& out) const
{
    Memory* memory = _context ? _context->pMemory : nullptr;
    if (!memory)
        return;
    const MemoryAccessTracker& tracker = memory->GetAccessTracker();
    const bool facts = tracker.GetLevel() == TrackerLevel::Facts;
    const bool z80View = tracker.GetTrackingMode() == TrackingMode::Z80AddressSpace;
    CoverageAnalyzer* coverage = nullptr;
    if (!facts && _context->pDebugManager && _context->pDebugManager->GetAnalyzerManager())
        coverage = _context->pDebugManager->GetAnalyzerManager()->getAnalyzer<CoverageAnalyzer>("coverage");
    if (!facts && !coverage)
        return;

    std::vector<uint8_t> kinds;
    for (const Window& window : windows)
    {
        const size_t size = static_cast<size_t>(window.hi - window.lo) + 1;
        kinds.assign(size, 0);
        // Physical facts name the page: claims on it; Z80 facts and the coverage name CPU addresses
        const bool onPage = facts && !z80View;
        if (facts && z80View)
        {
            if (!tracker.ReadZ80FactKinds(window.lo, size, kinds.data()))
                continue;
        }
        else if (onPage)
        {
            if (!window.mapped ||
                !tracker.ReadPhysicalFactKinds(AbsolutePage(window.space, window.page), window.offset, size,
                                               kinds.data()))
                continue;
        }
        else
        {
            // The coverage knows the instruction starts: the bytes up to the next one close by are their operands
            size_t previous = SIZE_MAX;
            for (size_t i = 0; i < size; i++)
            {
                if (!coverage->isExecuted(static_cast<uint16_t>(window.lo + i)))
                    continue;
                kinds[i] = static_cast<uint8_t>(AccessKind::OpcodeFetch);
                if (previous != SIZE_MAX && i - previous <= kLongestInstruction)
                    for (size_t j = previous + 1; j < i; j++)
                        kinds[j] = static_cast<uint8_t>(AccessKind::OperandFetch);
                previous = i;
            }
        }

        auto placeOf = [&](size_t first, size_t last) {
            if (onPage)
                return Place{window.space, window.page, static_cast<uint16_t>(window.offset + first),
                             static_cast<uint16_t>(window.offset + last)};
            return Place{Space::Cpu, 0, static_cast<uint16_t>(window.lo + first),
                         static_cast<uint16_t>(window.lo + last)};
        };
        // Code where the CPU fetched, data where it only read or wrote; one claim per run of one class
        auto classOf = [&kinds](size_t i) { return (kinds[i] & kCodeKinds) ? 1 : (kinds[i] & kDataKinds) ? 2 : 0; };
        size_t i = 0;
        while (i < size)
        {
            const int cls = classOf(i);
            size_t end = i;
            while (end + 1 < size && classOf(end + 1) == cls)
                end++;
            if (cls != 0)
            {
                Claim claim;
                claim.place = placeOf(i, end);
                claim.kind = cls == 1 ? analysis::RegionKind::Code : analysis::RegionKind::Data;
                claim.title = cls == 1 ? "executed" : "accessed";
                claim.continues = cls == 1 && !(kinds[i] & AccessKind::OpcodeFetch);
                out.claims.push_back(std::move(claim));
            }
            i = end + 1;
        }
        for (size_t b = 0; b < size; b++)
            if (kinds[b] & AccessKind::OpcodeFetch)
                out.entries.push_back(placeOf(b, b));
    }
}

std::vector<MemoryMarkup::Window> MemoryMarkup::WindowsOf(uint16_t from, uint16_t to) const
{
    std::vector<Window> windows;
    Memory* memory = _context ? _context->pMemory : nullptr;
    const uint32_t slot = 1u << (memory ? memory->PhysicalSlotShift() : 14);
    for (uint32_t start = from & ~(slot - 1); start <= to; start += slot)
    {
        Window w;
        w.lo = static_cast<uint16_t>(std::max<uint32_t>(start, from));
        w.hi = static_cast<uint16_t>(std::min<uint32_t>(start + slot - 1, to));
        if (memory)
        {
            const MemoryPageDescriptor where = memory->MapZ80AddressToPhysicalPage(w.lo);
            w.mapped = where.mode == BANK_RAM || where.mode == BANK_ROM || where.mode == BANK_CACHE;
            w.space = where.mode == BANK_ROM ? Space::Rom : where.mode == BANK_CACHE ? Space::Cache : Space::Ram;
            w.page = where.page;
            w.offset = where.addressInPage;
        }
        windows.push_back(w);
    }
    return windows;
}

bool MemoryMarkup::InWindow(const Place& place, const Window& window, uint16_t& lo, uint16_t& hi)
{
    if (place.space == Space::Cpu)
    {
        lo = std::max(place.lo, window.lo);
        hi = std::min(place.hi, window.hi);
        return lo <= hi;
    }
    if (!window.mapped || place.space != window.space || place.page != window.page)
        return false;
    const uint32_t windowEnd = static_cast<uint32_t>(window.offset) + (window.hi - window.lo);
    const uint32_t offLo = std::max<uint32_t>(place.lo, window.offset);
    const uint32_t offHi = std::min<uint32_t>(place.hi, windowEnd);
    if (offLo > offHi)
        return false;
    lo = static_cast<uint16_t>(window.lo + (offLo - window.offset));
    hi = static_cast<uint16_t>(window.lo + (offHi - window.offset));
    return true;
}

bool MemoryMarkup::GuardHolds(const Claim& claim) const
{
    if (claim.sha256.empty())
        return true;
    Memory* memory = _context ? _context->pMemory : nullptr;
    if (!memory)
        return false;
    const Place& p = claim.place;
    const size_t size = static_cast<size_t>(p.hi - p.lo) + 1;
    std::vector<uint8_t> bytes(size);
    if (p.space == Space::Cpu)
    {
        for (size_t i = 0; i < size; i++)
            bytes[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(p.lo + i));
    }
    else
    {
        const uint8_t* base = nullptr;
        if (p.space == Space::Ram && p.page < MAX_RAM_PAGES)
            base = memory->RAMBase() + static_cast<size_t>(p.page) * PAGE_SIZE;
        else if (p.space == Space::Rom && p.page < MAX_ROM_PAGES)
            base = memory->ROMBase() + static_cast<size_t>(p.page) * PAGE_SIZE;
        else if (p.space == Space::Cache && p.page < MAX_CACHE_PAGES && memory->CacheBase())
            base = memory->CacheBase() + static_cast<size_t>(p.page) * PAGE_SIZE;
        if (!base)
            return false;
        std::copy(base + p.lo, base + p.lo + size, bytes.begin());
    }
    return digestpp::sha256().absorb(bytes.data(), bytes.size()).hexdigest() == claim.sha256;
}

std::vector<Span> MemoryMarkup::RangesLocked(const View& view, const std::vector<Window>& windows, uint16_t from,
                                             uint16_t to) const
{
    std::vector<Span> spans;
    if (from > to)
        return spans;

    std::vector<Piece> pieces;
    for (int layer = 0; layer < kLayerCount; layer++)
    {
        const std::vector<Claim>& claims = view.layers[layer]->claims;
        for (size_t i = 0; i < claims.size(); i++)
        {
            bool checked = false;
            for (const Window& window : windows)
            {
                Piece piece;
                if (!InWindow(claims[i].place, window, piece.lo, piece.hi))
                    continue;
                if (!checked && !GuardHolds(claims[i]))
                    break;
                checked = true;
                piece.layer = layer;
                piece.index = i;
                // The claim's first byte, when this window shows it
                const Place& place = claims[i].place;
                if (claims[i].continues)
                    piece.start = -1;
                else if (place.space == Space::Cpu)
                    piece.start = place.lo;
                else if (place.lo >= window.offset && place.lo - window.offset <= window.hi - window.lo)
                    piece.start = window.lo + (place.lo - window.offset);
                pieces.push_back(piece);
            }
        }
    }

    // Paint from the highest claim down: each takes what is still free
    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
        return a.layer != b.layer ? a.layer > b.layer : a.index > b.index;
    });
    std::map<uint16_t, uint16_t> taken;  // lo -> hi, disjoint
    std::vector<Piece> won;
    for (const Piece& piece : pieces)
    {
        uint32_t cursor = piece.lo;
        while (cursor <= piece.hi)
        {
            auto next = taken.upper_bound(static_cast<uint16_t>(cursor));
            if (next != taken.begin() && std::prev(next)->second >= cursor)
            {
                cursor = static_cast<uint32_t>(std::prev(next)->second) + 1;  // inside a taken stretch: skip it
                continue;
            }
            const uint32_t freeEnd = next == taken.end() ? piece.hi : std::min<uint32_t>(piece.hi, next->first - 1u);
            Piece part = piece;
            part.lo = static_cast<uint16_t>(cursor);
            part.hi = static_cast<uint16_t>(freeEnd);
            won.push_back(part);
            cursor = freeEnd + 1;
        }
        // Mark the whole piece taken, merged with its neighbours
        uint16_t lo = piece.lo, hi = piece.hi;
        auto first = taken.lower_bound(lo);
        if (first != taken.begin() && std::prev(first)->second + 1u >= lo)
            first = std::prev(first);
        auto end = first;
        while (end != taken.end() && end->first <= static_cast<uint32_t>(hi) + 1u)
        {
            lo = std::min(lo, end->first);
            hi = std::max(hi, end->second);
            ++end;
        }
        taken.erase(first, end);
        taken[lo] = hi;
    }

    std::sort(won.begin(), won.end(), [](const Piece& a, const Piece& b) { return a.lo < b.lo; });
    const Piece* last = nullptr;
    for (const Piece& piece : won)
    {
        // The next part of the same claim, right after: one span
        if (last && last->layer == piece.layer && last->index == piece.index && spans.back().hi + 1u == piece.lo)
        {
            spans.back().hi = piece.hi;
            last = &piece;
            continue;
        }
        const Claim& claim = view.layers[piece.layer]->claims[piece.index];
        Span span;
        span.lo = piece.lo;
        span.hi = piece.hi;
        span.kind = claim.kind;
        span.layer = static_cast<Layer>(piece.layer);
        span.source = view.layers[piece.layer]->source;
        span.title = claim.title;
        span.confidence = claim.confidence;
        span.claimStart = piece.start == piece.lo;
        spans.push_back(std::move(span));
        last = &piece;
    }
    return spans;
}

std::vector<Span> MemoryMarkup::Ranges(uint16_t from, uint16_t to) const
{
    if (from > to)
        return {};
    const std::vector<Window> windows = WindowsOf(from, to);
    std::lock_guard<std::mutex> lock(_lock);
    View view;
    ViewOf(windows, true, view);
    return RangesLocked(view, windows, from, to);
}

std::optional<Span> MemoryMarkup::At(uint16_t address) const
{
    const std::vector<Window> windows = WindowsOf(address, address);
    std::lock_guard<std::mutex> lock(_lock);
    View view;
    ViewOf(windows, true, view);
    std::vector<Span> spans = RangesLocked(view, windows, address, address);
    if (spans.empty())
        return std::nullopt;
    return spans.front();
}

std::vector<uint16_t> MemoryMarkup::Anchors(uint16_t from, uint16_t to) const
{
    std::vector<uint16_t> anchors;
    if (from > to)
        return anchors;
    const std::vector<Window> windows = WindowsOf(from, to);
    std::lock_guard<std::mutex> lock(_lock);
    View view;
    ViewOf(windows, true, view);
    const std::vector<Span> spans = RangesLocked(view, windows, from, to);
    for (const Span& span : spans)
        if (span.kind == analysis::RegionKind::Code && span.claimStart)
            anchors.push_back(span.lo);
    // An entry stands unless a higher layer says the byte is no code
    auto overruled = [&spans](uint16_t address, int layer) {
        auto it = std::upper_bound(spans.begin(), spans.end(), address,
                                   [](uint16_t a, const Span& span) { return a < span.lo; });
        if (it == spans.begin())
            return false;
        --it;
        return address <= it->hi && it->kind != analysis::RegionKind::Code && static_cast<int>(it->layer) > layer;
    };
    for (int layer = 0; layer < kLayerCount; layer++)
        for (const Place& entry : view.layers[layer]->entries)
            for (const Window& window : windows)
            {
                uint16_t lo = 0, hi = 0;
                if (InWindow(Place{entry.space, entry.page, entry.lo, entry.lo}, window, lo, hi) &&
                    !overruled(lo, layer))
                    anchors.push_back(lo);
            }
    std::sort(anchors.begin(), anchors.end());
    anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());
    return anchors;
}

bool MemoryMarkup::IsInstructionStart(uint16_t address) const
{
    const std::vector<uint16_t> anchors = Anchors(address, address);
    return !anchors.empty();
}

std::vector<CommentAt> MemoryMarkup::Comments(uint16_t from, uint16_t to) const
{
    std::map<uint16_t, CommentAt> byAddress;
    if (from > to)
        return {};
    const std::vector<Window> windows = WindowsOf(from, to);
    std::lock_guard<std::mutex> lock(_lock);
    View view;
    ViewOf(windows, false, view);
    for (int layer = 0; layer < kLayerCount; layer++)
        for (const Note& note : view.layers[layer]->comments)
            for (const Window& window : windows)
            {
                uint16_t lo = 0, hi = 0;
                if (InWindow(Place{note.place.space, note.place.page, note.place.lo, note.place.lo}, window, lo, hi))
                    byAddress[lo] = CommentAt{lo, static_cast<Layer>(layer), note.text};
            }
    std::vector<CommentAt> comments;
    for (auto& [address, comment] : byAddress)
        comments.push_back(std::move(comment));
    return comments;
}

bool MemoryMarkup::Mark(uint16_t lo, uint16_t hi, analysis::RegionKind kind, const std::string& title, bool cpuView)
{
    if (lo > hi)
        return false;
    std::vector<Claim> made;
    if (cpuView)
    {
        Claim claim;
        claim.place = Place{Space::Cpu, 0, lo, hi};
        claim.kind = kind;
        claim.title = title;
        made.push_back(std::move(claim));
    }
    else
        for (const Window& window : WindowsOf(lo, hi))
        {
            Claim claim;
            claim.place = Place{Space::Cpu, 0, window.lo, window.hi};
            claim.kind = kind;
            claim.title = title;
            if (window.mapped)
                claim.place = Place{window.space, window.page, window.offset,
                                    static_cast<uint16_t>(window.offset + (window.hi - window.lo))};
            claim.continues = !made.empty();
            made.push_back(std::move(claim));
        }
    std::lock_guard<std::mutex> lock(_lock);
    LayerContent& user = _layers[static_cast<int>(Layer::User)];
    user.source = "user";
    for (Claim& claim : made)
        user.claims.push_back(std::move(claim));
    _revision++;
    return true;
}

void MemoryMarkup::Unmark(uint16_t lo, uint16_t hi)
{
    if (lo > hi)
        return;
    const std::vector<Window> windows = WindowsOf(lo, hi);
    std::lock_guard<std::mutex> lock(_lock);
    LayerContent& user = _layers[static_cast<int>(Layer::User)];
    std::vector<Claim> kept;
    for (const Claim& claim : user.claims)
    {
        std::vector<Place> rest{claim.place};
        if (claim.place.space == Space::Cpu)
        {
            rest.clear();
            Subtract(claim.place, lo, hi, rest);
        }
        else
            for (const Window& window : windows)
            {
                if (!window.mapped || window.space != claim.place.space || window.page != claim.place.page)
                    continue;
                // The cut in page offsets: the part of the window inside lo..hi
                const uint16_t cutLo = window.offset;
                const uint16_t cutHi = static_cast<uint16_t>(window.offset + (window.hi - window.lo));
                std::vector<Place> next;
                for (const Place& place : rest)
                    Subtract(place, cutLo, cutHi, next);
                rest = std::move(next);
            }
        for (const Place& place : rest)
        {
            Claim part = claim;
            part.place = place;
            part.continues = claim.continues || place.lo != claim.place.lo;  // a cut start is no instruction start
            kept.push_back(std::move(part));
        }
    }
    user.claims = std::move(kept);
    _revision++;
}

void MemoryMarkup::SetComment(uint16_t address, const std::string& text)
{
    const std::vector<Window> windows = WindowsOf(address, address);
    Place place{Space::Cpu, 0, address, address};
    if (!windows.empty() && windows.front().mapped)
        place = Place{windows.front().space, windows.front().page, windows.front().offset, windows.front().offset};
    std::lock_guard<std::mutex> lock(_lock);
    LayerContent& user = _layers[static_cast<int>(Layer::User)];
    user.source = "user";
    std::erase_if(user.comments, [&place](const Note& note) { return note.place == place; });
    if (!text.empty())
        user.comments.push_back(Note{place, text});
    _revision++;
}

}  // namespace markup
