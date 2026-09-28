#include "stdafx.h"

#include "sessionwritemap.h"

#include <cstring>
#include <fstream>

SessionWriteMap::SessionWriteMap(std::unique_ptr<IBlockDevice> base) : _base(std::move(base)) {}

bool SessionWriteMap::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= SectorCount())
        return false;

    const auto it = _sectors.find(lba);
    if (it != _sectors.end())
    {
        std::memcpy(dst, it->second.data(), kSectorSize);
        return true;
    }
    return _base->ReadSector(lba, dst);
}

bool SessionWriteMap::WriteSector(uint64_t lba, const uint8_t* src)
{
    if (lba >= SectorCount())
        return false;

    // Writing the medium's own contents back frees the entry
    uint8_t original[kSectorSize];
    if (_base->ReadSector(lba, original) && std::memcmp(original, src, kSectorSize) == 0)
    {
        _sectors.erase(lba);
        return true;
    }

    std::memcpy(_sectors[lba].data(), src, kSectorSize);
    return true;
}

uint64_t SessionWriteMap::ContentId() const
{
    // The medium's id, moved by every change (FNV-1a over lba + data)
    uint64_t h = _base->ContentId();
    for (const auto& [lba, data] : _sectors)
    {
        for (int i = 0; i < 8; i++)
        {
            h ^= static_cast<uint8_t>(lba >> (8 * i));
            h *= 0x100000001b3ULL;
        }
        for (const uint8_t byte : data)
        {
            h ^= byte;
            h *= 0x100000001b3ULL;
        }
    }
    return h;
}

bool SessionWriteMap::ExportTo(const std::string& path, std::string* error)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        if (error)
            *error = "cannot create " + path;
        return false;
    }

    uint8_t sector[kSectorSize];
    for (uint64_t lba = 0; lba < SectorCount(); lba++)
    {
        if (!ReadSector(lba, sector))
        {
            if (error)
                *error = "cannot read sector " + std::to_string(lba);
            return false;
        }
        out.write(reinterpret_cast<const char*>(sector), static_cast<std::streamsize>(kSectorSize));
    }

    out.flush();
    if (!out && error)
        *error = "write error on " + path;
    return static_cast<bool>(out);
}
