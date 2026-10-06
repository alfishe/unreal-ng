#include "stdafx.h"

#include "partitioneddisk.h"

#include <algorithm>
#include <cstring>

namespace
{
    constexpr uint64_t kSector = IBlockDevice::kSectorSize;
    constexpr uint64_t kChsLimit = 1024ull * 255 * 63;  ///< sectors a CHS address reaches

    void Put32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            p[i] = static_cast<uint8_t>(v >> (8 * i));
    }

    void Chs(uint8_t* p, uint64_t lba)
    {
        uint32_t c = 1023, h = 254, s = 63;
        if (lba < kChsLimit)
        {
            c = static_cast<uint32_t>(lba / (255 * 63));
            h = static_cast<uint32_t>((lba / 63) % 255);
            s = static_cast<uint32_t>(lba % 63 + 1);
        }
        p[0] = static_cast<uint8_t>(h);
        p[1] = static_cast<uint8_t>((s & 0x3F) | ((c >> 2) & 0xC0));
        p[2] = static_cast<uint8_t>(c);
    }

    uint64_t AlignUp(uint64_t lba)
    {
        return (lba + PartitionedDisk::kAlign - 1) / PartitionedDisk::kAlign * PartitionedDisk::kAlign;
    }

    /// A FAT boot sector: a jump, 512 bytes per sector, a power-of-two cluster size, #55AA
    bool IsFatBootSector(const uint8_t* s)
    {
        const uint8_t spc = s[13];
        return (s[0] == 0xEB || s[0] == 0xE9) && s[11] == 0x00 && s[12] == 0x02 && spc && (spc & (spc - 1)) == 0 &&
               s[510] == 0x55 && s[511] == 0xAA;
    }
}  // namespace

uint8_t PartitionedDisk::FatType(int bits, uint64_t start, uint64_t sectors)
{
    const bool beyondChs = start + sectors > kChsLimit;
    if (bits == 12)
        return 0x01;
    if (bits == 32)
        return beyondChs ? 0x0C : 0x0B;
    if (beyondChs)
        return 0x0E;
    return sectors * kSector < 32ull * 1024 * 1024 ? 0x04 : 0x06;
}

std::unique_ptr<PartitionedDisk> PartitionedDisk::Build(std::vector<Part> parts, std::optional<uint64_t> totalSectors,
                                                        std::string description, std::string* error)
{
    if (parts.empty())
    {
        if (error)
            *error = "no partitions";
        return nullptr;
    }
    std::unique_ptr<PartitionedDisk> disk(new PartitionedDisk());
    disk->_description = std::move(description);
    disk->_tables.push_back(0);
    const bool extended = parts.size() > 4;
    uint64_t next = kAlign;
    for (size_t i = 0; i < parts.size(); i++)
    {
        Part& p = parts[i];
        if (p.sectors == 0 || !p.device)
        {
            if (error)
                *error = "partition '" + p.name + "' is empty";
            return nullptr;
        }
        p.logical = extended && i >= 3;
        if (p.logical)
        {
            // Its EBR, then the partition 1 MiB later
            const uint64_t ebr = AlignUp(next);
            if (i == 3)
                disk->_extendedStart = ebr;
            disk->_tables.push_back(ebr);
            p.start = ebr + kAlign;
        }
        else
            p.start = AlignUp(next);
        if (p.type == 0)
            p.type = FatType(p.fatBits ? p.fatBits : 16, p.start, p.sectors);
        next = p.start + p.sectors;
    }
    if (extended)
        disk->_extendedEnd = next;
    if (totalSectors && *totalSectors < next)
    {
        if (error)
            *error = "the partitions need " + std::to_string(next * kSector) + " bytes, the disk is " +
                     std::to_string(*totalSectors * kSector);
        return nullptr;
    }
    disk->_totalSectors = totalSectors.value_or(next);
    disk->_parts = std::move(parts);

    uint64_t id = 0xcbf29ce484222325ULL;
    auto mix = [&id](uint64_t v) {
        for (int i = 0; i < 8; i++)
        {
            id ^= static_cast<uint8_t>(v >> (8 * i));
            id *= 0x100000001b3ULL;
        }
    };
    mix(disk->_totalSectors);
    for (const Part& p : disk->_parts)
    {
        mix(p.device->ContentId());
        mix(p.start);
        mix(p.sectors);
        mix(p.type);
    }
    disk->_contentId = id;
    return disk;
}

void PartitionedDisk::Entry(uint8_t* e, uint8_t type, uint64_t first, uint64_t count, uint64_t absoluteFirst)
{
    e[0] = 0x00;
    Chs(e + 1, absoluteFirst);
    e[4] = type;
    Chs(e + 5, absoluteFirst + count - 1);
    Put32(e + 8, static_cast<uint32_t>(first));
    Put32(e + 12, static_cast<uint32_t>(std::min<uint64_t>(count, 0xFFFFFFFFu)));
}

void PartitionedDisk::BuildTable(uint64_t lba, uint8_t* dst) const
{
    std::memset(dst, 0, kSector);
    dst[510] = 0x55;
    dst[511] = 0xAA;
    if (lba == 0)
    {
        if (_mbrCode.empty())
            Put32(dst + 440, static_cast<uint32_t>(_contentId ^ (_contentId >> 32)));  // disk signature
        else
            std::copy(_mbrCode.begin(), _mbrCode.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(_mbrCode.size(), 446)), dst);
        int slot = 0;
        for (const Part& p : _parts)
        {
            if (p.logical)
                break;
            Entry(dst + 446 + 16 * slot, p.type, p.start, p.sectors, p.start);
            if (slot++ == 0)
                dst[446] = 0x80;  // the first partition boots
        }
        if (_extendedStart)
            Entry(dst + 446 + 16 * slot, 0x0F, _extendedStart, _extendedEnd - _extendedStart, _extendedStart);
        return;
    }
    // An EBR: its logical partition, then the link to the next EBR (relative to the extended partition)
    const auto it = std::find(_tables.begin(), _tables.end(), lba);
    const size_t index = static_cast<size_t>(it - _tables.begin());  // 1.. : the n-th logical partition
    const Part* logical = nullptr;
    size_t seen = 0;
    for (const Part& p : _parts)
    {
        if (p.logical && ++seen == index)
            logical = &p;
    }
    if (!logical)
        return;
    Entry(dst + 446, logical->type, logical->start - lba, logical->sectors, logical->start);
    if (index + 1 < _tables.size())
    {
        const uint64_t nextEbr = _tables[index + 1];
        // The link covers the next EBR and its partition
        uint64_t nextEnd = nextEbr;
        seen = 0;
        for (const Part& p : _parts)
        {
            if (p.logical && ++seen == index + 1)
                nextEnd = p.start + p.sectors;
        }
        Entry(dst + 446 + 16, 0x05, nextEbr - _extendedStart, nextEnd - nextEbr, nextEbr);
    }
}

bool PartitionedDisk::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _totalSectors)
        return false;
    if (std::find(_tables.begin(), _tables.end(), lba) != _tables.end())
    {
        BuildTable(lba, dst);
        return true;
    }
    for (const Part& p : _parts)
    {
        if (lba < p.start || lba >= p.start + p.sectors)
            continue;
        const uint64_t rel = lba - p.start;
        if (rel >= p.device->SectorCount())
        {
            std::memset(dst, 0, kSector);
            return true;
        }
        if (!p.device->ReadSector(rel, dst))
            return false;
        // The boot sector (and FAT32's backup) names where the volume starts on this disk
        if ((rel == 0 || rel == 6) && IsFatBootSector(dst))
            Put32(dst + 28, static_cast<uint32_t>(p.start));
        return true;
    }
    std::memset(dst, 0, kSector);
    return true;
}

std::string PartitionedDisk::Describe() const
{
    std::string text = _description + " (" + std::to_string(_parts.size()) + " partitions:";
    for (const Part& p : _parts)
        text += " " + p.name;
    return text + ")";
}
