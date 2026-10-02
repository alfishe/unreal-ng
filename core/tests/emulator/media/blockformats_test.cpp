// Block media as image files through the media manager (blockformats.h): a CHD inserted like any image, guest
// writes kept in the change layer, `save` writing the CHD again (or the raw family's sectors in place), `export` to
// a raw image or a CHD with any codecs or as a child (docs/inprogress/2026-10-02-media-chd/design.md §4-§5).

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/io/storage/chd/chdfile.h"
#include "emulator/io/storage/chd/chdimage.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/blockformats.h"
#include "emulator/media/mediacontrol.h"
#include "emulator/media/mediamanager.h"

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::string Fixture(const std::string& name)
    {
        return TestPathHelper::GetTestDataPath("media/chd/" + name);
    }

    std::vector<uint8_t> Slurp(const std::string& path)
    {
        std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    EjectOptions DiscardChanges()
    {
        EjectOptions options;
        options.disposition = Disposition::Discard;
        return options;
    }

    class BlockSlot : public IMediaSlot
    {
    public:
        explicit BlockSlot(std::string id)
        {
            _descriptor.id = std::move(id);
            _descriptor.kind = MediaKind::Block;
            _descriptor.label = "test disk";
            _descriptor.tags = {"ide", "hdd"};
        }
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override { attached = &medium; }
        void Detach() override { attached = nullptr; }
        void SourceChanged(Medium&) override { sourceChanges++; }
        Medium* attached = nullptr;
        int sourceChanges = 0;

    private:
        SlotDescriptor _descriptor;
    };

    class BlockFormats_Test : public ::testing::Test
    {
    protected:
        ScratchFolder _folder{"block-formats"};
        MediaManager _manager{nullptr};
        BlockSlot _slot{"ide0.master"};

        void SetUp() override { _manager.RegisterSlot(_slot); }
        void TearDown() override
        {
            _manager.Eject("ide0.master", DiscardChanges());
            _manager.UnregisterSlot("ide0.master");
        }

        /// A fixture copied into the scratch folder (a save must not touch testdata)
        std::string Copy(const std::string& name)
        {
            const std::filesystem::path to = _folder.Path() / name;
            std::filesystem::copy_file(FileHelper::ToFsPath(Fixture(name)), to, std::filesystem::copy_options::overwrite_existing);
            return Utf8(to);
        }

        MediaResult Insert(const std::string& path, AccessMode access)
        {
            MediaSource source;
            source.path = path;
            InsertOptions options;
            options.access = access;
            return _manager.Insert("ide0.master", source, options);
        }

        void GuestWrite(uint64_t lba, uint8_t fill)
        {
            ASSERT_NE(_slot.attached, nullptr);
            const std::vector<uint8_t> data(512, fill);
            ASSERT_TRUE(_slot.attached->Block()->WriteSector(lba, data.data()));
            _manager.ApplyPending();
        }

        std::vector<uint8_t> GuestView()
        {
            std::vector<uint8_t> all;
            uint8_t sector[512];
            for (uint64_t lba = 0; lba < _slot.attached->Block()->SectorCount(); lba++)
            {
                EXPECT_TRUE(_slot.attached->Block()->ReadSector(lba, sector));
                all.insert(all.end(), sector, sector + 512);
            }
            return all;
        }
    };
}  // namespace

TEST_F(BlockFormats_Test, AChdGoesInLikeAnyImageAndItsFileStaysUntouched)
{
    const std::string path = Copy("mixed-default.chd");
    const std::vector<uint8_t> before = Slurp(path);

    // WriteThrough is the IDE default; a CHD keeps its writes in the change layer instead
    MediaResult result = Insert(path, AccessMode::WriteThrough);
    ASSERT_TRUE(result.Ok()) << result.message;
    ASSERT_FALSE(result.report.empty());
    EXPECT_NE(result.report.front().find("session"), std::string::npos) << result.report.front();
    auto info = _manager.Info("ide0.master");
    EXPECT_EQ(info->format, "chd");
    EXPECT_EQ(info->access, AccessMode::Session);
    ASSERT_TRUE(_slot.attached->Block()->NativeGeometry().has_value());
    EXPECT_EQ(_slot.attached->Block()->NativeGeometry()->heads, 4u) << "GDDD reaches the ATA identify through the stack";
    EXPECT_EQ(GuestView(), Slurp(Fixture("mixed.img")));

    GuestWrite(3, 0xA5);
    EXPECT_TRUE(_manager.Info("ide0.master")->dirty);
    EXPECT_EQ(Slurp(path), before) << "the CHD is written only by a save";

    // Read-only: the guard refuses
    ASSERT_TRUE(_manager.Eject("ide0.master", DiscardChanges()).Ok());
    ASSERT_TRUE(Insert(path, AccessMode::ReadOnly).Ok());
    const std::vector<uint8_t> data(512, 1);
    EXPECT_FALSE(_slot.attached->Block()->WriteSector(0, data.data()));
}

TEST_F(BlockFormats_Test, SaveWritesTheChdAgainWithItsCodecsAndStoredHunks)
{
    const std::string path = Copy("mixed-default.chd");
    ASSERT_TRUE(Insert(path, AccessMode::Session).Ok());
    GuestWrite(100, 0x3C);  // hunk 12 (FLAC)
    GuestWrite(101, 0x3C);
    const std::vector<uint8_t> expected = GuestView();

    SaveOutcome outcome;
    const MediaResult saved = _manager.Save("ide0.master", {}, &outcome);
    ASSERT_TRUE(saved.Ok()) << saved.message;
    EXPECT_EQ(outcome.savedPath, path);
    EXPECT_FALSE(_manager.Info("ide0.master")->dirty) << "the change layer is empty after a save";
    EXPECT_EQ(_slot.attached->Session()->ChangedSectors(), 0u);
    EXPECT_EQ(GuestView(), expected) << "the medium now reads the saved CHD";
    EXPECT_FALSE(FileHelper::FileExists(path + ".writing"));

    std::string error;
    auto file = chd::ChdFile::Open(path, &error);
    ASSERT_NE(file, nullptr) << error;
    EXPECT_EQ(chd::FormatCodecList(file->Codecs()), "lzma,zlib,huff,flac") << "the source's codecs";
    EXPECT_EQ(file->MetadataText(chd::kTagHardDisk).value_or(""), "CYLS:8,HEADS:4,SECS:16,BPS:512");
    EXPECT_TRUE(file->Verify(&error)) << error;
    auto source = chd::ChdFile::Open(Fixture("mixed-default.chd"));
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(file->Entry(20).length, source->Entry(20).length) << "an untouched hunk keeps its stored bytes";

    // compression on save: the CHD changes its codecs
    GuestWrite(0, 0x01);
    SaveOptions options;
    options.compression = "zstd";
    ASSERT_TRUE(_manager.Save("ide0.master", options).Ok());
    file = chd::ChdFile::Open(path, &error);
    ASSERT_NE(file, nullptr) << error;
    EXPECT_EQ(chd::FormatCodecList(file->Codecs()), "zstd");
}

TEST_F(BlockFormats_Test, SavingAChildKeepsItAChildOfTheSameParent)
{
    Copy("mixed-default.chd");
    const std::string child = Copy("child-of-default.chd");
    const std::vector<uint8_t> parentBefore = Slurp(Utf8(_folder.Path() / "mixed-default.chd"));
    ASSERT_TRUE(Insert(child, AccessMode::Session).Ok());
    GuestWrite(200, 0x77);
    const std::vector<uint8_t> expected = GuestView();
    ASSERT_TRUE(_manager.Save("ide0.master").Ok());

    std::string error;
    auto file = chd::ChdFile::Open(child, &error);
    ASSERT_NE(file, nullptr) << error;
    ASSERT_NE(file->Parent(), nullptr);
    EXPECT_EQ(file->ParentSha1(), file->Parent()->OverallSha1());
    EXPECT_EQ(GuestView(), expected);
    EXPECT_EQ(Slurp(Utf8(_folder.Path() / "mixed-default.chd")), parentBefore) << "the parent is never written";
    int parentHunks = 0;
    for (uint32_t h = 0; h < file->HunkCount(); h++)
        parentHunks += file->Entry(h).type == chd::HunkEntry::Type::Parent ? 1 : 0;
    EXPECT_EQ(parentHunks, 62);
}

// Seven exports, five of them compressed CHDs through the real encoders: ~120 ms, over the 50 ms budget
TEST_F(BlockFormats_Test, ExportWritesRawImagesAndChdsOfAnyKind)
{
    const std::string path = Copy("mixed-lzma.chd");
    ASSERT_TRUE(Insert(path, AccessMode::Session).Ok());
    GuestWrite(9, 0xEE);
    const std::vector<uint8_t> expected = GuestView();

    const std::string raw = Utf8(_folder.Path() / "out.img");
    ASSERT_TRUE(_manager.Export("ide0.master", raw).Ok());
    EXPECT_EQ(Slurp(raw), expected);

    // Default: the source's codecs
    const std::string same = Utf8(_folder.Path() / "same.chd");
    ASSERT_TRUE(_manager.Export("ide0.master", same).Ok());
    std::string error;
    auto file = chd::ChdFile::Open(same, &error);
    ASSERT_NE(file, nullptr) << error;
    EXPECT_EQ(chd::FormatCodecList(file->Codecs()), "lzma");
    EXPECT_TRUE(file->Verify(&error)) << error;

    for (const std::string compression : {"none", "default", "huff,flac", "zstd"})
    {
        const std::string out = Utf8(_folder.Path() / ("out-" + std::to_string(compression.size()) + ".chd"));
        BlockWriteOptions options;
        options.compression = compression;
        const MediaResult result = _manager.Export("ide0.master", out, options);
        ASSERT_TRUE(result.Ok()) << compression << ": " << result.message;
        auto chd = ChdImage::Open(out, &error);
        ASSERT_NE(chd, nullptr) << error;
        std::vector<uint8_t> all(expected.size());
        for (uint64_t lba = 0; lba < chd->SectorCount(); lba++)
            ASSERT_TRUE(chd->ReadSector(lba, all.data() + lba * 512));
        EXPECT_EQ(all, expected) << compression;
        EXPECT_TRUE(chd->File().Verify(&error)) << error;
    }

    // A child of the original disk: only the changed hunk is stored
    BlockWriteOptions childOptions;
    childOptions.parent = Copy("mixed-default.chd");
    const std::string childPath = Utf8(_folder.Path() / "diff.chd");
    ASSERT_TRUE(_manager.Export("ide0.master", childPath, childOptions).Ok());
    file = chd::ChdFile::Open(childPath, &error);
    ASSERT_NE(file, nullptr) << error;
    int stored = 0;
    for (uint32_t h = 0; h < file->HunkCount(); h++)
        stored += file->Entry(h).type == chd::HunkEntry::Type::Parent ? 0 : 1;
    EXPECT_EQ(stored, 1);

    BlockWriteOptions bad;
    bad.compression = "cdlz";
    EXPECT_EQ(_manager.Export("ide0.master", Utf8(_folder.Path() / "bad.chd"), bad).error, MediaError::BadRequest);
    bad.compression = "zstd";
    EXPECT_EQ(_manager.Export("ide0.master", Utf8(_folder.Path() / "bad.img"), bad).error, MediaError::BadRequest)
        << "compression is for a .chd target";
}

TEST_F(BlockFormats_Test, SaveWritesRawImageSectorsInPlaceAndSaveAsRebases)
{
    const std::string raw = Utf8(_folder.File("disk.img", std::string(64 * 512, '\x42')));
    ASSERT_TRUE(Insert(raw, AccessMode::Session).Ok());
    GuestWrite(5, 0x99);
    ASSERT_TRUE(_manager.Save("ide0.master").Ok());
    const std::vector<uint8_t> bytes = Slurp(raw);
    EXPECT_EQ(bytes[5 * 512], 0x99);
    EXPECT_EQ(bytes[4 * 512], 0x42);
    EXPECT_FALSE(_manager.Info("ide0.master")->dirty);

    // Save as a CHD: the medium now stands for that file
    GuestWrite(6, 0x11);
    SaveOptions options;
    options.path = Utf8(_folder.Path() / "disk.chd");
    ASSERT_TRUE(_manager.Save("ide0.master", options).Ok());
    auto info = _manager.Info("ide0.master");
    EXPECT_EQ(info->format, "chd");
    EXPECT_EQ(info->source, options.path);
    EXPECT_FALSE(info->dirty);
    EXPECT_EQ(_slot.sourceChanges, 1);
    EXPECT_EQ(Slurp(raw)[6 * 512], 0x42) << "the old file keeps its contents";
    EXPECT_EQ(GuestView()[6 * 512], 0x11);
}

TEST_F(BlockFormats_Test, MediaControlTakesCompressionAndParent)
{
    const std::string path = Copy("mixed-zstd.chd");
    ASSERT_TRUE(Insert(path, AccessMode::Session).Ok());
    const std::string out = Utf8(_folder.Path() / "control.chd");
    // The verbs' option lists (MediaControl_Test.ChdOnTheSharedVerbs runs them on a machine)
    EXPECT_EQ(MediaControl::OptionsFor("export"), (std::vector<std::string>{"compression", "parent"}));
    EXPECT_EQ(MediaControl::OptionsFor("save"), (std::vector<std::string>{"retarget", "compression"}));
    BlockWriteOptions options;
    options.compression = "lzma,huff";
    ASSERT_TRUE(_manager.Export("ide0.master", out, options).Ok());
    std::string error;
    auto file = chd::ChdFile::Open(out, &error);
    ASSERT_NE(file, nullptr) << error;
    EXPECT_EQ(chd::FormatCodecList(file->Codecs()), "lzma,huff");
}
