// Composite media with FAT disk image layers (multi-source phases/c3-image-sources.md §6;
// fs-compatibility.md S-1, S-2)

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/chd/chdwriter.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    std::string Read(FatVolumeReader& reader, const std::string& path)
    {
        std::vector<uint8_t> data;
        std::string error;
        EXPECT_TRUE(reader.ReadFile(path, data, &error)) << path << ": " << error;
        return std::string(data.begin(), data.end());
    }

    std::string Text(const std::filesystem::path& path)
    {
        const auto u8 = path.generic_u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// Two images: a FAT16 card (MBR) and a FAT32 superfloppy, plus a folder
    struct Sources
    {
        ScratchFolder root{"compose-fat"};

        void Make()
        {
            ScratchFolder sixteen("compose-fat-src16");
            sixteen.File("DSS/command.com", "dss shell");
            sixteen.File("GAMES/elite.trd", "elite from fat16");
            sixteen.File("readme.txt", "fat16 readme");
            const FatSourceImage a = FolderToFatDisk(sixteen.Path(), FatType::Fat16, CodePage::Cp866, true);
            ASSERT_TRUE(a.ok()) << a.error;
            ASSERT_TRUE(SaveSparse(a, root.Path() / "fat16.img"));

            ScratchFolder thirtyTwo("compose-fat-src32");
            thirtyTwo.File("GAMES/exolon.scl", "exolon from fat32");
            thirtyTwo.File("readme.txt", "fat32 readme");
            const FatSourceImage b = FolderToFatDisk(thirtyTwo.Path(), FatType::Fat32, CodePage::Cp866, false);
            ASSERT_TRUE(b.ok()) << b.error;
            ASSERT_TRUE(SaveSparse(b, root.Path() / "fat32.img"));

            root.File("work/tool.com", "tool");
        }

        ComposeDescriptor Descriptor(const std::string& yaml)
        {
            root.File("disk.ucompose.yaml", yaml);
            return ComposeDescriptor::Load(root.Path() / "disk.ucompose.yaml");
        }
    };

    const char* kThreeLayers = R"(
version: 1
target: {build: rebuild, fs: %FS%, free: 1MiB, fixedTime: 1767268800}
layers:
  - {name: sixteen, source: {image: fat16.img}}
  - {name: thirtytwo, source: {image: fat32.img}}
  - {name: work, source: {folder: work}, mount: /WORK}
)";

    std::string WithFs(const char* fs)
    {
        std::string yaml = kThreeLayers;
        yaml.replace(yaml.find("%FS%"), 4, fs);
        return yaml;
    }
}  // namespace

/// S-1: a FAT16 image and a FAT32 image (and a folder) merged into one FAT32 volume
TEST(ComposeFat_Test, MergeFat16AndFat32IntoFat32)
{
    Sources s;
    s.Make();
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = CompositeMediumFactory::Build(s.Descriptor(WithFs("fat32")), {}, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    EXPECT_EQ(info.fs, FatType::Fat32);
    EXPECT_EQ(info.sourceDevices, 2u);
    ASSERT_EQ(info.layers.size(), 3u);
    EXPECT_EQ(info.layers[0].kind, "image");

    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(reader.Type(), FatReaderType::Fat32);
    EXPECT_EQ(Read(reader, "/DSS/command.com"), "dss shell");
    EXPECT_EQ(Read(reader, "/GAMES/elite.trd"), "elite from fat16") << "directories of both images merge";
    EXPECT_EQ(Read(reader, "/GAMES/exolon.scl"), "exolon from fat32");
    EXPECT_EQ(Read(reader, "/readme.txt"), "fat32 readme") << "the upper image shadows the lower";
    EXPECT_EQ(Read(reader, "/WORK/tool.com"), "tool");
}

/// S-2: the same union fits FAT16, so auto and fat16 build FAT16
TEST(ComposeFat_Test, IntoFat16WhenFits)
{
    Sources s;
    s.Make();
    for (const char* fs : {"auto", "fat16"})
    {
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        const MediaResult result = CompositeMediumFactory::Build(s.Descriptor(WithFs(fs)), {}, volume, info);
        ASSERT_TRUE(result.Ok()) << fs << ": " << result.message;
        EXPECT_EQ(info.fs, FatType::Fat16) << fs;
        FatVolumeReader reader;
        ASSERT_TRUE(reader.Open(*volume));
        EXPECT_EQ(Read(reader, "/GAMES/exolon.scl"), "exolon from fat32");
    }
}

/// S-2 refused: more than FAT16 holds (a sparse 2.1 GiB host file next to the images)
TEST(ComposeFat_Test, IntoFat16TooBigFails)
{
    Sources s;
    s.Make();
    const auto big = s.root.File("work/big.bin", "");
    std::error_code ec;
    std::filesystem::resize_file(big, 2200ull * 1024 * 1024, ec);  // sparse: nothing is written
    ASSERT_FALSE(ec) << ec.message();
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = CompositeMediumFactory::Build(s.Descriptor(WithFs("fat16")), {}, volume, info);
    EXPECT_EQ(result.error, MediaError::DoesNotFit) << result.message;
    EXPECT_NE(result.message.find("fat32"), std::string::npos) << "names the way out: " << result.message;
}

/// A CHD named by two layers is opened once; its files read right
TEST(ComposeFat_Test, ChdSourceReadsThroughSharedCache)
{
    Sources s;
    ScratchFolder content("compose-fat-chd-src");
    content.File("A/one.txt", "one");
    content.File("B/two.txt", "two");
    const FatSourceImage image = FolderToFatDisk(content.Path(), FatType::Fat16, CodePage::Cp866, true);
    ASSERT_TRUE(image.ok()) << image.error;
    chd::WriteOptions options;  // uncompressed: the codecs have their own tests, this one is about sharing
    std::string error;
    ASSERT_TRUE(chd::WriteChd(Text(s.root.Path() / "card.chd"), *image.disk, options, &error)) << error;

    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = CompositeMediumFactory::Build(
        s.Descriptor("version: 1\ntarget: {free: 1MiB}\nlayers:\n"
                     "  - {name: a, source: {image: card.chd}, from: /A, mount: /ONE}\n"
                     "  - {name: b, source: {image: ./card.chd}, from: /B, mount: /TWO}\n"),
        {}, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    EXPECT_EQ(info.sourceDevices, 1u) << "one ChdImage, one hunk cache";
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(Read(reader, "/ONE/one.txt"), "one");
    EXPECT_EQ(Read(reader, "/TWO/two.txt"), "two");
}

/// What cannot be a FAT image layer fails with the layer named
TEST(ComposeFat_Test, ImageLayerErrors)
{
    Sources s;
    s.Make();
    s.root.File("notfat.img", std::string(64 * 1024, '\0'));
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    for (const char* yaml : {"version: 1\nlayers: [{name: x, source: {image: missing.img}}]\n",
                             "version: 1\nlayers: [{name: x, source: {image: work}}]\n",
                             "version: 1\nlayers: [{name: x, source: {image: fat16.img, partition: 3}}]\n",
                             "version: 1\nlayers: [{name: x, source: {image: fat32.img, partition: 1}}]\n",
                             "version: 1\nlayers: [{name: x, source: {image: notfat.img}}]\n",
                             "version: 1\nlayers: [{name: x, source: {image: fat16.img}, from: /NOPE}]\n"})
    {
        const MediaResult r = CompositeMediumFactory::Build(s.Descriptor(yaml), {}, volume, info);
        EXPECT_EQ(r.error, MediaError::UnreadableSource) << yaml << " -> " << r.message;
        EXPECT_NE(r.message.find("layer 'x'"), std::string::npos) << r.message;
    }
}
