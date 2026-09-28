#include "stdafx.h"

#include "fatvolumereader.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{
    constexpr uint32_t kSector = 512;

    uint16_t Get16(const uint8_t* p)
    {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }
    uint32_t Get32(const uint8_t* p)
    {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    bool IsPowerOfTwo(uint32_t v)
    {
        return v != 0 && (v & (v - 1)) == 0;
    }

    /// A boot sector with a plausible BPB (what readers check before trusting it)
    bool LooksLikeBootSector(const uint8_t* s)
    {
        return (s[0] == 0xEB || s[0] == 0xE9) && Get16(s + 11) == kSector && IsPowerOfTwo(s[13]) && s[13] <= 128 &&
               Get16(s + 14) != 0 && s[16] != 0 && s[510] == 0x55 && s[511] == 0xAA;
    }

    bool IsFatPartitionType(uint8_t type)
    {
        switch (type)
        {
            case 0x01: case 0x04: case 0x06: case 0x0B: case 0x0C: case 0x0E:
                return true;
            default:
                return false;
        }
    }

    bool EqualsIgnoringAsciiCase(const std::string& a, const std::string& b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    }

    std::vector<std::string> SplitPath(const std::string& path)
    {
        std::vector<std::string> parts;
        std::string part;
        for (char c : path)
        {
            if (c == '/' || c == '\\')
            {
                if (!part.empty())
                    parts.push_back(part);
                part.clear();
            }
            else
                part.push_back(c);
        }
        if (!part.empty())
            parts.push_back(part);
        return parts;
    }
}  // namespace

bool FatVolumeReader::Sector(uint64_t volumeLba, uint8_t* dst)
{
    return _device->ReadSector(_volumeStart + volumeLba, dst);
}

bool FatVolumeReader::Open(IBlockDevice& device, CodePage page, std::string* error)
{
    auto fail = [error](const char* text) {
        if (error)
            *error = text;
        return false;
    };

    _device = &device;
    _page = page;
    uint8_t s[kSector];
    if (!device.ReadSector(0, s))
        return fail("cannot read sector 0");

    // Sector 0 is either a boot sector (superfloppy) or an MBR
    _volumeStart = 0;
    if (!LooksLikeBootSector(s))
    {
        if (s[510] != 0x55 || s[511] != 0xAA)
            return fail("no boot sector or partition table signature");
        bool found = false;
        for (int i = 0; i < 4 && !found; i++)
        {
            const uint8_t* entry = s + 446 + i * 16;
            if (IsFatPartitionType(entry[4]) && Get32(entry + 8) != 0)
            {
                _volumeStart = Get32(entry + 8);
                found = true;
            }
        }
        if (!found)
            return fail("no FAT partition in the partition table");
        if (!device.ReadSector(_volumeStart, s) || !LooksLikeBootSector(s))
            return fail("the partition does not start with a FAT boot sector");
    }

    _sectorsPerCluster = s[13];
    _reservedSectors = Get16(s + 14);
    _fats = s[16];
    _rootEntries = Get16(s + 17);
    const uint32_t totalSectors = Get16(s + 19) ? Get16(s + 19) : Get32(s + 32);
    _fatSectors = Get16(s + 22) ? Get16(s + 22) : Get32(s + 36);
    _rootDirSectors = (_rootEntries * 32 + kSector - 1) / kSector;
    if (_fatSectors == 0 || totalSectors == 0)
        return fail("the boot sector has no FAT size or no volume size");

    const uint64_t overhead = _reservedSectors + static_cast<uint64_t>(_fats) * _fatSectors + _rootDirSectors;
    if (overhead >= totalSectors)
        return fail("the boot sector's sizes do not add up");
    _clusterCount = static_cast<uint32_t>((totalSectors - overhead) / _sectorsPerCluster);

    if (_clusterCount <= 4085)
        _type = FatReaderType::Fat12;
    else if (_clusterCount <= 65525)
        _type = FatReaderType::Fat16;
    else
        _type = FatReaderType::Fat32;

    if (_type == FatReaderType::Fat32)
    {
        if (_rootEntries != 0)
            return fail("FAT32-sized volume with a fixed root directory");
        _rootCluster = Get32(s + 44);
    }
    else if (_rootEntries == 0)
        return fail("a FAT12 / FAT16 volume needs a fixed root directory (the cluster count says FAT12 / FAT16)");

    // The label is the root directory's volume entry (the BPB copy is informative)
    _label.clear();
    std::vector<FatDirEntryInfo> ignored;
    return ReadDirectory(_type == FatReaderType::Fat32 ? _rootCluster : 0, _type != FatReaderType::Fat32, ignored, error);
}

bool FatVolumeReader::IsEndOfChain(uint32_t value) const
{
    switch (_type)
    {
        case FatReaderType::Fat12: return value >= 0xFF8;
        case FatReaderType::Fat16: return value >= 0xFFF8;
        case FatReaderType::Fat32: return (value & 0x0FFFFFFF) >= 0x0FFFFFF8;
    }
    return true;
}

uint32_t FatVolumeReader::NextCluster(uint32_t cluster)
{
    uint8_t s[kSector * 2];
    uint64_t byteOffset = 0;
    switch (_type)
    {
        case FatReaderType::Fat12: byteOffset = cluster + cluster / 2; break;
        case FatReaderType::Fat16: byteOffset = static_cast<uint64_t>(cluster) * 2; break;
        case FatReaderType::Fat32: byteOffset = static_cast<uint64_t>(cluster) * 4; break;
    }
    const uint64_t sector = _reservedSectors + byteOffset / kSector;
    const uint32_t within = static_cast<uint32_t>(byteOffset % kSector);
    if (!Sector(sector, s) || !Sector(sector + 1, s + kSector))
        return 0x0FFFFFFF;

    switch (_type)
    {
        case FatReaderType::Fat12:
        {
            const uint16_t v = Get16(s + within);
            return (cluster & 1) ? (v >> 4) : (v & 0x0FFF);
        }
        case FatReaderType::Fat16: return Get16(s + within);
        case FatReaderType::Fat32: return Get32(s + within) & 0x0FFFFFFF;
    }
    return 0x0FFFFFFF;
}

bool FatVolumeReader::ReadClusterChain(uint32_t firstCluster, uint64_t maxBytes, std::vector<uint8_t>& data, std::string* error)
{
    const uint64_t dataStart = _reservedSectors + static_cast<uint64_t>(_fats) * _fatSectors + _rootDirSectors;
    uint32_t cluster = firstCluster;
    uint32_t steps = 0;
    uint8_t s[kSector];
    while (data.size() < maxBytes)
    {
        if (cluster < 2 || cluster >= _clusterCount + 2)
        {
            if (error)
                *error = "a cluster chain leaves the volume at cluster " + std::to_string(cluster);
            return false;
        }
        if (++steps > _clusterCount)
        {
            if (error)
                *error = "a cluster chain loops";
            return false;
        }
        const uint64_t first = dataStart + static_cast<uint64_t>(cluster - 2) * _sectorsPerCluster;
        for (uint32_t i = 0; i < _sectorsPerCluster && data.size() < maxBytes; i++)
        {
            if (!Sector(first + i, s))
            {
                if (error)
                    *error = "cannot read a data sector";
                return false;
            }
            const size_t take = static_cast<size_t>(std::min<uint64_t>(kSector, maxBytes - data.size()));
            data.insert(data.end(), s, s + take);
        }
        const uint32_t next = NextCluster(cluster);
        if (IsEndOfChain(next))
            break;
        cluster = next;
    }
    return true;
}

bool FatVolumeReader::ReadDirectory(uint32_t firstCluster, bool fixedRoot, std::vector<FatDirEntryInfo>& entries,
                                    std::string* error)
{
    std::vector<uint8_t> bytes;
    if (fixedRoot)
    {
        uint8_t s[kSector];
        const uint64_t start = _reservedSectors + static_cast<uint64_t>(_fats) * _fatSectors;
        for (uint32_t i = 0; i < _rootDirSectors; i++)
        {
            if (!Sector(start + i, s))
            {
                if (error)
                    *error = "cannot read the root directory";
                return false;
            }
            bytes.insert(bytes.end(), s, s + kSector);
        }
    }
    else if (!ReadClusterChain(firstCluster, UINT64_MAX, bytes, error))
        return false;

    // Long-name pieces collected in front of their short entry
    std::u16string longName;
    uint8_t longChecksum = 0;
    int expectedSequence = 0;
    bool longValid = false;

    for (size_t at = 0; at + 32 <= bytes.size(); at += 32)
    {
        const uint8_t* e = bytes.data() + at;
        if (e[0] == 0x00)
            break;  // end of directory
        if (e[0] == 0xE5)
        {
            longValid = false;  // deleted
            continue;
        }

        const uint8_t attr = e[11];
        if ((attr & 0x3F) == 0x0F)
        {
            static constexpr size_t kOffsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            const int sequence = e[0] & 0x1F;
            if (e[0] & 0x40)
            {
                longName.assign(static_cast<size_t>(sequence) * 13, u'\0');
                longChecksum = e[13];
                expectedSequence = sequence;
                longValid = sequence > 0;
            }
            if (!longValid || sequence != expectedSequence || e[13] != longChecksum)
            {
                longValid = false;
                continue;
            }
            for (size_t k = 0; k < 13; k++)
                longName[(sequence - 1) * 13 + k] = static_cast<char16_t>(Get16(e + kOffsets[k]));
            expectedSequence--;
            continue;
        }

        uint8_t shortBytes[11];
        std::memcpy(shortBytes, e, 11);
        if (shortBytes[0] == 0x05)
            shortBytes[0] = 0xE5;

        if (attr & 0x08)
        {
            if (!(attr & 0x10) && _label.empty() && fixedRoot == (_type != FatReaderType::Fat32))
            {
                std::string label;
                for (uint8_t b : shortBytes)
                    UnicodeHelper::AppendUtf8(label, UnicodeHelper::FromCodePage(_page, b));
                while (!label.empty() && label.back() == ' ')
                    label.pop_back();
                _label = label;
            }
            longValid = false;
            continue;
        }

        FatDirEntryInfo info;
        auto decode = [this, &shortBytes](size_t from, size_t length, bool lower) {
            std::string text;
            size_t end = from + length;
            while (end > from && shortBytes[end - 1] == ' ')
                end--;
            for (size_t i = from; i < end; i++)
            {
                uint8_t b = shortBytes[i];
                if (lower && b >= 'A' && b <= 'Z')
                    b = static_cast<uint8_t>(b + 32);
                UnicodeHelper::AppendUtf8(text, UnicodeHelper::FromCodePage(_page, b));
            }
            return text;
        };
        const std::string base = decode(0, 8, (e[12] & 0x08) != 0);
        const std::string ext = decode(8, 3, (e[12] & 0x10) != 0);
        info.shortName = ext.empty() ? base : base + "." + ext;

        if (info.shortName == "." || info.shortName == "..")
        {
            longValid = false;
            continue;
        }

        // Check the long name against this short entry
        uint8_t sum = 0;
        for (uint8_t b : std::vector<uint8_t>(e, e + 11))
            sum = static_cast<uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + b);
        if (longValid && expectedSequence == 0 && sum == longChecksum)
        {
            const size_t end = longName.find(u'\0');
            info.name = UnicodeHelper::EncodeUtf8(UnicodeHelper::FromUtf16(longName.substr(0, end)));
        }
        else
            info.name = info.shortName;
        longValid = false;

        info.attributes = attr;
        info.isDirectory = (attr & 0x10) != 0;
        info.firstCluster = (static_cast<uint32_t>(Get16(e + 20)) << 16) | Get16(e + 26);
        if (_type != FatReaderType::Fat32)
            info.firstCluster &= 0xFFFF;
        info.size = Get32(e + 28);
        info.time = Get16(e + 22);
        info.date = Get16(e + 24);
        entries.push_back(info);
    }
    return true;
}

bool FatVolumeReader::Find(const std::string& path, FatDirEntryInfo& found, std::string* error)
{
    found = FatDirEntryInfo();
    found.isDirectory = true;
    found.firstCluster = 0;  // the root

    for (const std::string& part : SplitPath(path))
    {
        if (!found.isDirectory)
        {
            if (error)
                *error = "not a directory on the way to " + path;
            return false;
        }
        std::vector<FatDirEntryInfo> entries;
        const bool root = found.firstCluster == 0;
        if (!ReadDirectory(root ? _rootCluster : found.firstCluster, root && _type != FatReaderType::Fat32, entries, error))
            return false;
        auto it = std::find_if(entries.begin(), entries.end(), [&part](const FatDirEntryInfo& e) {
            return EqualsIgnoringAsciiCase(e.name, part) || EqualsIgnoringAsciiCase(e.shortName, part);
        });
        if (it == entries.end())
        {
            if (error)
                *error = "no such entry: " + path;
            return false;
        }
        found = *it;
    }
    return true;
}

bool FatVolumeReader::List(const std::string& path, std::vector<FatDirEntryInfo>& entries, std::string* error)
{
    entries.clear();
    FatDirEntryInfo dir;
    if (!Find(path, dir, error))
        return false;
    if (!dir.isDirectory)
    {
        if (error)
            *error = "not a directory: " + path;
        return false;
    }
    const bool root = dir.firstCluster == 0;
    return ReadDirectory(root ? _rootCluster : dir.firstCluster, root && _type != FatReaderType::Fat32, entries, error);
}

bool FatVolumeReader::ReadFile(const std::string& path, std::vector<uint8_t>& data, std::string* error)
{
    data.clear();
    FatDirEntryInfo file;
    if (!Find(path, file, error))
        return false;
    if (file.isDirectory)
    {
        if (error)
            *error = "a directory, not a file: " + path;
        return false;
    }
    if (file.size == 0)
        return true;
    return ReadClusterChain(file.firstCluster, file.size, data, error);
}
