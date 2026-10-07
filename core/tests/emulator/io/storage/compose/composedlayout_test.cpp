// IComposedLayout (multi-source phases/c6-provenance-flatten.md §3): what each sector of a rebuilt or grafted volume
// holds, checked against the independent FatVolumeReader's view of the same volume.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/compose/composedlayout.h"
#include "emulator/io/storage/compose/graftvolume.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    struct Layout
    {
        ScratchFolder root{"composed-layout"};
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        FatVolumeReader reader;

        void Build(const std::string& yaml)
        {
            root.File("disk.ucompose.yaml", yaml);
            const MediaResult result =
                CompositeMediumFactory::Build(ComposeDescriptor::Load(root.Path() / "disk.ucompose.yaml"), {}, volume, info);
            ASSERT_TRUE(result.Ok()) << result.message;
            ASSERT_TRUE(reader.Open(*volume));
        }

        const IComposedLayout& Of() const { return *dynamic_cast<const IComposedLayout*>(volume.get()); }

        uint64_t ClusterLba(uint32_t cluster) const
        {
            return reader.VolumeStart() + reader.DataStart() + static_cast<uint64_t>(cluster - 2) * reader.SectorsPerCluster();
        }

        FatDirEntryInfo Stat(const std::string& path)
        {
            FatDirEntryInfo e;
            EXPECT_TRUE(reader.Stat(path, e)) << path;
            return e;
        }

        std::string NameOf(const SectorOwner& owner) const { return owner.HasNode() ? Of().Tree().PathOf(owner.node) : "-"; }
    };
}  // namespace

TEST(ComposedLayout_Test, EveryRegionKindOfARebuild)
{
    Layout l;
    l.root.File("base/README.TXT", std::string(1500, 'r'));
    l.root.File("base/DSS/COMMAND.COM", "shell");
    l.root.File("up/ELITE.TRD", std::string(5000, 'e'));
    l.Build("version: 1\ntarget: {build: rebuild, free: 1MiB}\nlayers:\n  - {name: base, source: {folder: base}}\n"
            "  - {name: up, source: {folder: up}, mount: /GAMES}\n");
    const IComposedLayout& layout = l.Of();
    const FatVolumeReader& r = l.reader;
    ASSERT_GT(r.VolumeStart(), 1u) << "an MBR volume";

    EXPECT_EQ(layout.OwnerOf(0).role, SectorRole::PartitionTable);
    EXPECT_EQ(layout.OwnerOf(1).role, SectorRole::BootArea);
    EXPECT_EQ(layout.OwnerOf(r.VolumeStart()).role, SectorRole::VolumeHeader);
    EXPECT_EQ(layout.OwnerOf(r.VolumeStart() + r.ReservedSectors()).role, SectorRole::Fat);
    EXPECT_EQ(layout.OwnerOf(r.VolumeStart() + r.ReservedSectors() + 2ull * r.FatSectors() - 1).role, SectorRole::Fat);

    const SectorOwner root = layout.OwnerOf(r.VolumeStart() + r.ReservedSectors() + 2ull * r.FatSectors() + 1);
    EXPECT_EQ(root.role, SectorRole::Directory);
    EXPECT_EQ(l.NameOf(root), "/");
    EXPECT_EQ(root.offset, 512u);
    EXPECT_EQ(root.dirCluster, 0u);

    const FatDirEntryInfo dss = l.Stat("/DSS");
    const SectorOwner dir = layout.OwnerOf(l.ClusterLba(dss.firstCluster));
    EXPECT_EQ(dir.role, SectorRole::Directory);
    EXPECT_EQ(l.NameOf(dir), "/DSS");
    EXPECT_EQ(dir.dirCluster, dss.firstCluster);
    EXPECT_EQ(dir.layer, 0);

    const FatDirEntryInfo elite = l.Stat("/GAMES/ELITE.TRD");
    const FatDirEntryInfo games = l.Stat("/GAMES");
    std::vector<FatChainExtent> extents;
    ASSERT_TRUE(l.reader.ChainExtents(elite.firstCluster, elite.size, extents));
    const SectorOwner data = layout.OwnerOf(extents[0].lba + 3);
    EXPECT_EQ(data.role, SectorRole::FileData);
    EXPECT_EQ(l.NameOf(data), "/GAMES/ELITE.TRD");
    EXPECT_EQ(data.offset, 3u * 512);
    EXPECT_EQ(data.layer, 1);
    EXPECT_EQ(data.dirCluster, games.firstCluster) << "the file's directory";

    EXPECT_EQ(layout.OwnerOf(l.volume->SectorCount() - 1).role, SectorRole::Free);
}

TEST(ComposedLayout_Test, GraftOwners)
{
    Layout l;
    ScratchFolder base("composed-layout-base");
    base.File("README.TXT", std::string(1500, 'r'));
    base.File("DSS/COMMAND.COM", "shell");
    const FatSourceImage image = FolderToFatDisk(base.Path(), FatType::Fat16);
    ASSERT_TRUE(image.ok()) << image.error;
    ASSERT_TRUE(SaveSparse(image, l.root.Path() / "base.img"));
    l.root.File("up/ELITE.TRD", std::string(5000, 'e'));
    l.Build("version: 1\nlayers:\n  - {name: base, source: {image: base.img}}\n  - {name: up, source: {folder: up}, mount: /GAMES}\n");
    ASSERT_EQ(l.info.build, "graft");
    const IComposedLayout& layout = l.Of();

    const FatDirEntryInfo readme = l.Stat("/README.TXT");
    const SectorOwner baseFile = layout.OwnerOf(l.ClusterLba(readme.firstCluster) + 1);
    EXPECT_EQ(baseFile.role, SectorRole::FileData);
    EXPECT_EQ(l.NameOf(baseFile), "/README.TXT");
    EXPECT_EQ(baseFile.layer, 0);
    EXPECT_EQ(baseFile.offset, 512u);
    EXPECT_FALSE(baseFile.patched);

    const FatDirEntryInfo elite = l.Stat("/GAMES/ELITE.TRD");
    const SectorOwner grafted = layout.OwnerOf(l.ClusterLba(elite.firstCluster));
    EXPECT_EQ(grafted.role, SectorRole::FileData);
    EXPECT_EQ(l.NameOf(grafted), "/GAMES/ELITE.TRD");
    EXPECT_EQ(grafted.layer, 1);

    // C4b: an untouched base directory is not read at build: no tree node, but its layer and cluster
    const SectorOwner dss = layout.OwnerOf(l.ClusterLba(l.Stat("/DSS").firstCluster));
    EXPECT_EQ(dss.role, SectorRole::Directory) << "an untouched base directory";
    EXPECT_TRUE(dss.unlisted);
    EXPECT_TRUE(dss.Known());
    EXPECT_EQ(dss.layer, 0);
    EXPECT_EQ(dss.dirCluster, l.Stat("/DSS").firstCluster);
    EXPECT_FALSE(dss.patched);
    const SectorOwner shell = layout.OwnerOf(l.ClusterLba(l.Stat("/DSS/COMMAND.COM").firstCluster));
    EXPECT_EQ(shell.role, SectorRole::FileData);
    EXPECT_TRUE(shell.unlisted);
    EXPECT_EQ(shell.dirCluster, l.Stat("/DSS").firstCluster) << "the file's directory";

    const SectorOwner games = layout.OwnerOf(l.ClusterLba(l.Stat("/GAMES").firstCluster));
    EXPECT_EQ(games.role, SectorRole::Directory) << "a new directory";
    EXPECT_TRUE(games.patched);

    const FatVolumeReader& r = l.reader;
    const SectorOwner rootRegion = layout.OwnerOf(r.VolumeStart() + r.ReservedSectors() + 2ull * r.FatSectors());
    EXPECT_EQ(rootRegion.role, SectorRole::Directory);
    EXPECT_TRUE(rootRegion.patched) << "the root gained /GAMES";
    EXPECT_EQ(layout.OwnerOf(r.VolumeStart() + r.ReservedSectors()).role, SectorRole::Fat);
}

TEST(ComposedLayout_Test, ForEachChangedOwnerInLbaOrder)
{
    Layout l;
    l.root.File("base/A.TXT", std::string(3000, 'a'));
    l.Build("version: 1\ntarget: {free: 1MiB}\nlayers: [{source: {folder: base}}]\n");
    std::map<uint64_t, std::array<uint8_t, 512>> changes;
    for (uint64_t lba : {uint64_t(0), l.reader.VolumeStart() + 1, l.ClusterLba(l.Stat("/A.TXT").firstCluster) + 2, uint64_t(40)})
        changes[lba] = {};
    std::vector<uint64_t> seen;
    ForEachChangedOwner(l.Of(), MapChangeView(changes), [&](uint64_t lba, const SectorOwner& owner) {
        seen.push_back(lba);
        EXPECT_EQ(owner.role, l.Of().OwnerOf(lba).role) << lba;
    });
    ASSERT_EQ(seen.size(), 4u);
    EXPECT_TRUE(std::is_sorted(seen.begin(), seen.end()));
}
