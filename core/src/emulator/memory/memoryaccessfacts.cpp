#include "memoryaccessfacts.h"

#include <algorithm>
#include <array>

namespace
{
const char* const kKindNames[8] = {"OpcodeFetch", "OperandFetch", "DataRead",    "DataWrite",
                                   "StackRead",   "StackWrite",   "BlockAccess", "WrittenWhileCode"};
}  // namespace

MemoryAccessFacts::MemoryAccessFacts() = default;

MemoryAccessFacts::~MemoryAccessFacts()
{
    Free();
}

void MemoryAccessFacts::Allocate(size_t pageCount, bool z80View)
{
    if (_pages)
    {
        SetZ80View(z80View);
        return;
    }
    if (z80View)
        _z80.reset(new ByteFacts[kZ80Size]());
    _pages.reset(new std::atomic<ByteFacts*>[pageCount]);
    for (size_t i = 0; i < pageCount; i++)
        _pages[i].store(nullptr, std::memory_order_relaxed);
    _pageCount = pageCount;
}

void MemoryAccessFacts::SetZ80View(bool z80View)
{
    if (!_pages)
        return;
    if (z80View && !_z80)
        _z80.reset(new ByteFacts[kZ80Size]());
    else if (!z80View)
        _z80.reset();
}

void MemoryAccessFacts::Free()
{
    if (_pages)
    {
        for (size_t i = 0; i < _pageCount; i++)
            delete[] _pages[i].exchange(nullptr, std::memory_order_acq_rel);
    }
    _pages.reset();
    _pageCount = 0;
    _z80.reset();
}

void MemoryAccessFacts::Clear()
{
    if (!_pages)
        return;
    if (_z80)
        std::fill_n(_z80.get(), kZ80Size, ByteFacts{});
    for (size_t i = 0; i < _pageCount; i++)
        if (ByteFacts* entries = _pages[i].load(std::memory_order_acquire))
            std::fill_n(entries, kPageSize, ByteFacts{});
}

ByteFacts* MemoryAccessFacts::AllocatePage(size_t page)
{
    // Only the emulation thread allocates; a reader sees the pointer after the zeroed page (release)
    ByteFacts* entries = new ByteFacts[kPageSize]();
    _pages[page].store(entries, std::memory_order_release);
    return entries;
}

bool MemoryAccessFacts::ReadZ80(uint16_t address, ByteFacts& out) const
{
    if (!_z80)
        return false;
    out = _z80[address];
    return true;
}

bool MemoryAccessFacts::ReadPhysical(uint16_t page, uint16_t offset, ByteFacts& out) const
{
    if (!_pages || page >= _pageCount || offset >= kPageSize)
        return false;
    const ByteFacts* entries = _pages[page].load(std::memory_order_acquire);
    if (entries == nullptr)
        return false;
    out = entries[offset];
    return true;
}

std::vector<uint16_t> MemoryAccessFacts::TouchedPages() const
{
    std::vector<uint16_t> pages;
    for (size_t i = 0; _pages && i < _pageCount; i++)
        if (_pages[i].load(std::memory_order_acquire) != nullptr)
            pages.push_back(static_cast<uint16_t>(i));
    return pages;
}

size_t MemoryAccessFacts::BytesAllocated() const
{
    if (!_pages)
        return 0;
    return (_z80 ? kZ80Size * sizeof(ByteFacts) : 0) + _pageCount * sizeof(std::atomic<ByteFacts*>) +
           TouchedPages().size() * kPageSize * sizeof(ByteFacts);
}

size_t MemoryAccessFacts::TouchedZ80Bytes() const
{
    size_t touched = 0;
    for (size_t i = 0; _z80 && i < kZ80Size; i++)
        touched += _z80[i].kinds != 0 ? 1 : 0;
    return touched;
}

std::vector<std::string> MemoryAccessFacts::KindList(uint8_t kinds)
{
    std::vector<std::string> names;
    for (unsigned bit = 0; bit < 8; bit++)
        if (kinds & (1u << bit))
            names.emplace_back(kKindNames[bit]);
    return names;
}

std::string MemoryAccessFacts::KindNames(uint8_t kinds)
{
    std::string text;
    for (const std::string& name : KindList(kinds))
        text += (text.empty() ? "" : ", ") + name;
    return text;
}
