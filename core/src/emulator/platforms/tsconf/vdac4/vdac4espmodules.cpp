#include "stdafx.h"

#include "vdac4espmodules.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

// Ports of VDAC4 Firmware src/zm_cache.cpp, src/path_table.cpp (MIT, andrewinsidelazarev).

namespace
{
uint32_t Read32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void Write32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

// --- path_table.cpp --------------------------------------------------------------------------------------------

bool HexValue(const char* text, size_t length, uint32_t& value)
{
    if (length == 0 || length > 8)
        return false;
    uint32_t result = 0;
    for (size_t i = 0; i < length; ++i)
    {
        const char c = text[i];
        uint32_t digit;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<uint32_t>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint32_t>(c - 'a' + 10);
        else
            return false;
        result = result << 4 | digit;
    }
    value = result;
    return true;
}

bool Space(char c)
{
    return c == ' ' || c == '\t';
}

bool NameChar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '~' || c == '-';
}

bool ReadWhole(const std::filesystem::path& path, std::vector<uint8_t>& out)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool WriteWhole(const std::filesystem::path& path, const uint8_t* data, size_t size)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    output.close();
    return !output.fail();
}
}  // namespace


// --- EspModuleCache --------------------------------------------------------------------------------------------

EspModuleCache::EspModuleCache(std::filesystem::path directory, size_t capacity)
    : _directory(std::move(directory)), _capacity(capacity)
{
}

bool EspModuleCache::StorageRead(const std::string& file, uint8_t* data, size_t size) const
{
    std::vector<uint8_t> bytes;
    if (!ReadWhole(_directory / file, bytes) || bytes.size() != size)
        return false;
    if (size != 0)
        std::memcpy(data, bytes.data(), size);
    return true;
}

bool EspModuleCache::StorageWrite(const std::string& file, const uint8_t* data, size_t size) const
{
    std::error_code ec;
    std::filesystem::create_directories(_directory, ec);
    return WriteWhole(_directory / file, data, size);
}

void EspModuleCache::StorageRemove(const std::string& file) const
{
    std::error_code ec;
    std::filesystem::remove(_directory / file, ec);
}

long EspModuleCache::StorageFileSize(const std::string& file) const
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(_directory / file, ec);
    return ec ? -1 : static_cast<long>(size);
}

bool EspModuleCache::ValidName(const char* name)
{
    if (name == nullptr)
        return false;
    size_t stem = 0;
    size_t extension = 0;
    bool dot = false;
    for (const char* c = name; *c != 0; ++c)
    {
        if (*c == '.')
        {
            if (dot || stem == 0)
                return false;
            dot = true;
        }
        else if (!NameChar(*c))
            return false;
        else if (dot)
            ++extension;
        else
            ++stem;
    }
    return stem >= 1 && stem <= 8 && extension <= 3 && (!dot || extension >= 1);
}

void EspModuleCache::Load()
{
    for (EspModuleCacheEntry& entry : _entries)
        entry = EspModuleCacheEntry{};
    _clock = 0;
    uint8_t index[kIndexBytes];
    if (!StorageRead(kIndex, index, sizeof(index)) || Read32(index) != kMagic)
        return;
    _clock = Read32(index + 4);
    bool changed = false;
    for (size_t i = 0; i < kMaxEntries; ++i)
    {
        const uint8_t* p = index + 8 + i * 28;
        EspModuleCacheEntry entry;
        std::memcpy(entry.name, p, sizeof(entry.name));
        entry.name[sizeof(entry.name) - 1] = 0;
        entry.size = Read32(p + 16);
        entry.stamp = Read32(p + 20);
        entry.used = Read32(p + 24);
        if (entry.name[0] == 0 || !ValidName(entry.name))
            continue;
        const long size = StorageFileSize(entry.name);
        if (size < 0 || static_cast<unsigned long>(size) != entry.size)
        {
            changed = true;
            continue;
        }
        _entries[i] = entry;
    }
    if (changed)
        SaveIndex();
    // Files the index does not know only take room
    std::error_code ec;
    std::vector<std::string> stray;
    for (const auto& item : std::filesystem::directory_iterator(_directory, ec))
    {
        if (!item.is_regular_file(ec))
            continue;
        const std::string file = item.path().filename().string();
        if (file != kIndex && Find(file.c_str()) == nullptr)
            stray.push_back(file);
    }
    for (const std::string& file : stray)
        StorageRemove(file);
}

bool EspModuleCache::SaveIndex()
{
    uint8_t index[kIndexBytes] = {};
    Write32(index, kMagic);
    Write32(index + 4, _clock);
    for (size_t i = 0; i < kMaxEntries; ++i)
    {
        uint8_t* p = index + 8 + i * 28;
        std::memcpy(p, _entries[i].name, sizeof(_entries[i].name));
        Write32(p + 16, _entries[i].size);
        Write32(p + 20, _entries[i].stamp);
        Write32(p + 24, _entries[i].used);
    }
    return StorageWrite(kIndex, index, sizeof(index));
}

const EspModuleCacheEntry* EspModuleCache::Find(const char* name) const
{
    if (name == nullptr)
        return nullptr;
    for (const EspModuleCacheEntry& entry : _entries)
    {
        if (entry.name[0] != 0 && std::strcmp(entry.name, name) == 0)
            return &entry;
    }
    return nullptr;
}

EspModuleCacheEntry* EspModuleCache::Slot(const char* name)
{
    return const_cast<EspModuleCacheEntry*>(Find(name));
}

void EspModuleCache::Drop(EspModuleCacheEntry* entry)
{
    const std::string name = entry->name;
    *entry = EspModuleCacheEntry{};
    SaveIndex();
    StorageRemove(name);
}

bool EspModuleCache::Has(const char* name, uint32_t size, uint32_t stamp) const
{
    const EspModuleCacheEntry* entry = Find(name);
    return entry != nullptr && entry->size == size && entry->stamp == stamp;
}

size_t EspModuleCache::Footprint(size_t size)
{
    return (size + kBlockBytes - 1) / kBlockBytes * kBlockBytes + kBlockBytes;
}

size_t EspModuleCache::FreeBytes() const
{
    size_t used = Footprint(kIndexBytes);
    for (const EspModuleCacheEntry& entry : _entries)
    {
        if (entry.name[0] != 0)
            used += Footprint(entry.size);
    }
    return _capacity > used ? _capacity - used : 0;
}

bool EspModuleCache::CanFit(uint32_t size, const char* busy) const
{
    size_t kept = Footprint(kIndexBytes);
    const EspModuleCacheEntry* held = busy != nullptr ? Find(busy) : nullptr;
    if (held != nullptr)
        kept += Footprint(held->size);
    return _capacity > kept && _capacity - kept >= Footprint(size);
}

size_t EspModuleCache::Count() const
{
    size_t total = 0;
    for (const EspModuleCacheEntry& entry : _entries)
    {
        if (entry.name[0] != 0)
            ++total;
    }
    return total;
}

bool EspModuleCache::EvictOne(const char* busy)
{
    EspModuleCacheEntry* oldest = nullptr;
    for (EspModuleCacheEntry& entry : _entries)
    {
        if (entry.name[0] == 0 || (busy != nullptr && std::strcmp(entry.name, busy) == 0))
            continue;
        if (oldest == nullptr || entry.used < oldest->used)
            oldest = &entry;
    }
    if (oldest == nullptr)
        return false;
    Drop(oldest);
    return true;
}

bool EspModuleCache::Store(const char* name, uint32_t stamp, const uint8_t* data, uint32_t size, const char* busy)
{
    if (!ValidName(name) || data == nullptr || size == 0)
        return false;
    if (EspModuleCacheEntry* old = Slot(name))
        Drop(old);
    while (FreeBytes() < Footprint(size))
    {
        if (!EvictOne(busy))
        {
            SaveIndex();
            return false;
        }
    }
    auto freeSlot = [this]() -> EspModuleCacheEntry* {
        for (EspModuleCacheEntry& entry : _entries)
        {
            if (entry.name[0] == 0)
                return &entry;
        }
        return nullptr;
    };
    EspModuleCacheEntry* slot = freeSlot();
    if (slot == nullptr)
    {
        if (!EvictOne(busy))
        {
            SaveIndex();
            return false;
        }
        slot = freeSlot();
    }
    if (!StorageWrite(name, data, size))
    {
        StorageRemove(name);
        SaveIndex();
        return false;
    }
    *slot = EspModuleCacheEntry{};
    std::strncpy(slot->name, name, sizeof(slot->name) - 1);
    slot->size = size;
    slot->stamp = stamp;
    slot->used = ++_clock;
    return SaveIndex();
}

bool EspModuleCache::Read(const char* name, uint8_t* data, uint32_t size)
{
    EspModuleCacheEntry* entry = Slot(name);
    if (entry == nullptr || entry->size != size)
        return false;
    if (!StorageRead(name, data, size))
    {
        Drop(entry);  // unreadable: the plugin sends it again
        return false;
    }
    entry->used = ++_clock;
    SaveIndex();
    return true;
}

// --- EspPathTable ----------------------------------------------------------------------------------------------

bool EspPathTable::NormalizeName(const char* name, size_t length, char out[13])
{
    if (length == 0 || length > kMaxName)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        char c = name[i];
        if (c < 0x21 || c > 0x7E || c == ';' || c == '#')
            return false;
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
        out[i] = c;
    }
    out[length] = 0;
    return true;
}

void EspPathTable::Parse(const char* text, size_t length)
{
    _count = 0;
    size_t at = 0;
    while (at < length)
    {
        size_t end = at;
        while (end < length && text[end] != '\n')
            ++end;
        const char* fields[3] = {};
        size_t sizes[3] = {};
        size_t found = 0;
        size_t i = at;
        while (i < end)
        {
            while (i < end && (Space(text[i]) || text[i] == '\r'))
                ++i;
            if (i == end)
                break;
            if (found == 0 && (text[i] == ';' || text[i] == '#'))
                break;
            const size_t start = i;
            while (i < end && !Space(text[i]) && text[i] != '\r')
                ++i;
            if (found < 3)
            {
                fields[found] = text + start;
                sizes[found] = i - start;
            }
            ++found;
        }
        EspPathEntry item;
        if (found == 3 && NormalizeName(fields[0], sizes[0], item.name) && HexValue(fields[1], sizes[1], item.serial) &&
            HexValue(fields[2], sizes[2], item.cluster))
            Set(item.name, item.serial, item.cluster);
        at = end + 1;
    }
}

std::string EspPathTable::Format() const
{
    std::string text = "; VDAC4: folders of Evo programs on SD cards (PATH_GET/PATH_SET).\n"
                       "; NAME, FAT32 volume serial, directory cluster (hex). A line may be deleted:\n"
                       "; the program then searches the card once and stores the folder again.\n";
    for (size_t i = 0; i < _count; ++i)
    {
        char line[48];
        std::snprintf(line, sizeof(line), "%-12s %08X %08X\n", _entries[i].name,
                      static_cast<unsigned>(_entries[i].serial), static_cast<unsigned>(_entries[i].cluster));
        text += line;
    }
    return text.size() < kMaxText ? text : std::string();
}

int EspPathTable::IndexOf(const char* name, uint32_t serial) const
{
    for (size_t i = 0; i < _count; ++i)
    {
        if (_entries[i].serial == serial && std::strcmp(_entries[i].name, name) == 0)
            return static_cast<int>(i);
    }
    return -1;
}

void EspPathTable::RemoveAt(size_t index)
{
    for (size_t i = index + 1; i < _count; ++i)
        _entries[i - 1] = _entries[i];
    --_count;
}

bool EspPathTable::Find(const char* name, uint32_t serial, uint32_t& cluster) const
{
    char key[13];
    if (!NormalizeName(name, std::strlen(name), key))
        return false;
    const int index = IndexOf(key, serial);
    if (index < 0)
        return false;
    cluster = _entries[index].cluster;
    return true;
}

bool EspPathTable::Set(const char* name, uint32_t serial, uint32_t cluster)
{
    char key[13];
    if (!NormalizeName(name, std::strlen(name), key))
        return false;
    const int index = IndexOf(key, serial);
    if (index >= 0)
        RemoveAt(static_cast<size_t>(index));
    else if (_count == kMaxEntries)
        RemoveAt(0);
    EspPathEntry& item = _entries[_count++];
    std::memcpy(item.name, key, sizeof(item.name));
    item.serial = serial;
    item.cluster = cluster;
    return true;
}

bool EspPathTable::LoadFile(const std::filesystem::path& directory)
{
    Clear();
    std::vector<uint8_t> bytes;
    if (!ReadWhole(directory / "path.cfg", bytes))
        return true;  // no file yet: empty table
    const size_t length = bytes.size() < kMaxText ? bytes.size() : kMaxText;
    Parse(reinterpret_cast<const char*>(bytes.data()), length);
    return true;
}

bool EspPathTable::SaveFile(const std::filesystem::path& directory) const
{
    const std::string text = Format();
    if (text.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const std::filesystem::path temp = directory / "path.tmp";
    if (!WriteWhole(temp, reinterpret_cast<const uint8_t*>(text.data()), text.size()))
    {
        std::filesystem::remove(temp, ec);
        return false;
    }
    std::filesystem::rename(temp, directory / "path.cfg", ec);
    return !ec;
}
