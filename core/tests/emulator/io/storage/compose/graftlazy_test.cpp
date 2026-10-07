// C4b: a graft reads only the base directories its upper layers reach (multi-source
// phases/c4b-lazy-graft-base.md). Every case is built twice, lazily and with the base read in full
// (CompositeBuildOptions::lazyBase = false), and the two must agree sector for sector.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatguest.h"
#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/compose/graftvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/media/compositemediumfactory.h"
#include "emulator/media/mediachanges.h"
#include "emulator/media/mediamanager.h"

namespace
{
    /// A base of `directories` directories D0.. with two files each (D1 also has SUB/DEEP.TXT), an upper folder
    struct LazyCase
    {
        ScratchFolder root{"graft-lazy"};
        ScratchFolder baseFiles{"graft-lazy-base"};

        explicit LazyCase(int directories = 40)
        {
            for (int d = 0; d < directories; d++)
            {
                const std::string dir = "D" + std::to_string(d) + "/";
                baseFiles.File(dir + "F0.TXT", "file 0 of " + dir);
                baseFiles.File(dir + "F1.TXT", std::string(700, static_cast<char>('a' + d % 26)));
            }
            baseFiles.File("D1/SUB/DEEP.TXT", "deep");
            baseFiles.File("README.TXT", "base readme");
            const FatSourceImage base = FolderToFatDisk(baseFiles.Path(), FatType::Fat16, CodePage::Cp866, true, 256 * 1024);
            EXPECT_TRUE(base.ok()) << base.error;
            EXPECT_TRUE(SaveSparse(base, root.Path() / "base.img"));
            std::filesystem::create_directories(root.Path() / "up");
        }

        MediaResult Build(const std::string& yaml, bool lazy, std::unique_ptr<IBlockDevice>& volume, CompositeInfo& info)
        {
            root.File("disk.ucompose.yaml", yaml);
            CompositeBuildOptions options;
            options.lazyBase = lazy;
            return CompositeMediumFactory::Build(ComposeDescriptor::Load(root.Path() / "disk.ucompose.yaml"), options, volume, info);
        }
    };

    const char* kBaseAndUpper = "version: 1\ntarget: {fixedTime: 1767268800}\n"
                                "layers:\n  - {name: base, source: {image: base.img}}\n  - {name: up, source: {folder: up}}\n";

    size_t UnexpandedReachable(const FileTree& tree, uint32_t dir = FileTree::kRoot)
    {
        size_t n = 0;
        for (uint32_t child : tree.Node(dir).children)
        {
            const TreeNode& node = tree.Node(child);
            if (node.unexpanded)
                n++;
            else if (node.isDirectory)
                n += UnexpandedReachable(tree, child);
        }
        return n;
    }

    /// Lazy and full builds of `yaml` give the same sectors, counts and provenance
    void ExpectSameAsFull(LazyCase& c, const std::string& yaml, size_t* unlisted = nullptr)
    {
        std::unique_ptr<IBlockDevice> lazy, full;
        CompositeInfo lazyInfo, fullInfo;
        const MediaResult a = c.Build(yaml, true, lazy, lazyInfo);
        const MediaResult b = c.Build(yaml, false, full, fullInfo);
        ASSERT_TRUE(a.Ok()) << a.message;
        ASSERT_TRUE(b.Ok()) << b.message;
        EXPECT_FALSE(fullInfo.countLater);
        lazyInfo.CompleteCounts();
        EXPECT_FALSE(lazyInfo.countLater) << "counted once";
        EXPECT_EQ(lazyInfo.build, fullInfo.build);
        EXPECT_EQ(lazyInfo.files, fullInfo.files);
        EXPECT_EQ(lazyInfo.bytes, fullInfo.bytes);
        ASSERT_EQ(lazyInfo.layers.size(), fullInfo.layers.size());
        for (size_t i = 0; i < lazyInfo.layers.size(); i++)
        {
            EXPECT_EQ(lazyInfo.layers[i].files, fullInfo.layers[i].files) << "layer " << i;
            EXPECT_EQ(lazyInfo.layers[i].bytes, fullInfo.layers[i].bytes) << "layer " << i;
        }
        EXPECT_EQ(lazy->ContentId(), fullInfo.contentId);
        ASSERT_EQ(lazy->SectorCount(), full->SectorCount());

        const auto* lazyLayout = dynamic_cast<const IComposedLayout*>(lazy.get());
        const auto* fullLayout = dynamic_cast<const IComposedLayout*>(full.get());
        ASSERT_TRUE(lazyLayout && fullLayout);
        uint8_t x[512], y[512];
        size_t differ = 0, owners = 0, unlistedOwners = 0;
        for (uint64_t lba = 0; lba < lazy->SectorCount(); lba++)
        {
            ASSERT_TRUE(lazy->ReadSector(lba, x));
            ASSERT_TRUE(full->ReadSector(lba, y));
            differ += std::equal(x, x + 512, y) ? 0 : 1;
            const SectorOwner p = lazyLayout->OwnerOf(lba);
            const SectorOwner q = fullLayout->OwnerOf(lba);
            unlistedOwners += p.unlisted ? 1 : 0;
            const bool same = p.role == q.role && p.Known() == q.Known() &&
                              (!q.Known() || (p.layer == q.layer && p.dirCluster == q.dirCluster && p.offset == q.offset));
            if (!same && owners++ < 3)
                ADD_FAILURE() << "LBA " << lba << ": role " << static_cast<int>(p.role) << "/" << static_cast<int>(q.role) << " dir "
                              << p.dirCluster << "/" << q.dirCluster << " offset " << p.offset << "/" << q.offset;
        }
        EXPECT_EQ(differ, 0u) << "sectors differ between the lazy and the full build";
        if (unlisted)
            *unlisted = unlistedOwners;
    }
}  // namespace

TEST(GraftLazy_Test, OnlyTouchedDirectoriesRead)
{
    LazyCase c;
    c.root.File("up/D1/NEW.TXT", "new in D1");
    c.root.File("up/D7/NEW.TXT", "new in D7");
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = c.Build(kBaseAndUpper, true, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    ASSERT_EQ(info.build, "graft");
    const auto* graft = dynamic_cast<const GraftVolume*>(volume.get());
    ASSERT_NE(graft, nullptr);

    // D1 and D7 read (D1's SUB not), the other 38 directories never
    const FileTree& tree = graft->Tree();
    EXPECT_EQ(UnexpandedReachable(tree), 39u);
    const uint32_t d1 = tree.Find("/D1");
    ASSERT_NE(d1, FileTree::kNone);
    EXPECT_FALSE(tree.Node(d1).unexpanded);
    EXPECT_TRUE(tree.Node(tree.Find("/D1/SUB")).unexpanded);
    EXPECT_TRUE(tree.Node(tree.Find("/D5")).unexpanded);
    EXPECT_LT(tree.NodeCount(), 60u) << "the root, 40 directories, two expanded ones and the new files";
    EXPECT_EQ(graft->DirectoriesEncoded(), 2u);

    // The counts of the 38 unread directories (and D1/SUB) wait for somebody to ask
    ASSERT_TRUE(info.countLater);
    EXPECT_LT(info.layers[0].files, 40u * 2 + 2);
    info.CompleteCounts();
    EXPECT_EQ(info.layers[0].files, 40u * 2 + 2);
    EXPECT_EQ(info.layers[1].files, 2u);
    EXPECT_EQ(info.files, 40u * 2 + 2 + 2);

    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    std::vector<uint8_t> data;
    ASSERT_TRUE(reader.ReadFile("/D1/NEW.TXT", data));
    EXPECT_EQ(std::string(data.begin(), data.end()), "new in D1");
    ASSERT_TRUE(reader.ReadFile("/D1/SUB/DEEP.TXT", data));
    EXPECT_EQ(std::string(data.begin(), data.end()), "deep");
    ASSERT_TRUE(reader.ReadFile("/D5/F0.TXT", data));
    EXPECT_EQ(std::string(data.begin(), data.end()), "file 0 of D5/");
}

TEST(GraftLazy_Test, SameSectorsAsAFullEnumeration)
{
    LazyCase c;
    c.root.File("up/D1/SUB/MORE.TXT", "into an unexpanded grandchild");
    c.root.File("up/D2/F0.TXT", "replaces a base file");
    c.root.File("up/NEWDIR/A.TXT", "a new directory");
    c.root.File("up/d3/lower.txt", "by the FAT key into D3");
    const std::string yaml = "version: 1\ntarget: {fixedTime: 1767268800}\n"
                             "layers:\n  - {name: base, source: {image: base.img}}\n"
                             "  - {name: up, source: {folder: up}, whiteout: [/D4/F1.TXT, /D6]}\n";
    size_t unlisted = 0;
    ExpectSameAsFull(c, yaml, &unlisted);
    EXPECT_GT(unlisted, 0u) << "untouched directories are attributed from the image";
}

TEST(GraftLazy_Test, MountedLayerAndOpaqueDirectory)
{
    LazyCase c;
    c.root.File("up/X.TXT", "mounted into D9/IN");
    c.root.File("op/ONLY.TXT", "the only entry left in D8");
    const std::string yaml = "version: 1\ntarget: {fixedTime: 1767268800}\n"
                             "layers:\n  - {name: base, source: {image: base.img}}\n"
                             "  - {name: up, source: {folder: up}, mount: /D9/IN}\n"
                             "  - {name: op, source: {folder: op}, mount: /D8, opaque: [/D8]}\n";
    ExpectSameAsFull(c, yaml);
}

TEST(GraftLazy_Test, FallbackRebuildSeesEverything)
{
    LazyCase c;
    for (int i = 0; i < 160; i++)  // three entries per long name: the base's FAT16 root (512 entries) cannot take them
        c.root.File("up/upper file number " + std::to_string(i) + ".txt", "n");
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = c.Build(kBaseAndUpper, true, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    ASSERT_EQ(info.build, "rebuild");
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    std::vector<uint8_t> data;
    for (const char* path : {"/D0/F0.TXT", "/D17/F1.TXT", "/D39/F0.TXT", "/D1/SUB/DEEP.TXT", "/README.TXT", "/upper file number 159.txt"})
        EXPECT_TRUE(reader.ReadFile(path, data)) << path;
    EXPECT_EQ(info.files, 40u * 2 + 2 + 160);
    EXPECT_EQ(info.layers[0].files, 40u * 2 + 2);
}

TEST(GraftLazy_Test, PartitionCountsCompleteOnTheDisk)
{
    LazyCase c;
    c.root.File("up/D3/NEW.TXT", "new");
    const std::string yaml = "version: 1\ntarget: {fixedTime: 1767268800}\npartitions:\n"
                             "  - {name: a, compose: {layers: [{name: base, source: {image: base.img, partition: 1}},"
                             " {name: up, source: {folder: up}}]}}\n";
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = c.Build(yaml, true, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    ASSERT_EQ(info.partitions.size(), 1u);
    EXPECT_EQ(info.partitions[0].build, "graft");
    ASSERT_TRUE(info.countLater);
    info.CompleteCounts();
    ASSERT_EQ(info.layers.size(), 2u);
    EXPECT_EQ(info.layers[0].name, "a/base");
    EXPECT_EQ(info.layers[0].files, 40u * 2 + 2);
    EXPECT_EQ(info.files, 40u * 2 + 2 + 1);
}

TEST(GraftLazy_Test, GuestChangeInAnUntouchedDirectoryIsTheBaseLayers)
{
    class Slot : public IMediaSlot
    {
    public:
        Slot()
        {
            _d.id = "ide0.master";
            _d.kind = MediaKind::Block;
            _d.label = "disk";
            _d.tags = {"ide", "hdd"};
        }
        const SlotDescriptor& Descriptor() const override { return _d; }
        void Attach(Medium& m) override { attached = &m; }
        void Detach() override { attached = nullptr; }
        void SourceChanged(Medium&) override {}
        Medium* attached = nullptr;

    private:
        SlotDescriptor _d;
    };
    LazyCase c;
    c.root.File("up/D1/NEW.TXT", "new");
    c.root.File("disk.ucompose.yaml", kBaseAndUpper);
    MediaManager manager(nullptr);
    Slot slot;
    manager.RegisterSlot(slot);
    MediaSource source;
    source.path = FileHelper::FromFsPath(c.root.Path() / "disk.ucompose.yaml");
    ASSERT_TRUE(manager.Insert("ide0.master", source, {}).Ok());
    {
        FatGuest guest(*slot.attached->Block());
        ASSERT_TRUE(guest.Poke("/D5/F1.TXT", 3, 'Z'));
    }
    manager.ApplyPending();

    MediumChanges changes;
    ASSERT_TRUE(ListMediumChanges(*slot.attached, changes).Ok());
    ASSERT_EQ(changes.changes.size(), 1u);
    EXPECT_EQ(changes.changes[0].op, "modify");
    EXPECT_EQ(changes.changes[0].path, "/D5/F1.TXT");
    EXPECT_EQ(changes.changes[0].layer, "base");

    EjectOptions discard;
    discard.disposition = Disposition::Discard;
    manager.Eject("ide0.master", discard);
    manager.UnregisterSlot("ide0.master");
}
