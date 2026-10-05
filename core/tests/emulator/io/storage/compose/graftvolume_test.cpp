// GraftVolume: upper layers written into the free clusters of a FAT image base
// (multi-source phases/c4-graft.md). Built through CompositeMediumFactory from
// descriptors, read back with the independent FatVolumeReader.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "emulator/io/storage/compose/graftvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/hddimageformats.h"
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

    std::vector<std::string> Names(FatVolumeReader& reader, const std::string& path)
    {
        std::vector<FatDirEntryInfo> entries;
        EXPECT_TRUE(reader.List(path, entries)) << path;
        std::vector<std::string> names;
        for (const FatDirEntryInfo& e : entries)
            names.push_back(e.name);
        return names;
    }

    bool Has(const std::vector<std::string>& lines, const std::string& text)
    {
        return std::any_of(lines.begin(), lines.end(), [&text](const std::string& l) { return l.find(text) != std::string::npos; });
    }

    /// A base image made from a folder, an upper folder, a descriptor naming both
    struct GraftCase
    {
        ScratchFolder root{"graft"};
        ScratchFolder baseFiles{"graft-base"};
        FatSourceImage base;

        void MakeBase(FatType type = FatType::Fat16, bool mbr = true, uint64_t freeBytes = 64 * 1024)
        {
            base = FolderToFatDisk(baseFiles.Path(), type, CodePage::Cp866, mbr, freeBytes);
            ASSERT_TRUE(base.ok()) << base.error;
            ASSERT_TRUE(SaveSparse(base, root.Path() / "base.img"));
        }

        MediaResult Build(const std::string& yaml, std::unique_ptr<IBlockDevice>& volume, CompositeInfo& info,
                          const CompositeBuildOptions& options = {})
        {
            root.File("disk.ucompose.yaml", yaml);
            return CompositeMediumFactory::Build(ComposeDescriptor::Load(root.Path() / "disk.ucompose.yaml"), options, volume, info);
        }

        /// The graft, asserting that one was built
        GraftVolume* Graft(const std::string& yaml, std::unique_ptr<IBlockDevice>& volume, std::vector<std::string>* report = nullptr)
        {
            CompositeInfo info;
            const MediaResult result = Build(yaml, volume, info);
            EXPECT_TRUE(result.Ok()) << result.message;
            EXPECT_EQ(info.build, "graft") << (result.report.empty() ? std::string() : result.report.back());
            if (report)
                *report = result.report;
            return dynamic_cast<GraftVolume*>(volume.get());
        }
    };

    const char* kBaseAndUpper = "version: 1\ntarget: {fixedTime: 1767268800}\n"
                                "layers:\n  - {name: base, source: {image: base.img}}\n  - {name: up, source: {folder: up}}\n";
}  // namespace

/// Outside the patched sectors and the grafted clusters every sector is the base's
TEST(GraftVolume_Test, UnpatchedSectorsIdenticalToBase)
{
    GraftCase g;
    g.baseFiles.File("readme.txt", "base readme");
    g.baseFiles.File("DOCS/a.txt", std::string(3000, 'a'));
    g.MakeBase();
    g.root.File("up/new.bin", std::string(5000, 'n'));
    g.root.File("up/DOCS/b.txt", "b");

    std::unique_ptr<IBlockDevice> volume;
    GraftVolume* graft = g.Graft(kBaseAndUpper, volume);
    ASSERT_NE(graft, nullptr);
    ASSERT_EQ(graft->SectorCount(), g.base.disk->SectorCount());
    uint8_t ours[512];
    uint8_t theirs[512];
    uint64_t patched = 0;
    uint64_t grafted = 0;
    for (uint64_t lba = 0; lba < g.base.usedEnd + 64; lba++)
    {
        ASSERT_TRUE(graft->ReadSector(lba, ours));
        g.base.disk->ReadSector(lba, theirs);
        switch (graft->SectorOrigin(lba))
        {
            case GraftSectorOrigin::Base:
                ASSERT_EQ(std::memcmp(ours, theirs, 512), 0) << "LBA " << lba;
                break;
            case GraftSectorOrigin::Patch: patched++; break;
            case GraftSectorOrigin::Graft: grafted++; break;
        }
    }
    EXPECT_EQ(patched, graft->PatchedSectors());
    EXPECT_GE(grafted, 5000u / 512) << "new.bin's sectors";
}

/// New files in the root, in a base directory and in a new deep directory; the base's files untouched
TEST(GraftVolume_Test, AddedFilesReadByOracle)
{
    GraftCase g;
    g.baseFiles.File("readme.txt", "base readme");
    g.baseFiles.File("DOCS/a.txt", "a");
    g.MakeBase();
    g.root.File("up/root.txt", "root");
    g.root.File("up/DOCS/b.txt", "b");
    g.root.File("up/NEW/deep/er/c.txt", std::string(70000, 'c'));
    g.root.File("up/Long Name Here.txt", "long");

    std::unique_ptr<IBlockDevice> volume;
    ASSERT_NE(g.Graft(kBaseAndUpper, volume), nullptr);
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(Read(reader, "/readme.txt"), "base readme");
    EXPECT_EQ(Read(reader, "/DOCS/a.txt"), "a");
    EXPECT_EQ(Read(reader, "/root.txt"), "root");
    EXPECT_EQ(Read(reader, "/DOCS/b.txt"), "b");
    EXPECT_EQ(Read(reader, "/NEW/deep/er/c.txt"), std::string(70000, 'c'));
    EXPECT_EQ(Read(reader, "/Long Name Here.txt"), "long");
}

/// An upper file of the same FAT key replaces a base file in place: same position and short name, new bytes,
/// the old clusters free
TEST(GraftVolume_Test, ReplacedFileShowsNewData)
{
    GraftCase g;
    g.baseFiles.File("AAA.TXT", "first");
    g.baseFiles.File("README.TXT", std::string(5000, 'o'));
    g.baseFiles.File("ZZZ.TXT", "last");
    g.MakeBase();
    FatVolumeReader before;
    ASSERT_TRUE(before.Open(*g.base.disk));
    FatDirEntryInfo old;
    ASSERT_TRUE(before.Stat("/README.TXT", old));
    g.root.File("up/readme.txt", "replaced");

    std::unique_ptr<IBlockDevice> volume;
    ASSERT_NE(g.Graft(kBaseAndUpper, volume), nullptr);
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(Names(reader, "/"), (std::vector<std::string>{"AAA.TXT", "README.TXT", "ZZZ.TXT"})) << "the base's order and name";
    EXPECT_EQ(Read(reader, "/README.TXT"), "replaced");
    EXPECT_EQ(reader.FatEntry(old.firstCluster + 1), 0u) << "the old chain is free (the new data needs one cluster)";
}

/// A whiteout file and a whiteout directory are gone and their clusters free
TEST(GraftVolume_Test, WhiteoutHidesBaseFile)
{
    GraftCase g;
    g.baseFiles.File("keep.txt", "keep");
    g.baseFiles.File("old.txt", std::string(3000, 'x'));
    g.baseFiles.File("OLDDIR/inner.txt", std::string(3000, 'y'));
    g.MakeBase();
    FatVolumeReader before;
    ASSERT_TRUE(before.Open(*g.base.disk));
    FatDirEntryInfo oldFile;
    FatDirEntryInfo oldDir;
    FatDirEntryInfo inner;
    ASSERT_TRUE(before.Stat("/old.txt", oldFile));
    ASSERT_TRUE(before.Stat("/OLDDIR", oldDir));
    ASSERT_TRUE(before.Stat("/OLDDIR/inner.txt", inner));
    std::vector<bool> baseFree;
    ASSERT_TRUE(before.ScanFree(baseFree));
    std::vector<uint32_t> a, b, c;
    ASSERT_TRUE(before.ChainClusters(oldFile.firstCluster, a));
    ASSERT_TRUE(before.ChainClusters(oldDir.firstCluster, b));
    ASSERT_TRUE(before.ChainClusters(inner.firstCluster, c));
    const uint64_t released = a.size() + b.size() + c.size();
    g.root.File("up/note.txt", "n");

    std::unique_ptr<IBlockDevice> volume;
    GraftVolume* graft = nullptr;
    ASSERT_NE(graft = g.Graft("version: 1\nlayers:\n  - {name: base, source: {image: base.img}}\n"
                      "  - {name: up, source: {folder: up}, whiteout: [/old.txt, /OLDDIR]}\n",
                      volume),
              nullptr);
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(Names(reader, "/"), (std::vector<std::string>{"keep.txt", "note.txt"}));
    // Released clusters are free again (note.txt may take one of them: best fit)
    const uint64_t baseFreeCount = static_cast<uint64_t>(std::count(baseFree.begin(), baseFree.end(), true));
    EXPECT_EQ(graft->FreeClusters(), baseFreeCount + released - 1) << "note.txt takes one cluster";
    std::vector<bool> nowFree;
    ASSERT_TRUE(reader.ScanFree(nowFree));
    EXPECT_EQ(static_cast<uint64_t>(std::count(nowFree.begin(), nowFree.end(), true)), graft->FreeClusters());
}

/// A base with little room: replacing its big file frees the clusters a new big file needs
TEST(GraftVolume_Test, FreedClustersReused)
{
    GraftCase g;
    g.baseFiles.File("big.bin", std::string(512 * 1024, 'b'));
    g.MakeBase(FatType::Fat16, true, 0);
    FatVolumeReader probe;
    ASSERT_TRUE(probe.Open(*g.base.disk));
    std::vector<bool> free;
    ASSERT_TRUE(probe.ScanFree(free));
    const uint64_t freeBytes =
        static_cast<uint64_t>(std::count(free.begin(), free.end(), true)) * probe.SectorsPerCluster() * 512;
    const std::string fresh(static_cast<size_t>(freeBytes + 256 * 1024), 'f');  // more than the base has free
    g.root.File("up/fresh.bin", fresh);

    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    MediaResult result = g.Build("version: 1\ntarget: {build: graft}\nlayers:\n  - {name: base, source: {image: base.img}}\n"
                                 "  - {name: up, source: {folder: up}}\n",
                                 volume, info);
    EXPECT_EQ(result.error, MediaError::DoesNotFit) << "big.bin still takes its room";
    EXPECT_NE(result.message.find("KiB short"), std::string::npos) << result.message;

    g.root.File("up/big.bin", "small now");
    result = g.Build("version: 1\ntarget: {build: graft}\nlayers:\n  - {name: base, source: {image: base.img}}\n"
                     "  - {name: up, source: {folder: up}}\n",
                     volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(Read(reader, "/fresh.bin"), fresh);
    EXPECT_EQ(Read(reader, "/big.bin"), "small now");
}

/// Many files into a one-cluster directory: its chain grows
TEST(GraftVolume_Test, DirectoryGrowsNewCluster)
{
    GraftCase g;
    g.baseFiles.File("D/first.txt", "1");
    g.MakeBase();
    FatVolumeReader before;
    ASSERT_TRUE(before.Open(*g.base.disk));
    FatDirEntryInfo dir;
    ASSERT_TRUE(before.Stat("/D", dir));
    std::vector<uint32_t> chain;
    ASSERT_TRUE(before.ChainClusters(dir.firstCluster, chain));
    const uint32_t perCluster = before.SectorsPerCluster() * 512 / 32;
    for (uint32_t i = 0; i < perCluster; i++)
        g.root.File("up/D/f" + std::to_string(i) + ".txt", "x");

    std::unique_ptr<IBlockDevice> volume;
    std::vector<std::string> report;
    ASSERT_NE(g.Graft(kBaseAndUpper, volume, &report), nullptr);
    EXPECT_TRUE(Has(report, "/D: directory grown")) << (report.empty() ? "" : report.front());
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    std::vector<uint32_t> grown;
    ASSERT_TRUE(reader.ChainClusters(dir.firstCluster, grown));
    EXPECT_GT(grown.size(), chain.size());
    EXPECT_EQ(Names(reader, "/D").size(), perCluster + 1);
    EXPECT_EQ(Read(reader, "/D/first.txt"), "1");
    EXPECT_EQ(Read(reader, "/D/f" + std::to_string(perCluster - 1) + ".txt"), "x");
}

/// S-10: a FAT16 root with no slot left: graft fails DoesNotFit, auto rebuilds and says why
TEST(GraftVolume_Test, Fat16RootFullFallsBackToRebuild)
{
    GraftCase g;
    for (int i = 0; i < 505; i++)
        g.baseFiles.File("F" + std::to_string(i) + ".TXT", "");
    g.MakeBase();
    FatVolumeReader probe;
    ASSERT_TRUE(probe.Open(*g.base.disk));
    ASSERT_EQ(probe.RootEntries(), 512u);
    for (int i = 0; i < 10; i++)
        g.root.File("up/N" + std::to_string(i) + ".TXT", "n");

    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    MediaResult result = g.Build("version: 1\ntarget: {build: graft}\nlayers:\n  - {name: base, source: {image: base.img}}\n"
                                 "  - {name: up, source: {folder: up}}\n",
                                 volume, info);
    EXPECT_EQ(result.error, MediaError::DoesNotFit);
    EXPECT_NE(result.message.find("root directory"), std::string::npos) << result.message;

    result = g.Build(kBaseAndUpper, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    EXPECT_EQ(info.build, "rebuild");
    EXPECT_TRUE(Has(result.report, "rebuild instead of a graft: the root directory needs"));
}

/// FAT32: the FSInfo free count and next-free hint follow the graft
TEST(GraftVolume_Test, Fat32FsInfoUpdated)
{
    GraftCase g;
    g.baseFiles.File("a.txt", "a");
    g.MakeBase(FatType::Fat32, false);
    g.root.File("up/b.bin", std::string(20000, 'b'));

    std::unique_ptr<IBlockDevice> volume;
    GraftVolume* graft = g.Graft(kBaseAndUpper, volume);
    ASSERT_NE(graft, nullptr);
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    ASSERT_NE(reader.FsInfoSector(), 0u);
    uint8_t s[512];
    ASSERT_TRUE(volume->ReadSector(reader.FsInfoSector(), s));
    const uint32_t freeCount = s[488] | (s[489] << 8) | (s[490] << 16) | (static_cast<uint32_t>(s[491]) << 24);
    EXPECT_EQ(freeCount, graft->FreeClusters());
    std::vector<bool> free;
    ASSERT_TRUE(reader.ScanFree(free));
    EXPECT_EQ(freeCount, static_cast<uint32_t>(std::count(free.begin(), free.end(), true)));
    EXPECT_EQ(Read(reader, "/b.bin"), std::string(20000, 'b'));
}

/// One file added to one of twenty directories re-encodes that directory only
TEST(GraftVolume_Test, BuildCostIndependentOfBaseFiles)
{
    GraftCase g;
    for (int d = 0; d < 20; d++)
    {
        for (int f = 0; f < 5; f++)
            g.baseFiles.File("D" + std::to_string(d) + "/f" + std::to_string(f) + ".txt", "x");
    }
    g.MakeBase();
    g.root.File("up/D7/new.txt", "new");

    std::unique_ptr<IBlockDevice> volume;
    GraftVolume* graft = g.Graft(kBaseAndUpper, volume);
    ASSERT_NE(graft, nullptr);
    EXPECT_EQ(graft->DirectoriesEncoded(), 1u);
    EXPECT_EQ(graft->FilesGrafted(), 1u);
}

/// The DSS boot floppy as the base: boot sector, the loader in LBA 1-9 and the single FAT copy stay
TEST(GraftVolume_Test, BootSectorAndReservedPreserved)
{
    const std::filesystem::path floppy = TestPathHelper::FindProjectRoot() / "testdata/machines/sprinter/dss_1_62_92.img";
    if (!std::filesystem::exists(floppy))
        GTEST_SKIP() << "no DSS floppy";
    GraftCase g;
    std::filesystem::copy_file(floppy, g.root.Path() / "base.img");
    g.root.File("up/UTIL/hello.txt", "hello from the host");

    std::unique_ptr<IBlockDevice> volume;
    ASSERT_NE(g.Graft(kBaseAndUpper, volume), nullptr);
    auto base = HddImageFormats::OpenBlock((g.root.Path() / "base.img").string(), "raw", RawImage::Access::ReadOnly);
    ASSERT_NE(base, nullptr);
    uint8_t ours[512];
    uint8_t theirs[512];
    for (uint64_t lba = 0; lba < 10; lba++)
    {
        ASSERT_TRUE(volume->ReadSector(lba, ours));
        ASSERT_TRUE(base->ReadSector(lba, theirs));
        EXPECT_EQ(std::memcmp(ours, theirs, 512), 0) << "LBA " << lba;
    }
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(reader.Type(), FatReaderType::Fat12);
    EXPECT_EQ(reader.FatCount(), 1u);
    EXPECT_EQ(reader.ReservedSectors(), 10u);
    EXPECT_EQ(Read(reader, "/UTIL/hello.txt"), "hello from the host");
    std::vector<uint8_t> system;
    EXPECT_TRUE(reader.ReadFile("/SYSTEM.DOS", system));
    EXPECT_GT(system.size(), 0u);
}

/// A touched directory keeps its base entries byte for byte and in order, the new ones after them
TEST(GraftVolume_Test, KeptEntriesByteIdentical)
{
    GraftCase g;
    g.baseFiles.File("Mixed Case Name.txt", "1");
    g.baseFiles.File("PLAIN.TXT", "2");
    g.baseFiles.File(".hidden", "3");
    g.MakeBase();
    g.root.File("up/added.txt", "4");

    std::unique_ptr<IBlockDevice> volume;
    ASSERT_NE(g.Graft(kBaseAndUpper, volume), nullptr);
    FatVolumeReader before;
    FatVolumeReader after;
    ASSERT_TRUE(before.Open(*g.base.disk));
    ASSERT_TRUE(after.Open(*volume));
    std::vector<FatRawEntry> was;
    std::vector<FatRawEntry> now;
    std::vector<FatDirEntryInfo> wasInfo;
    std::vector<FatDirEntryInfo> nowInfo;
    ASSERT_TRUE(before.ReadRawDirectory(0, was, wasInfo));
    ASSERT_TRUE(after.ReadRawDirectory(0, now, nowInfo));
    ASSERT_EQ(now.size(), was.size() + 1);
    for (size_t i = 0; i < was.size(); i++)
        EXPECT_EQ(now[i].slots, was[i].slots) << "entry " << i;
    EXPECT_EQ(nowInfo.back().name, "added.txt");
}

/// partition: 1 of an MBR image: the MBR untouched, the volume grafted
TEST(GraftVolume_Test, ExplicitPartition)
{
    GraftCase g;
    g.baseFiles.File("in.txt", "in");
    g.MakeBase(FatType::Fat16, true);
    g.root.File("up/added.txt", "added");

    std::unique_ptr<IBlockDevice> volume;
    ASSERT_NE(g.Graft("version: 1\nlayers:\n  - {name: base, source: {image: base.img, partition: 1}}\n"
                      "  - {name: up, source: {folder: up}}\n",
                      volume),
              nullptr);
    uint8_t ours[512];
    uint8_t theirs[512];
    ASSERT_TRUE(volume->ReadSector(0, ours));
    g.base.disk->ReadSector(0, theirs);
    EXPECT_EQ(std::memcmp(ours, theirs, 512), 0);
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(Read(reader, "/added.txt"), "added");
    EXPECT_EQ(Read(reader, "/in.txt"), "in");
}

/// DT-4 leaves: auto grafts a fitting base; a type the slot or target does not take rebuilds (auto) or
/// fails (graft); graft needs an image at the bottom
TEST(ComposeGraft_Test, DecisionTreeLeaves)
{
    GraftCase g;
    g.baseFiles.File("a.txt", "a");
    g.MakeBase(FatType::Fat16);
    g.root.File("up/b.txt", "b");
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;

    MediaResult r = g.Build(kBaseAndUpper, volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(info.build, "graft");
    EXPECT_EQ(info.fsName, "fat16");
    EXPECT_TRUE(Has(r.report, "graft onto"));

    r = g.Build("version: 1\ntarget: {fs: fat32, free: 1MiB}\nlayers:\n  - {name: base, source: {image: base.img}}\n"
                "  - {name: up, source: {folder: up}}\n",
                volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(info.build, "rebuild");
    EXPECT_EQ(info.fsName, "fat32");
    EXPECT_TRUE(Has(r.report, "rebuild instead of a graft: the base is FAT16, the target asks for fat32"));

    CompositeBuildOptions sprinter;
    sprinter.allowedFs = {FatType::Fat32};
    r = g.Build("version: 1\ntarget: {build: graft}\nlayers:\n  - {name: base, source: {image: base.img}}\n", volume, info, sprinter);
    EXPECT_EQ(r.error, MediaError::BadRequest);
    EXPECT_NE(r.message.find("the slot does not read it"), std::string::npos) << r.message;

    r = g.Build("version: 1\ntarget: {build: graft}\nlayers:\n  - {name: up, source: {folder: up}}\n", volume, info);
    EXPECT_EQ(r.error, MediaError::BadRequest);
    EXPECT_NE(r.message.find("needs a FAT image as the bottom layer"), std::string::npos) << r.message;

    r = g.Build("version: 1\ntarget: {free: 1MiB, label: X}\nlayers:\n  - {name: base, source: {image: base.img}}\n", volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(info.build, "graft");
    EXPECT_TRUE(Has(r.report, "target.free, label: ignored"));

    r = g.Build("version: 1\ntarget: {build: rebuild}\nlayers:\n  - {name: base, source: {image: base.img}}\n", volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(info.build, "rebuild");
}
