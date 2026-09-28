#pragma once

/// @file fatimagebuilder.h
/// @brief Deterministic FAT16 / FAT32 disk images for tests, built at run
/// time into the scratch folder instead of committed binaries.
///
/// The layout follows the Microsoft FAT specification (fatgen103):
///   FAT12 < 4085 clusters <= FAT16 < 65525 clusters <= FAT32
/// so a FAT32 volume needs at least 32 MB even with one sector per cluster.
/// The file is written **sparse**: only the boot area, the FATs, the root
/// directory and the file data are written, so a 36 MB image takes about as
/// much disk as its files.
///
/// Everything that would vary between runs (timestamps, volume serial) is
/// fixed, so the same spec always gives a byte-identical image. The cluster
/// size is the largest that keeps the count in the type's range, as
/// formatting tools do (some Z80 FAT code mishandles one sector per cluster
/// on large volumes).
///
/// Usage:
///   FatImageSpec spec;
///   spec.fat = 16;
///   spec.sizeBytes = 8u << 20;
///   spec.files = {{"NEOGS.ROM", romBytes}};
///   ScratchFatImage image("neogs-fat16.img", spec);   // removed on destruction
///   ASSERT_TRUE(image.ok()) << image.error();
///   open(image.path())...

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "_helpers/testpathhelper.h"

struct FatImageFile
{
    std::string name;            // 8.3, stored upper case in the root directory
    std::vector<uint8_t> data;
};

struct FatImageSpec
{
    int fat = 16;                        // 16 or 32
    uint64_t sizeBytes = 8ull << 20;     // whole image, MBR included
    bool mbr = true;                     // partition table with one partition at LBA 2048
    uint8_t partitionType = 0;           // 0: #06 for FAT16 (as real cards carry), #0C for FAT32
    std::string label = "NO NAME";       // volume label, up to 11 characters
    std::vector<FatImageFile> files;     // root directory, in this order
};

struct FatImageLayout
{
    uint32_t sectorsPerCluster = 0;
    uint32_t clusters = 0;
    uint32_t fatSectors = 0;
    uint64_t volumeStartSector = 0;      // 2048 with an MBR, 0 without
};

namespace fatimage
{
constexpr uint32_t SECTOR = 512;
constexpr uint32_t PART_START = 2048;             // 1 MiB alignment, as partitioning tools do
constexpr uint32_t FIXED_SERIAL = 0x4E47530A;     // "NGS\n"
constexpr uint16_t FAT_DATE = ((2026 - 1980) << 9) | (9 << 5) | 27; // 2026-09-27
constexpr uint16_t FAT_TIME = 12 << 11;                             // 12:00:00

inline void put16(std::vector<uint8_t>& b, size_t at, uint16_t v)
{
    b[at] = static_cast<uint8_t>(v);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}

inline void put32(std::vector<uint8_t>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        b[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

inline void putText(std::vector<uint8_t>& b, size_t at, const std::string& text, size_t width)
{
    for (size_t i = 0; i < width; i++)
        b[at + i] = static_cast<uint8_t>(i < text.size() ? text[i] : ' ');
}

/// "name.ext" -> 11 bytes, upper case, space padded; empty on an invalid name
inline std::string shortName(const std::string& name)
{
    const size_t dot = name.find('.');
    std::string base = name.substr(0, dot);
    std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    if (base.empty() || base.size() > 8 || ext.size() > 3)
        return {};
    for (auto* part : {&base, &ext})
        for (char& c : *part)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    base.resize(8, ' ');
    ext.resize(3, ' ');
    return base + ext;
}

/// Cluster size and FAT size that put the cluster count in the type's range
inline bool chooseLayout(uint64_t totalSectors, int fat, FatImageLayout& out)
{
    const uint32_t reserved = fat == 16 ? 1 : 32;
    const uint32_t rootSectors = fat == 16 ? 512 * 32 / SECTOR : 0;
    const uint32_t entryBytes = fat == 16 ? 2 : 4;
    const uint64_t lo = fat == 16 ? 4085 : 65525;
    const uint64_t hi = fat == 16 ? 65524 : 0x0FFFFFF5;
    for (uint32_t spc : {64u, 32u, 16u, 8u, 4u, 2u, 1u})
    {
        uint64_t fatSectors = 1;
        uint64_t clusters = 0;
        while (true)
        {
            const int64_t data = static_cast<int64_t>(totalSectors) - reserved - 2 * static_cast<int64_t>(fatSectors) - rootSectors;
            if (data <= 0)
                return false;
            clusters = static_cast<uint64_t>(data) / spc;
            const uint64_t need = ((clusters + 2) * entryBytes + SECTOR - 1) / SECTOR;
            if (need <= fatSectors)
                break;
            fatSectors = need;
        }
        if (clusters >= lo && clusters <= hi)
        {
            out.sectorsPerCluster = spc;
            out.clusters = static_cast<uint32_t>(clusters);
            out.fatSectors = static_cast<uint32_t>(fatSectors);
            return true;
        }
    }
    return false;
}
} // namespace fatimage

/// Writes the image described by `spec` to `path` (sparse). Returns false and
/// fills `error` when the spec cannot be built.
inline bool BuildFatImage(const std::string& path, const FatImageSpec& spec, FatImageLayout* layoutOut = nullptr,
                          std::string* error = nullptr)
{
    using namespace fatimage;
    const auto fail = [error](const std::string& why)
    {
        if (error)
            *error = why;
        return false;
    };
    if (spec.fat != 16 && spec.fat != 32)
        return fail("FAT type must be 16 or 32");

    const uint64_t totalSectors = spec.sizeBytes / SECTOR;
    const uint64_t volumeStart = spec.mbr ? PART_START : 0;
    if (totalSectors <= volumeStart)
        return fail("image too small");
    const uint64_t volumeSectors = totalSectors - volumeStart;
    if (volumeSectors > 0xFFFFFFFFull)
        return fail("volume too large for a 32-bit sector count");

    FatImageLayout layout;
    if (!chooseLayout(volumeSectors, spec.fat, layout))
        return fail("size cannot hold a valid FAT" + std::to_string(spec.fat));
    layout.volumeStartSector = volumeStart;

    const uint32_t reserved = spec.fat == 16 ? 1 : 32;
    const uint32_t rootEntries = spec.fat == 16 ? 512 : 0;
    const uint32_t rootSectors = rootEntries * 32 / SECTOR;
    const uint64_t clusterBytes = static_cast<uint64_t>(layout.sectorsPerCluster) * SECTOR;
    const uint64_t rootStart = reserved + 2ull * layout.fatSectors; // FAT16 fixed root
    const uint64_t dataStart = rootStart + rootSectors;
    const auto clusterOffset = [&](uint32_t cluster)
    { return (volumeStart + dataStart + static_cast<uint64_t>(cluster - 2) * layout.sectorsPerCluster) * SECTOR; };

    // The FAT, only as far as clusters are allocated (the rest stays zero)
    const uint32_t eoc = spec.fat == 16 ? 0xFFFF : 0x0FFFFFFF;
    std::vector<uint32_t> fatTable = {spec.fat == 16 ? 0xFFF8u : 0x0FFFFFF8u, eoc};
    const auto alloc = [&](uint64_t bytes) -> uint32_t
    {
        const uint64_t count = std::max<uint64_t>(1, (bytes + clusterBytes - 1) / clusterBytes);
        const uint32_t first = static_cast<uint32_t>(fatTable.size());
        for (uint64_t i = 0; i < count; i++)
            fatTable.push_back(i + 1 < count ? static_cast<uint32_t>(first + i + 1) : eoc);
        return first;
    };

    struct Chunk
    {
        uint64_t offset;
        std::vector<uint8_t> bytes;
    };
    std::vector<Chunk> chunks;

    const uint32_t rootCluster = spec.fat == 32 ? alloc(clusterBytes) : 0;

    // Root directory: the volume label, then the files
    std::vector<uint8_t> root;
    const auto addEntry = [&root](const std::string& name11, uint8_t attr, uint32_t cluster, uint32_t size)
    {
        std::vector<uint8_t> e(32, 0);
        memcpy(e.data(), name11.data(), 11);
        e[11] = attr;
        put16(e, 14, FAT_TIME);
        put16(e, 16, FAT_DATE);
        put16(e, 18, FAT_DATE);
        put16(e, 20, static_cast<uint16_t>(cluster >> 16));
        put16(e, 22, FAT_TIME);
        put16(e, 24, FAT_DATE);
        put16(e, 26, static_cast<uint16_t>(cluster));
        put32(e, 28, size);
        root.insert(root.end(), e.begin(), e.end());
    };
    std::string label = spec.label.substr(0, 11);
    for (char& c : label)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    label.resize(11, ' ');
    addEntry(label, 0x08, 0, 0);
    for (const FatImageFile& file : spec.files)
    {
        const std::string name11 = shortName(file.name);
        if (name11.empty())
            return fail("not an 8.3 name: " + file.name);
        const uint32_t first = alloc(file.data.size());
        if (fatTable.size() > static_cast<size_t>(layout.clusters) + 2)
            return fail("image too small for the files");
        chunks.push_back({clusterOffset(first), file.data});
        addEntry(name11, 0x20, first, static_cast<uint32_t>(file.data.size()));
    }
    if (spec.fat == 16)
    {
        if (root.size() / 32 > rootEntries)
            return fail("too many files for the root directory");
        chunks.push_back({(volumeStart + rootStart) * SECTOR, root});
    }
    else
    {
        if (root.size() > clusterBytes)
            return fail("root directory needs more than one cluster");
        chunks.push_back({clusterOffset(rootCluster), root});
    }

    // Boot sector (BPB)
    std::vector<uint8_t> bs(SECTOR, 0);
    const uint8_t jump16[3] = {0xEB, 0x3C, 0x90};
    const uint8_t jump32[3] = {0xEB, 0x58, 0x90};
    memcpy(bs.data(), spec.fat == 32 ? jump32 : jump16, 3);
    putText(bs, 3, "UNREALNG", 8);
    put16(bs, 11, SECTOR);
    bs[13] = static_cast<uint8_t>(layout.sectorsPerCluster);
    put16(bs, 14, static_cast<uint16_t>(reserved));
    bs[16] = 2;
    put16(bs, 17, static_cast<uint16_t>(rootEntries));
    put16(bs, 19, volumeSectors < 0x10000 ? static_cast<uint16_t>(volumeSectors) : 0);
    bs[21] = 0xF8;
    put16(bs, 22, spec.fat == 16 ? static_cast<uint16_t>(layout.fatSectors) : 0);
    put16(bs, 24, 63);
    put16(bs, 26, 255);
    put32(bs, 28, static_cast<uint32_t>(volumeStart));
    put32(bs, 32, volumeSectors < 0x10000 ? 0 : static_cast<uint32_t>(volumeSectors));
    const size_t ext = spec.fat == 16 ? 36 : 64;
    if (spec.fat == 32)
    {
        put32(bs, 36, layout.fatSectors);
        put32(bs, 44, rootCluster);
        put16(bs, 48, 1); // FSInfo sector
        put16(bs, 50, 6); // backup boot sector
    }
    bs[ext] = 0x80;
    bs[ext + 2] = 0x29;
    put32(bs, ext + 3, FIXED_SERIAL);
    putText(bs, ext + 7, label, 11);
    putText(bs, ext + 18, spec.fat == 16 ? "FAT16" : "FAT32", 8);
    bs[510] = 0x55;
    bs[511] = 0xAA;
    chunks.push_back({volumeStart * SECTOR, bs});

    if (spec.fat == 32)
    {
        std::vector<uint8_t> fsi(SECTOR, 0);
        put32(fsi, 0, 0x41615252);
        put32(fsi, 484, 0x61417272);
        put32(fsi, 488, static_cast<uint32_t>(layout.clusters + 2 - fatTable.size()));
        put32(fsi, 492, static_cast<uint32_t>(fatTable.size()));
        put32(fsi, 508, 0xAA550000);
        chunks.push_back({(volumeStart + 1) * SECTOR, fsi});
        chunks.push_back({(volumeStart + 6) * SECTOR, bs});
        chunks.push_back({(volumeStart + 7) * SECTOR, fsi});
    }

    // Both FAT copies
    std::vector<uint8_t> rawFat(fatTable.size() * (spec.fat == 16 ? 2 : 4), 0);
    for (size_t i = 0; i < fatTable.size(); i++)
    {
        if (spec.fat == 16)
            put16(rawFat, i * 2, static_cast<uint16_t>(fatTable[i]));
        else
            put32(rawFat, i * 4, fatTable[i]);
    }
    for (uint32_t copy = 0; copy < 2; copy++)
        chunks.push_back({(volumeStart + reserved + static_cast<uint64_t>(copy) * layout.fatSectors) * SECTOR, rawFat});

    // MBR with one partition
    if (spec.mbr)
    {
        std::vector<uint8_t> mbr(SECTOR, 0);
        put32(mbr, 440, FIXED_SERIAL);
        const uint8_t type = spec.partitionType ? spec.partitionType : (spec.fat == 32 ? 0x0C : 0x06);
        const uint8_t chsFull[3] = {0xFE, 0xFF, 0xFF};
        memcpy(mbr.data() + 447, chsFull, 3);
        mbr[450] = type;
        memcpy(mbr.data() + 451, chsFull, 3);
        put32(mbr, 454, PART_START);
        put32(mbr, 458, static_cast<uint32_t>(volumeSectors));
        mbr[510] = 0x55;
        mbr[511] = 0xAA;
        chunks.push_back({0, mbr});
    }

    // Write sparse: size first, then only the chunks
    std::error_code ec;
    std::filesystem::remove(path, ec);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return fail("cannot create " + path);
    }
    std::filesystem::resize_file(path, totalSectors * SECTOR, ec);
    if (ec)
        return fail("cannot size " + path + ": " + ec.message());
    std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
    for (const Chunk& chunk : chunks)
    {
        io.seekp(static_cast<std::streamoff>(chunk.offset));
        io.write(reinterpret_cast<const char*>(chunk.bytes.data()), static_cast<std::streamsize>(chunk.bytes.size()));
    }
    if (!io)
        return fail("cannot write " + path);
    if (layoutOut)
        *layoutOut = layout;
    return true;
}

/// A FAT image in the scratch folder, unique to this process, removed when
/// the object goes. Keep it alive for as long as anything has the file open.
class ScratchFatImage
{
public:
    ScratchFatImage(const std::string& leafName, const FatImageSpec& spec)
        : _path(TestPathHelper::GetUniqueTestScratchPath(leafName))
    {
        _ok = BuildFatImage(_path, spec, &_layout, &_error);
    }
    ~ScratchFatImage()
    {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
    }
    ScratchFatImage(const ScratchFatImage&) = delete;
    ScratchFatImage& operator=(const ScratchFatImage&) = delete;

    bool ok() const { return _ok; }
    const std::string& error() const { return _error; }
    const std::string& path() const { return _path; }
    const FatImageLayout& layout() const { return _layout; }

private:
    std::string _path;
    bool _ok = false;
    std::string _error;
    FatImageLayout _layout;
};
