#include "unrealasm/symbols/live.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace unrealasm::symbols
{
namespace
{
constexpr uint32_t kPage = 0x4000;

std::string Hex(uint32_t value, int digits)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out(static_cast<size_t>(digits), '0');
    for (int k = digits - 1; k >= 0; --k, value >>= 4)
        out[static_cast<size_t>(k)] = kDigits[value & 0xF];
    return out;
}

std::string Where(const LiveCandidate& c)
{
    return "ram" + std::to_string(c.page) + ":#" + Hex(c.offset, 4);
}

const MemoryPage* Page(const MemoryView& memory, uint16_t page)
{
    for (const MemoryPage& p : memory.pages)
        if (p.page == page && p.bytes.size() >= kPage)
            return &p;
    return nullptr;
}

LiveReadResult Start(const LiveCandidate& c, std::string_view title)
{
    LiveReadResult r;
    r.set.id = c.scanner + "@" + Where(c);
    r.set.title = std::string(title) + " (" + Where(c) + ")";
    r.set.origin = {"live", Where(c), {}};
    return r;
}

Symbol Label(std::string name, uint32_t value, const LiveCandidate& c, uint32_t at)
{
    Symbol s;
    s.name = std::move(name);
    s.location.offset = value;   // the CPU view: neither table says which page a value was assembled for
    s.provenance.importer = "live-" + c.scanner;
    s.provenance.raw = "ram" + std::to_string(c.page) + ":#" + Hex(at, 4);
    return s;
}

// ---- ALASM: records below a zero byte, read upward (research-labeltables.md §1) ----
//
// +0  bits 0-5: the record's size (5 + the name's length), bits 6-7: 0 defined, 1 macro, 2 used but not defined,
//     3 wrong (an assembly error)
// +1  the value, low byte first
// +3  two bytes (zero in every table seen)
// +5  the name, last character first
// The newest label is the lowest record; the table ends at a zero byte (#3DFF in ALASM 5.0x, #3F7F in 4.4x).

bool AlasmNameChar(uint8_t c)
{
    return std::isalnum(c) != 0 || c == '_' || c == '@' || c == '.' || c == '!';
}

/// The records from `at` up to a zero byte: their count, or 0 when they do not chain to one
size_t AlasmChain(std::span<const uint8_t> d, uint32_t at, uint32_t& end)
{
    size_t n = 0;
    while (at < kPage)
    {
        const uint8_t head = d[at];
        if (head == 0)
        {
            end = at;
            return n;
        }
        const uint32_t size = head & 63u;
        if (size < 6 || at + size > kPage)
            return 0;
        for (uint32_t k = 5; k < size; ++k)
            if (!AlasmNameChar(d[at + k]))
                return 0;
        if (std::isdigit(d[at + size - 1]) != 0)
            return 0;   // the first character of a name is no digit
        at += size;
        ++n;
    }
    return 0;
}

class AlasmScanner : public ILiveScanner
{
public:
    std::string_view Id() const override { return "alasm-table"; }
    std::string_view Title() const override { return "ALASM label table"; }

    std::vector<LiveCandidate> Find(const MemoryView& memory) const override
    {
        std::vector<LiveCandidate> out;
        for (const MemoryPage& p : memory.pages)
        {
            if (p.bytes.size() < kPage)
                continue;
            // Every start that chains to a zero; a chain's head is a start no other start's chain runs through
            std::vector<uint32_t> ends(kPage, 0);
            std::vector<size_t> counts(kPage, 0);
            std::set<uint32_t> inner;
            for (uint32_t s = 0; s < kPage; ++s)
            {
                uint32_t end = 0;
                const size_t n = AlasmChain(p.bytes, s, end);
                if (n == 0)
                    continue;
                counts[s] = n;
                ends[s] = end;
                inner.insert(s + (p.bytes[s] & 63u));
            }
            // The longest chain per terminator
            std::vector<LiveCandidate> best;
            for (uint32_t s = 0; s < kPage; ++s)
            {
                if (counts[s] < 2 || inner.count(s))
                    continue;
                LiveCandidate c;
                c.scanner = std::string(Id());
                c.page = p.page;
                c.offset = s;
                c.end = ends[s];
                c.count = counts[s];
                c.version = c.end == 0x3F7F ? "4.4x" : c.end == 0x3DFF ? "5.0x" : "?";
                c.score = static_cast<int>(std::min<size_t>(c.count, 200)) + (c.version != "?" ? 50 : 0) + (p.page == 3 ? 20 : 0);
                auto same = std::find_if(best.begin(), best.end(), [&](const LiveCandidate& b) { return b.end == c.end; });
                if (same == best.end())
                    best.push_back(c);
                else if (c.count > same->count)
                    *same = c;
            }
            out.insert(out.end(), best.begin(), best.end());
        }
        std::sort(out.begin(), out.end(), [](const LiveCandidate& a, const LiveCandidate& b) { return a.score > b.score; });
        return out;
    }

    LiveReadResult Read(const MemoryView& memory, const LiveCandidate& c) const override
    {
        LiveReadResult r = Start(c, Title());
        const MemoryPage* p = Page(memory, c.page);
        if (!p)
        {
            r.diagnostics.push_back({Severity::Error, 0, 0, "page " + std::to_string(c.page) + " not in the memory given"});
            return r;
        }
        std::span<const uint8_t> d = p->bytes;
        size_t skipped[4] = {0, 0, 0, 0};
        std::vector<Symbol> found;
        for (uint32_t at = c.offset; at < c.end && d[at] != 0;)
        {
            const uint32_t size = d[at] & 63u;
            const unsigned flags = d[at] >> 6;
            std::string name;
            for (uint32_t k = size; k > 5; --k)
                name.push_back(static_cast<char>(d[at + k - 1]));
            if (flags == 0)
                found.push_back(Label(std::move(name), static_cast<uint32_t>(d[at + 1] | d[at + 2] << 8), c, at));
            else
                ++skipped[flags];
            at += size;
        }
        std::reverse(found.begin(), found.end());   // the oldest record is the highest: definition order
        r.set.symbols = std::move(found);
        if (skipped[1])
            r.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(skipped[1]) + " macro name(s) left out"});
        if (skipped[2])
            r.diagnostics.push_back({Severity::Info, 0, 0, std::to_string(skipped[2]) + " label(s) used but not defined left out"});
        if (skipped[3])
            r.diagnostics.push_back({Severity::Warning, 0, 0, std::to_string(skipped[3]) + " label(s) of lines with assembly errors left out"});
        r.ok = true;
        return r;
    }
};

// ---- XAS: 9-byte entries, a 7-character name padded with blanks and the value (research-labeltables.md §2) ----
//
// 7.x: page 6 holds two lists marked by 5 at +#1FFF and +#3FFF, each going down from just below the marker: names
//      A-L under #3FFF, M-Z under #1FFF, sorted; an entry starting with a byte >= #80 ends a list.
// 4.x: one list going up in definition order (from #0B16 of page 6 in 4.18), ended by a zero byte.

bool XasNameAt(std::span<const uint8_t> d, uint32_t at)
{
    if (at + 9 > kPage)
        return false;
    const uint8_t first = d[at];
    if (!(std::isupper(first) != 0 || first == '_'))
        return false;
    bool blank = false;
    for (uint32_t k = 1; k < 7; ++k)
    {
        const uint8_t c = d[at + k];
        if (c == ' ')
            blank = true;
        else if (blank || !(std::isupper(c) != 0 || std::isdigit(c) != 0 || c == '_'))
            return false;   // blanks only at the end
    }
    return true;
}

std::string XasName(std::span<const uint8_t> d, uint32_t at)
{
    std::string name(reinterpret_cast<const char*>(d.data() + at), 7);
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    return name;
}

bool Xas7Marked(std::span<const uint8_t> d)
{
    return d[0x1FFF] == 5 && d[0x3FFF] == 5;
}

/// Entries of a 7.x list going down from `top` (the marker): their count, or -1 when an entry is no name
int Xas7List(std::span<const uint8_t> d, uint32_t top)
{
    int n = 0;
    for (uint32_t at = top - 9; at >= 9; at -= 9)
    {
        if (d[at] >= 0x80)
            return n;
        if (!XasNameAt(d, at))
            return -1;
        ++n;
    }
    return -1;
}

class XasScanner : public ILiveScanner
{
public:
    std::string_view Id() const override { return "xas-table"; }
    std::string_view Title() const override { return "XAS label table"; }

    std::vector<LiveCandidate> Find(const MemoryView& memory) const override
    {
        std::vector<LiveCandidate> out;
        for (const MemoryPage& p : memory.pages)
        {
            if (p.bytes.size() < kPage)
                continue;
            std::span<const uint8_t> d = p.bytes;
            if (Xas7Marked(d))
            {
                const int high = Xas7List(d, 0x3FFF), low = Xas7List(d, 0x1FFF);
                if (high >= 0 && low >= 0 && high + low > 0)
                {
                    LiveCandidate c;
                    c.scanner = std::string(Id());
                    c.version = "7.x";
                    c.page = p.page;
                    c.offset = 0x1FFF;
                    c.end = 0x4000;
                    c.count = static_cast<size_t>(high + low);
                    c.score = static_cast<int>(std::min<size_t>(c.count, 200)) + 50 + (p.page == 6 ? 20 : 0);
                    out.push_back(c);
                    continue;   // the two lists also read as 4.x runs: not twice
                }
            }
            // 4.x: runs of entries going up to a zero byte
            for (uint32_t s = 0; s + 9 <= kPage; ++s)
            {
                if (!XasNameAt(d, s) || (s >= 9 && XasNameAt(d, s - 9)))
                    continue;
                uint32_t at = s;
                size_t n = 0;
                while (XasNameAt(d, at))
                {
                    at += 9;
                    ++n;
                }
                if (n < 2 || at >= kPage || d[at] != 0)
                    continue;
                LiveCandidate c;
                c.scanner = std::string(Id());
                c.version = "4.x";
                c.page = p.page;
                c.offset = s;
                c.end = at;
                c.count = n;
                c.score = static_cast<int>(std::min<size_t>(n, 200)) + (s == 0x0B16 ? 50 : 0) + (p.page == 6 ? 20 : 0);
                out.push_back(c);
                s = at;
            }
        }
        std::sort(out.begin(), out.end(), [](const LiveCandidate& a, const LiveCandidate& b) { return a.score > b.score; });
        return out;
    }

    LiveReadResult Read(const MemoryView& memory, const LiveCandidate& c) const override
    {
        LiveReadResult r = Start(c, Title());
        const MemoryPage* p = Page(memory, c.page);
        if (!p)
        {
            r.diagnostics.push_back({Severity::Error, 0, 0, "page " + std::to_string(c.page) + " not in the memory given"});
            return r;
        }
        std::span<const uint8_t> d = p->bytes;
        auto add = [&](uint32_t at) {
            r.set.symbols.push_back(Label(XasName(d, at), static_cast<uint32_t>(d[at + 7] | d[at + 8] << 8), c, at));
        };
        if (c.version == "7.x")
        {
            for (const uint32_t top : {0x3FFFu, 0x1FFFu})   // A-L, then M-Z: alphabetical
                for (uint32_t at = top - 9; at >= 9 && d[at] < 0x80 && XasNameAt(d, at); at -= 9)
                    add(at);
        }
        else
        {
            for (uint32_t at = c.offset; at < c.end; at += 9)
                add(at);
        }
        r.set.caseRule = CaseRule::Fold;   // XAS keeps names in capitals and compares them without case
        r.ok = true;
        return r;
    }
};
}  // namespace

const std::vector<std::unique_ptr<ILiveScanner>>& LiveScanners()
{
    static const std::vector<std::unique_ptr<ILiveScanner>> scanners = [] {
        std::vector<std::unique_ptr<ILiveScanner>> v;
        v.push_back(std::make_unique<AlasmScanner>());
        v.push_back(std::make_unique<XasScanner>());
        return v;
    }();
    return scanners;
}

std::vector<LiveCandidate> FindLabelTables(const MemoryView& memory)
{
    std::vector<LiveCandidate> out;
    for (const auto& s : LiveScanners())
    {
        std::vector<LiveCandidate> found = s->Find(memory);
        out.insert(out.end(), found.begin(), found.end());
    }
    std::stable_sort(out.begin(), out.end(), [](const LiveCandidate& a, const LiveCandidate& b) { return a.score > b.score; });
    return out;
}

LiveReadResult ReadLabelTable(const MemoryView& memory, const LiveCandidate& candidate)
{
    for (const auto& s : LiveScanners())
        if (s->Id() == candidate.scanner)
            return s->Read(memory, candidate);
    LiveReadResult r;
    r.diagnostics.push_back({Severity::Error, 0, 0, "no scanner " + candidate.scanner});
    return r;
}
}  // namespace unrealasm::symbols
