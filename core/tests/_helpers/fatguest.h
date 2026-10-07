#pragma once

/// @file fatguest.h
/// @brief A FAT12 / FAT16 / FAT32 writer that acts as a guest operating
/// system would: it changes a volume through its sectors (directory entries,
/// both FATs, data clusters, first fit), so the change layer of a medium sees
/// what a DOS would leave. 8.3 names only (no long names are written; entries
/// with long names are deleted with their slots). Tests of change attribution
/// and of the session delta.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/iblockdevice.h"

class FatGuest
{
public:
    explicit FatGuest(IBlockDevice& device) : _device(device) { Reopen(); }

    bool ok() const { return _ok; }

    /// A new file `path` ("/DIR/NAME.EXT") holding `data`
    bool Create(const std::string& path, const std::vector<uint8_t>& data, uint8_t attributes = 0x20)
    {
        const auto [dir, name] = Split(path);
        uint32_t dirCluster = 0;
        if (!DirCluster(dir, dirCluster))
            return false;
        const uint32_t first = data.empty() ? 0 : Allocate(Clusters(data.size()), 0);
        if (!data.empty() && !first)
            return false;
        WriteChain(first, data);
        uint8_t entry[32] = {};
        ShortName(name, entry);
        entry[11] = attributes;
        SetCluster(entry, first);
        Put32(entry + 28, static_cast<uint32_t>(data.size()));
        Put16(entry + 24, (46 << 9) | (1 << 5) | 2);  // 2026-01-02
        return AddEntry(dirCluster, entry) && Reopen();
    }

    /// Replace the bytes of `path` (its chain grows or shrinks; the first cluster is kept when there is one)
    bool Write(const std::string& path, const std::vector<uint8_t>& data)
    {
        Slot slot;
        if (!FindEntry(path, slot))
            return false;
        uint32_t first = Cluster(slot.entry);
        std::vector<uint32_t> chain;
        if (first >= 2)
            _reader.ChainClusters(first, chain);
        const uint32_t want = Clusters(data.size());
        if (want < chain.size())
        {
            for (size_t i = want; i < chain.size(); i++)
                SetFat(chain[i], 0);
            chain.resize(want);
            if (!chain.empty())
                SetFat(chain.back(), EndOfChain());
        }
        else if (want > chain.size())
        {
            const uint32_t added = Allocate(want - static_cast<uint32_t>(chain.size()), chain.empty() ? 0 : chain.back());
            if (!added)
                return false;
            if (chain.empty())
                first = added;
            chain.clear();
            _reader.Open(_device);
            _reader.ChainClusters(first, chain);
        }
        if (want == 0)
            first = 0;
        WriteChain(first, data);
        SetCluster(slot.entry, first);
        Put32(slot.entry + 28, static_cast<uint32_t>(data.size()));
        return StoreEntry(slot) && Reopen();
    }

    /// Bytes at `offset` overwritten in place (the size and chain unchanged)
    bool Poke(const std::string& path, uint64_t offset, uint8_t value)
    {
        Slot slot;
        if (!FindEntry(path, slot))
            return false;
        std::vector<FatChainExtent> extents;
        if (!_reader.ChainExtents(Cluster(slot.entry), Get32(slot.entry + 28), extents))
            return false;
        uint64_t sector = offset / 512;
        for (const FatChainExtent& x : extents)
        {
            if (sector < x.sectors)
            {
                uint8_t s[512];
                _device.ReadSector(x.lba + sector, s);
                s[offset % 512] = value;
                return _device.WriteSector(x.lba + sector, s) && Reopen();
            }
            sector -= x.sectors;
        }
        return false;
    }

    bool Delete(const std::string& path)
    {
        Slot slot;
        if (!FindEntry(path, slot))
            return false;
        FreeChain(Cluster(slot.entry));
        MarkDeleted(slot);
        return Reopen();
    }

    /// Rename or move: the entry goes to `to` (another directory too); the data stays where it is
    bool Rename(const std::string& from, const std::string& to)
    {
        Slot slot;
        if (!FindEntry(from, slot))
            return false;
        const auto [dir, name] = Split(to);
        uint32_t dirCluster = 0;
        if (!DirCluster(dir, dirCluster))
            return false;
        uint8_t entry[32];
        std::memcpy(entry, slot.entry, 32);
        ShortName(name, entry);
        MarkDeleted(slot);
        Reopen();
        if (!AddEntry(dirCluster, entry))
            return false;
        // A moved directory's ".." names its new parent
        if ((entry[11] & 0x10) && Cluster(entry) >= 2)
        {
            uint8_t s[512];
            const uint64_t lba = ClusterLba(Cluster(entry));
            _device.ReadSector(lba, s);
            SetCluster(s + 32, dirCluster);
            _device.WriteSector(lba, s);
        }
        return Reopen();
    }

    bool Mkdir(const std::string& path)
    {
        const auto [dir, name] = Split(path);
        uint32_t parent = 0;
        if (!DirCluster(dir, parent))
            return false;
        const uint32_t cluster = Allocate(1, 0);
        if (!cluster)
            return false;
        // "." and "..", then zeros
        uint8_t dots[64] = {};
        std::memset(dots, ' ', 11);
        dots[0] = '.';
        dots[11] = 0x10;
        SetCluster(dots, cluster);
        std::memset(dots + 32, ' ', 11);
        dots[32] = '.';
        dots[33] = '.';
        dots[32 + 11] = 0x10;
        SetCluster(dots + 32, parent);
        std::vector<uint8_t> bytes(dots, dots + sizeof dots);
        bytes.resize(ClusterBytes(), 0);
        WriteChain(cluster, bytes);
        uint8_t entry[32] = {};
        ShortName(name, entry);
        entry[11] = 0x10;
        SetCluster(entry, cluster);
        return AddEntry(parent, entry) && Reopen();
    }

    /// An empty directory removed (as DOS RMDIR: the caller deletes its files first)
    bool Rmdir(const std::string& path) { return Delete(path); }

    bool SetAttributes(const std::string& path, uint8_t attributes)
    {
        Slot slot;
        if (!FindEntry(path, slot))
            return false;
        slot.entry[11] = static_cast<uint8_t>((slot.entry[11] & 0x30) | attributes);
        return StoreEntry(slot) && Reopen();
    }

    /// Clusters marked used in the FAT that no entry names (what a crash mid-write leaves)
    bool LoseClusters(uint32_t count) { return Allocate(count, 0) != 0 && Reopen(); }

private:
    struct Slot
    {
        uint64_t lba = 0;
        size_t offset = 0;    ///< of the short entry in the sector
        uint8_t entry[32] = {};
        std::vector<std::pair<uint64_t, size_t>> longSlots;  ///< its long-name slots
    };

    static void Put16(uint8_t* p, uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
    }
    static void Put32(uint8_t* p, uint32_t v)
    {
        Put16(p, v & 0xFFFF);
        Put16(p + 2, v >> 16);
    }
    static uint32_t Get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

    bool Fat32() const { return _reader.Type() == FatReaderType::Fat32; }
    bool Fat12() const { return _reader.Type() == FatReaderType::Fat12; }
    uint32_t EndOfChain() const { return Fat32() ? 0x0FFFFFFF : Fat12() ? 0xFFF : 0xFFFF; }
    uint32_t ClusterBytes() const { return _reader.SectorsPerCluster() * 512; }
    uint32_t Clusters(uint64_t bytes) const { return static_cast<uint32_t>((bytes + ClusterBytes() - 1) / ClusterBytes()); }
    uint64_t ClusterLba(uint32_t cluster) const
    {
        return _reader.VolumeStart() + _reader.DataStart() + static_cast<uint64_t>(cluster - 2) * _reader.SectorsPerCluster();
    }
    uint32_t Cluster(const uint8_t* entry) const
    {
        return (entry[26] | (entry[27] << 8)) | (Fat32() ? static_cast<uint32_t>(entry[20] | (entry[21] << 8)) << 16 : 0);
    }
    void SetCluster(uint8_t* entry, uint32_t cluster) const
    {
        Put16(entry + 26, cluster & 0xFFFF);
        if (Fat32())
            Put16(entry + 20, cluster >> 16);
    }

    bool Reopen()
    {
        _ok = _reader.Open(_device);
        return _ok;
    }

    static std::pair<std::string, std::string> Split(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return {slash == 0 ? "/" : path.substr(0, slash), path.substr(slash + 1)};
    }

    static void ShortName(const std::string& name, uint8_t* entry)
    {
        std::memset(entry, ' ', 11);
        const size_t dot = name.find('.');
        const std::string base = name.substr(0, std::min(dot, name.size()));
        const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
        std::memcpy(entry, base.data(), std::min<size_t>(base.size(), 8));
        std::memcpy(entry + 8, ext.data(), std::min<size_t>(ext.size(), 3));
    }

    bool DirCluster(const std::string& dir, uint32_t& cluster)
    {
        if (dir == "/")
        {
            cluster = 0;
            return true;
        }
        FatDirEntryInfo info;
        if (!_reader.Stat(dir, info) || !info.isDirectory)
            return false;
        cluster = info.firstCluster;
        return true;
    }

    /// The sectors of a directory: the fixed root region, or its chain
    std::vector<uint64_t> DirSectors(uint32_t cluster)
    {
        std::vector<uint64_t> sectors;
        if (cluster == 0 && !Fat32())
        {
            const uint64_t first = _reader.VolumeStart() + _reader.ReservedSectors() +
                                   static_cast<uint64_t>(_reader.FatCount()) * _reader.FatSectors();
            for (uint32_t s = 0; s < _reader.RootDirSectors(); s++)
                sectors.push_back(first + s);
            return sectors;
        }
        std::vector<uint32_t> chain;
        _reader.ChainClusters(cluster == 0 ? _reader.RootCluster() : cluster, chain);
        for (uint32_t c : chain)
            for (uint32_t s = 0; s < _reader.SectorsPerCluster(); s++)
                sectors.push_back(ClusterLba(c) + s);
        return sectors;
    }

    bool FindEntry(const std::string& path, Slot& slot)
    {
        const auto [dir, name] = Split(path);
        uint32_t cluster = 0;
        if (!DirCluster(dir, cluster))
            return false;
        uint8_t want[11];
        ShortName(name, want);
        std::vector<std::pair<uint64_t, size_t>> pending;
        for (uint64_t lba : DirSectors(cluster))
        {
            uint8_t s[512];
            _device.ReadSector(lba, s);
            for (size_t e = 0; e < 512; e += 32)
            {
                if (s[e] == 0)
                    return false;
                if (s[e] == 0xE5)
                {
                    pending.clear();
                    continue;
                }
                if (s[e + 11] == 0x0F)
                {
                    pending.push_back({lba, e});
                    continue;
                }
                if (std::memcmp(s + e, want, 11) == 0)
                {
                    slot.lba = lba;
                    slot.offset = e;
                    std::memcpy(slot.entry, s + e, 32);
                    slot.longSlots = pending;
                    return true;
                }
                pending.clear();
            }
        }
        return false;
    }

    bool StoreEntry(const Slot& slot)
    {
        uint8_t s[512];
        _device.ReadSector(slot.lba, s);
        std::memcpy(s + slot.offset, slot.entry, 32);
        return _device.WriteSector(slot.lba, s);
    }

    void MarkDeleted(const Slot& slot)
    {
        std::vector<std::pair<uint64_t, size_t>> all = slot.longSlots;
        all.push_back({slot.lba, slot.offset});
        for (const auto& [lba, offset] : all)
        {
            uint8_t s[512];
            _device.ReadSector(lba, s);
            s[offset] = 0xE5;
            _device.WriteSector(lba, s);
        }
    }

    bool AddEntry(uint32_t dirCluster, const uint8_t* entry)
    {
        for (uint64_t lba : DirSectors(dirCluster))
        {
            uint8_t s[512];
            _device.ReadSector(lba, s);
            for (size_t e = 0; e < 512; e += 32)
            {
                if (s[e] == 0 || s[e] == 0xE5)
                {
                    std::memcpy(s + e, entry, 32);
                    return _device.WriteSector(lba, s);
                }
            }
        }
        return false;  // a full directory: tests keep directories small
    }

    void SetFat(uint32_t cluster, uint32_t value)
    {
        for (uint32_t copy = 0; copy < _reader.FatCount(); copy++)
        {
            const uint64_t fat = _reader.VolumeStart() + _reader.ReservedSectors() + static_cast<uint64_t>(copy) * _reader.FatSectors();
            const uint64_t at = Fat32() ? cluster * 4ull : Fat12() ? cluster + cluster / 2 : cluster * 2ull;
            for (uint64_t b = 0; b < (Fat32() ? 4u : 2u); b++)
            {
                const uint64_t lba = fat + (at + b) / 512;
                uint8_t s[512];
                _device.ReadSector(lba, s);
                uint8_t& byte = s[(at + b) % 512];
                if (Fat12())
                {
                    const uint16_t v = static_cast<uint16_t>(value & 0xFFF);
                    if (cluster & 1)
                        byte = b == 0 ? static_cast<uint8_t>((byte & 0x0F) | ((v << 4) & 0xF0)) : static_cast<uint8_t>(v >> 4);
                    else
                        byte = b == 0 ? static_cast<uint8_t>(v) : static_cast<uint8_t>((byte & 0xF0) | (v >> 8));
                }
                else
                    byte = static_cast<uint8_t>(value >> (8 * b));
                _device.WriteSector(lba, s);
            }
        }
    }

    /// `count` free clusters, first fit, chained after `after` (0: a new chain); the first one, 0 when full
    uint32_t Allocate(uint32_t count, uint32_t after)
    {
        std::vector<bool> free;
        _reader.ScanFree(free);
        std::vector<uint32_t> picked;
        for (uint32_t i = 0; i < free.size() && picked.size() < count; i++)
        {
            if (free[i])
                picked.push_back(i + 2);
        }
        if (picked.size() < count)
            return 0;
        for (size_t i = 0; i < picked.size(); i++)
            SetFat(picked[i], i + 1 < picked.size() ? picked[i + 1] : EndOfChain());
        if (after)
            SetFat(after, picked.front());
        Reopen();
        return picked.front();
    }

    void FreeChain(uint32_t first)
    {
        if (first < 2)
            return;
        std::vector<uint32_t> chain;
        _reader.ChainClusters(first, chain);
        for (uint32_t c : chain)
            SetFat(c, 0);
    }

    void WriteChain(uint32_t first, const std::vector<uint8_t>& data)
    {
        if (first < 2)
            return;
        std::vector<uint32_t> chain;
        _reader.Open(_device);
        _reader.ChainClusters(first, chain);
        size_t at = 0;
        for (uint32_t c : chain)
        {
            for (uint32_t s = 0; s < _reader.SectorsPerCluster(); s++, at += 512)
            {
                uint8_t sector[512] = {};
                if (at < data.size())
                    std::memcpy(sector, data.data() + at, std::min<size_t>(512, data.size() - at));
                _device.WriteSector(ClusterLba(c) + s, sector);
            }
        }
    }

    IBlockDevice& _device;
    FatVolumeReader _reader;
    bool _ok = false;
};
