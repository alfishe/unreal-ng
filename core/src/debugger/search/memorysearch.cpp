#include "memorysearch.h"

#include <algorithm>
#include <cctype>

#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/memory/devicememory.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

namespace MemorySearch
{

namespace
{
int Nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/// A readable byte source over one space: a function from an index to the byte
struct View
{
    // Cpu: the Z80 view; Page: one host page; AllRam: `pages` RAM pages back to back
    Memory* memory = nullptr;
    const uint8_t* page = nullptr;
    std::vector<const uint8_t*> ramPages;
    std::vector<uint8_t> copy;   // Region: the region's bytes (read once through DeviceMemory)
    std::string regionName;      // Region: its canonical name
    MemorySearchRequest::Space space = MemorySearchRequest::Space::Cpu;
    uint32_t size = 0;

    uint8_t At(uint32_t index) const
    {
        switch (space)
        {
            case MemorySearchRequest::Space::Cpu: return memory->DirectReadFromZ80Memory(static_cast<uint16_t>(index));
            case MemorySearchRequest::Space::Page: return page[index];
            default: return ramPages[index / PAGE_SIZE][index % PAGE_SIZE];
        }
    }
};
}  // namespace

uint32_t RamPageCount(EmulatorContext* context)
{
    return context && context->config.ramsize ? context->config.ramsize / 16 : MAX_RAM_PAGES;
}

const uint8_t* PageHost(EmulatorContext* context, MemoryBankModeEnum type, uint32_t page)
{
    Memory* memory = context ? context->pMemory : nullptr;
    if (!memory)
        return nullptr;
    if (type == BANK_RAM && page < RamPageCount(context))
        return memory->RAMPageAddress(static_cast<uint16_t>(page));
    if (type == BANK_ROM && page < MAX_ROM_PAGES)
        return memory->ROMPageHostAddress(static_cast<uint8_t>(page));
    if (type == BANK_CACHE && page < MAX_CACHE_PAGES && memory->CacheBase())
        return memory->CacheBase() + static_cast<size_t>(page) * PAGE_SIZE;
    return nullptr;
}

bool ParsePattern(const std::string& text, std::vector<uint8_t>& pattern, std::vector<uint8_t>& mask, std::string& error)
{
    pattern.clear();
    mask.clear();
    std::string digits;
    for (char c : text)
    {
        if (c == ' ' || c == ':' || c == ',' || c == '\t')
            continue;
        if ((c == 'x' || c == 'X') && !digits.empty() && digits.back() == '0' && digits.size() % 2 == 1)
        {
            digits.pop_back();  // a 0x prefix ("0xAF 0x32")
            continue;
        }
        if (c != '?' && Nibble(c) < 0)
        {
            error = std::string("invalid pattern character '") + c + "' (hex bytes, ?? for any byte, A? for any low nibble)";
            return false;
        }
        digits += c;
    }
    if (digits.empty() || digits.size() % 2 != 0)
    {
        error = "the pattern needs whole bytes: two hex digits (or ?) each";
        return false;
    }
    for (size_t i = 0; i < digits.size(); i += 2)
    {
        uint8_t value = 0, bits = 0;
        for (int half = 0; half < 2; half++)
        {
            const char c = digits[i + half];
            const int shift = half == 0 ? 4 : 0;
            if (c != '?')
            {
                value = static_cast<uint8_t>(value | (Nibble(c) << shift));
                bits = static_cast<uint8_t>(bits | (0x0F << shift));
            }
        }
        pattern.push_back(value);
        mask.push_back(bits);
    }
    if (std::all_of(mask.begin(), mask.end(), [](uint8_t m) { return m == 0; }))
    {
        error = "the pattern is all wildcards";
        return false;
    }
    if (std::all_of(mask.begin(), mask.end(), [](uint8_t m) { return m == 0xFF; }))
        mask.clear();  // an exact pattern: the fast path
    return true;
}

std::string NumberPattern(uint64_t value)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string digits;
    do
    {
        digits.insert(digits.begin(), kHex[value & 0x0F]);
        value >>= 4;
    } while (value);
    if (digits.size() % 2)
        digits.insert(digits.begin(), '0');
    return digits;
}

bool ParseSpace(const std::string& text, MemorySearchRequest& request, std::string& error)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.empty() || lower == "cpu")
    {
        request.space = MemorySearchRequest::Space::Cpu;
        return true;
    }
    if (lower == "ram")
    {
        request.space = MemorySearchRequest::Space::AllRam;
        return true;
    }
    // ram5 / rom2 / cache0 (and a bad page number of those kinds) is a page; any other name is a region
    const size_t digits = lower.find_first_of("0123456789");
    const std::string kind = lower.substr(0, digits);
    const bool pageLike = digits != std::string::npos && lower.find_first_not_of("0123456789", digits) == std::string::npos &&
                          (kind == "ram" || kind == "rom" || kind == "cache" || kind == "misc");
    if (!pageLike)
    {
        request.space = MemorySearchRequest::Space::Region;
        request.region = lower;
        return true;
    }
    if (!BreakpointManager::ParsePageSpec(lower, request.page, request.pageType, error))
    {
        error = "space must be cpu, ram (every RAM page), a page (ram5, rom2, cache0) or a memory region; " + error;
        return false;
    }
    request.space = MemorySearchRequest::Space::Page;
    return true;
}

std::string SpaceName(const MemorySearchRequest& request)
{
    switch (request.space)
    {
        case MemorySearchRequest::Space::Cpu: return "cpu";
        case MemorySearchRequest::Space::AllRam: return "ram";
        case MemorySearchRequest::Space::Region: return request.region;
        default: return std::string(BreakpointManager::PageKindName(request.pageType)) + std::to_string(request.page);
    }
}

MemorySearchResult Search(EmulatorContext* context, const MemorySearchRequest& request)
{
    MemorySearchResult result;
    Memory* memory = context ? context->pMemory : nullptr;
    if (!memory)
    {
        result.error = "memory not available";
        return result;
    }
    if (request.pattern.empty() || request.pattern.size() > 64)
    {
        result.error = "the pattern must be 1 to 64 bytes";
        return result;
    }
    if (!request.mask.empty() && request.mask.size() != request.pattern.size())
    {
        result.error = "the mask must be as long as the pattern";
        return result;
    }
    if (request.alignment != 1 && request.alignment != 2)
    {
        result.error = "alignment must be 1 or 2";
        return result;
    }

    View view;
    view.memory = memory;
    view.space = request.space;
    switch (request.space)
    {
        case MemorySearchRequest::Space::Cpu:
            view.size = 0x10000;
            break;
        case MemorySearchRequest::Space::Page:
        {
            view.page = PageHost(context, request.pageType, request.page);
            if (!view.page)
            {
                result.error = "this machine has no page " + SpaceName(request);
                return result;
            }
            view.size = PAGE_SIZE;
            break;
        }
        case MemorySearchRequest::Space::Region:
        {
            std::string error;
            IDeviceMemoryRegion* region = DeviceMemory::Find(context, request.region, &error);
            if (!region || !DeviceMemory::Read(context, request.region, 0, region->Size(), view.copy, error))
            {
                result.error = "space must be cpu, ram, a page (ram5, rom2, cache0) or a memory region: " + error;
                return result;
            }
            view.space = MemorySearchRequest::Space::Page;   // read like a page, from the copy
            view.page = view.copy.data();
            view.size = region->Size();
            view.regionName = region->Name();
            break;
        }
        case MemorySearchRequest::Space::AllRam:
        {
            for (uint32_t p = 0; p < RamPageCount(context); p++)
            {
                const uint8_t* address = memory->RAMPageAddress(static_cast<uint16_t>(p));
                if (!address)
                    break;
                view.ramPages.push_back(address);
            }
            view.size = static_cast<uint32_t>(view.ramPages.size()) * PAGE_SIZE;
            break;
        }
    }

    const uint32_t end = std::min<uint32_t>(request.end, view.size - 1);
    const size_t n = request.pattern.size();
    if (request.start > end || end - request.start + 1 < n)
        return result;  // nothing fits: no match, not an error
    const uint32_t last = end - static_cast<uint32_t>(n) + 1;
    const bool masked = !request.mask.empty();
    // The first byte that must match fully gates the inner loop
    size_t lead = 0;
    if (masked)
        while (lead < n && request.mask[lead] != 0xFF)
            lead++;

    for (uint64_t position = request.start; position <= last; position += request.alignment)
    {
        const uint32_t at = static_cast<uint32_t>(position);
        // All RAM: the pages are not neighbors, a match stays inside one
        if (request.space == MemorySearchRequest::Space::AllRam && at % PAGE_SIZE + n > PAGE_SIZE)
            continue;
        if (!masked)
        {
            if (view.At(at) != request.pattern[0])
                continue;
        }
        else if (lead < n && view.At(at + static_cast<uint32_t>(lead)) != request.pattern[lead])
            continue;

        bool matched = true;
        for (size_t i = 0; i < n && matched; i++)
        {
            const uint8_t m = masked ? request.mask[i] : 0xFF;
            matched = ((view.At(at + static_cast<uint32_t>(i)) ^ request.pattern[i]) & m) == 0;
        }
        if (!matched)
            continue;
        if (result.matches.size() >= request.max)
        {
            result.truncated = true;
            break;
        }

        MemorySearchMatch match;
        if (request.space == MemorySearchRequest::Space::Cpu)
            match.address = at;
        else if (request.space == MemorySearchRequest::Space::Page)
        {
            match.address = at;
            match.pageType = request.pageType;
            match.page = request.page;
        }
        else if (request.space == MemorySearchRequest::Space::Region)
        {
            match.address = at;
            match.region = view.regionName;
        }
        else
        {
            match.address = at % PAGE_SIZE;
            match.pageType = BANK_RAM;
            match.page = static_cast<int>(at / PAGE_SIZE);
        }
        // The context stays in the space (all RAM: in the match's page)
        const bool allRam = request.space == MemorySearchRequest::Space::AllRam;
        const uint32_t floor = allRam ? at - at % PAGE_SIZE : 0;
        const uint64_t ceiling = allRam ? floor + PAGE_SIZE : view.size;
        const uint32_t contextBegin = at - std::min<uint32_t>(at - floor, request.contextBefore);
        const uint32_t contextEnd =
            static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(at) + n + request.contextAfter, ceiling));
        match.contextStart = request.space == MemorySearchRequest::Space::AllRam ? contextBegin % PAGE_SIZE : contextBegin;
        for (uint32_t i = contextBegin; i < contextEnd; i++)
            match.bytes.push_back(view.At(i));
        result.matches.push_back(std::move(match));
    }
    return result;
}

bool BuildRequest(const std::string& pattern, const std::vector<uint8_t>* patternBytes, const std::string& mask,
                  const std::string& space, uint32_t start, uint32_t end, unsigned max, unsigned alignment,
                  MemorySearchRequest& out, std::string& error)
{
    out = MemorySearchRequest();
    if (patternBytes)
    {
        out.pattern = *patternBytes;
        if (out.pattern.empty())
        {
            error = "the pattern is empty";
            return false;
        }
    }
    else if (!ParsePattern(pattern, out.pattern, out.mask, error))
        return false;
    // An explicit mask (1 bits must match) replaces the one the wildcards made
    if (!mask.empty())
    {
        std::vector<uint8_t> bits, unused;
        if (!ParsePattern(mask, bits, unused, error))
        {
            error = "mask: " + error;
            return false;
        }
        if (bits.size() != out.pattern.size())
        {
            error = "the mask must be as long as the pattern";
            return false;
        }
        out.mask = bits;
    }
    if (!space.empty() && !ParseSpace(space, out, error))
        return false;
    if (start > end)
    {
        error = "the range starts after it ends";
        return false;
    }
    out.start = start;
    out.end = end;
    out.max = max == 0 ? 1 : max;
    out.alignment = alignment;
    return true;
}

StateNode ToState(const MemorySearchRequest& request, const MemorySearchResult& result)
{
    StateNode node = StateNode::Object();
    if (!result.error.empty())
    {
        node["error"] = result.error;
        return node;
    }
    node["space"] = SpaceName(request);
    node["count"] = static_cast<uint64_t>(result.matches.size());
    node["truncated"] = result.truncated;
    StateNode matches = StateNode::Array();
    for (const MemorySearchMatch& m : result.matches)
    {
        StateNode match = StateNode::Object();
        if (!m.region.empty())
        {
            match["region"] = m.region;
            match["offset"] = static_cast<uint64_t>(m.address);
        }
        else if (m.page < 0)
            match["address"] = static_cast<uint64_t>(m.address);
        else
        {
            StateNode page = StateNode::Object();
            page["kind"] = std::string(BreakpointManager::PageKindName(static_cast<MemoryBankModeEnum>(m.pageType)));
            page["page"] = static_cast<uint64_t>(m.page);
            match["page"] = page;
            match["offset"] = static_cast<uint64_t>(m.address);
        }
        match["context_start"] = static_cast<uint64_t>(m.contextStart);
        StateNode context = StateNode::Array();
        for (uint8_t b : m.bytes)
            context.push(StateNode(static_cast<uint64_t>(b)));
        match["context"] = context;
        matches.push(match);
    }
    node["matches"] = matches;
    return node;
}

}  // namespace MemorySearch
