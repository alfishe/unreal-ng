// S1 flat images (multi-source phases/c6-provenance-flatten.md §2): a composite with guest writes exported to raw,
// fixed VHD and CHD; `compact` re-synthesizing the merged FAT volume (every file contiguous, deleted data gone) on
// export and on save.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "common/filehelper.h"
#include "emulator/io/storage/chd/chdimage.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/blockformats.h"
#include "emulator/media/mediamanager.h"

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::vector<uint8_t> Slurp(const std::filesystem::path& path)
    {
        std::vector<uint8_t> bytes(static_cast<size_t>(std::filesystem::file_size(path)));
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return bytes;
    }

    /// Every file under `path`: its path and bytes
    void Walk(FatVolumeReader& reader, const std::string& path, std::map<std::string, std::vector<uint8_t>>& files)
    {
        std::vector<FatDirEntryInfo> entries;
        ASSERT_TRUE(reader.List(path, entries)) << path;
        for (const FatDirEntryInfo& e : entries)
        {
            const std::string child = (path == "/" ? "" : path) + "/" + e.name;
            if (e.isDirectory)
            {
                files[child + "/"] = {};
                Walk(reader, child, files);
            }
            else
                ASSERT_TRUE(reader.ReadFile(child, files[child])) << child;
        }
    }

    std::map<std::string, std::vector<uint8_t>> Tree(IBlockDevice& device)
    {
        std::map<std::string, std::vector<uint8_t>> files;
        FatVolumeReader reader;
        EXPECT_TRUE(reader.Open(device));
        Walk(reader, "/", files);
        return files;
    }

    /// A FAT16 superfloppy whose two big root files interleave cluster by cluster, with a deleted file whose
    /// data ("ZZZZ...") stays in its clusters: what a guest leaves behind after a while
    FatSourceImage FragmentedFat16(const std::filesystem::path& folder)
    {
        FatSourceImage image = FolderToFatDisk(folder, FatType::Fat16, CodePage::Cp866, false);
        if (!image.ok())
            return image;
        FatVolumeReader reader;
        reader.Open(*image.disk);
        const uint64_t fatStart = reader.ReservedSectors();
        const uint64_t rootStart = fatStart + uint64_t(reader.FatCount()) * reader.FatSectors();
        const uint64_t clusterSectors = reader.SectorsPerCluster();
        auto clusterLba = [&](uint32_t c) { return reader.DataStart() + uint64_t(c - 2) * clusterSectors; };

        FatDirEntryInfo a, b, del;
        reader.Stat("/A.BIN", a);
        reader.Stat("/B.BIN", b);
        reader.Stat("/DEL.BIN", del);
        std::vector<uint32_t> ca, cb, cd;
        reader.ChainClusters(a.firstCluster, ca);
        reader.ChainClusters(b.firstCluster, cb);
        reader.ChainClusters(del.firstCluster, cd);
        if (ca.size() != cb.size() || ca.size() < 3)
        {
            image.error = "A.BIN and B.BIN need the same cluster count (3+)";
            image.disk.reset();
            return image;
        }

        // The new place of each cluster: A takes the even slots of A u B, B the odd ones
        std::vector<uint32_t> slots(ca);
        slots.insert(slots.end(), cb.begin(), cb.end());
        std::sort(slots.begin(), slots.end());
        std::map<uint32_t, uint32_t> moved;
        for (size_t i = 0; i < ca.size(); i++)
        {
            moved[ca[i]] = slots[2 * i];
            moved[cb[i]] = slots[2 * i + 1];
        }
        std::map<uint32_t, std::vector<uint8_t>> data;
        std::vector<uint8_t> sector(512);
        for (const auto& [from, to] : moved)
        {
            for (uint64_t s = 0; s < clusterSectors; s++)
            {
                image.disk->ReadSector(clusterLba(from) + s, sector.data());
                data[to].insert(data[to].end(), sector.begin(), sector.end());
            }
        }
        for (const auto& [to, bytes] : data)
        {
            for (uint64_t s = 0; s < clusterSectors; s++)
                image.disk->WriteSector(clusterLba(to) + s, bytes.data() + s * 512);
        }

        auto setFat = [&](uint32_t cluster, uint16_t value) {
            for (uint32_t copy = 0; copy < reader.FatCount(); copy++)
            {
                const uint64_t lba = fatStart + uint64_t(copy) * reader.FatSectors() + cluster / 256;
                image.disk->ReadSector(lba, sector.data());
                sector[(cluster % 256) * 2] = static_cast<uint8_t>(value);
                sector[(cluster % 256) * 2 + 1] = static_cast<uint8_t>(value >> 8);
                image.disk->WriteSector(lba, sector.data());
            }
        };
        for (const std::vector<uint32_t>* chain : {&ca, &cb})
        {
            for (size_t i = 0; i < chain->size(); i++)
                setFat(moved[(*chain)[i]], i + 1 < chain->size() ? static_cast<uint16_t>(moved[(*chain)[i + 1]]) : 0xFFFF);
        }
        for (uint32_t c : cd)
            setFat(c, 0);

        // The root's entries: new first clusters, DEL.BIN deleted
        for (uint64_t lba = rootStart; lba < rootStart + reader.RootDirSectors(); lba++)
        {
            image.disk->ReadSector(lba, sector.data());
            for (size_t e = 0; e < 512; e += 32)
            {
                uint8_t* entry = &sector[e];
                if (entry[0] == 0 || entry[0] == 0xE5 || entry[11] == 0x0F)
                    continue;
                const uint32_t first = entry[26] | (entry[27] << 8);
                if (std::memcmp(entry, "DEL     BIN", 11) == 0)
                    entry[0] = 0xE5;
                else if (moved.count(first))
                {
                    entry[26] = static_cast<uint8_t>(moved[first]);
                    entry[27] = static_cast<uint8_t>(moved[first] >> 8);
                }
            }
            image.disk->WriteSector(lba, sector.data());
        }
        return image;
    }

    class FlatSlot : public IMediaSlot
    {
    public:
        FlatSlot()
        {
            _descriptor.id = "ide0.master";
            _descriptor.kind = MediaKind::Block;
            _descriptor.label = "test disk";
            _descriptor.tags = {"ide", "hdd"};
        }
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override { attached = &medium; }
        void Detach() override { attached = nullptr; }
        void SourceChanged(Medium&) override {}
        Medium* attached = nullptr;

    private:
        SlotDescriptor _descriptor;
    };

    class FlattenFlat_Test : public ::testing::Test
    {
    protected:
        ScratchFolder _folder{"flatten-flat"};
        MediaManager _manager{nullptr};
        FlatSlot _slot;

        void SetUp() override { _manager.RegisterSlot(_slot); }
        void TearDown() override
        {
            EjectOptions discard;
            discard.disposition = Disposition::Discard;
            _manager.Eject("ide0.master", discard);
            _manager.UnregisterSlot("ide0.master");
        }

        MediaResult Insert(const std::string& path)
        {
            MediaSource source;
            source.path = path;
            InsertOptions options;
            options.access = AccessMode::Session;
            return _manager.Insert("ide0.master", source, options);
        }

        IBlockDevice& Guest() { return *_slot.attached->Block(); }

        std::vector<uint8_t> GuestView()
        {
            std::vector<uint8_t> all(Guest().SectorCount() * 512);
            for (uint64_t lba = 0; lba < Guest().SectorCount(); lba++)
                EXPECT_TRUE(Guest().ReadSector(lba, all.data() + lba * 512));
            return all;
        }

        /// The fragmented FAT16 image as a file in the scratch folder
        std::string FragmentedFile()
        {
            ScratchFolder source("flatten-flat-src");
            source.File("A.BIN", std::string(6 * 2048, 'A'));
            source.File("B.BIN", std::string(6 * 2048, 'B'));
            source.File("DEL.BIN", std::string(2048, 'Z'));
            source.File("GAMES/ELITE.TRD", "elite");
            const FatSourceImage image = FragmentedFat16(source.Path());
            EXPECT_TRUE(image.ok()) << image.error;
            const std::filesystem::path path = _folder.Path() / "fragmented.img";
            EXPECT_TRUE(image.ok() && SaveSparse(image, path));
            return Utf8(path);
        }
    };

    /// A whole sector of `fill` somewhere in `bytes` (the deleted file's data)
    bool HasSectorOf(const std::vector<uint8_t>& bytes, uint8_t fill)
    {
        const std::vector<uint8_t> pattern(512, fill);
        for (size_t at = 0; at + 512 <= bytes.size(); at += 512)
        {
            if (std::memcmp(bytes.data() + at, pattern.data(), 512) == 0)
                return true;
        }
        return false;
    }
}  // namespace

// A composite (a FAT16 image under a folder, rebuilt) with guest writes: .img, .vhd and .chd hold every sector the
// guest sees
TEST_F(FlattenFlat_Test, ImgVhdChdEqualMergedView)
{
    ScratchFolder base("flatten-flat-base");
    base.File("DSS/COMMAND.COM", "shell");
    const FatSourceImage image = FolderToFatDisk(base.Path(), FatType::Fat16);
    ASSERT_TRUE(image.ok()) << image.error;
    ASSERT_TRUE(SaveSparse(image, _folder.Path() / "base.img"));
    _folder.File("work/TOOL.COM", "tool");
    const auto descriptor = _folder.File("disk.ucompose.yaml", "version: 1\n"
                                                               "target: {build: rebuild, free: 1MiB, fixedTime: 1767268800}\n"
                                                               "layers:\n"
                                                               "  - {source: {image: base.img}}\n"
                                                               "  - {source: {folder: work}, mount: /WORK}\n");
    const MediaResult inserted = Insert(Utf8(descriptor));
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    const std::vector<uint8_t> data(512, 0x5A);
    ASSERT_TRUE(Guest().WriteSector(7, data.data()));
    ASSERT_TRUE(Guest().WriteSector(Guest().SectorCount() - 1, data.data()));
    _manager.ApplyPending();
    const std::vector<uint8_t> expected = GuestView();

    const auto img = _folder.Path() / "flat.img";
    ASSERT_TRUE(_manager.Export("ide0.master", Utf8(img)).Ok());
    EXPECT_EQ(Slurp(img), expected);

    const auto vhd = _folder.Path() / "flat.vhd";
    ASSERT_TRUE(_manager.Export("ide0.master", Utf8(vhd)).Ok());
    std::vector<uint8_t> vhdBytes = Slurp(vhd);
    ASSERT_EQ(vhdBytes.size(), expected.size() + 512);
    EXPECT_EQ(std::memcmp(vhdBytes.data() + expected.size(), "conectix", 8), 0);
    vhdBytes.resize(expected.size());
    EXPECT_EQ(vhdBytes, expected);

    const auto chd = _folder.Path() / "flat.chd";
    BlockWriteOptions stored;
    stored.compression = "none";  // the codecs have their own tests
    ASSERT_TRUE(_manager.Export("ide0.master", Utf8(chd), stored).Ok());
    std::string error;
    auto file = ChdImage::Open(Utf8(chd), &error);
    ASSERT_NE(file, nullptr) << error;
    ASSERT_EQ(file->SectorCount(), Guest().SectorCount());
    std::vector<uint8_t> chdBytes(expected.size());
    for (uint64_t lba = 0; lba < file->SectorCount(); lba++)
        ASSERT_TRUE(file->ReadSector(lba, chdBytes.data() + lba * 512));
    EXPECT_EQ(chdBytes, expected);
}

// The fragmented image compacted: the same tree and bytes, every file in one run, the deleted file's data gone;
// fs: fat32 converts (FAT32's minimum, 256 MiB with 4 KiB clusters, written sparse); a FAT12 floppy needs fs
TEST_F(FlattenFlat_Test, CompactDefragmentsAndOracleAgrees)
{
    const std::string source = FragmentedFile();
    ASSERT_TRUE(Insert(source).Ok());
    {
        FatVolumeReader reader;
        ASSERT_TRUE(reader.Open(Guest()));
        FatDirEntryInfo a;
        ASSERT_TRUE(reader.Stat("/A.BIN", a));
        std::vector<FatChainExtent> extents;
        ASSERT_TRUE(reader.ChainExtents(a.firstCluster, a.size, extents));
        ASSERT_GT(extents.size(), 1u) << "the source is fragmented";
    }
    const auto oracle = Tree(Guest());
    ASSERT_EQ(oracle.size(), 4u) << "A.BIN, B.BIN, GAMES/, GAMES/ELITE.TRD (DEL.BIN is deleted)";
    ASSERT_TRUE(HasSectorOf(GuestView(), 'Z')) << "the deleted file's data is still on the disk";

    for (const char* fs : {"same", "fat32"})
    {
        BlockWriteOptions options;
        options.compact = true;
        if (std::string(fs) == "fat32")
            options.fs = FatType::Fat32;
        const auto out = _folder.Path() / (std::string("compact-") + fs + ".img");
        const MediaResult result = _manager.Export("ide0.master", Utf8(out), options);
        ASSERT_TRUE(result.Ok()) << fs << ": " << result.message;

        std::string error;
        auto disk = RawImage::Open(Utf8(out), RawImage::Access::ReadOnly, &error);
        ASSERT_NE(disk, nullptr) << error;
        EXPECT_EQ(Tree(*disk), oracle) << fs;
        if (std::string(fs) == "same")  // the FAT32 one is 256 MiB of mostly zeros: not read whole
            EXPECT_FALSE(HasSectorOf(Slurp(out), 'Z')) << "deleted data is not carried";
        FatVolumeReader reader;
        ASSERT_TRUE(reader.Open(*disk));
        EXPECT_EQ(reader.Type(), std::string(fs) == "fat32" ? FatReaderType::Fat32 : FatReaderType::Fat16) << fs;
        for (const char* path : {"/A.BIN", "/B.BIN", "/GAMES/ELITE.TRD"})
        {
            FatDirEntryInfo entry;
            ASSERT_TRUE(reader.Stat(path, entry)) << path;
            std::vector<FatChainExtent> extents;
            ASSERT_TRUE(reader.ChainExtents(entry.firstCluster, entry.size, extents)) << path;
            EXPECT_EQ(extents.size(), 1u) << fs << " " << path << " is contiguous";
        }
        if (std::string(fs) == "fat32")
        {
            EXPECT_GT(std::filesystem::file_size(out), std::filesystem::file_size(source)) << "FAT32 needs more than the FAT16 source";
            EXPECT_TRUE(std::any_of(result.report.begin(), result.report.end(),
                                    [](const std::string& line) { return line.find("does not hold it as fat32") != std::string::npos; }))
                << "the report says the size grew";
        }
        else
            EXPECT_EQ(std::filesystem::file_size(out), std::filesystem::file_size(source)) << "the medium's size";
    }

    // A size that cannot hold the content is refused; FAT12 needs fs
    BlockWriteOptions tooSmall;
    tooSmall.compact = true;
    tooSmall.size = 64 * 1024;
    EXPECT_EQ(_manager.Export("ide0.master", Utf8(_folder.Path() / "small.img"), tooSmall).error, MediaError::DoesNotFit);

    TearDown();
    _manager.RegisterSlot(_slot);
    auto floppy = Fat12Floppy({{"HELLO.TXT", std::vector<uint8_t>(100, 'h')}});
    const FatSourceImage floppyImage{floppy, floppy->SectorCount(), {}};
    ASSERT_TRUE(SaveSparse(floppyImage, _folder.Path() / "floppy.img"));
    ASSERT_TRUE(Insert(Utf8(_folder.Path() / "floppy.img")).Ok());
    BlockWriteOptions compact;
    compact.compact = true;
    const MediaResult fat12 = _manager.Export("ide0.master", Utf8(_folder.Path() / "floppy-compact.img"), compact);
    EXPECT_EQ(fat12.error, MediaError::NotSupported);
    EXPECT_NE(fat12.message.find("FAT12"), std::string::npos) << fat12.message;
}

// save with compact: a whole new file, the medium reads it, the change layer is empty
TEST_F(FlattenFlat_Test, SaveCompactRebasesTheMedium)
{
    const std::string source = FragmentedFile();
    ASSERT_TRUE(Insert(source).Ok());
    const std::vector<uint8_t> before = Slurp(source);
    const std::vector<uint8_t> data(512, 0x11);
    // A guest write in free space: not part of any file, so the compacted volume drops it
    ASSERT_TRUE(Guest().WriteSector(Guest().SectorCount() - 1, data.data()));
    _manager.ApplyPending();
    const auto oracle = Tree(Guest());

    SaveOptions options;
    options.path = Utf8(_folder.Path() / "saved.img");
    options.compact = true;
    SaveOutcome outcome;
    const MediaResult saved = _manager.Save("ide0.master", options, &outcome);
    ASSERT_TRUE(saved.Ok()) << saved.message;
    EXPECT_EQ(outcome.savedPath, options.path);
    auto info = _manager.Info("ide0.master");
    EXPECT_EQ(info->source, options.path);
    EXPECT_FALSE(info->dirty);
    EXPECT_EQ(_slot.attached->Session()->ChangedSectors(), 0u);
    EXPECT_EQ(Tree(Guest()), oracle);
    EXPECT_EQ(GuestView(), Slurp(options.path)) << "the medium reads the new file";
    EXPECT_EQ(Slurp(source), before) << "the old file is not written";

    SaveOptions noPath;
    noPath.compact = true;
    EXPECT_EQ(_manager.Save("ide0.master", noPath).error, MediaError::BadRequest) << "compact writes a new file: a path";
}
