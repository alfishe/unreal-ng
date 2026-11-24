#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

namespace
{
/// A GIF decoder for the frames this writer makes (local color tables, a transparent index, no interlace): each frame
/// drawn over the one before (disposal "leave in place"), the canvas returned as RGBA per frame. Empty on a format the
/// writer never produces
std::vector<std::vector<uint8_t>> DecodeGif(const std::vector<uint8_t>& file, uint32_t& width, uint32_t& height)
{
    std::vector<std::vector<uint8_t>> frames;
    size_t pos = 0;
    auto byte = [&]() -> uint32_t { return pos < file.size() ? file[pos++] : 0; };
    auto word = [&]() { const uint32_t lo = byte(); return lo | (byte() << 8); };
    if (file.size() < 13 || std::memcmp(file.data(), "GIF89a", 6) != 0)
        return frames;
    pos = 6;
    width = word();
    height = word();
    const uint32_t screenFlags = byte();
    pos += 2;
    std::vector<uint8_t> globalTable;
    if (screenFlags & 0x80)
    {
        const size_t entries = size_t(1) << ((screenFlags & 7) + 1);
        globalTable.assign(file.begin() + static_cast<std::ptrdiff_t>(pos),
                           file.begin() + static_cast<std::ptrdiff_t>(pos + entries * 3));
        pos += entries * 3;
    }
    std::vector<uint8_t> canvas(size_t(width) * height * 4, 0);
    int transparent = -1;
    while (pos < file.size())
    {
        const uint32_t block = byte();
        if (block == 0x3B)
            break;
        if (block == 0x21)
        {
            const uint32_t label = byte();
            uint32_t size = byte();
            if (label == 0xF9 && size == 4)
            {
                const uint32_t flags = byte();
                pos += 2;
                const uint32_t index = byte();
                transparent = (flags & 1) ? static_cast<int>(index) : -1;
                size = byte();
            }
            while (size)
            {
                pos += size;
                size = byte();
            }
            continue;
        }
        if (block != 0x2C)
            return {};
        const uint32_t left = word(), top = word(), w = word(), h = word();
        const uint32_t flags = byte();
        if (flags & 0x40)
            return {};  // interlaced: never written
        std::vector<uint8_t> table = globalTable;
        if (flags & 0x80)
        {
            const size_t entries = size_t(1) << ((flags & 7) + 1);
            table.assign(file.begin() + static_cast<std::ptrdiff_t>(pos),
                         file.begin() + static_cast<std::ptrdiff_t>(pos + entries * 3));
            pos += entries * 3;
        }
        // The LZW data, its sub-blocks joined
        const uint32_t minCode = byte();
        std::vector<uint8_t> data;
        for (uint32_t size = byte(); size; size = byte())
        {
            data.insert(data.end(), file.begin() + static_cast<std::ptrdiff_t>(pos),
                        file.begin() + static_cast<std::ptrdiff_t>(pos + size));
            pos += size;
        }
        const uint32_t clear = 1u << minCode, end = clear + 1;
        std::vector<std::vector<uint8_t>> dict;
        auto reset = [&]() {
            dict.assign(clear + 2, {});
            for (uint32_t i = 0; i < clear; i++)
                dict[i] = {static_cast<uint8_t>(i)};
        };
        reset();
        std::vector<uint8_t> indices;
        uint32_t codeSize = minCode + 1, bitPos = 0;
        std::vector<uint8_t> previous;
        for (;;)
        {
            if (bitPos + codeSize > data.size() * 8)
                break;
            uint32_t code = 0;
            for (uint32_t b = 0; b < codeSize; b++, bitPos++)
                code |= static_cast<uint32_t>((data[bitPos / 8] >> (bitPos % 8)) & 1) << b;
            if (code == clear)
            {
                reset();
                codeSize = minCode + 1;
                previous.clear();
                continue;
            }
            if (code == end)
                break;
            std::vector<uint8_t> entry;
            if (code < dict.size())
                entry = dict[code];
            else if (code == dict.size() && !previous.empty())
            {
                entry = previous;
                entry.push_back(previous[0]);
            }
            else
                return {};
            indices.insert(indices.end(), entry.begin(), entry.end());
            if (!previous.empty() && dict.size() < 4096)
            {
                std::vector<uint8_t> added = previous;
                added.push_back(entry[0]);
                dict.push_back(std::move(added));
                if (dict.size() == (size_t(1) << codeSize) && codeSize < 12)
                    codeSize++;
            }
            previous = std::move(entry);
        }
        if (indices.size() < size_t(w) * h)
            return {};
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
            {
                const uint8_t index = indices[size_t(y) * w + x];
                if (static_cast<int>(index) == transparent || size_t(index) * 3 + 2 >= table.size())
                    continue;
                uint8_t* out = &canvas[(size_t(top + y) * width + left + x) * 4];
                out[0] = table[index * 3u];
                out[1] = table[index * 3u + 1];
                out[2] = table[index * 3u + 2];
                out[3] = 255;
            }
        frames.push_back(canvas);
        transparent = -1;
    }
    return frames;
}

/// The local color table entries of each frame written (2^(depth) from the image descriptor's flags)
std::vector<int> LocalTableSizes(const std::vector<uint8_t>& file)
{
    std::vector<int> sizes;
    for (size_t i = 13; i + 10 < file.size(); i++)
        if (file[i] == 0x2C && file[i - 8] == 0x21 && file[i - 7] == 0xF9)  // an image descriptor after a GCE
            sizes.push_back(1 << ((file[i + 9] & 7) + 1));
    return sizes;
}

std::vector<uint8_t> ReadAll(const std::string& utf8Path)
{
    std::vector<uint8_t> data;
    FILE* f = FileHelper::OpenFile(utf8Path, "rb");
    if (!f)
        return data;
    uint8_t buffer[4096];
    size_t got;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
        data.insert(data.end(), buffer, buffer + got);
    FileHelper::CloseFile(f);
    return data;
}

/// The 16 colors of a ZX picture (RGBA)
constexpr uint8_t kZx[16][3] = {{0, 0, 0},       {0, 0, 202},     {202, 0, 0},     {202, 0, 202},
                                {0, 202, 0},     {0, 202, 202},   {202, 202, 0},   {202, 202, 202},
                                {0, 0, 0},       {0, 0, 255},     {255, 0, 0},     {255, 0, 255},
                                {0, 255, 0},     {0, 255, 255},   {255, 255, 0},   {255, 255, 255}};

void SetPixel(std::vector<uint8_t>& rgba, uint32_t width, uint32_t x, uint32_t y, const uint8_t* rgb)
{
    uint8_t* p = &rgba[(size_t(y) * width + x) * 4];
    p[0] = rgb[0];
    p[1] = rgb[1];
    p[2] = rgb[2];
    p[3] = 255;
}

/// Writes `frames` with GifWriteFrame (the recorder's Auto mode: 8 bits, no dither) and decodes the file
std::vector<std::vector<uint8_t>> RoundTrip(const std::vector<std::vector<uint8_t>>& frames, uint32_t width,
                                            uint32_t height, const std::string& name, std::vector<int>* tableSizes = nullptr)
{
    const std::string path = PathIn("gif-roundtrip", name);
    GifWriter writer = {};
    EXPECT_TRUE(GifBegin(&writer, path.c_str(), width, height, 2));
    for (const auto& frame : frames)
        EXPECT_TRUE(GifWriteFrame(&writer, frame.data(), width, height, 2, 8, false));
    EXPECT_TRUE(GifEnd(&writer));
    const std::vector<uint8_t> file = ReadAll(path);
    if (tableSizes)
        *tableSizes = LocalTableSizes(file);
    uint32_t w = 0, h = 0;
    auto decoded = DecodeGif(file, w, h);
    EXPECT_EQ(w, width);
    EXPECT_EQ(h, height);
    return decoded;
}

/// The RGB of two pictures equal (the alpha of the source is 255 everywhere)
::testing::AssertionResult SameRgb(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    if (a.size() != b.size())
        return ::testing::AssertionFailure() << "sizes " << a.size() << " and " << b.size();
    for (size_t i = 0; i < a.size(); i += 4)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2])
            return ::testing::AssertionFailure() << "pixel " << i / 4 << ": " << int(a[i]) << "," << int(a[i + 1]) << ","
                                                 << int(a[i + 2]) << " decoded as " << int(b[i]) << "," << int(b[i + 1])
                                                 << "," << int(b[i + 2]);
    return ::testing::AssertionSuccess();
}
}  // namespace

TEST(Gif_Test, FramesOfFewColorsDecodeToTheSourceExactly)
{
    // A ZX-like sequence: a full picture, a block that changes, a repeated frame, a change back. The palette of the
    // changed pixels is exact (no median split): every decoded frame is its source
    const uint32_t w = 40, h = 24;
    std::vector<std::vector<uint8_t>> frames;
    std::vector<uint8_t> picture(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            SetPixel(picture, w, x, y, kZx[(x / 8 + y / 8 * 5 + x * y) % 16]);
    frames.push_back(picture);
    for (int step = 1; step <= 6; step++)
    {
        for (uint32_t y = 4; y < 12; y++)
            for (uint32_t x = 8; x < 20; x++)
                SetPixel(picture, w, x, y, kZx[(x + y + static_cast<uint32_t>(step) * 3) % 16]);
        frames.push_back(picture);
        if (step == 3)
            frames.push_back(picture);  // a repeated frame
    }
    const auto decoded = RoundTrip(frames, w, h, "few-colors.gif");
    ASSERT_EQ(decoded.size(), frames.size());
    for (size_t i = 0; i < frames.size(); i++)
        EXPECT_TRUE(SameRgb(frames[i], decoded[i])) << "frame " << i;
}

TEST(Gif_Test, ARepeatedFrameKeepsTheTopLeftPixel)
{
    // A repeated frame is one transparent pixel. It wrote the previous frame's index of pixel (0,0) with an empty
    // palette: the corner turned black on every repeat
    const uint32_t w = 8, h = 4;
    std::vector<uint8_t> first(size_t(w) * h * 4), second;
    for (uint32_t i = 0; i < w * h; i++)
        SetPixel(first, w, i % w, i / w, kZx[7]);
    second = first;
    SetPixel(second, w, 0, 0, kZx[13]);  // the corner changes, then stays
    const auto decoded = RoundTrip({first, second, second, second}, w, h, "repeat.gif");
    ASSERT_EQ(decoded.size(), 4u);
    for (size_t i = 1; i < 4; i++)
        EXPECT_TRUE(SameRgb(second, decoded[i])) << "frame " << i;
}

TEST(Gif_Test, ManyColorsTakeTheMedianSplitAndFewColorsAfterAreExactAgain)
{
    // More than 255 changed colors: the median-split palette (close, not exact); the next frame of few colors is exact
    const uint32_t w = 32, h = 32;
    std::vector<uint8_t> gradient(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
        {
            const uint8_t rgb[3] = {static_cast<uint8_t>(x * 8), static_cast<uint8_t>(y * 8),
                                    static_cast<uint8_t>((x ^ y) * 8)};
            SetPixel(gradient, w, x, y, rgb);
        }
    std::vector<uint8_t> zx(size_t(w) * h * 4);
    for (uint32_t i = 0; i < w * h; i++)
        SetPixel(zx, w, i % w, i / w, kZx[i % 16]);
    std::vector<int> tables;
    const auto decoded = RoundTrip({gradient, zx}, w, h, "many-colors.gif", &tables);
    ASSERT_EQ(decoded.size(), 2u);
    int worst = 0;
    for (size_t i = 0; i < gradient.size(); i++)
        if (i % 4 != 3)
            worst = std::max(worst, std::abs(int(gradient[i]) - int(decoded[0][i])));
    EXPECT_GT(worst, 0) << "1024 colors cannot all be in a palette of 256";
    EXPECT_LE(worst, 64);
    EXPECT_TRUE(SameRgb(zx, decoded[1]));
    ASSERT_EQ(tables.size(), 2u);
    EXPECT_EQ(tables[0], 256);
    EXPECT_EQ(tables[1], 16) << "15 colors (black and bright black are one) and the transparent index: 4 bits";
}

TEST(Gif_Test, TheCodeSizeIsTheFewestBitsForTheChangedColors)
{
    // Two colors and the transparent index fit 2 bits (the GIF minimum); one more color does not
    const uint32_t w = 8, h = 8;
    std::vector<uint8_t> two(size_t(w) * h * 4), three;
    for (uint32_t i = 0; i < w * h; i++)
        SetPixel(two, w, i % w, i / w, kZx[(i & 1) ? 15 : 1]);
    three = two;
    SetPixel(three, w, 3, 3, kZx[2]);
    SetPixel(three, w, 4, 4, kZx[4]);
    SetPixel(three, w, 5, 5, kZx[6]);
    SetPixel(three, w, 6, 6, kZx[10]);
    std::vector<int> tables;
    const auto decoded = RoundTrip({two, three}, w, h, "code-size.gif", &tables);
    ASSERT_EQ(decoded.size(), 2u);
    EXPECT_TRUE(SameRgb(three, decoded[1]));
    ASSERT_EQ(tables.size(), 2u);
    EXPECT_EQ(tables[0], 4) << "2 colors + transparent";
    EXPECT_EQ(tables[1], 8) << "4 changed colors + transparent";
}
