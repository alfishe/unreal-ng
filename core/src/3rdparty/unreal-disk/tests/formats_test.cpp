// The format registry: every format of the repository's test images loads, and a TR-DOS disk saved in each writable
// format reads back with the same sectors

#include <gtest/gtest.h>

#include <cstring>

#include "testdata.h"
#include "unrealdisk/detail/helpers.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/trdosfs.h"

using namespace unrealdisk;
using unrealdisk::testing::ScratchFolder;
using unrealdisk::testing::TestDataPath;

namespace
{
std::unique_ptr<DiskImage> Load(const std::string& path, std::string* format = nullptr)
{
    std::unique_ptr<DiskImage> disk;
    std::string probed;
    const FormatResult result = FloppyFormats::Load({}, path, disk, probed);
    EXPECT_TRUE(result.Ok()) << path << ": " << result.message;
    if (format)
        *format = probed;
    return disk;
}

/// Every logical track's sectors 1-16 as one buffer (a TR-DOS view of the disk)
std::vector<uint8_t> TrdosBytes(DiskImage& disk)
{
    std::vector<uint8_t> out;
    for (size_t t = 0; t < static_cast<size_t>(disk.getCylinders()) * disk.getSides(); ++t)
        for (uint8_t s = 0; s < 16; ++s)
        {
            DiskImage::Track* track = disk.getTrack(static_cast<uint8_t>(t));
            const uint8_t* data = track ? track->getDataForSector(s) : nullptr;
            out.insert(out.end(), data ? data : nullptr, data ? data + 256 : nullptr);
            if (!data)
                out.insert(out.end(), 256, 0);
        }
    return out;
}
}  // namespace

TEST(Formats_Test, EveryTestImageLoads)
{
    const std::vector<std::pair<std::string, std::string>> images = {
        {"loaders/trd/EyeAche.trd", "trd"},          {"loaders/scl/eyeache2.scl", "scl"},
        {"loaders/fdi/VORON1.FDI", "fdi"},           {"loaders/udi/VORON1.UDI", "udi"},
        {"loaders/td0/trdos-sample.td0", "td0"},     {"loaders/dsk/plus3-blank.dsk", "dsk"},
        {"loaders/dsk/edsk-protected.dsk", "dsk"},   {"loaders/pro/ReadFile.pro", "pro"},
    };
    for (const auto& [path, expected] : images)
    {
        std::string format;
        const auto disk = Load(TestDataPath(path), &format);
        ASSERT_NE(disk, nullptr) << path;
        EXPECT_EQ(format, expected) << path;
        EXPECT_GT(disk->getCylinders(), 0) << path;
    }
}

TEST(Formats_Test, ATrdosDiskKeepsItsSectorsInEveryWritableFormat)
{
    auto original = Load(TestDataPath("loaders/trd/EyeAche.trd"));
    ASSERT_NE(original, nullptr);
    const std::vector<uint8_t> sectors = TrdosBytes(*original);
    const auto folder = ScratchFolder("roundtrip");
    for (const std::string ext : {"trd", "scl", "fdi", "udi", "td0"})
    {
        const std::string path = (folder / ("disk." + ext)).string();
        const FloppySaveResult saved = FloppyFormats::Save({}, *original, path, false);
        ASSERT_TRUE(saved.saved) << ext << ": " << saved.reason;
        std::string format;
        auto back = Load(path, &format);
        ASSERT_NE(back, nullptr) << ext;
        EXPECT_EQ(format, ext);
        std::vector<TrdosFile> before, after;
        std::string error;
        ASSERT_TRUE(TrdosFileSystem(*original).List(before, error)) << error;
        ASSERT_TRUE(TrdosFileSystem(*back).List(after, error)) << ext << ": " << error;
        ASSERT_EQ(after.size(), before.size()) << ext;
        for (size_t i = 0; i < before.size(); ++i)
        {
            std::vector<uint8_t> a, b;
            ASSERT_TRUE(TrdosFileSystem(*original).Read(before[i], a, nullptr, error)) << error;
            ASSERT_TRUE(TrdosFileSystem(*back).Read(after[i], b, nullptr, error)) << ext << ": " << error;
            EXPECT_EQ(a, b) << ext << ": " << before[i].TrimmedName();
        }
        if (ext != "scl")   // SCL keeps the files, not the free sectors' bytes
        {
            EXPECT_EQ(TrdosBytes(*back), sectors) << ext;
        }
    }
}

TEST(Formats_Test, UnknownFilesSayWhy)
{
    const auto folder = ScratchFolder("unknown");
    const std::string path = (folder / "notes.txt").string();
    {
        FILE* f = std::fopen(path.c_str(), "wb");
        std::fputs("not a disk", f);
        std::fclose(f);
    }
    std::unique_ptr<DiskImage> disk;
    std::string format;
    const FormatResult result = FloppyFormats::Load({}, path, disk, format);
    EXPECT_FALSE(result.Ok());
    EXPECT_EQ(result.error, FormatResult::Error::UnknownFormat);
    EXPECT_NE(result.message.find("unsupported"), std::string::npos) << result.message;
}

TEST(Formats_Test, ImagesInMemoryLoadLikeFiles)
{
    for (const std::string path : {"loaders/trd/EyeAche.trd", "loaders/scl/eyeache2.scl", "loaders/fdi/VORON1.FDI", "loaders/udi/VORON1.UDI",
                                   "loaders/td0/trdos-sample.td0", "loaders/dsk/plus3-blank.dsk"})
    {
        std::vector<uint8_t> bytes;
        ASSERT_TRUE(FileHelper::ReadFile(TestDataPath(path), bytes)) << path;
        std::unique_ptr<DiskImage> fromData;
        std::string dataFormat;
        const FormatResult result = FloppyFormats::LoadData({}, bytes.data(), bytes.size(), path, fromData, dataFormat);
        ASSERT_TRUE(result.Ok()) << path << ": " << result.message;
        std::string fileFormat;
        auto fromFile = Load(TestDataPath(path), &fileFormat);
        EXPECT_EQ(dataFormat, fileFormat) << path;
        ASSERT_EQ(fromData->getCylinders(), fromFile->getCylinders()) << path;
        EXPECT_EQ(TrdosBytes(*fromData), TrdosBytes(*fromFile)) << path;
    }
}

TEST(Formats_Test, AProDumpSavesBackByteForByteWithTheSystemTrackIds)
{
    const std::string path = TestDataPath("loaders/pro/ReadFile.pro");
    std::vector<uint8_t> original;
    ASSERT_TRUE(FileHelper::ReadFile(path, original));
    std::string format;
    auto disk = Load(path, &format);
    ASSERT_NE(disk, nullptr);
    EXPECT_EQ(format, "pro");
    // Cylinder 0 side 0 has IDs 1, 2, 3, 4, 9 (the BIOS boots from R = 9), every other track 1..5
    const DiskImage::Track* system = disk->getTrackForCylinderAndSide(0, 0);
    EXPECT_NE(system->findSector(9), nullptr);
    EXPECT_EQ(system->findSector(5), nullptr);
    EXPECT_NE(disk->getTrackForCylinderAndSide(0, 1)->findSector(5), nullptr);
    EXPECT_EQ(std::memcmp(system->findSector(9)->data, original.data() + 4 * 1024, 1024), 0);

    const auto folder = ScratchFolder("pro");
    const std::string copy = (folder / "copy.pro").string();
    const FloppySaveResult saved = FloppyFormats::Save({}, *disk, copy, false);
    ASSERT_TRUE(saved.saved) << saved.reason;
    std::vector<uint8_t> back;
    ASSERT_TRUE(FileHelper::ReadFile(copy, back));
    EXPECT_EQ(back, original);
}
