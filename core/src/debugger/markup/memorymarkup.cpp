#include "debugger/markup/memorymarkup.h"

#include <algorithm>
#include <map>

#include "3rdparty/digestpp/digestpp.hpp"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

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
}  // namespace

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
    return _layers[static_cast<int>(layer)];
}

uint64_t MemoryMarkup::Revision() const
{
    std::lock_guard<std::mutex> lock(_lock);
    return _revision;
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

std::vector<Span> MemoryMarkup::RangesLocked(uint16_t from, uint16_t to) const
{
    std::vector<Span> spans;
    if (from > to)
        return spans;
    const std::vector<Window> windows = WindowsOf(from, to);

    std::vector<Piece> pieces;
    for (int layer = 0; layer < kLayerCount; layer++)
    {
        const std::vector<Claim>& claims = _layers[layer].claims;
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
        const Claim& claim = _layers[piece.layer].claims[piece.index];
        Span span;
        span.lo = piece.lo;
        span.hi = piece.hi;
        span.kind = claim.kind;
        span.layer = static_cast<Layer>(piece.layer);
        span.source = _layers[piece.layer].source;
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
    std::lock_guard<std::mutex> lock(_lock);
    return RangesLocked(from, to);
}

std::optional<Span> MemoryMarkup::At(uint16_t address) const
{
    std::lock_guard<std::mutex> lock(_lock);
    std::vector<Span> spans = RangesLocked(address, address);
    if (spans.empty())
        return std::nullopt;
    return spans.front();
}

std::vector<uint16_t> MemoryMarkup::Anchors(uint16_t from, uint16_t to) const
{
    std::lock_guard<std::mutex> lock(_lock);
    std::vector<uint16_t> anchors;
    if (from > to)
        return anchors;
    for (const Span& span : RangesLocked(from, to))
        if (span.kind == analysis::RegionKind::Code && span.claimStart)
            anchors.push_back(span.lo);
    const std::vector<Window> windows = WindowsOf(from, to);
    for (const LayerContent& layer : _layers)
        for (const Place& entry : layer.entries)
            for (const Window& window : windows)
            {
                uint16_t lo = 0, hi = 0;
                if (InWindow(Place{entry.space, entry.page, entry.lo, entry.lo}, window, lo, hi))
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
    std::lock_guard<std::mutex> lock(_lock);
    std::map<uint16_t, CommentAt> byAddress;
    if (from > to)
        return {};
    const std::vector<Window> windows = WindowsOf(from, to);
    for (int layer = 0; layer < kLayerCount; layer++)
        for (const Note& note : _layers[layer].comments)
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
