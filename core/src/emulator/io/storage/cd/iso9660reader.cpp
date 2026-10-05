#include "stdafx.h"

#include "iso9660reader.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "common/unicodehelper.h"

namespace
{
    uint32_t Get32(const uint8_t* p)
    {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    bool EqualsIgnoringAsciiCase(const std::string& a, const std::string& b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    }

    /// A UCS-2 big-endian name as UTF-8
    std::string Ucs2BeToUtf8(const uint8_t* p, size_t bytes)
    {
        std::u16string text;
        for (size_t i = 0; i + 1 < bytes; i += 2)
            text.push_back(static_cast<char16_t>((p[i] << 8) | p[i + 1]));
        return UnicodeHelper::EncodeUtf8(UnicodeHelper::FromUtf16(text));
    }

    /// "README.TXT;1" -> "README.TXT"; "DIR." -> "DIR"
    std::string WithoutVersion(std::string name)
    {
        const size_t semicolon = name.find(';');
        if (semicolon != std::string::npos)
            name.resize(semicolon);
        if (!name.empty() && name.back() == '.')
            name.pop_back();
        return name;
    }
}  // namespace

int64_t Iso9660Reader::RecordDate(const uint8_t* d)
{
    // years since 1900, month, day, hour, minute, second, offset from GMT in 15-minute steps
    const int64_t year = 1900 + d[0];
    const int64_t month = std::clamp<int64_t>(d[1], 1, 12);
    const int64_t day = std::clamp<int64_t>(d[2], 1, 31);
    const int64_t y = month <= 2 ? year - 1 : year;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097 + doe - 719468;
    const int64_t local = days * 86400 + d[3] * 3600 + d[4] * 60 + d[5];
    return local - static_cast<int8_t>(d[6]) * 15 * 60;
}

bool Iso9660Reader::ReadBlock(uint32_t block, uint8_t* dst)
{
    for (uint32_t i = 0; i < 4; i++)
    {
        if (!_device->ReadSector(static_cast<uint64_t>(block) * 4 + i, dst + i * 512))
            return false;
    }
    return true;
}

bool Iso9660Reader::Open(IBlockDevice& device, std::string* error, bool useJoliet)
{
    auto fail = [error](const std::string& text) {
        if (error)
            *error = text;
        return false;
    };
    _device = &device;
    _joliet = false;
    _useJoliet = false;
    _bootCatalog = 0;

    uint8_t block[kBlock];
    bool primary = false;
    uint8_t primaryRoot[34] = {};
    uint8_t jolietRoot[34] = {};
    for (uint32_t b = 16; b < 16 + 64; b++)
    {
        if (!ReadBlock(b, block))
            return fail("cannot read volume descriptor block " + std::to_string(b));
        if (std::memcmp(block + 1, "CD001", 5) != 0)
            return fail(primary ? "the volume descriptor set has no terminator" : "no ISO 9660 volume (no CD001 at block 16)");
        const uint8_t type = block[0];
        if (type == 255)
            break;
        if (type == 0 && std::memcmp(block + 7, "EL TORITO SPECIFICATION", 23) == 0)
            _bootCatalog = Get32(block + 71);
        else if (type == 1 && !primary)
        {
            primary = true;
            std::string id(reinterpret_cast<const char*>(block + 40), 32);
            while (!id.empty() && id.back() == ' ')
                id.pop_back();
            _volumeId = id;
            _volumeBlocks = Get32(block + 80);
            std::memcpy(primaryRoot, block + 156, 34);
        }
        else if (type == 2 && block[88] == '%' && block[89] == '/' && (block[90] == '@' || block[90] == 'C' || block[90] == 'E'))
        {
            _joliet = true;
            std::memcpy(jolietRoot, block + 156, 34);
        }
    }
    if (!primary)
        return fail("no primary volume descriptor");

    _useJoliet = _joliet && useJoliet;
    const uint8_t* root = _useJoliet ? jolietRoot : primaryRoot;
    _root = IsoDirEntry();
    _root.name = "/";
    _root.isDirectory = true;
    _root.size = Get32(root + 10);
    _root.sections.push_back({Get32(root + 2), Get32(root + 10)});
    _root.mtimeUtc = RecordDate(root + 18);
    return true;
}

bool Iso9660Reader::ReadBootCatalog(std::vector<IsoBootEntry>& entries, std::string* error)
{
    entries.clear();
    auto fail = [error](const std::string& text) {
        if (error)
            *error = text;
        return false;
    };
    if (_bootCatalog == 0)
        return fail("no El Torito boot record");
    uint8_t block[kBlock];
    if (!ReadBlock(_bootCatalog, block))
        return fail("cannot read the boot catalog at block " + std::to_string(_bootCatalog));
    // Validation entry: header #01, key #55 #AA, the 16-bit words summing to 0
    uint16_t sum = 0;
    for (int i = 0; i < 32; i += 2)
        sum = static_cast<uint16_t>(sum + (block[i] | (block[i + 1] << 8)));
    if (block[0] != 0x01 || block[30] != 0x55 || block[31] != 0xAA || sum != 0)
        return fail("the boot catalog's validation entry is damaged (checksum or key)");

    auto entry = [this](const uint8_t* e, uint8_t platform) {
        IsoBootEntry b;
        b.platform = platform;
        b.bootable = e[0] == 0x88;
        b.emulation = e[1] & 0x0F;
        b.loadSegment = static_cast<uint16_t>(e[2] | (e[3] << 8));
        b.systemType = e[4];
        b.sectorCount = static_cast<uint16_t>(e[6] | (e[7] << 8));
        b.loadBlock = Get32(e + 8);
        switch (b.emulation)
        {
            case 1: b.imageBytes = 1228800; break;
            case 2: b.imageBytes = 1474560; break;
            case 3: b.imageBytes = 2949120; break;
            case 4:
            {
                // The disk its MBR's partitions span
                uint8_t mbr[kBlock];
                if (ReadBlock(b.loadBlock, mbr))
                {
                    for (int p = 0; p < 4; p++)
                    {
                        const uint8_t* pe = mbr + 446 + p * 16;
                        b.imageBytes = std::max<uint64_t>(b.imageBytes, (static_cast<uint64_t>(Get32(pe + 8)) + Get32(pe + 12)) * 512);
                    }
                }
                break;
            }
            default: b.imageBytes = static_cast<uint64_t>(b.sectorCount ? b.sectorCount : 4) * 512; break;
        }
        return b;
    };
    entries.push_back(entry(block + 32, block[1]));
    // Section headers (#90: more follow, #91: the last) with their entries
    for (uint32_t at = 64; at + 32 <= kBlock;)
    {
        const uint8_t header = block[at];
        if (header != 0x90 && header != 0x91)
            break;
        const uint8_t platform = block[at + 1];
        const uint16_t count = static_cast<uint16_t>(block[at + 2] | (block[at + 3] << 8));
        at += 32;
        for (uint16_t i = 0; i < count && at + 32 <= kBlock; i++, at += 32)
        {
            while (at + 32 <= kBlock && block[at] == 0x44)
                at += 32;  // an entry extension
            if (at + 32 <= kBlock)
                entries.push_back(entry(block + at, platform));
        }
        if (header == 0x91)
            break;
    }
    return true;
}

bool Iso9660Reader::List(const IsoDirEntry& directory, std::vector<IsoDirEntry>& entries, std::string* error)
{
    entries.clear();
    if (!directory.isDirectory || directory.sections.empty())
    {
        if (error)
            *error = "not a directory: " + directory.name;
        return false;
    }
    const uint32_t first = directory.sections.front().first;
    const uint32_t blocks = static_cast<uint32_t>((directory.size + kBlock - 1) / kBlock);
    uint8_t block[kBlock];
    bool continuing = false;  // the previous record was a section with more to come
    for (uint32_t b = 0; b < blocks; b++)
    {
        if (!ReadBlock(first + b, block))
        {
            if (error)
                *error = "cannot read directory block " + std::to_string(first + b);
            return false;
        }
        for (uint32_t at = 0; at < kBlock;)
        {
            const uint8_t length = block[at];
            if (length == 0)
                break;  // the rest of the block is padding
            if (length < 34 || at + length > kBlock)
            {
                if (error)
                    *error = "a damaged directory record in block " + std::to_string(first + b);
                return false;
            }
            const uint8_t* r = block + at;
            at += length;
            const uint8_t nameLength = r[32];
            if (nameLength == 1 && (r[33] == 0 || r[33] == 1))
                continue;  // "." and ".."
            const uint8_t flags = r[25];
            const uint32_t extent = Get32(r + 2);
            const uint32_t bytes = Get32(r + 10);
            if (continuing && !entries.empty())
            {
                IsoDirEntry& file = entries.back();
                file.sections.push_back({extent, bytes});
                file.size += bytes;
            }
            else
            {
                IsoDirEntry entry;
                const std::string raw(reinterpret_cast<const char*>(r + 33), nameLength);
                entry.isoName = _useJoliet ? Ucs2BeToUtf8(r + 33, nameLength) : raw;
                entry.name = _useJoliet ? WithoutVersion(entry.isoName) : WithoutVersion(raw);
                entry.isDirectory = (flags & 0x02) != 0;
                entry.hidden = (flags & 0x01) != 0;
                entry.size = bytes;
                entry.mtimeUtc = RecordDate(r + 18);
                entry.sections.push_back({extent, bytes});
                entries.push_back(std::move(entry));
            }
            continuing = (flags & 0x80) != 0;
        }
    }
    return true;
}

bool Iso9660Reader::Stat(const std::string& path, IsoDirEntry& entry, std::string* error)
{
    entry = _root;
    size_t pos = 0;
    while (pos < path.size())
    {
        const size_t slash = path.find('/', pos);
        const std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        pos = slash == std::string::npos ? path.size() : slash + 1;
        if (part.empty())
            continue;
        std::vector<IsoDirEntry> entries;
        if (!List(entry, entries, error))
            return false;
        auto it = std::find_if(entries.begin(), entries.end(), [&part](const IsoDirEntry& e) {
            return EqualsIgnoringAsciiCase(e.name, part) || EqualsIgnoringAsciiCase(e.isoName, part);
        });
        if (it == entries.end())
        {
            if (error)
                *error = "no such entry: " + path;
            return false;
        }
        entry = *it;
    }
    return true;
}

bool Iso9660Reader::ReadFile(const std::string& path, std::vector<uint8_t>& data, std::string* error)
{
    data.clear();
    IsoDirEntry entry;
    if (!Stat(path, entry, error))
        return false;
    if (entry.isDirectory)
    {
        if (error)
            *error = "a directory, not a file: " + path;
        return false;
    }
    uint8_t block[kBlock];
    for (const auto& [first, bytes] : entry.sections)
    {
        for (uint32_t done = 0, b = first; done < bytes; b++)
        {
            if (!ReadBlock(b, block))
            {
                if (error)
                    *error = "cannot read block " + std::to_string(b);
                return false;
            }
            const uint32_t take = std::min<uint32_t>(kBlock, bytes - done);
            data.insert(data.end(), block, block + take);
            done += take;
        }
    }
    return true;
}
