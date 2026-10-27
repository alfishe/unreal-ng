#pragma once

/// @file fatinplace.h
/// @brief A file inside a FAT16 volume on a block device, rewritten in place (tests, benchmarks)

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/iblockdevice.h"

/// Files inside a FAT16 volume on a block device, rewritten in place (same size, the cluster chain kept): the
/// session layer takes the writes. Enough to change SYSTEM.BAT or swap a TRD for another of the same size
class FatInPlace
{
public:
    explicit FatInPlace(IBlockDevice& device) : _device(device) {}

    bool Open()
    {
        if (!_reader.Open(_device))
            return false;
        uint8_t bs[512];
        if (!_device.ReadSector(_reader.VolumeStart(), bs))
            return false;
        _spc = bs[13];
        const uint32_t reserved = bs[14] | bs[15] << 8;
        const uint32_t fats = bs[16];
        const uint32_t rootEntries = bs[17] | bs[18] << 8;
        const uint32_t fatSize = bs[22] | bs[23] << 8;
        _fatLba = _reader.VolumeStart() + reserved;
        _dataLba = _fatLba + fats * fatSize + (rootEntries * 32 + 511) / 512;
        return _spc != 0;
    }

    /// The entry of `path` ("/DIR/NAME.EXT")
    bool Entry(const std::string& path, FatDirEntryInfo& out)
    {
        const size_t slash = path.find_last_of('/');
        const std::string dir = slash == 0 ? "/" : path.substr(0, slash);
        const std::string name = path.substr(slash + 1);
        std::vector<FatDirEntryInfo> entries;
        if (!_reader.List(dir, entries))
            return false;
        for (const FatDirEntryInfo& e : entries)
        {
            if (EqualNoCase(e.shortName, name) || EqualNoCase(e.name, name))
            {
                out = e;
                return true;
            }
        }
        return false;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data) { return _reader.ReadFile(path, data); }

    /// `data` over the file's bytes (data.size() <= the file's size; the size in the directory is kept)
    bool Overwrite(const std::string& path, const std::vector<uint8_t>& data)
    {
        FatDirEntryInfo e;
        if (!Entry(path, e) || data.size() > e.size)
            return false;
        uint32_t cluster = e.firstCluster;
        const size_t clusterBytes = static_cast<size_t>(_spc) * 512;
        for (size_t at = 0; at < data.size(); at += clusterBytes)
        {
            if (cluster < 2 || cluster >= 0xFFF8)
                return false;
            for (uint32_t s = 0; s < _spc && at + s * 512 < data.size(); s++)
            {
                uint8_t sector[512];
                const uint64_t lba = _dataLba + static_cast<uint64_t>(cluster - 2) * _spc + s;
                if (!_device.ReadSector(lba, sector))
                    return false;
                const size_t n = std::min<size_t>(512, data.size() - (at + s * 512));
                std::memcpy(sector, data.data() + at + s * 512, n);
                if (!_device.WriteSector(lba, sector))
                    return false;
            }
            cluster = NextCluster(cluster);
        }
        return true;
    }

private:
    static bool EqualNoCase(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    uint32_t NextCluster(uint32_t cluster)
    {
        uint8_t sector[512];
        if (!_device.ReadSector(_fatLba + cluster * 2 / 512, sector))
            return 0xFFFF;
        const size_t at = cluster * 2 % 512;
        return static_cast<uint32_t>(sector[at] | sector[at + 1] << 8);
    }

    IBlockDevice& _device;
    FatVolumeReader _reader;
    uint32_t _spc = 0;
    uint64_t _fatLba = 0;
    uint64_t _dataLba = 0;
};
