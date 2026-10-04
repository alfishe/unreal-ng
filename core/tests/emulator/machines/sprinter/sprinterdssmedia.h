#pragma once

/// @file sprinterdssmedia.h
/// @brief DSS media for Sprinter machine tests: a file from the DSS 1.62.92 boot
/// floppy (testdata/machines/sprinter/dss_1_62_92.img) and a bootable DSS hard
/// disk built from it. Shared by the boot tests and the TTD tests.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

/// A file from the DSS 1.62.92 floppy's root (FAT12: one FAT at LBA 10, the root at LBA 19, cluster 2 at
/// LBA 33, one sector per cluster; testdata/machines/sprinter/README.md "Layout"); empty when not found
inline std::vector<uint8_t> FloppyRootFile(const std::vector<uint8_t>& floppy, const char name[11])
{
    const uint8_t* root = floppy.data() + 19 * 512;
    for (int i = 0; i < 224; i++)
    {
        const uint8_t* e = root + i * 32;
        if (std::memcmp(e, name, 11) != 0)
            continue;
        const uint32_t size = e[28] | e[29] << 8 | e[30] << 16 | static_cast<uint32_t>(e[31]) << 24;
        std::vector<uint8_t> data;
        uint16_t cluster = static_cast<uint16_t>(e[26] | e[27] << 8);
        const uint8_t* fat = floppy.data() + 10 * 512;
        while (cluster >= 2 && cluster < 0xFF8 && data.size() < size)
        {
            const uint8_t* sector = floppy.data() + (33 + cluster - 2) * 512;
            data.insert(data.end(), sector, sector + 512);
            const uint16_t pair = static_cast<uint16_t>(fat[cluster * 3 / 2] | fat[cluster * 3 / 2 + 1] << 8);
            cluster = (cluster & 1) ? static_cast<uint16_t>(pair >> 4) : static_cast<uint16_t>(pair & 0xFFF);
        }
        data.resize(size);
        return data;
    }
    return {};
}

/// A bootable DSS hard disk, built the way DSS's BOOT.EXE leaves one (hardware-reference §9.3,
/// materials.md "A reference HDD image"): 16 MiB, an MBR whose entry 0 is a FAT16 partition (type #06) at
/// LBA 63, the 3-sector DSS loader at LBA 1-3 (`mbrCode`, when given, goes into the MBR right before the partition
/// table, where DSS 1.71's BOOT.EXE puts the loader's sector-0 part), and a FAT16 volume (4 sectors per cluster, 2 FATs, 512 root
/// entries) holding `files` in its root, in order
struct DssHddFile
{
    const char* name;  ///< 8.3 directory form, 11 characters
    std::vector<uint8_t> data;
};

constexpr uint32_t kDssHddStart = 63, kDssHddFatSize = 32;
constexpr uint32_t kDssHddRootLba = kDssHddStart + 1 + 2 * kDssHddFatSize;  ///< 128

inline std::vector<uint8_t> BuildDssHdd(const std::vector<uint8_t>& loader, const std::vector<DssHddFile>& files,
                                        const std::vector<uint8_t>& mbrCode = {})
{
    constexpr uint32_t kTotal = 32768, kStart = kDssHddStart, kSectors = kTotal - kStart;
    constexpr uint32_t kSpc = 4, kReserved = 1, kFatSize = kDssHddFatSize, kRootEntries = 512;
    constexpr uint32_t kRootLba = kDssHddRootLba, kDataLba = kRootLba + kRootEntries * 32 / 512;
    std::vector<uint8_t> disk(static_cast<size_t>(kTotal) * 512);
    auto put16 = [&](size_t at, uint32_t v) { disk[at] = static_cast<uint8_t>(v); disk[at + 1] = static_cast<uint8_t>(v >> 8); };
    auto put32 = [&](size_t at, uint32_t v) { put16(at, v & 0xFFFF); put16(at + 2, v >> 16); };

    // MBR: entry 0 = active FAT16 (#06) from LBA 63; the DSS loader checks entry 0 only
    disk[446] = 0x80;
    disk[446 + 4] = 0x06;
    put32(446 + 8, kStart);
    put32(446 + 12, kSectors);
    put16(510, 0xAA55);
    std::memcpy(disk.data() + 512, loader.data(), std::min<size_t>(loader.size(), 3 * 512));
    if (!mbrCode.empty() && mbrCode.size() <= 446)
        std::memcpy(disk.data() + 446 - mbrCode.size(), mbrCode.data(), mbrCode.size());

    // Partition boot sector with the BPB (DOSBOOT4: "FAT16   " at +#36, media #F8)
    const size_t bs = static_cast<size_t>(kStart) * 512;
    const uint8_t jump[3] = {0xEB, 0x3C, 0x90};
    std::memcpy(&disk[bs], jump, 3);
    std::memcpy(&disk[bs + 3], "DSS 1.62", 8);
    put16(bs + 11, 512);
    disk[bs + 13] = kSpc;
    put16(bs + 14, kReserved);
    disk[bs + 16] = 2;
    put16(bs + 17, kRootEntries);
    put16(bs + 19, kSectors);
    disk[bs + 21] = 0xF8;
    put16(bs + 22, kFatSize);
    put16(bs + 24, 32);  // sectors per track
    put16(bs + 26, 16);  // heads
    put32(bs + 28, kStart);
    disk[bs + 36] = 0x80;
    disk[bs + 38] = 0x29;
    put32(bs + 39, 0x53334200);
    std::memcpy(&disk[bs + 43], "NO NAME    ", 11);
    std::memcpy(&disk[bs + 0x36], "FAT16   ", 8);
    put16(bs + 510, 0xAA55);

    // Files: consecutive clusters from 2, a chain in both FATs, an entry in the root (2026-10-02 12:00)
    std::vector<uint16_t> fat(kFatSize * 256);
    fat[0] = 0xFFF8;
    fat[1] = 0xFFFF;
    uint16_t next = 2;
    for (size_t i = 0; i < files.size(); i++)
    {
        const DssHddFile& file = files[i];
        const uint16_t first = file.data.empty() ? 0 : next;
        const uint32_t clusters = static_cast<uint32_t>((file.data.size() + kSpc * 512 - 1) / (kSpc * 512));
        for (uint32_t c = 0; c < clusters; c++, next++)
            fat[next] = c + 1 < clusters ? static_cast<uint16_t>(next + 1) : 0xFFFF;
        if (!file.data.empty())
            std::memcpy(&disk[(kDataLba + (first - 2) * kSpc) * 512], file.data.data(), file.data.size());
        const size_t e = kRootLba * 512 + i * 32;
        std::memcpy(&disk[e], file.name, 11);
        disk[e + 11] = 0x20;
        put16(e + 22, 12 << 11);
        put16(e + 24, (2026 - 1980) << 9 | 10 << 5 | 2);
        put16(e + 26, first);
        put32(e + 28, static_cast<uint32_t>(file.data.size()));
    }
    for (uint32_t copy = 0; copy < 2; copy++)
    {
        for (size_t i = 0; i < fat.size(); i++)
            put16((kStart + kReserved + copy * kFatSize) * 512 + i * 2, fat[i]);
    }
    return disk;
}
