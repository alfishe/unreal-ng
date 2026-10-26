// FatFs (unrealdisk/fatfs.h): the changes a DOS makes to a FAT volume, checked with the independent reader
// (FatVolumeReader) on FAT16 and FAT32 images built by FatImageBuilder
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatimagebuilder.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/rawimage.h"
#include "unrealdisk/fatfs.h"

using unrealdisk::FatFs;
using unrealdisk::FatNode;

namespace
{
std::vector<uint8_t> Pattern(size_t size, uint8_t seed)
{
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; i++)
        data[i] = static_cast<uint8_t>(i * 7 + seed + (i >> 9));
    return data;
}
}  // namespace

class FatFs_Test : public ::testing::TestWithParam<int>
{
protected:
    std::unique_ptr<ScratchFatImage> _image;
    std::unique_ptr<RawImage> _disk;

    void SetUp() override
    {
        FatImageSpec spec;
        spec.fat = GetParam();
        spec.sizeBytes = spec.fat == 32 ? 40ull << 20 : 8ull << 20;
        spec.files = {{"README.TXT", Pattern(1500, 1)}};
        _image = std::make_unique<ScratchFatImage>("fatfs-" + std::to_string(spec.fat) + ".img", spec);
        ASSERT_TRUE(_image->ok()) << _image->error();
        std::string error;
        _disk = RawImage::Open(_image->path(), RawImage::Access::ReadWrite, &error);
        ASSERT_NE(_disk, nullptr) << error;
    }

    /// The file as the independent reader sees it
    std::vector<uint8_t> ReadBack(const std::string& path)
    {
        FatVolumeReader reader;
        std::vector<uint8_t> data;
        EXPECT_TRUE(reader.Open(*_disk));
        EXPECT_TRUE(reader.ReadFile(path, data)) << path;
        return data;
    }
    std::vector<FatDirEntryInfo> ListBack(const std::string& path)
    {
        FatVolumeReader reader;
        std::vector<FatDirEntryInfo> entries;
        EXPECT_TRUE(reader.Open(*_disk));
        EXPECT_TRUE(reader.List(path, entries)) << path;
        return entries;
    }
};

TEST_P(FatFs_Test, ReadsAtAnyOffset)
{
    FatFs fs(*_disk);
    ASSERT_TRUE(fs.Open());
    FatNode file;
    ASSERT_TRUE(fs.Resolve("/readme.txt", file));
    EXPECT_EQ(file.size, 1500u);
    const std::vector<uint8_t> expected = Pattern(1500, 1);
    std::vector<uint8_t> part(700);
    ASSERT_EQ(fs.Read(file, 600, part.data(), part.size()), 700u);
    EXPECT_TRUE(std::equal(part.begin(), part.end(), expected.begin() + 600));
    EXPECT_EQ(fs.Read(file, 1400, part.data(), part.size()), 100u) << "short at the end";
    EXPECT_EQ(fs.Read(file, 1500, part.data(), part.size()), 0u);
    FatNode missing;
    EXPECT_FALSE(fs.Resolve("/NOPE.TXT", missing));
    EXPECT_EQ(fs.LastError(), FatFs::Error::NotFound);
}

TEST_P(FatFs_Test, CreatesAndGrowsFilesAcrossClusters)
{
    FatFs fs(*_disk);
    ASSERT_TRUE(fs.Open());
    fs.SetClock(((2026 - 1980) << 9) | (10 << 5) | 26, (15 << 11) | (30 << 5));
    FatNode file;
    ASSERT_TRUE(fs.Create(fs.Root(), "SCORES.DAT", false, file));
    EXPECT_EQ(file.shortName, "SCORES.DAT");
    EXPECT_TRUE(file.longSlots.empty()) << "an 8.3 name needs no long name";
    const std::vector<uint8_t> data = Pattern(3 * fs.ClusterBytes() + 77, 9);
    size_t written = 0;
    ASSERT_TRUE(fs.Write(file, 0, data.data(), 1000, written));
    ASSERT_TRUE(fs.Write(file, 1000, data.data() + 1000, data.size() - 1000, written));
    EXPECT_EQ(written, data.size() - 1000);
    EXPECT_EQ(ReadBack("/SCORES.DAT"), data);

    // a write past the end leaves zeros between
    ASSERT_TRUE(fs.Write(file, static_cast<uint32_t>(data.size() + 10), data.data(), 5, written));
    std::vector<uint8_t> expected = data;
    expected.resize(data.size() + 10, 0);
    expected.insert(expected.end(), data.begin(), data.begin() + 5);
    EXPECT_EQ(ReadBack("/SCORES.DAT"), expected);
    for (const FatDirEntryInfo& e : ListBack("/"))
        if (e.shortName == "SCORES.DAT")
        {
            EXPECT_EQ(e.date, ((2026 - 1980) << 9) | (10 << 5) | 26) << "the clock the caller set";
            EXPECT_EQ(e.time, (15 << 11) | (30 << 5));
        }
}

TEST_P(FatFs_Test, ALongNameGetsItsShortName)
{
    FatFs fs(*_disk);
    ASSERT_TRUE(fs.Open());
    FatNode a, b, lower;
    ASSERT_TRUE(fs.Create(fs.Root(), "High Scores Table.dat", false, a));
    ASSERT_TRUE(fs.Create(fs.Root(), "High Scores Other.dat", false, b));
    ASSERT_TRUE(fs.Create(fs.Root(), "notes.txt", false, lower));
    EXPECT_EQ(a.shortName, "HIGHSC~1.DAT");
    EXPECT_EQ(b.shortName, "HIGHSC~2.DAT");
    EXPECT_EQ(lower.shortName, "notes.txt") << "a lower-case 8.3 name keeps its case in the NT flags";
    EXPECT_TRUE(lower.longSlots.empty());
    std::vector<std::string> names;
    for (const FatDirEntryInfo& e : ListBack("/"))
        names.push_back(e.name);
    EXPECT_NE(std::find(names.begin(), names.end(), "High Scores Table.dat"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "High Scores Other.dat"), names.end());
    FatNode again;
    EXPECT_FALSE(fs.Create(fs.Root(), "high scores table.DAT", false, again)) << "names compare without case";
    EXPECT_EQ(fs.LastError(), FatFs::Error::Exists);
    EXPECT_FALSE(fs.Create(fs.Root(), "a*b", false, again));
    EXPECT_EQ(fs.LastError(), FatFs::Error::InvalidName);
}

TEST_P(FatFs_Test, DirectoriesNestAndGrow)
{
    FatFs fs(*_disk);
    ASSERT_TRUE(fs.Open());
    FatNode games, sub;
    ASSERT_TRUE(fs.Create(fs.Root(), "GAMES", true, games));
    ASSERT_TRUE(fs.Create(games, "Saved Games", true, sub));
    // more entries than one cluster holds: the directory takes another
    const uint32_t perCluster = fs.ClusterBytes() / 32;
    for (uint32_t i = 0; i < perCluster / 2 + 4; i++)
    {
        FatNode f;
        ASSERT_TRUE(fs.Create(sub, "slot number " + std::to_string(i) + ".sav", false, f)) << i;
        size_t written = 0;
        const uint8_t byte = static_cast<uint8_t>(i);
        ASSERT_TRUE(fs.Write(f, 0, &byte, 1, written));
    }
    const std::vector<FatDirEntryInfo> entries = ListBack("/GAMES/Saved Games");
    ASSERT_EQ(entries.size(), perCluster / 2 + 4);
    EXPECT_EQ(ReadBack("/GAMES/Saved Games/slot number 7.sav"), std::vector<uint8_t>{7});

    FatNode file;
    ASSERT_TRUE(fs.Resolve("/games/saved games/SLOT NUMBER 3.SAV", file)) << "any case";
    EXPECT_FALSE(fs.Remove(sub));
    EXPECT_EQ(fs.LastError(), FatFs::Error::NotEmpty);
}

TEST_P(FatFs_Test, RemoveTruncateRenameFreeTheirClusters)
{
    FatFs fs(*_disk);
    ASSERT_TRUE(fs.Open());
    const uint32_t freeBefore = fs.FreeClusters();
    FatNode file, dir;
    ASSERT_TRUE(fs.Create(fs.Root(), "big file.bin", false, file));
    const std::vector<uint8_t> data = Pattern(5 * fs.ClusterBytes(), 3);
    size_t written = 0;
    ASSERT_TRUE(fs.Write(file, 0, data.data(), data.size(), written));
    EXPECT_EQ(fs.FreeClusters(), freeBefore - 5);
    ASSERT_TRUE(fs.Resize(file, fs.ClusterBytes() + 1));
    EXPECT_EQ(fs.FreeClusters(), freeBefore - 2);
    EXPECT_EQ(ReadBack("/big file.bin"), std::vector<uint8_t>(data.begin(), data.begin() + fs.ClusterBytes() + 1));

    ASSERT_TRUE(fs.Create(fs.Root(), "ARCHIVE", true, dir));
    FatNode moved;
    ASSERT_TRUE(fs.Rename(file, dir, "kept.bin", moved));
    EXPECT_TRUE(ListBack("/").size() == 2u) << "README.TXT and ARCHIVE";
    EXPECT_EQ(ReadBack("/ARCHIVE/kept.bin").size(), fs.ClusterBytes() + 1);
    FatNode recased;
    ASSERT_TRUE(fs.Rename(moved, dir, "KEPT.BIN", recased)) << "a case-only rename";
    EXPECT_EQ(recased.name, "KEPT.BIN");
    ASSERT_TRUE(fs.Remove(recased));
    ASSERT_TRUE(fs.Remove(dir));
    EXPECT_EQ(fs.FreeClusters(), freeBefore) << "every cluster back";
    EXPECT_EQ(ListBack("/").size(), 1u);
}

TEST_P(FatFs_Test, AReadOnlyDeviceRefusesChanges)
{
    std::string error;
    std::unique_ptr<RawImage> disk = RawImage::Open(_image->path(), RawImage::Access::ReadOnly, &error);
    ASSERT_NE(disk, nullptr) << error;
    FatFs fs(*disk);
    ASSERT_TRUE(fs.Open());
    FatNode file;
    EXPECT_FALSE(fs.Create(fs.Root(), "NEW.TXT", false, file));
    EXPECT_EQ(fs.LastError(), FatFs::Error::ReadOnly);
}

INSTANTIATE_TEST_SUITE_P(Fat, FatFs_Test, ::testing::Values(16, 32), [](const auto& info) {
    return "Fat" + std::to_string(info.param);
});
