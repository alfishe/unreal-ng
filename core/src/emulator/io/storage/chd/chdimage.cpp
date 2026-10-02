#include "stdafx.h"

#include "chdimage.h"

#include <cstring>

#include "emulator/io/storage/chd/chdwriter.h"

std::unique_ptr<ChdImage> ChdImage::Open(const std::string& path, std::string* error)
{
    auto file = chd::ChdFile::Open(path, error);
    if (!file)
        return nullptr;
    if (auto text = file->MetadataText(chd::kTagHardDisk))
    {
        BlockGeometry geometry;
        uint32_t sectorBytes = 0;
        if (chd::ParseHardDiskMetadata(*text, geometry, sectorBytes) && sectorBytes != kSectorSize)
        {
            if (error)
                *error = path + ": a hard disk of " + std::to_string(sectorBytes) + "-byte sectors (only 512-byte sectors are supported)";
            return nullptr;
        }
    }
    return std::make_unique<ChdImage>(std::move(file));
}

ChdImage::ChdImage(std::unique_ptr<chd::ChdFile> file) : _file(std::move(file))
{
    _sectors = (_file->LogicalBytes() + kSectorSize - 1) / kSectorSize;
    if (auto text = _file->MetadataText(chd::kTagHardDisk))
    {
        BlockGeometry geometry;
        uint32_t sectorBytes = 0;
        if (chd::ParseHardDiskMetadata(*text, geometry, sectorBytes) && geometry.heads && geometry.sectors)
            _geometry = geometry;
    }

    // The same file at the same content is the same medium: path and overall SHA-1
    uint64_t h = 0xcbf29ce484222325ULL;
    auto mix = [&h](uint8_t b) {
        h ^= b;
        h *= 0x100000001b3ULL;
    };
    for (const char c : _file->Path())
        mix(static_cast<uint8_t>(c));
    for (uint8_t b : _file->OverallSha1())
        mix(b);
    for (int i = 0; i < 8; i++)
        mix(static_cast<uint8_t>(_file->LogicalBytes() >> (8 * i)));
    _contentId = h;

    _cache.resize(static_cast<size_t>(kCacheHunks) * _file->HunkBytes());
    _cacheTags.assign(kCacheHunks, -1);
}

bool ChdImage::ReadSector(uint64_t lba, uint8_t* dst)
{
    if (lba >= _sectors)
        return false;
    const uint32_t hunkBytes = _file->HunkBytes();
    uint64_t offset = lba * kSectorSize;
    size_t done = 0;
    while (done < kSectorSize)
    {
        const uint64_t hunk = offset / hunkBytes;
        const uint32_t within = static_cast<uint32_t>(offset % hunkBytes);
        const size_t take = std::min<size_t>(kSectorSize - done, hunkBytes - within);
        const size_t slot = static_cast<size_t>(hunk % kCacheHunks);
        uint8_t* cached = _cache.data() + slot * hunkBytes;
        if (_cacheTags[slot] != static_cast<int64_t>(hunk))
        {
            _cacheTags[slot] = -1;
            _hunkReads++;
            if (hunk >= _file->HunkCount() || !_file->ReadHunk(static_cast<uint32_t>(hunk), cached, &_lastError))
                return false;
            _cacheTags[slot] = static_cast<int64_t>(hunk);
        }
        std::memcpy(dst + done, cached + within, take);
        done += take;
        offset += take;
    }
    return true;
}
