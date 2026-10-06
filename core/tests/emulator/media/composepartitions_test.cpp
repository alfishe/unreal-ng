// Partitioned composites (multi-source phases/c7-partitions.md): the descriptor's partitions, passthrough and composed
// partitions on one disk (fs-compatibility S-5), a graft over an image partition, and changes / delta per partition.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatguest.h"
#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/partitioneddisk.h"
#include "emulator/io/storage/subrangedevice.h"
#include "emulator/media/compositemediumfactory.h"
#include "emulator/media/mediachanges.h"
#include "emulator/media/mediamanager.h"

namespace
{
    std::string Read(FatVolumeReader& reader, const std::string& path)
    {
        std::vector<uint8_t> data;
        EXPECT_TRUE(reader.ReadFile(path, data)) << path;
        return std::string(data.begin(), data.end());
    }

    /// Partition `n` (1-based, primary) of `disk` as its own volume
    std::shared_ptr<IBlockDevice> PartitionOf(std::shared_ptr<IBlockDevice> disk, uint32_t n)
    {
        FatPartition p;
        if (!FatVolumeReader::FindPartition(*disk, n, p))
            return nullptr;
        return std::make_shared<SubRangeDevice>(disk, p.first, p.count);
    }

    struct Disk
    {
        ScratchFolder root{"compose-partitions"};

        /// An MBR image with one FAT16 partition holding DOS.SYS and README.TXT
        void MakeImage()
        {
            ScratchFolder files("compose-partitions-img");
            files.File("DOS.SYS", std::string(2000, 'd'));
            files.File("README.TXT", "from the image");
            const FatSourceImage image = FolderToFatDisk(files.Path(), FatType::Fat16, CodePage::Cp866, /*mbr*/ true);
            ASSERT_TRUE(image.ok()) << image.error;
            ASSERT_TRUE(SaveSparse(image, root.Path() / "dos.img"));
        }

        ComposeDescriptor Descriptor(const std::string& yaml)
        {
            root.File("disk.ucompose.yaml", yaml);
            return ComposeDescriptor::Load(root.Path() / "disk.ucompose.yaml");
        }
    };
}  // namespace

TEST(ComposePartitions_Test, DescriptorParsesAndNormalizes)
{
    Disk d;
    ComposeDescriptor ok = d.Descriptor("version: 1\ntarget: {fixedTime: 1767268800}\npartitions:\n"
                                        "  - {name: dos, source: {image: dos.img, partition: 1}}\n"
                                        "  - {fs: fat32, size: 300MiB, label: DATA, type: 0x0C, compose: {build: rebuild, free: 1MiB,\n"
                                        "     layers: [{source: {folder: data}}]}}\n");
    ASSERT_TRUE(ok.Ok()) << ok.error;
    ASSERT_EQ(ok.partitions.size(), 2u);
    EXPECT_EQ(ok.partitions[0].name, "dos");
    ASSERT_TRUE(ok.partitions[0].source.has_value());
    EXPECT_EQ(ok.partitions[0].source->partition.value_or(0), 1u);
    EXPECT_EQ(ok.partitions[1].name, "p2");
    ASSERT_NE(ok.partitions[1].compose, nullptr);
    const ComposeTarget& t = ok.partitions[1].compose->target;
    EXPECT_EQ(t.fs, ComposeTarget::Fs::Fat32);
    EXPECT_EQ(t.size.value_or(0), 300ull * 1024 * 1024);
    EXPECT_EQ(t.label.value_or(""), "DATA");
    EXPECT_EQ(t.build, ComposeTarget::Build::Rebuild);
    EXPECT_EQ(t.free.value_or(0), 1024u * 1024);
    EXPECT_EQ(t.mbr.value_or(true), false) << "a composed partition has no MBR of its own";
    EXPECT_EQ(ok.partitions[1].type.value_or(0), 0x0C);
    EXPECT_NE(ok.Normalized().find("\"partitions\":[{\"name\":\"dos\""), std::string::npos) << ok.Normalized();

    EXPECT_FALSE(d.Descriptor("version: 1\npartitions: [{name: x}]\n").Ok()) << "neither source nor compose";
    EXPECT_FALSE(d.Descriptor("version: 1\npartitions: [{source: {image: a.img}, compose: {layers: [{source: {folder: x}}]}}]\n").Ok())
        << "both";
    EXPECT_FALSE(d.Descriptor("version: 1\npartitions: [{source: {folder: x}}]\n").Ok()) << "a passthrough is an image";
    EXPECT_FALSE(d.Descriptor("version: 1\nlayers: [{source: {folder: x}}]\npartitions: [{source: {image: a.img}}]\n").Ok());
    EXPECT_FALSE(d.Descriptor("version: 1\npartitions: [{compose: {layers: []}}]\n").Ok()) << "a composition needs layers";
}

// S-5: a FAT16 partition passed through from an image and a composed FAT32 partition on one disk
TEST(ComposePartitions_Test, Fat16AndFat32OnOneDisk)
{
    Disk d;
    d.MakeImage();
    d.root.File("data/GAMES/ELITE.TRD", std::string(5000, 'e'));
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = CompositeMediumFactory::Build(
        d.Descriptor("version: 1\ntarget: {fixedTime: 1767268800}\npartitions:\n"
                     "  - {name: dos, source: {image: dos.img, partition: 1}}\n"
                     "  - {name: data, fs: fat32, compose: {layers: [{source: {folder: data}}]}}\n"),
        {}, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    EXPECT_EQ(info.build, "partitions");
    ASSERT_EQ(info.partitions.size(), 2u);
    EXPECT_EQ(info.partitions[0].kind, "image");
    EXPECT_EQ(info.partitions[0].fs, "fat16");
    EXPECT_EQ(info.partitions[1].fs, "fat32");
    EXPECT_TRUE(info.partitions[1].type == 0x0B || info.partitions[1].type == 0x0C);
    ASSERT_EQ(info.layers.size(), 2u);
    EXPECT_EQ(info.layers[0].name, "dos");
    EXPECT_EQ(info.layers[1].name, "data/layer0");

    std::shared_ptr<IBlockDevice> disk(std::move(volume));
    auto first = PartitionOf(disk, 1);
    auto second = PartitionOf(disk, 2);
    ASSERT_TRUE(first && second);
    FatVolumeReader one, two;
    ASSERT_TRUE(one.Open(*first));
    EXPECT_EQ(one.Type(), FatReaderType::Fat16);
    EXPECT_EQ(Read(one, "/README.TXT"), "from the image");
    ASSERT_TRUE(two.Open(*second));
    EXPECT_EQ(two.Type(), FatReaderType::Fat32);
    EXPECT_EQ(Read(two, "/GAMES/ELITE.TRD"), std::string(5000, 'e'));

    // The same descriptor, the same disk; a target size below the layout does not fit
    std::unique_ptr<IBlockDevice> again;
    CompositeInfo againInfo;
    ASSERT_TRUE(CompositeMediumFactory::Build(d.Descriptor("version: 1\ntarget: {fixedTime: 1767268800}\npartitions:\n"
                                                           "  - {name: dos, source: {image: dos.img, partition: 1}}\n"
                                                           "  - {name: data, fs: fat32, compose: {layers: [{source: {folder: data}}]}}\n"),
                                              {}, again, againInfo)
                    .Ok());
    EXPECT_EQ(againInfo.contentId, info.contentId);
    EXPECT_EQ(CompositeMediumFactory::Build(d.Descriptor("version: 1\ntarget: {size: 1MiB}\npartitions:\n"
                                                         "  - {source: {image: dos.img, partition: 1}}\n"),
                                            {}, again, againInfo)
                  .error,
              MediaError::DoesNotFit);
}

// A graft over an image partition, placed second: its volume is cut out of the image's coordinates, and its boot
// sector names its new start
TEST(ComposePartitions_Test, GraftOverAnImagePartition)
{
    Disk d;
    d.MakeImage();
    d.root.File("first/A.TXT", "first partition");
    d.root.File("patch/PATCH.TXT", "grafted");
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = CompositeMediumFactory::Build(
        d.Descriptor("version: 1\ntarget: {fixedTime: 1767268800}\npartitions:\n"
                     "  - {name: first, fs: fat16, compose: {layers: [{source: {folder: first}}]}}\n"
                     "  - {name: dos, compose: {build: graft, layers: [{source: {image: dos.img, partition: 1}}, "
                     "{source: {folder: patch}}]}}\n"),
        {}, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    ASSERT_EQ(info.partitions.size(), 2u);
    EXPECT_EQ(info.partitions[1].build, "graft");
    std::shared_ptr<IBlockDevice> disk(std::move(volume));
    auto dos = PartitionOf(disk, 2);
    ASSERT_NE(dos, nullptr);
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*dos));
    EXPECT_EQ(Read(reader, "/README.TXT"), "from the image");
    EXPECT_EQ(Read(reader, "/PATCH.TXT"), "grafted");
    uint8_t boot[512];
    ASSERT_TRUE(dos->ReadSector(0, boot));
    const uint32_t hidden = boot[28] | (boot[29] << 8) | (boot[30] << 16) | (static_cast<uint32_t>(boot[31]) << 24);
    EXPECT_EQ(hidden, info.partitions[1].start);
}

// The guest writes in both partitions: `media changes` names each with its partition and layer; a session delta
// restores both
TEST(ComposePartitions_Test, ChangesAndDeltaPerPartition)
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
    Disk d;
    d.MakeImage();
    d.root.File("data/DATA.TXT", "data");
    d.root.File("disk.ucompose.yaml", "version: 1\ntarget: {fixedTime: 1767268800}\npartitions:\n"
                                      "  - {name: dos, source: {image: dos.img, partition: 1}}\n"
                                      "  - {name: data, fs: fat16, compose: {free: 1MiB, layers: [{name: files, source: {folder: data}}]}}\n");
    MediaManager manager(nullptr);
    Slot slot;
    manager.RegisterSlot(slot);
    MediaSource source;
    source.path = FileHelper::FromFsPath(d.root.Path() / "disk.ucompose.yaml");
    ASSERT_TRUE(manager.Insert("ide0.master", source, {}).Ok());
    std::shared_ptr<IBlockDevice> stack(slot.attached->Block(), [](IBlockDevice*) {});
    {
        auto first = PartitionOf(stack, 1);
        auto second = PartitionOf(stack, 2);
        ASSERT_TRUE(first && second);
        FatGuest one(*first);
        ASSERT_TRUE(one.Create("/NEW.TXT", std::vector<uint8_t>(100, 'n')));
        FatGuest two(*second);
        ASSERT_TRUE(two.Poke("/DATA.TXT", 0, 'D'));
    }
    manager.ApplyPending();

    MediumChanges changes;
    ASSERT_TRUE(ListMediumChanges(*slot.attached, changes).Ok());
    ASSERT_EQ(changes.changes.size(), 2u);
    EXPECT_EQ(changes.changes[0].op, "create");
    EXPECT_EQ(changes.changes[0].path, "dos:/NEW.TXT");
    EXPECT_EQ(changes.changes[1].op, "modify");
    EXPECT_EQ(changes.changes[1].path, "data:/DATA.TXT");
    EXPECT_EQ(changes.changes[1].layer, "data/files");
    EXPECT_TRUE(changes.warnings.empty()) << changes.warnings.front();

    ASSERT_TRUE(manager.Save("ide0.master", {}).Ok());
    EjectOptions plain;
    ASSERT_TRUE(manager.Eject("ide0.master", plain).Ok());
    const MediaResult again = manager.Insert("ide0.master", source, {});
    ASSERT_TRUE(again.Ok()) << again.message;
    EXPECT_TRUE(std::any_of(again.report.begin(), again.report.end(),
                            [](const std::string& l) { return l.find("session restored") != std::string::npos; }));
    std::shared_ptr<IBlockDevice> restored(slot.attached->Block(), [](IBlockDevice*) {});
    auto restoredFirst = PartitionOf(restored, 1);
    ASSERT_NE(restoredFirst, nullptr);
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*restoredFirst));
    FatDirEntryInfo entry;
    EXPECT_TRUE(reader.Stat("/NEW.TXT", entry));

    BlockWriteOptions compact;
    compact.compact = true;
    EXPECT_EQ(manager.Export("ide0.master", FileHelper::FromFsPath(d.root.Path() / "c.img"), compact).error, MediaError::NotSupported)
        << "compact makes one FAT volume";
    EjectOptions discard;
    discard.disposition = Disposition::Discard;
    manager.Eject("ide0.master", discard);
    manager.UnregisterSlot("ide0.master");
}
