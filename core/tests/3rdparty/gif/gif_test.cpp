#include "stdafx.h"
#include "pch.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "3rdparty/gif/gif.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"

/// The vendored GIF writer opens its file from a UTF-8 path on every platform (a local change to gif-h: the original
/// passed the bytes to the narrow fopen, which reads them in the ANSI code page on Windows). The paths below are real
/// UTF-8 in the test source: Cyrillic, CJK and an emoji, none of them in a Western ANSI code page. On POSIX UTF-8 is
/// native, so these tests pin the contract there; the Windows branch is the same code path through _wfopen and is
/// checked to compile warning-free by the MinGW cross build.

namespace
{
/// A 4x4 RGBA picture, a few colors
std::vector<uint8_t> Picture()
{
    std::vector<uint8_t> rgba(4 * 4 * 4);
    for (size_t i = 0; i < 16; i++)
    {
        rgba[i * 4 + 0] = static_cast<uint8_t>(i * 16);
        rgba[i * 4 + 1] = static_cast<uint8_t>(255 - i * 16);
        rgba[i * 4 + 2] = static_cast<uint8_t>((i % 4) * 80);
        rgba[i * 4 + 3] = 255;
    }
    return rgba;
}

/// A scratch folder with `name` (UTF-8) in it, created, and the UTF-8 path of `leaf` inside it
std::string PathIn(const std::string& folderName, const std::string& leaf)
{
    const std::string folder = TestPathHelper::GetUniqueTestScratchPath(folderName + "/placeholder");
    const std::filesystem::path dir = FileHelper::ToFsPath(folder).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto u8 = dir.u8string();
    return std::string(u8.begin(), u8.end()) + "/" + leaf;
}

std::string ReadHead(const std::string& utf8Path, size_t& size)
{
    FILE* f = FileHelper::OpenFile(utf8Path, "rb");
    size = 0;
    if (!f)
        return "";
    std::fseek(f, 0, SEEK_END);
    size = static_cast<size_t>(std::ftell(f));
    std::fseek(f, 0, SEEK_SET);
    char head[7] = {};
    const size_t got = std::fread(head, 1, 6, f);
    FileHelper::CloseFile(f);
    return std::string(head, got);
}
}  // namespace

TEST(Gif_Test, BeginWritesToAUtf8PathWithNonAsciiFolderAndName)
{
    for (const char* folder : {"папка-гиф", "文件夹", "emoji-\xF0\x9F\x8E\xAE-folder"})
    {
        SCOPED_TRACE(folder);
        const std::string path = PathIn(folder, "кадр-画像.gif");
        const std::vector<uint8_t> rgba = Picture();

        GifWriter writer = {};
        ASSERT_TRUE(GifBegin(&writer, path.c_str(), 4, 4, 0));
        EXPECT_TRUE(GifWriteFrame(&writer, rgba.data(), 4, 4, 0));
        EXPECT_TRUE(GifEnd(&writer));

        size_t size = 0;
        EXPECT_EQ(ReadHead(path, size), "GIF89a");
        EXPECT_GT(size, 20u);
    }
}

TEST(Gif_Test, BeginFileTakesAnOpenFileAndEndClosesIt)
{
    const std::string path = PathIn("открытый-файл", "файл.gif");
    FILE* file = FileHelper::OpenFile(path, "wb");
    ASSERT_NE(file, nullptr);

    const std::vector<uint8_t> rgba = Picture();
    GifWriter writer = {};
    ASSERT_TRUE(GifBeginFile(&writer, file, 4, 4, 0));
    EXPECT_TRUE(GifWriteFrame(&writer, rgba.data(), 4, 4, 0));
    EXPECT_TRUE(GifEnd(&writer));  // closes `file`

    size_t size = 0;
    EXPECT_EQ(ReadHead(path, size), "GIF89a");
    EXPECT_GT(size, 20u);
}

TEST(Gif_Test, ANullFileOrAMissingFolderIsAFailureNotACrash)
{
    GifWriter writer = {};
    EXPECT_FALSE(GifBeginFile(&writer, nullptr, 4, 4, 0));
    EXPECT_EQ(writer.f, nullptr);

    const std::string missing = PathIn("есть-папка", "нет-такой-папки/файл.gif");
    EXPECT_FALSE(GifBegin(&writer, missing.c_str(), 4, 4, 0));
}

#if !defined(_WIN32) && !defined(__APPLE__)
/// A name that is not valid UTF-8 (a Latin-1 byte) is opened as the raw bytes, as before the change: the legacy
/// callers that pass a narrow ANSI path keep working (on Windows it falls back to the ANSI fopen). Not on macOS:
/// APFS refuses file names that are not valid UTF-8
TEST(Gif_Test, ANameThatIsNotUtf8IsOpenedAsRawBytes)
{
    const std::string folder = TestPathHelper::GetUniqueTestScratchPath("raw-bytes/placeholder");
    std::error_code ec;
    std::filesystem::create_directories(FileHelper::ToFsPath(folder).parent_path(), ec);
    const auto u8 = FileHelper::ToFsPath(folder).parent_path().u8string();
    const std::string path = std::string(u8.begin(), u8.end()) + "/caf\xE9.gif";

    const std::vector<uint8_t> rgba = Picture();
    GifWriter writer = {};
    ASSERT_TRUE(GifBegin(&writer, path.c_str(), 4, 4, 0));
    EXPECT_TRUE(GifWriteFrame(&writer, rgba.data(), 4, 4, 0));
    EXPECT_TRUE(GifEnd(&writer));

    FILE* f = std::fopen(path.c_str(), "rb");
    ASSERT_NE(f, nullptr) << "the file is there under the raw byte name";
    std::fclose(f);
}
#endif

/// A picture with fewer colors than the palette leaves subtrees without pixels: GifSplitPalette returns there without
/// filling the node. GifMakePalette used to fill a stack GifPalette as it found it, so the nearest-color search read a
/// garbage split component as an index into its three-element r/g/b array (AddressSanitizer: stack-buffer-overflow in
/// GifGetClosestPaletteColor on every frame) and garbage leaf colors. The palette here starts as 0xFF garbage
TEST(Gif_Test, APaletteOfAFewColorsHasEveryNodeFilled)
{
    const std::vector<uint8_t> rgba = Picture();
    for (const bool dither : {false, true})
    {
        GifPalette pal;
        std::memset(&pal, 0xFF, sizeof(pal));
        GifMakePalette(nullptr, rgba.data(), 4, 4, 8, dither, &pal);
        for (int node = 1; node < (1 << pal.bitDepth); node++)
            EXPECT_LE(pal.treeSplitElt[node], 2) << "node " << node << (dither ? " dither" : "");

        int bestIndex = 0;
        int bestDiff = 1000000;
        GifGetClosestPaletteColor(&pal, rgba[0], rgba[1], rgba[2], bestIndex, bestDiff);
        EXPECT_EQ(bestDiff, 0) << "the first pixel's own color is in the palette" << (dither ? " dither" : "");
    }
}
