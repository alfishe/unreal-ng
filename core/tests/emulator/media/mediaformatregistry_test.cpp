// MediaFormatRegistry: what a source opens as, with which access layer, and why not

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "emulator/media/mediaformatregistry.h"

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }
}  // namespace

TEST(MediaFormatRegistry_Test, ImageFileOpensRawInEveryAccessMode)
{
    ScratchFolder folder("registry-image");
    const std::string image = Utf8(folder.File("card.img", std::string(8 * 512, '\x5A')));

    for (AccessMode access : {AccessMode::ReadOnly, AccessMode::Session, AccessMode::WriteThrough})
    {
        OpenRequest request;
        request.source.path = image;
        request.access = access;
        std::unique_ptr<Medium> medium;
        const MediaResult result = MediaFormatRegistry::Open(request, medium);
        ASSERT_TRUE(result.Ok()) << AccessModeName(access) << ": " << result.message;
        ASSERT_NE(medium, nullptr);
        EXPECT_EQ(medium->Format(), "raw");
        EXPECT_EQ(medium->Access(), access);
        EXPECT_EQ(medium->Source().type, MediaSourceType::File);
        EXPECT_EQ(medium->Block()->SectorCount(), 8u);
        EXPECT_EQ(medium->Block()->IsWritable(), access != AccessMode::ReadOnly) << AccessModeName(access);
        EXPECT_EQ(medium->Session() != nullptr, access == AccessMode::Session) << AccessModeName(access);
    }
}

TEST(MediaFormatRegistry_Test, FolderOpensAsAFatVolumeOfTheAskedType)
{
    ScratchFolder folder("registry-folder");
    folder.File("GAME.TRD", "x");

    OpenRequest request;
    request.source.path = Utf8(folder.Path());  // the type is found from the path
    request.freeBytes = 1024 * 1024;
    std::unique_ptr<Medium> medium;
    ASSERT_TRUE(MediaFormatRegistry::Open(request, medium).Ok());
    EXPECT_EQ(medium->Format(), "folder-fat16");
    EXPECT_EQ(medium->Source().type, MediaSourceType::Folder);

    request.fs = FatType::Fat32;
    ASSERT_TRUE(MediaFormatRegistry::Open(request, medium).Ok());
    EXPECT_EQ(medium->Format(), "folder-fat32");

    request.access = AccessMode::WriteThrough;
    const MediaResult refused = MediaFormatRegistry::Open(request, medium);
    EXPECT_EQ(refused.error, MediaError::KindMismatch) << "a folder is never written";
    EXPECT_EQ(medium, nullptr);
}

TEST(MediaFormatRegistry_Test, FailuresSayWhy)
{
    std::unique_ptr<Medium> medium;
    OpenRequest request;
    request.source.path = TestPathHelper::GetUniqueTestScratchPath("registry-missing.img");
    EXPECT_EQ(MediaFormatRegistry::Open(request, medium).error, MediaError::UnreadableSource);

    request.kind = MediaKind::Tape;
    const MediaResult tape = MediaFormatRegistry::Open(request, medium);
    EXPECT_EQ(tape.error, MediaError::UnreadableSource);
    EXPECT_NE(tape.message.find("registry-missing"), std::string::npos) << tape.message;
    request.source.type = MediaSourceType::Blank;
    EXPECT_EQ(MediaFormatRegistry::Open(request, medium).error, MediaError::NotSupported) << "no blank tape";
    request.source.type = MediaSourceType::File;

    request.kind = MediaKind::Block;
    request.source.type = MediaSourceType::Blank;
    EXPECT_EQ(MediaFormatRegistry::Open(request, medium).error, MediaError::NotSupported);
    EXPECT_EQ(medium, nullptr);
}

TEST(MediaFormatRegistry_Test, ExtensionsPerKind)
{
    const auto block = MediaFormatRegistry::Extensions(MediaKind::Block);
    EXPECT_NE(std::find(block.begin(), block.end(), "img"), block.end());
    const auto tape = MediaFormatRegistry::Extensions(MediaKind::Tape);
    EXPECT_NE(std::find(tape.begin(), tape.end(), "tzx"), tape.end());
    EXPECT_NE(std::find(tape.begin(), tape.end(), "tap"), tape.end());
}

/// The same 720 KB .img is a raw PC floppy in a floppy slot and a raw block image in a block slot: the slot kind
/// settles the .img ambiguity (storage manager G9)
TEST(MediaFormatRegistry_Test, RawPcFloppyImageOpensByTheSlotKind)
{
    ScratchFolder folder("registry-rawpc");
    const std::string image = Utf8(folder.File("cpm.img", std::string(737280, '\xE5')));

    OpenRequest request;
    request.source.path = image;
    request.kind = MediaKind::Floppy;
    std::unique_ptr<Medium> medium;
    const MediaResult floppy = MediaFormatRegistry::Open(request, medium);
    ASSERT_TRUE(floppy.Ok()) << floppy.message;
    EXPECT_EQ(medium->Format(), "rawpc");

    request.kind = MediaKind::Block;
    ASSERT_TRUE(MediaFormatRegistry::Open(request, medium).Ok());
    EXPECT_EQ(medium->Format(), "raw");

    const auto extensions = MediaFormatRegistry::Extensions(MediaKind::Floppy);
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "ima"), extensions.end());
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "img"), extensions.end());
}
