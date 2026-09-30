// HostFolderFat: a host folder as a FAT16 / FAT32 volume, checked through the
// independent FatVolumeReader (technical design §6)

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"

namespace
{
    constexpr int64_t kTime = 1767268800;  // 2026-01-01 12:00:00 UTC

    std::string Pattern(size_t size, uint8_t seed)
    {
        std::string bytes(size, '\0');
        for (size_t i = 0; i < size; i++)
            bytes[i] = static_cast<char>((i * 31 + seed) & 0xFF);
        return bytes;
    }

    /// The test folder of the design's worked example, plus corner cases
    void Populate(ScratchFolder& folder)
    {
        folder.File("SD_BOOT.$C", Pattern(1297, 1));
        folder.File("games/EYEACHE.TRD", Pattern(70000, 2));
        folder.File("games/deep/er/than/that.bin", Pattern(5000, 3));
        folder.File("Длинное имя.txt", "0123456789");
        folder.File("empty.bin", "");
        folder.File(".profile", "hidden on the host");
        folder.File("LongFileName1.txt", "one");
        folder.File("LongFileName2.txt", "two");
        folder.File(".DS_Store", "service file");
        folder.Folder("empty-folder");
    }

    std::unique_ptr<HostFolderFat> BuildVolume(const ScratchFolder& folder, FatVolumeOptions options, FolderSnapshot& snapshot)
    {
        options.freeBytes = 1024 * 1024;  // keep test volumes small
        options.fixedTimeUtc = kTime;
        EXPECT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, snapshot));
        std::string error;
        std::vector<std::string> report;
        auto volume = HostFolderFat::Build(snapshot, options, &error, &report);
        EXPECT_NE(volume, nullptr) << error;
        EXPECT_TRUE(report.empty());
        return volume;
    }

    /// Every host entry is on the volume with the same name, kind, size and contents
    void Compare(FatVolumeReader& reader, const FolderEntry& host, const std::string& path)
    {
        std::vector<FatDirEntryInfo> listed;
        std::string error;
        ASSERT_TRUE(reader.List(path.empty() ? "/" : path, listed, &error)) << error;
        ASSERT_EQ(listed.size(), host.children.size()) << path;
        for (size_t i = 0; i < host.children.size(); i++)
        {
            const FolderEntry& h = host.children[i];
            const FatDirEntryInfo& v = listed[i];
            SCOPED_TRACE(path + "/" + h.name);
            EXPECT_EQ(v.name, h.name) << "the same name, in the same order";
            EXPECT_EQ(v.isDirectory, h.isDirectory);
            if (h.isDirectory)
            {
                Compare(reader, h, path + "/" + h.name);
                continue;
            }
            EXPECT_EQ(v.size, h.size);
            std::vector<uint8_t> data;
            ASSERT_TRUE(reader.ReadFile(path + "/" + h.name, data, &error)) << error;
            std::ifstream in(h.hostPath, std::ios::binary);
            const std::string expected((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            EXPECT_TRUE(data.size() == expected.size() && std::memcmp(data.data(), expected.data(), data.size()) == 0)
                << "contents";
        }
    }

    std::vector<uint8_t> ReadSector(IBlockDevice& device, uint64_t lba)
    {
        std::vector<uint8_t> s(512);
        EXPECT_TRUE(device.ReadSector(lba, s.data()));
        return s;
    }
}  // namespace

TEST(HostFolderFat_Test, Fat16RoundTripThroughAnIndependentReader)
{
    ScratchFolder folder("hff-fat16");
    Populate(folder);
    FolderSnapshot snapshot;
    auto volume = BuildVolume(folder, {}, snapshot);
    ASSERT_NE(volume, nullptr);

    FatVolumeReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(*volume, CodePage::Cp866, &error)) << error;
    EXPECT_EQ(reader.Type(), FatReaderType::Fat16);
    EXPECT_GE(reader.ClusterCount(), 4086u);
    EXPECT_LE(reader.ClusterCount(), 65525u);
    EXPECT_EQ(reader.VolumeStart(), 2048u);
    EXPECT_EQ(reader.Label(), "UNREAL NG");
    Compare(reader, snapshot.Root(), "");

    std::vector<FatDirEntryInfo> root;
    ASSERT_TRUE(reader.List("/", root));
    for (const auto& e : root)
    {
        if (e.name == ".profile")
            EXPECT_TRUE(e.attributes & 0x02) << "dot-files are hidden";
        if (e.name == "Длинное имя.txt")
            EXPECT_EQ(e.shortName, "ДЛИННО~1.TXT");
        if (e.name == "LongFileName2.txt")
            EXPECT_EQ(e.shortName, "LONGFI~2.TXT");
        EXPECT_EQ(e.date, ((2026 - 1980) << 9) | (1 << 5) | 1) << e.name;
        EXPECT_EQ(e.time, 12 << 11) << e.name;
    }
}

TEST(HostFolderFat_Test, Fat32RoundTripAndClusterMinimum)
{
    ScratchFolder folder("hff-fat32");
    Populate(folder);
    FolderSnapshot snapshot;
    FatVolumeOptions options;
    options.fs = FatType::Fat32;
    auto volume = BuildVolume(folder, options, snapshot);
    ASSERT_NE(volume, nullptr);

    FatVolumeReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(*volume, CodePage::Cp866, &error)) << error;
    EXPECT_EQ(reader.Type(), FatReaderType::Fat32) << "a strict reader (cluster count) sees FAT32";
    EXPECT_GE(reader.ClusterCount(), 65526u);
    EXPECT_EQ(reader.SectorsPerCluster(), 8u) << "4 KiB clusters";
    Compare(reader, snapshot.Root(), "");
}

TEST(HostFolderFat_Test, BootRecordsAndPartitionTable)
{
    ScratchFolder folder("hff-records");
    folder.File("a.bin", "a");
    FolderSnapshot snapshot;

    auto fat16 = BuildVolume(folder, {}, snapshot);
    const auto mbr = ReadSector(*fat16, 0);
    EXPECT_EQ(mbr[510], 0x55);
    EXPECT_EQ(mbr[511], 0xAA);
    EXPECT_EQ(mbr[446 + 4], 0x04) << "FAT16 below 32 MB";
    EXPECT_EQ(mbr[446 + 8] | (mbr[446 + 9] << 8), 2048);
    const auto boot = ReadSector(*fat16, 2048);
    EXPECT_EQ(std::string(boot.begin() + 54, boot.begin() + 62), "FAT16   ");
    EXPECT_EQ(std::string(boot.begin() + 43, boot.begin() + 54), "UNREAL NG  ");

    FatVolumeOptions options;
    options.fs = FatType::Fat32;
    FolderSnapshot snapshot32;
    auto fat32 = BuildVolume(folder, options, snapshot32);
    EXPECT_EQ(ReadSector(*fat32, 0)[446 + 4], 0x0C) << "FAT32 LBA";
    const auto boot32 = ReadSector(*fat32, 2048);
    EXPECT_EQ(std::string(boot32.begin() + 82, boot32.begin() + 90), "FAT32   ");
    EXPECT_EQ(ReadSector(*fat32, 2048 + 6), boot32) << "the backup boot sector equals the boot sector";
    const auto fsinfo = ReadSector(*fat32, 2049);
    EXPECT_EQ(fsinfo[0], 0x52);
    EXPECT_EQ(fsinfo[484], 0x72);
    EXPECT_EQ(fsinfo[510], 0x55);
}

TEST(HostFolderFat_Test, Cp1251NamesAndTheLabel)
{
    ScratchFolder folder("hff-cp1251");
    folder.File("Игра.trd", "game");
    FolderSnapshot snapshot;
    FatVolumeOptions options;
    options.codePage = CodePage::Cp1251;
    options.label = "МОИ ИГРЫ";
    auto volume = BuildVolume(folder, options, snapshot);

    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume, CodePage::Cp1251));
    EXPECT_EQ(reader.Label(), "МОИ ИГРЫ");
    std::vector<FatDirEntryInfo> root;
    ASSERT_TRUE(reader.List("/", root));
    ASSERT_EQ(root.size(), 1u);
    EXPECT_EQ(root[0].name, "Игра.trd");
    EXPECT_EQ(root[0].shortName, "ИГРА.TRD");

    // The same bytes read in the other code page show that the page matters
    FatVolumeReader wrongPage;
    ASSERT_TRUE(wrongPage.Open(*volume, CodePage::Cp866));
    std::vector<FatDirEntryInfo> again;
    ASSERT_TRUE(wrongPage.List("/", again));
    EXPECT_NE(again[0].shortName, "ИГРА.TRD");
    EXPECT_EQ(again[0].name, "Игра.trd") << "the long name is UTF-16, independent of the page";
}

/// Creating 90 host files is the cost (tens of ms on a busy disk): the root
/// region only grows past 512 entries with that many names
TEST(HostFolderFat_Test, LargeRootGrowsAndSuperfloppyWorks)
{
    ScratchFolder folder("hff-many");
    // 90 long names of 6 entries each (5 LFN + 1 short) = 540 root entries, more than the usual 512
    for (int i = 0; i < 90; i++)
        folder.File("a longer file name that needs five LFN entries " + std::to_string(1000 + i) + ".dat", "x");
    FolderSnapshot snapshot;
    FatVolumeOptions options;
    options.mbr = false;
    auto volume = BuildVolume(folder, options, snapshot);

    FatVolumeReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(*volume, CodePage::Cp866, &error)) << error;
    EXPECT_EQ(reader.VolumeStart(), 0u) << "superfloppy: the volume starts at LBA 0";
    std::vector<FatDirEntryInfo> root;
    ASSERT_TRUE(reader.List("/", root));
    EXPECT_EQ(root.size(), 90u);
    EXPECT_EQ(root[0].name, "a longer file name that needs five LFN entries 1000.dat");
}

TEST(HostFolderFat_Test, SameFolderSameBytes)
{
    ScratchFolder folder("hff-determinism");
    Populate(folder);
    FolderSnapshot a;
    FolderSnapshot b;
    auto first = BuildVolume(folder, {}, a);
    auto second = BuildVolume(folder, {}, b);
    ASSERT_EQ(first->SectorCount(), second->SectorCount());
    EXPECT_EQ(first->ContentId(), second->ContentId());
    uint8_t x[512];
    uint8_t y[512];
    for (uint64_t lba = 0; lba < first->SectorCount(); lba++)
    {
        ASSERT_TRUE(first->ReadSector(lba, x));
        ASSERT_TRUE(second->ReadSector(lba, y));
        ASSERT_EQ(std::memcmp(x, y, 512), 0) << "sector " << lba;
    }
}

TEST(HostFolderFat_Test, TooLargeForFat16SaysUseFat32)
{
    ScratchFolder folder("hff-too-big");
    folder.File("a.bin", "a");
    FolderSnapshot snapshot;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, snapshot));
    FatVolumeOptions options;
    options.freeBytes = 3ull * 1024 * 1024 * 1024;  // 3 GiB of room cannot be FAT16
    std::string error;
    EXPECT_EQ(HostFolderFat::Build(snapshot, options, &error, nullptr), nullptr);
    EXPECT_NE(error.find("fs=fat32"), std::string::npos) << error;

    options.fs = FatType::Fat32;
    auto volume = HostFolderFat::Build(snapshot, options, &error, nullptr);
    ASSERT_NE(volume, nullptr) << "the same folder as FAT32";
}

TEST(HostFolderFat_Test, HostFileThatShrankReadsZerosAndWarns)
{
    ScratchFolder folder("hff-shrink");
    const auto path = folder.File("data.bin", Pattern(3000, 7));
    FolderSnapshot snapshot;
    auto volume = BuildVolume(folder, {}, snapshot);

    std::ofstream(path, std::ios::binary | std::ios::trunc) << "tiny";  // the host changes the file after the scan
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    std::vector<uint8_t> data;
    ASSERT_TRUE(reader.ReadFile("/data.bin", data));
    ASSERT_EQ(data.size(), 3000u) << "the size of the snapshot";
    EXPECT_EQ(data[0], 't');
    EXPECT_EQ(data[2999], 0) << "past the new end: zeros";
    ASSERT_EQ(volume->Warnings().size(), 1u);
}
