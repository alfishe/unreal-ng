#pragma once

/// @file fatsourceimage.h
/// @brief FAT volumes to use as *sources* in tests (image layers of composite
/// media, FatImageSource, FatVolumeReader): built at run time, in memory.
///   - FolderToFatDisk: a host folder laid out by FatSynthVolume (FAT16 / FAT32,
///     subdirectories, long names, hidden files) copied into a SparseDisk
///   - SaveSparse: such a disk as an image file (only the used sectors written)
///   - Fat12Floppy: a 1.44 MB FAT12 floppy with files in the root, contiguous
/// FatSynthVolume is the target side of the multi-source work; using it to make
/// sources is fine because FatVolumeReader (independent of it) reads them.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "emulator/io/storage/compose/hostfoldersource.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/iblockdevice.h"

/// A disk in memory that stores only the sectors written: a FAT32 source is at
/// least 32 MiB and with 4 KiB clusters 256 MiB, nearly all of it zeros, which a
/// MemoryDisk would allocate and clear (hundreds of milliseconds per test)
class SparseDisk : public IBlockDevice
{
public:
    explicit SparseDisk(uint64_t sectors) : _sectors(sectors), _contentId(0x5350415253450000ULL | NextId()) {}

    uint64_t SectorCount() const override { return _sectors; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override
    {
        if (lba >= _sectors)
            return false;
        const auto it = _data.find(lba);
        if (it == _data.end())
            std::memset(dst, 0, 512);
        else
            std::memcpy(dst, it->second.data(), 512);
        return true;
    }
    bool WriteSector(uint64_t lba, const uint8_t* src) override
    {
        if (lba >= _sectors)
            return false;
        std::memcpy(_data[lba].data(), src, 512);
        return true;
    }
    bool IsWritable() const override { return true; }
    std::string Describe() const override { return "sparse test disk"; }
    uint64_t ContentId() const override { return _contentId; }

private:
    static uint64_t NextId()
    {
        static std::atomic<uint64_t> next{1};
        return next.fetch_add(1);
    }

    uint64_t _sectors;
    uint64_t _contentId;
    std::unordered_map<uint64_t, std::array<uint8_t, 512>> _data;
};

struct FatSourceImage
{
    std::shared_ptr<SparseDisk> disk;
    uint64_t usedEnd = 0;  ///< sectors past this one are zeros
    std::string error;
    bool ok() const { return disk != nullptr; }
};

/// `folder` as a FAT volume in memory (fixed times: the same folder gives the same bytes)
inline FatSourceImage FolderToFatDisk(const std::filesystem::path& folder, FatType fs, CodePage page = CodePage::Cp866,
                                      bool mbr = true, uint64_t freeBytes = 64 * 1024)
{
    FatSourceImage image;
    FolderSnapshot snapshot;
    if (!FolderSnapshot::Scan(folder, {}, snapshot, &image.error))
        return image;
    auto pool = std::make_shared<SourcePool>();
    auto tree = std::make_shared<FileTree>();
    if (!HostFolderSource::Enumerate(snapshot, {}, *pool, *tree, nullptr, &image.error))
        return image;
    FatVolumeOptions options;
    options.fs = fs;
    options.codePage = page;
    options.mbr = mbr;
    options.freeBytes = freeBytes;
    options.label = "SOURCE";
    options.fixedTimeUtc = 1767268800;  // 2026-01-01 12:00:00 UTC
    auto volume = FatSynthVolume::Build(tree, pool, options, snapshot.Identity(), "test source", &image.error, nullptr);
    if (!volume)
        return image;
    auto disk = std::make_shared<SparseDisk>(volume->SectorCount());
    uint8_t sector[512];
    for (uint64_t lba = 0; lba < volume->UsedSectorEnd(); lba++)
    {
        if (!volume->ReadSector(lba, sector) || !disk->WriteSector(lba, sector))
        {
            image.error = "copying sector " + std::to_string(lba);
            return image;
        }
    }
    image.usedEnd = volume->UsedSectorEnd();
    image.disk = disk;
    return image;
}

/// The disk as an image file: the used sectors written, the rest a sparse tail
inline bool SaveSparse(const FatSourceImage& image, const std::filesystem::path& path)
{
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        uint8_t sector[512];
        for (uint64_t lba = 0; lba < image.usedEnd; lba++)
        {
            image.disk->ReadSector(lba, sector);
            out.write(reinterpret_cast<const char*>(sector), sizeof sector);
        }
        if (!out)
            return false;
    }
    std::error_code ec;
    std::filesystem::resize_file(path, image.disk->SectorCount() * 512, ec);
    return !ec;
}

/// A 1.44 MB FAT12 floppy (superfloppy: no MBR): files in the root, each
/// contiguous, in order, from cluster 2. Names are 8.3 ("NAME.EXT", upper case)
inline std::shared_ptr<SparseDisk> Fat12Floppy(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files)
{
    constexpr uint32_t kSectors = 2880, kFatSectors = 9, kRootEntries = 224, kRootSectors = 14;
    constexpr uint32_t kFatStart = 1, kRootStart = kFatStart + 2 * kFatSectors, kDataStart = kRootStart + kRootSectors;
    auto disk = std::make_shared<SparseDisk>(kSectors);
    std::vector<uint8_t> image(kSectors * 512, 0);
    auto put16 = [&image](size_t at, uint32_t v) {
        image[at] = static_cast<uint8_t>(v);
        image[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    auto put32 = [&](size_t at, uint32_t v) {
        put16(at, v & 0xFFFF);
        put16(at + 2, v >> 16);
    };
    // Boot sector
    image[0] = 0xEB;
    image[1] = 0x3C;
    image[2] = 0x90;
    std::memcpy(&image[3], "MSWIN4.1", 8);
    put16(11, 512);
    image[13] = 1;   // sectors per cluster
    put16(14, 1);    // reserved
    image[16] = 2;   // FATs
    put16(17, kRootEntries);
    put16(19, kSectors);
    image[21] = 0xF0;
    put16(22, kFatSectors);
    put16(24, 18);
    put16(26, 2);
    image[510] = 0x55;
    image[511] = 0xAA;

    auto setFat = [&](uint32_t cluster, uint32_t value) {
        for (uint32_t copy = 0; copy < 2; copy++)
        {
            const size_t at = (kFatStart + copy * kFatSectors) * 512 + cluster + cluster / 2;
            uint16_t v = static_cast<uint16_t>(image[at] | (image[at + 1] << 8));
            v = (cluster & 1) ? static_cast<uint16_t>((v & 0x000F) | (value << 4)) : static_cast<uint16_t>((v & 0xF000) | value);
            put16(at, v);
        }
    };
    setFat(0, 0xFF0);
    setFat(1, 0xFFF);

    uint32_t cluster = 2;
    for (size_t i = 0; i < files.size(); i++)
    {
        const std::string& name = files[i].first;
        const std::vector<uint8_t>& data = files[i].second;
        const size_t dot = name.find('.');
        const std::string base = name.substr(0, dot);
        const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
        uint8_t* e = &image[kRootStart * 512 + i * 32];
        std::memset(e, ' ', 11);
        std::memcpy(e, base.data(), std::min<size_t>(base.size(), 8));
        std::memcpy(e + 8, ext.data(), std::min<size_t>(ext.size(), 3));
        e[11] = 0x20;
        const size_t at = kRootStart * 512 + i * 32;
        put16(at + 22, 12 << 11);                                // 12:00:00
        put16(at + 24, ((2026 - 1980) << 9) | (1 << 5) | 1);     // 2026-01-01
        put32(at + 28, static_cast<uint32_t>(data.size()));
        const uint32_t clusters = static_cast<uint32_t>((data.size() + 511) / 512);
        put16(at + 26, clusters ? cluster : 0);
        for (uint32_t c = 0; c < clusters; c++)
        {
            setFat(cluster + c, c + 1 < clusters ? cluster + c + 1 : 0xFFF);
            std::memcpy(&image[(kDataStart + cluster + c - 2) * 512], data.data() + c * 512,
                        std::min<size_t>(512, data.size() - c * 512));
        }
        cluster += clusters;
    }
    for (uint32_t lba = 0; lba < kSectors; lba++)
        disk->WriteSector(lba, &image[lba * 512]);
    return disk;
}
