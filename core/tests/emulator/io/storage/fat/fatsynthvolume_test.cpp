// FatSynthVolume over a union of several folder layers, checked through the
// independent FatVolumeReader (multi-source tdd.md §5)

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/compose/hostfoldersource.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/compose/unionbuilder.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"

namespace
{
    struct TwoLayers
    {
        ScratchFolder low{"fsv-low"};
        ScratchFolder up{"fsv-up"};
        FolderSnapshot lowScan;
        FolderSnapshot upScan;
        std::shared_ptr<SourcePool> pool = std::make_shared<SourcePool>();
        FileTree lowTree;
        FileTree upTree;

        std::unique_ptr<FatSynthVolume> Build(FatType fs, const char* upMount, std::vector<std::string>* report = nullptr)
        {
            EXPECT_TRUE(FolderSnapshot::Scan(low.Path(), {}, lowScan));
            EXPECT_TRUE(FolderSnapshot::Scan(up.Path(), {}, upScan));
            EXPECT_TRUE(HostFolderSource::Enumerate(lowScan, {}, *pool, lowTree, nullptr, nullptr));
            EXPECT_TRUE(HostFolderSource::Enumerate(upScan, {}, *pool, upTree, nullptr, nullptr));
            UnionLayer a;
            a.tree = &lowTree;
            a.name = "low";
            UnionLayer b;
            b.tree = &upTree;
            b.name = "up";
            b.mount = upMount;
            auto tree = std::make_shared<FileTree>();
            std::string error;
            EXPECT_TRUE(UnionBuilder::Merge({a, b}, UnionBuilder::FatKey, *tree, report, &error)) << error;
            FatVolumeOptions options;
            options.fs = fs;
            options.freeBytes = 1024 * 1024;
            options.fixedTimeUtc = 1767268800;
            auto volume = FatSynthVolume::Build(tree, pool, options, lowScan.Identity() ^ upScan.Identity(), "two layers",
                                                &error, report);
            EXPECT_NE(volume, nullptr) << error;
            return volume;
        }
    };

    std::string ReadAll(FatVolumeReader& reader, const std::string& path)
    {
        std::vector<uint8_t> data;
        std::string error;
        EXPECT_TRUE(reader.ReadFile(path, data, &error)) << path << ": " << error;
        return std::string(data.begin(), data.end());
    }
}  // namespace

TEST(FatSynthVolume_Test, TwoFolderLayersReadBackByOracle)
{
    for (FatType fs : {FatType::Fat16, FatType::Fat32})
    {
        TwoLayers layers;
        layers.low.File("boot.bin", "low boot");
        layers.low.File("GAMES/old.trd", std::string(5000, 'o'));
        layers.low.File("readme.txt", "lower readme");
        layers.up.File("GAMES/new.trd", std::string(70000, 'n'));
        layers.up.File("README.TXT", "upper readme");
        std::vector<std::string> report;
        auto volume = layers.Build(fs, "/", &report);
        ASSERT_NE(volume, nullptr);

        FatVolumeReader reader;
        std::string error;
        ASSERT_TRUE(reader.Open(*volume, CodePage::Cp866, &error)) << error;
        std::vector<FatDirEntryInfo> root;
        ASSERT_TRUE(reader.List("/", root));
        ASSERT_EQ(root.size(), 3u);
        EXPECT_EQ(root[0].name, "GAMES");
        EXPECT_EQ(root[1].name, "README.TXT") << "the upper layer's entry replaces readme.txt";
        EXPECT_EQ(root[2].name, "boot.bin");
        EXPECT_EQ(ReadAll(reader, "/README.TXT"), "upper readme");
        EXPECT_EQ(ReadAll(reader, "/boot.bin"), "low boot");
        EXPECT_EQ(ReadAll(reader, "/GAMES/old.trd"), std::string(5000, 'o'));
        EXPECT_EQ(ReadAll(reader, "/GAMES/new.trd"), std::string(70000, 'n'));
        EXPECT_FALSE(report.empty()) << "the shadowing is reported";
    }
}

TEST(FatSynthVolume_Test, MountedLayerAndShortNamesUniqueAcrossLayers)
{
    TwoLayers layers;
    layers.low.File("LongFileName1.txt", "1");
    layers.up.File("LongFileName2.txt", "2");
    layers.up.File("LongFileName3.txt", "3");
    auto volume = layers.Build(FatType::Fat16, "/", nullptr);
    ASSERT_NE(volume, nullptr);

    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    std::vector<FatDirEntryInfo> root;
    ASSERT_TRUE(reader.List("/", root));
    ASSERT_EQ(root.size(), 3u);
    EXPECT_EQ(root[0].shortName, "LONGFI~1.TXT");
    EXPECT_EQ(root[1].shortName, "LONGFI~2.TXT");
    EXPECT_EQ(root[2].shortName, "LONGFI~3.TXT") << "8.3 names are made per merged directory, unique across layers";
    EXPECT_EQ(ReadAll(reader, "/LongFileName3.txt"), "3");
}

TEST(FatSynthVolume_Test, LayerMountedUnderAPath)
{
    TwoLayers layers;
    layers.low.File("SYSTEM/boot.com", "boot");
    layers.up.File("x.com", "x");
    layers.up.File("lib/y.com", "y");
    auto volume = layers.Build(FatType::Fat16, "/BIN/TOOLS", nullptr);
    ASSERT_NE(volume, nullptr);

    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(ReadAll(reader, "/BIN/TOOLS/x.com"), "x");
    EXPECT_EQ(ReadAll(reader, "/BIN/TOOLS/lib/y.com"), "y");
    EXPECT_EQ(ReadAll(reader, "/SYSTEM/boot.com"), "boot");
}
