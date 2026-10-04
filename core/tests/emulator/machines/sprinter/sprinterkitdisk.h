#pragma once

/// @file sprinterkitdisk.h
/// @brief A 1.44 MB DOS floppy built by a test, with the files a DSS program test needs in its root (the network
/// kit's programs and a NET.CFG; network tdd §15 T-NET-8 / T-NET-9). FAT12 as DOS formats it: 512-byte sectors,
/// 18 per track, 2 heads, 80 tracks; 1 reserved sector, 2 FATs of 9 sectors, 224 root entries, one sector per
/// cluster, data from LBA 33. Files are stored whole and in order (contiguous clusters).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/testpathhelper.h"

struct KitFile
{
    std::string name;            ///< "PING.EXE"
    std::vector<uint8_t> data;
};

inline std::vector<uint8_t> BuildFat12Floppy(const std::vector<KitFile>& files, const char* label = "UNREALNGNET")
{
    constexpr size_t kSectors = 2880, kFatLba = 1, kFatSectors = 9, kRootLba = 19, kDataLba = 33;
    std::vector<uint8_t> disk(kSectors * 512, 0);
    auto put16 = [&](size_t at, unsigned v) {
        disk[at] = static_cast<uint8_t>(v);
        disk[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    const uint8_t jump[3] = {0xEB, 0x3C, 0x90};
    std::memcpy(disk.data(), jump, 3);
    std::memcpy(disk.data() + 3, "MSDOS5.0", 8);
    put16(11, 512);
    disk[13] = 1;
    put16(14, 1);
    disk[16] = 2;
    put16(17, 224);
    put16(19, kSectors);
    disk[21] = 0xF0;
    put16(22, kFatSectors);
    put16(24, 18);
    put16(26, 2);
    disk[38] = 0x29;
    std::memcpy(disk.data() + 43, label, std::min<size_t>(11, std::strlen(label)));
    std::memcpy(disk.data() + 54, "FAT12   ", 8);
    put16(510, 0xAA55);

    std::vector<uint16_t> fat(2 + (kSectors - kDataLba), 0);
    fat[0] = 0xFF0;
    fat[1] = 0xFFF;
    uint16_t cluster = 2;
    size_t entry = 0;
    // The volume label first, then the files
    {
        uint8_t* e = disk.data() + kRootLba * 512 + 32 * entry++;
        std::memset(e, ' ', 11);
        std::memcpy(e, label, std::min<size_t>(11, std::strlen(label)));
        e[11] = 0x08;
    }
    for (const KitFile& f : files)
    {
        uint8_t* e = disk.data() + kRootLba * 512 + 32 * entry++;
        std::memset(e, ' ', 11);
        const size_t dot = f.name.find('.');
        const std::string base = f.name.substr(0, dot);
        const std::string ext = dot == std::string::npos ? std::string() : f.name.substr(dot + 1);
        for (size_t i = 0; i < base.size() && i < 8; ++i)
            e[i] = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(base[i])));
        for (size_t i = 0; i < ext.size() && i < 3; ++i)
            e[8 + i] = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(ext[i])));
        e[11] = 0x20;   // archive
        e[24] = 0x21;   // 2026-01-01
        e[25] = 0x5C;
        const size_t clusters = (f.data.size() + 511) / 512;
        e[26] = static_cast<uint8_t>(clusters ? cluster : 0);
        e[27] = static_cast<uint8_t>(clusters ? cluster >> 8 : 0);
        const uint32_t size = static_cast<uint32_t>(f.data.size());
        std::memcpy(e + 28, &size, 4);
        for (size_t c = 0; c < clusters; ++c)
        {
            const size_t lba = kDataLba + (cluster - 2);
            const size_t n = std::min<size_t>(512, f.data.size() - c * 512);
            std::memcpy(disk.data() + lba * 512, f.data.data() + c * 512, n);
            fat[cluster] = c + 1 == clusters ? 0xFFF : static_cast<uint16_t>(cluster + 1);
            ++cluster;
        }
    }
    for (size_t copy = 0; copy < 2; ++copy)
    {
        uint8_t* table = disk.data() + (kFatLba + copy * kFatSectors) * 512;
        for (size_t n = 0; n + 1 < fat.size(); n += 2)
        {
            const uint32_t pair = static_cast<uint32_t>(fat[n] & 0xFFF) | (static_cast<uint32_t>(fat[n + 1] & 0xFFF) << 12);
            const size_t at = n * 3 / 2;
            if (at + 2 >= kFatSectors * 512)
                break;
            table[at] = static_cast<uint8_t>(pair);
            table[at + 1] = static_cast<uint8_t>(pair >> 8);
            table[at + 2] = static_cast<uint8_t>(pair >> 16);
        }
    }
    return disk;
}

/// Every file of a kit release folder in testdata/machines/sprinter/network/<folder> (top level), byte for byte
inline std::vector<KitFile> KitReleaseFiles(const std::string& folder)
{
    std::vector<KitFile> files;
    const std::filesystem::path dir =
        TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "sprinter" / "network" / folder;
    if (!std::filesystem::is_directory(dir))
        return files;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;
        std::ifstream in(entry.path(), std::ios::binary);
        KitFile f;
        f.name = entry.path().filename().string();
        f.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        files.push_back(std::move(f));
    }
    std::sort(files.begin(), files.end(), [](const KitFile& a, const KitFile& b) { return a.name < b.name; });
    return files;
}

/// DOS text: lines joined with CR LF
inline std::vector<uint8_t> DosText(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& line : lines)
        text += line + "\r\n";
    return std::vector<uint8_t>(text.begin(), text.end());
}
