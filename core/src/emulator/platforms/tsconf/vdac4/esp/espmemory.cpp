#include "espmemory.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "esprom.h"

namespace vdac4
{

namespace
{
constexpr uint32_t kGranule = 16;

uint32_t RoundUp(uint32_t size)
{
    return (size + kGranule - 1) & ~(kGranule - 1);
}
}  // namespace

/// region <EspHeap>

EspHeap::EspHeap(uint32_t base, uint32_t size) : _base(base), _size(size) {}

uint32_t EspHeap::Alloc(uint32_t size)
{
    if (size == 0)
        size = 1;
    if (size > _size)
        return 0;
    const uint32_t need = RoundUp(size);
    uint32_t cursor = _base;
    for (const auto& [start, length] : _used)
    {
        if (start - cursor >= need)
            break;
        cursor = start + length;
    }
    if (cursor - _base > _size - need)
        return 0;
    _used[cursor] = need;
    return cursor;
}

bool EspHeap::Free(uint32_t address)
{
    return _used.erase(address) != 0;
}

uint32_t EspHeap::BlockSize(uint32_t address) const
{
    auto it = _used.find(address);
    return it == _used.end() ? 0 : it->second;
}

uint32_t EspHeap::FreeBytes() const
{
    uint32_t used = 0;
    for (const auto& block : _used)
        used += block.second;
    return _size - used;
}

void EspHeap::Clear()
{
    _used.clear();
}

/// endregion

/// region <EspMemory>

EspMemory::EspMemory()
    : _psram(kPsramSize), _sram(kSramSize), _rom(kEspRomSize), _psramHeap(kPsramDbus, kPsramSize),
      _sramHeap(kSramBase, kSramSize)
{
    Reset();
}

void EspMemory::Reset()
{
    std::fill(_psram.begin(), _psram.end(), 0);
    std::fill(_sram.begin(), _sram.end(), 0);
    std::fill(_rom.begin(), _rom.end(), 0);
    std::memcpy(_rom.data(), kEspRomImage, kEspRomImageSize);
    _psramHeap.Clear();
    _sramHeap.Clear();
}

const uint8_t* EspMemory::Bytes(uint32_t address, uint32_t size) const
{
    return const_cast<EspMemory*>(this)->Bytes(address, size);
}

uint8_t* EspMemory::Bytes(uint32_t address, uint32_t size)
{
    auto inside = [&](uint32_t base, uint32_t length) {
        return address >= base && address - base <= length && size <= length - (address - base);
    };
    if (inside(kPsramDbus, kPsramSize))
        return _psram.data() + (address - kPsramDbus);
    if (inside(kPsramIbus, kPsramSize))
        return _psram.data() + (address - kPsramIbus);
    if (inside(kSramBase, kSramSize))
        return _sram.data() + (address - kSramBase);
    if (inside(kEspRomBase, kEspRomSize))
        return _rom.data() + (address - kEspRomBase);
    return nullptr;
}

uint32_t EspMemory::Read32(uint32_t address) const
{
    const uint8_t* p = Bytes(address, 4);
    if (p == nullptr)
        return 0;
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void EspMemory::Write32(uint32_t address, uint32_t value)
{
    uint8_t* p = Bytes(address, 4);
    if (p == nullptr)
        return;
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

std::string EspMemory::ReadString(uint32_t address, size_t max) const
{
    std::string text;
    for (size_t i = 0; i < max; i++)
    {
        const uint8_t* p = Bytes(address + static_cast<uint32_t>(i), 1);
        if (p == nullptr || *p == 0)
            break;
        text.push_back(static_cast<char>(*p));
    }
    return text;
}

void EspMemory::WriteString(uint32_t address, const std::string& text)
{
    uint8_t* p = Bytes(address, static_cast<uint32_t>(text.size() + 1));
    if (p != nullptr)
        std::memcpy(p, text.c_str(), text.size() + 1);
}

uint32_t EspMemory::HeapCapsMalloc(uint32_t size, uint32_t caps)
{
    bool internal;
    if (caps & kCapInternal)
        internal = true;
    else if (caps & kCapSpiram)
        internal = false;
    else
        internal = size <= kAlwaysInternal;
    uint32_t address = internal ? _sramHeap.Alloc(size) : _psramHeap.Alloc(size);
    if (address == 0)
        address = internal ? _psramHeap.Alloc(size) : _sramHeap.Alloc(size);
    return address;
}

void EspMemory::HeapCapsFree(uint32_t address)
{
    if (!_psramHeap.Free(address))
        _sramHeap.Free(address);
}

uint32_t EspMemory::AllocatedSize(uint32_t address) const
{
    const uint32_t size = _psramHeap.BlockSize(address);
    return size != 0 ? size : _sramHeap.BlockSize(address);
}

/// endregion

}  // namespace vdac4
