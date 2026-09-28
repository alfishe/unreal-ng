// Medium: identity of the source, dirtiness per access mode, export

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "common/filehelper.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/medium.h"

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::unique_ptr<Medium> MemoryMedium(AccessMode access, uint64_t sectors = 8)
    {
        MediaSource blank;
        blank.type = MediaSourceType::Blank;
        return MediaFormatRegistry::WrapBlock(blank, access, "memory", std::make_unique<MemoryDisk>(sectors));
    }
}  // namespace

/// The "one source, one slot" rule compares keys: two spellings of one file
/// are one source; sources without a path never collide
TEST(Medium_Test, SourceKeyIsTheCanonicalPath)
{
    ScratchFolder folder("medium-key");
    folder.File("sub/card.img", std::string(512, '\0'));
    MediaSource direct;
    direct.path = Utf8(folder.Path() / "sub" / "card.img");
    MediaSource roundabout;
    roundabout.path = Utf8(folder.Path() / "sub" / ".." / "sub" / "card.img");

    const Medium a(direct, AccessMode::ReadOnly, "raw", std::make_unique<MemoryDisk>(1), nullptr);
    const Medium b(roundabout, AccessMode::ReadOnly, "raw", std::make_unique<MemoryDisk>(1), nullptr);
    EXPECT_EQ(a.SourceKey(), b.SourceKey());

    EXPECT_NE(MemoryMedium(AccessMode::Session)->SourceKey(), MemoryMedium(AccessMode::Session)->SourceKey());
}

TEST(Medium_Test, OnlyASessionGetsDirty)
{
    const std::vector<uint8_t> sector(512, 0x42);

    auto session = MemoryMedium(AccessMode::Session);
    ASSERT_NE(session->Session(), nullptr);
    EXPECT_FALSE(session->IsDirty());
    ASSERT_TRUE(session->Block()->WriteSector(2, sector.data()));
    ASSERT_TRUE(session->Block()->WriteSector(5, sector.data()));
    EXPECT_TRUE(session->IsDirty());
    EXPECT_EQ(session->ChangedUnits(), 2u);

    auto through = MemoryMedium(AccessMode::WriteThrough);
    ASSERT_TRUE(through->Block()->WriteSector(2, sector.data()));
    EXPECT_FALSE(through->IsDirty()) << "the write is already in the source";

    auto readOnly = MemoryMedium(AccessMode::ReadOnly);
    EXPECT_FALSE(readOnly->Block()->WriteSector(2, sector.data()));
    EXPECT_FALSE(readOnly->IsDirty());
}

/// Export writes the guest's view, session writes included, sector by sector
TEST(Medium_Test, ExportWritesEverySectorAsTheGuestSeesIt)
{
    auto medium = MemoryMedium(AccessMode::Session, 16);
    const std::vector<uint8_t> sector(512, 0x77);
    ASSERT_TRUE(medium->Block()->WriteSector(15, sector.data()));

    ScratchFolder folder("medium-export");
    const std::string path = Utf8(folder.Path() / "export.img");
    std::string error;
    ASSERT_TRUE(ExportBlockDevice(*medium->Block(), path, &error)) << error;

    auto image = RawImage::Open(path, RawImage::Access::ReadOnly);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->SectorCount(), 16u);
    uint8_t back[512];
    ASSERT_TRUE(image->ReadSector(15, back));
    EXPECT_EQ(back[0], 0x77);
    ASSERT_TRUE(image->ReadSector(0, back));
    EXPECT_EQ(back[0], 0x00);

    EXPECT_FALSE(ExportBlockDevice(*medium->Block(), Utf8(folder.Path() / "no-such-folder" / "x.img"), &error));
    EXPECT_FALSE(error.empty());
}
