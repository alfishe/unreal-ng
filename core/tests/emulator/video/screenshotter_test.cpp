#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/displaygeometry.h"
#include "emulator/video/screen.h"
#include "emulator/video/screenshotter.h"

/// The screenshotter: what it returns for a given frame snapshot, how it fails, and that every
/// answer carries the geometry it was cut from (docs/inprogress/2026-10-03-screenshotter/design.md).
/// Frames here are synthetic, so the expected pixels are known exactly (in the CRT's 11:9 shape, so they come back
/// pixel for pixel; the hi-res region checks the shaping); the machine tests at the end
/// check the real frames of a Spectrum and of an FT812 picture.

namespace
{
/// A frame where pixel (x, y) is {x, y, x ^ y, 255}: every pixel tells where it came from
FrameSnapshot Gradient(uint16_t width, uint16_t height, PictureRect window)
{
    FrameSnapshot s;
    s.geometry.width = width;
    s.geometry.height = height;
    s.geometry.stride = static_cast<uint32_t>(width) * 4;
    s.geometry.screenWindow = window;
    s.geometry.frameNumber = 7;
    s.pixels.resize(static_cast<size_t>(s.geometry.stride) * height);
    for (uint16_t y = 0; y < height; y++)
    {
        for (uint16_t x = 0; x < width; x++)
        {
            uint8_t* p = &s.pixels[(static_cast<size_t>(y) * width + x) * 4];
            p[0] = static_cast<uint8_t>(x);
            p[1] = static_cast<uint8_t>(y);
            p[2] = static_cast<uint8_t>(x ^ y);
            p[3] = 255;
        }
    }
    return s;
}

/// Decode a PNG back to RGBA
bool DecodePng(const std::vector<uint8_t>& png, unsigned& width, unsigned& height, std::vector<uint8_t>& rgba)
{
    return lodepng::decode(rgba, width, height, png) == 0;
}

ScreenshotOptions Options(ScreenshotArea area, ScreenshotFormat format = ScreenshotFormat::Png)
{
    ScreenshotOptions o;
    o.area = area;
    o.format = format;
    return o;
}
}  // namespace

/// region <Crop and encode>

TEST(Screenshotter_Test, FullFrameIsReturnedAsIsAndSaysWhatItIs)
{
    const FrameSnapshot frame = Gradient(22, 18, PictureRect{4, 2, 8, 6});
    const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Full));
    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(r.width, 22);
    EXPECT_EQ(r.height, 18);
    EXPECT_EQ(r.crop.x, 0);
    EXPECT_EQ(r.crop.y, 0);
    EXPECT_EQ(r.crop.width, 22);
    EXPECT_EQ(r.crop.height, 18);
    EXPECT_EQ(r.frame.width, 22);
    EXPECT_EQ(r.frame.screenWindow.width, 8) << "the window is reported next to the pixels";
    EXPECT_EQ(r.frame.frameNumber, 7u);

    unsigned w = 0, h = 0;
    std::vector<uint8_t> rgba;
    ASSERT_TRUE(DecodePng(r.bytes, w, h, rgba));
    EXPECT_EQ(w, 22u);
    EXPECT_EQ(h, 18u);
    EXPECT_EQ(rgba, frame.pixels) << "lossless: every pixel";
}

TEST(Screenshotter_Test, ScreenAreaIsExactlyTheFramesWindow)
{
    const FrameSnapshot frame = Gradient(22, 18, PictureRect{4, 2, 8, 6});
    const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Screen));
    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(r.width, 8);
    EXPECT_EQ(r.height, 6);
    EXPECT_EQ(r.crop.x, 4);
    EXPECT_EQ(r.crop.y, 2);

    unsigned w = 0, h = 0;
    std::vector<uint8_t> rgba;
    ASSERT_TRUE(DecodePng(r.bytes, w, h, rgba));
    ASSERT_EQ(w, 8u);
    ASSERT_EQ(h, 6u);
    for (unsigned y = 0; y < h; y++)
    {
        for (unsigned x = 0; x < w; x++)
        {
            const uint8_t* p = &rgba[(y * w + x) * 4];
            ASSERT_EQ(p[0], 4 + x) << "x " << x << " y " << y;
            ASSERT_EQ(p[1], 2 + y) << "x " << x << " y " << y;
        }
    }
}

TEST(Screenshotter_Test, WindowThatIsTheWholeFrameMakesFullAndScreenTheSamePicture)
{
    // Hires modes: the working picture can be the whole frame, and then there is nothing to cut
    const FrameSnapshot frame = Gradient(22, 18, PictureRect{0, 0, 22, 18});
    const ScreenshotResult full = Screenshotter::Render(frame, Options(ScreenshotArea::Full));
    const ScreenshotResult screen = Screenshotter::Render(frame, Options(ScreenshotArea::Screen));
    ASSERT_TRUE(full.ok && screen.ok);
    EXPECT_EQ(full.bytes, screen.bytes);
}

TEST(Screenshotter_Test, WindowOutsideTheFrameIsAnErrorNamingTheSizes)
{
    for (const PictureRect window : {PictureRect{15, 0, 8, 5}, PictureRect{0, 16, 4, 4}, PictureRect{0, 0, 0, 0}})
    {
        const FrameSnapshot frame = Gradient(22, 18, window);
        const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Screen));
        EXPECT_FALSE(r.ok);
        EXPECT_EQ(r.error, ScreenshotError::BadGeometry);
        EXPECT_NE(r.errorMessage.find("22x18"), std::string::npos) << r.errorMessage;
        EXPECT_TRUE(r.bytes.empty()) << "never a silent substitute picture";
    }
    // The whole frame still works for the same snapshot
    EXPECT_TRUE(Screenshotter::Render(Gradient(22, 18, PictureRect{0, 0, 0, 0}), Options(ScreenshotArea::Full)).ok);
}

TEST(Screenshotter_Test, PixelsThatDoNotMatchTheirGeometryAreNoFrame)
{
    FrameSnapshot frame = Gradient(22, 18, PictureRect{0, 0, 22, 18});
    frame.pixels.resize(frame.pixels.size() - 4);
    EXPECT_EQ(Screenshotter::Render(frame, Options(ScreenshotArea::Full)).error, ScreenshotError::NoFrame);
    EXPECT_EQ(Screenshotter::Render(FrameSnapshot{}, Options(ScreenshotArea::Full)).error, ScreenshotError::NoFrame);
}

/// endregion </Crop and encode>

/// region <The CRT's shape>

/// A hi-res frame (Sprinter 736x288, TS-Conf 720x288) stores one line per TV line: the screenshot shows each line
/// twice, as the window and a recording do, and every pixel is still exact
TEST(Screenshotter_Test, HiResFrameDoublesItsLines)
{
    for (const auto& size : {std::pair<uint16_t, uint16_t>{736, 288}, std::pair<uint16_t, uint16_t>{720, 288}})
    {
        SCOPED_TRACE(testing::Message() << size.first << "x" << size.second);
        const FrameSnapshot frame = Gradient(size.first, size.second, PictureRect{0, 0, size.first, size.second});
        for (const ScreenshotArea area : {ScreenshotArea::Full, ScreenshotArea::Screen})
        {
            const ScreenshotResult r = Screenshotter::Render(frame, Options(area));
            ASSERT_TRUE(r.ok) << r.errorMessage;
            EXPECT_EQ(r.width, size.first);
            EXPECT_EQ(r.height, size.second * 2);
            EXPECT_EQ(r.crop.height, size.second) << "the crop stays in stored pixels";

            unsigned w = 0, h = 0;
            std::vector<uint8_t> rgba;
            ASSERT_TRUE(DecodePng(r.bytes, w, h, rgba));
            ASSERT_EQ(h, size.second * 2u);
            for (unsigned y = 0; y < h; y++)
                ASSERT_EQ(0, std::memcmp(&rgba[static_cast<size_t>(y) * w * 4],
                                         &frame.pixels[static_cast<size_t>(y / 2) * frame.geometry.stride], w * 4))
                    << "line " << y << " is stored line " << y / 2;
        }
    }
}

/// A working window takes its share of the CRT's frame: the ATM / Profi hi-res 640x200 of a 640x288 frame is not
/// within 5% of any line repeat, so it grows to its shape (1.76) instead of coming out 10% too narrow
TEST(Screenshotter_Test, HiResWindowTakesItsShapeOnTheCrt)
{
    const FrameSnapshot frame = Gradient(640, 288, PictureRect{0, 44, 640, 200});
    const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Screen));
    ASSERT_TRUE(r.ok) << r.errorMessage;
    const DisplayGeometry::Size expected =
        DisplayGeometry::Picture(640, 200, DisplayGeometry::WindowAspect(640, 288, 640, 200));
    EXPECT_EQ(r.width, expected.width);
    EXPECT_EQ(r.height, expected.height);
    EXPECT_NEAR(static_cast<double>(r.width) / r.height, 1.76, 0.02);

    const ScreenshotResult full = Screenshotter::Render(frame, Options(ScreenshotArea::Full));
    ASSERT_TRUE(full.ok);
    EXPECT_EQ(full.width, 640);
    EXPECT_EQ(full.height, 524) << "the whole 640x288 frame takes the CRT's 352:288 (doubled it would be 10% too flat)";
}

/// Frames already in the CRT's shape stay as they are: a Spectrum's 352x288, the Next's 640x512
TEST(Screenshotter_Test, FrameInTheCrtShapeStaysAsItIs)
{
    for (const auto& size : {std::pair<uint16_t, uint16_t>{352, 288}, std::pair<uint16_t, uint16_t>{640, 512}})
    {
        const FrameSnapshot frame = Gradient(size.first, size.second, PictureRect{0, 0, size.first, size.second});
        const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Full));
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.width, size.first);
        EXPECT_EQ(r.height, size.second);
    }
}

/// A video card's picture (the FT812's VGA modes) has square pixels of its own: never reshaped
TEST(Screenshotter_Test, ExternalPictureKeepsItsSize)
{
    FrameSnapshot frame = Gradient(640, 240, PictureRect{0, 0, 640, 240});
    frame.geometry.source = FrameSource::External;
    const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Full));
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.width, 640);
    EXPECT_EQ(r.height, 240);
}

/// endregion </The CRT's shape>

/// region <GIF>

TEST(Screenshotter_Test, GifIsAGifOfTheRightSize)
{
    const FrameSnapshot frame = Gradient(22, 18, PictureRect{4, 2, 8, 6});
    const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Screen, ScreenshotFormat::Gif));
    ASSERT_TRUE(r.ok) << r.errorMessage;
    ASSERT_GT(r.bytes.size(), 13u);
    EXPECT_EQ(std::string(r.bytes.begin(), r.bytes.begin() + 4), "GIF8");
    EXPECT_EQ(r.bytes[6] | (r.bytes[7] << 8), 8) << "logical screen width";
    EXPECT_EQ(r.bytes[8] | (r.bytes[9] << 8), 6) << "logical screen height";
    EXPECT_EQ(r.format, ScreenshotFormat::Gif);
}

#ifndef _WIN32
/// The GIF is written through a temp file: a temp folder whose name is not ASCII (a Cyrillic or CJK user name on
/// Windows; here TMPDIR) must work, and the temp file must be gone afterwards
TEST(Screenshotter_Test, GifWorksWithANonAsciiTempFolderAndLeavesNothingBehind)
{
    const std::string folder = TestPathHelper::GetUniqueTestScratchPath("времянка-临时/placeholder");
    const std::filesystem::path dir = FileHelper::ToFsPath(folder).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const char* oldTmp = std::getenv("TMPDIR");
    const std::string saved = oldTmp ? oldTmp : "";
    const auto u8 = dir.u8string();
    setenv("TMPDIR", std::string(u8.begin(), u8.end()).c_str(), 1);

    const FrameSnapshot frame = Gradient(22, 18, PictureRect{4, 2, 8, 6});
    const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Full, ScreenshotFormat::Gif));

    if (oldTmp)
        setenv("TMPDIR", saved.c_str(), 1);
    else
        unsetenv("TMPDIR");

    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(std::string(r.bytes.begin(), r.bytes.begin() + 4), "GIF8");
    size_t left = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        left += entry.path().extension() == ".gif" ? 1 : 0;
    EXPECT_EQ(left, 0u) << "the temp GIF is removed";
}
#endif

/// The GIF writer wants a file. The old temp name came from the buffer address, so two captures of the
/// same buffer shared it; here many threads encode the same frame at once and each gets its own file
TEST(Screenshotter_Test, ConcurrentGifEncodesDoNotShareATempFile)
{
    const FrameSnapshot frame = Gradient(66, 54, PictureRect{0, 0, 66, 54});
    const ScreenshotResult reference = Screenshotter::Render(frame, Options(ScreenshotArea::Full, ScreenshotFormat::Gif));
    ASSERT_TRUE(reference.ok);

    std::atomic<int> bad{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++)
    {
        threads.emplace_back([&] {
            for (int i = 0; i < 6; i++)
            {
                const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Full, ScreenshotFormat::Gif));
                if (!r.ok || r.bytes != reference.bytes)
                    bad++;
            }
        });
    }
    for (auto& th : threads)
        th.join();
    EXPECT_EQ(bad.load(), 0);
}

/// endregion </GIF>

/// region <Saving to a file>

TEST(Screenshotter_Test, SaveToWritesTheSameBytesAndCreatesTheFolders)
{
    const FrameSnapshot frame = Gradient(22, 18, PictureRect{4, 2, 8, 6});
    const ScreenshotResult inMemory = Screenshotter::Render(frame, Options(ScreenshotArea::Screen));
    ASSERT_TRUE(inMemory.ok);

    ScreenshotOptions options = Options(ScreenshotArea::Screen);
    options.saveTo = TestPathHelper::GetUniqueTestScratchPath("screenshotter/deeper/shot.png");
    std::filesystem::remove_all(std::filesystem::path(options.saveTo).parent_path().parent_path());
    const ScreenshotResult saved = Screenshotter::Render(frame, options);
    ASSERT_TRUE(saved.ok) << saved.errorMessage;
    // The reply names the file by its absolute path (a relative saveTo resolves against the emulator's directory)
    EXPECT_TRUE(std::filesystem::path(saved.savedFile).is_absolute()) << saved.savedFile;
    EXPECT_EQ(std::filesystem::path(saved.savedFile), std::filesystem::absolute(options.saveTo).lexically_normal());
    EXPECT_TRUE(saved.bytes.empty()) << "saved to a file: the bytes are not also returned";
    EXPECT_EQ(saved.encodedSize, inMemory.bytes.size());

    std::ifstream in(options.saveTo, std::ios::binary);
    std::vector<uint8_t> onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(onDisk, inMemory.bytes);
}

TEST(Screenshotter_Test, UnwritablePathIsAnIoErrorWithThePath)
{
    // A file where the folder should be
    const std::string blocker = TestPathHelper::GetUniqueTestScratchPath("screenshotter/blocker.txt");
    std::ofstream(blocker) << "x";
    ScreenshotOptions options = Options(ScreenshotArea::Full);
    options.saveTo = blocker + "/shot.png";
    const ScreenshotResult r = Screenshotter::Render(Gradient(8, 8, PictureRect{0, 0, 8, 8}), options);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error, ScreenshotError::IoFailed);
    EXPECT_NE(r.errorMessage.find("shot.png"), std::string::npos) << r.errorMessage;
}

/// endregion </Saving to a file>

/// region <Parameters and helpers>

TEST(Screenshotter_Test, ParametersAreStrict)
{
    ScreenshotArea area = ScreenshotArea::Full;
    EXPECT_TRUE(Screenshotter::ParseArea("screen", area));
    EXPECT_EQ(area, ScreenshotArea::Screen);
    EXPECT_TRUE(Screenshotter::ParseArea("full", area));
    EXPECT_EQ(area, ScreenshotArea::Full);
    for (const char* bad : {"", "FULL", "Screen", "256x192", "border", "1,2,3,4"})
        EXPECT_FALSE(Screenshotter::ParseArea(bad, area)) << bad;

    ScreenshotFormat format = ScreenshotFormat::Png;
    EXPECT_TRUE(Screenshotter::ParseFormat("gif", format));
    EXPECT_EQ(format, ScreenshotFormat::Gif);
    EXPECT_TRUE(Screenshotter::ParseFormat("png", format));
    EXPECT_EQ(format, ScreenshotFormat::Png);
    for (const char* bad : {"", "PNG", "bmp", "jpeg", "rgba"})
        EXPECT_FALSE(Screenshotter::ParseFormat(bad, format)) << bad;
}

TEST(Screenshotter_Test, RequestWordsDefaultToTheWholeFrameAsPng)
{
    ScreenshotOptions options;
    std::string message;
    ASSERT_TRUE(Screenshotter::ParseRequestWords("", "", "", "", options, message)) << message;
    EXPECT_EQ(options.area, ScreenshotArea::Full);
    EXPECT_EQ(options.format, ScreenshotFormat::Png);
}

TEST(Screenshotter_Test, RequestWordsAreTakenAsGiven)
{
    ScreenshotOptions options;
    std::string message;
    ASSERT_TRUE(Screenshotter::ParseRequestWords("screen", "", "gif", "", options, message)) << message;
    EXPECT_EQ(options.area, ScreenshotArea::Screen);
    EXPECT_EQ(options.format, ScreenshotFormat::Gif);
}

TEST(Screenshotter_Test, ModeIsADeprecatedAliasOfArea)
{
    for (const char* word : {"full", "screen"})
    {
        ScreenshotOptions viaMode, viaArea;
        std::string message;
        ASSERT_TRUE(Screenshotter::ParseRequestWords("", word, "", "", viaMode, message)) << message;
        ASSERT_TRUE(Screenshotter::ParseRequestWords(word, "", "", "", viaArea, message)) << message;
        EXPECT_EQ(viaMode.area, viaArea.area) << word;
        // The same word in both is not a conflict
        ScreenshotOptions both;
        EXPECT_TRUE(Screenshotter::ParseRequestWords(word, word, "", "", both, message)) << message;
    }
}

TEST(Screenshotter_Test, AreaAndModeThatDisagreeAreAnErrorNamingBoth)
{
    ScreenshotOptions options;
    std::string message;
    EXPECT_FALSE(Screenshotter::ParseRequestWords("full", "screen", "", "", options, message));
    EXPECT_NE(message.find("area=full"), std::string::npos) << message;
    EXPECT_NE(message.find("mode=screen"), std::string::npos) << message;
}

TEST(Screenshotter_Test, BadRequestWordsNameTheWordAndTheAllowedOnes)
{
    struct Bad
    {
        const char *area, *mode, *format, *mentions, *allowed;
    };
    const Bad cases[] = {
        {"border", "", "", "'border'", "full or screen"},
        {"", "256x192", "", "'256x192'", "full or screen"},
        {"", "", "bmp", "'bmp'", "png or gif"},
        {"FULL", "", "", "'FULL'", "full or screen"},
    };
    for (const Bad& c : cases)
    {
        ScreenshotOptions options;
        std::string message;
        EXPECT_FALSE(Screenshotter::ParseRequestWords(c.area, c.mode, c.format, "", options, message)) << c.mentions;
        EXPECT_NE(message.find(c.mentions), std::string::npos) << message;
        EXPECT_NE(message.find(c.allowed), std::string::npos) << message;
    }
}

TEST(Screenshotter_Test, SourceWordsArePresentedLiveOrWindow)
{
    ScreenshotOptions options;
    std::string message;
    ASSERT_TRUE(Screenshotter::ParseRequestWords("", "", "", "", options, message)) << message;
    EXPECT_EQ(options.source, ScreenshotSource::Presented) << "the default";
    ASSERT_TRUE(Screenshotter::ParseRequestWords("", "", "", "live", options, message)) << message;
    EXPECT_EQ(options.source, ScreenshotSource::Live);
    ASSERT_TRUE(Screenshotter::ParseRequestWords("", "", "", "presented", options, message)) << message;
    EXPECT_EQ(options.source, ScreenshotSource::Presented);
    ASSERT_TRUE(Screenshotter::ParseRequestWords("", "", "", "window", options, message)) << message;
    EXPECT_EQ(options.source, ScreenshotSource::Window);
    EXPECT_STREQ(Screenshotter::RequestSourceName(ScreenshotSource::Window), "window");
    for (const char* bad : {"Live", "now", "latest", "1", "Window"})
    {
        EXPECT_FALSE(Screenshotter::ParseRequestWords("", "", "", bad, options, message)) << bad;
        EXPECT_NE(message.find("presented, live or window"), std::string::npos) << message;
    }
}

TEST(Screenshotter_Test, Base64MatchesTheStandardVectors)
{
    auto b64 = [](const char* text) {
        const std::string s(text);
        return Screenshotter::Base64Encode(std::vector<uint8_t>(s.begin(), s.end()));
    };
    EXPECT_EQ(b64(""), "");
    EXPECT_EQ(b64("f"), "Zg==");
    EXPECT_EQ(b64("fo"), "Zm8=");
    EXPECT_EQ(b64("foo"), "Zm9v");
    EXPECT_EQ(b64("foob"), "Zm9vYg==");
    EXPECT_EQ(b64("fooba"), "Zm9vYmE=");
    EXPECT_EQ(b64("foobar"), "Zm9vYmFy");
}

TEST(Screenshotter_Test, UnknownEmulatorIsNotFound)
{
    const ScreenshotResult r = Screenshotter::Take("no-such-emulator", Options(ScreenshotArea::Full));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error, ScreenshotError::NotFound);
    EXPECT_NE(r.errorMessage.find("no-such-emulator"), std::string::npos);
}

/// endregion </Parameters and helpers>

/// region <Real frames>

class ScreenshotterMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Screen* _screen = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _screen = _emulator->GetContext()->pScreen;
        ASSERT_NE(_screen, nullptr);
        _screen->SetPresentDelayFrames(0);
        _emulator->RunNFrames(3);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

TEST_F(ScreenshotterMachine_Test, SpectrumFullIsTheWholeFrameAndScreenIsThePaper)
{
    const ScreenshotResult full = Screenshotter::TakeFrom(*_screen, Options(ScreenshotArea::Full));
    ASSERT_TRUE(full.ok) << full.errorMessage;
    EXPECT_EQ(full.width, 352);
    EXPECT_EQ(full.height, 288);

    const ScreenshotResult screen = Screenshotter::TakeFrom(*_screen, Options(ScreenshotArea::Screen));
    ASSERT_TRUE(screen.ok) << screen.errorMessage;
    EXPECT_EQ(screen.width, 256);
    EXPECT_EQ(screen.height, 192);
    EXPECT_EQ(screen.crop.x, 48);
    EXPECT_EQ(screen.crop.y, 48);
}

/// The case the old crop got wrong: a picture that is not a Spectrum screen. Whatever its size, the
/// default is all of it, and the screen area is all of it too (an FT812 picture has no border)
TEST_F(ScreenshotterMachine_Test, ExternalPictureIsNotCroppedToASpectrumScreen)
{
    for (const auto& size : {std::pair<int, int>{1024, 768}, std::pair<int, int>{40, 20}})
    {
        SCOPED_TRACE(testing::Message() << size.first << "x" << size.second);
        std::vector<uint8_t> picture(static_cast<size_t>(size.first) * size.second * 4, 0x7F);
        _screen->SetExternalPicture(picture.data(), static_cast<uint16_t>(size.first),
                                    static_cast<uint16_t>(size.second), 20000);
        _screen->LatchExternalFrame();

        for (const ScreenshotArea area : {ScreenshotArea::Full, ScreenshotArea::Screen})
        {
            const ScreenshotResult r = Screenshotter::TakeFrom(*_screen, Options(area));
            ASSERT_TRUE(r.ok) << r.errorMessage;
            EXPECT_EQ(r.width, size.first);
            EXPECT_EQ(r.height, size.second);
            EXPECT_EQ(r.frame.source, FrameSource::External);
        }
        _screen->ClearExternalPicture();
    }
}

/// The live frame is what the buffer holds now; the presented one is the last latched, a few frames old
TEST_F(ScreenshotterMachine_Test, LiveFrameOfAParkedMachineIsReadDirectlyAndNotTheLatchedOne)
{
    const FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
    ASSERT_NE(fb.memoryBuffer, nullptr);
    const uint32_t marker = 0xFF123456u;
    reinterpret_cast<uint32_t*>(fb.memoryBuffer)[0] = marker;  // drawn after the last latch

    ScreenshotOptions live = Options(ScreenshotArea::Full);
    live.source = ScreenshotSource::Live;
    const ScreenshotResult now = Screenshotter::TakeFrom(*_screen, live, /*emulationParked=*/true);
    ASSERT_TRUE(now.ok) << now.errorMessage;
    EXPECT_EQ(now.width, 352);

    FrameSnapshot presented;
    ASSERT_TRUE(_screen->SnapshotPresented(presented));
    EXPECT_NE(reinterpret_cast<const uint32_t*>(presented.pixels.data())[0], marker)
        << "the presented frame was latched before the marker was drawn";

    FrameSnapshot liveSnap;
    ASSERT_TRUE(_screen->SnapshotLive(liveSnap, true, 100));
    EXPECT_EQ(reinterpret_cast<const uint32_t*>(liveSnap.pixels.data())[0], marker);
    EXPECT_GE(liveSnap.geometry.beamLine, 0) << "a stopped machine says where the beam stood";
    EXPECT_GE(liveSnap.geometry.beamTstate, 0);
}

/// A running machine: the caller's thread never touches the buffer. The emulation thread serves the request at its
/// frame end (here a thread that calls ServeLiveRequest the way MainLoop does, once per frame)
TEST_F(ScreenshotterMachine_Test, LiveFrameOfARunningMachineIsServedByTheEmulationThread)
{
    const FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
    const uint32_t marker = 0xFF654321u;
    reinterpret_cast<uint32_t*>(fb.memoryBuffer)[0] = marker;

    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load())
        {
            _screen->ServeLiveRequest();
            std::this_thread::yield();
        }
    });

    FrameSnapshot snap;
    const bool ok = _screen->SnapshotLive(snap, /*emulationParked=*/false, 5000);
    stop = true;
    emulation.join();

    ASSERT_TRUE(ok) << "the request was not served";
    EXPECT_EQ(reinterpret_cast<const uint32_t*>(snap.pixels.data())[0], marker);
    EXPECT_EQ(snap.geometry.beamLine, -1) << "served at a frame end: the frame is finished, no beam to report";
    EXPECT_FALSE(snap.geometry.partial);
}

TEST_F(ScreenshotterMachine_Test, LiveFrameThatNeverComesIsAnErrorNotAHang)
{
    // Nobody serves the request: no emulation thread, as with a stuck emulator
    ScreenshotOptions live = Options(ScreenshotArea::Full);
    live.source = ScreenshotSource::Live;
    live.liveTimeoutMs = 30;
    const ScreenshotResult r = Screenshotter::TakeFrom(*_screen, live, /*emulationParked=*/false);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error, ScreenshotError::NoFrame);
    EXPECT_NE(r.errorMessage.find("30 ms"), std::string::npos) << r.errorMessage;

    // The abandoned request does not leak into the next one
    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load())
        {
            _screen->ServeLiveRequest();
            std::this_thread::yield();
        }
    });
    live.liveTimeoutMs = 5000;
    const ScreenshotResult again = Screenshotter::TakeFrom(*_screen, live, false);
    stop = true;
    emulation.join();
    EXPECT_TRUE(again.ok) << again.errorMessage;
}

TEST_F(ScreenshotterMachine_Test, ConcurrentLiveRequestsAreServedOneByOne)
{
    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load())
        {
            _screen->ServeLiveRequest();
            std::this_thread::yield();
        }
    });
    std::atomic<int> good{0};
    std::vector<std::thread> callers;
    for (int i = 0; i < 6; i++)
    {
        callers.emplace_back([&] {
            FrameSnapshot snap;
            if (_screen->SnapshotLive(snap, false, 5000) && snap.geometry.width == 352 && snap.pixels.size() == 352u * 288u * 4u)
                good++;
        });
    }
    for (auto& c : callers)
        c.join();
    stop = true;
    emulation.join();
    EXPECT_EQ(good.load(), 6);
}

/// region <Window source>

namespace
{
/// A front end's window as the tests draw it: a picture of known pixels, shown for one emulator id
class FakeWindowView : public IWindowViewProvider
{
public:
    std::string shownId;
    std::string lastAskedId;

    bool GrabWindowView(const std::string& emulatorId, WindowViewGrab& out, std::string& error) override
    {
        lastAskedId = emulatorId;
        if (emulatorId != shownId)
        {
            error = "The window shows another emulator";
            return false;
        }
        out.width = 6;
        out.height = 4;
        out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0x40);
        out.viewport = PictureRect{0, 0, 8, 4};
        out.picture = PictureRect{1, 0, 6, 4};
        out.devicePixelRatio = 1.0;
        out.backend = "fake";
        return true;
    }

    // The display settings are windowdisplay_test.cpp's
    bool GetWindowDisplay(WindowDisplaySettings&, std::string& error) override
    {
        error = "not in this test";
        return false;
    }
    bool SetWindowDisplay(const WindowDisplayChange&, WindowDisplaySettings&, std::string& error) override
    {
        error = "not in this test";
        return false;
    }
};

/// Registers a provider for one test, and always takes it away again: the registry is process-wide
struct ScopedWindowView
{
    explicit ScopedWindowView(std::shared_ptr<IWindowViewProvider> provider) { WindowView::SetProvider(std::move(provider)); }
    ~ScopedWindowView() { WindowView::SetProvider(nullptr); }
};

ScreenshotOptions WindowOptions(ScreenshotArea area = ScreenshotArea::Full)
{
    ScreenshotOptions o = Options(area);
    o.source = ScreenshotSource::Window;
    return o;
}
}  // namespace

/// Headless (no front end registered): the source says there is no window, it never falls back to the frame
TEST_F(ScreenshotterMachine_Test, WindowSourceWithoutAFrontEndIsNoWindow)
{
    WindowView::SetProvider(nullptr);
    const ScreenshotResult r = Screenshotter::TakeFrom(*_screen, WindowOptions());
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error, ScreenshotError::NoWindow);
    EXPECT_STREQ(Screenshotter::ErrorName(r.error), "no-window");
}

/// The front end's picture comes back as it drew it, with the window's geometry and the frame it shows
TEST_F(ScreenshotterMachine_Test, WindowSourceIsThePictureTheFrontEndDrew)
{
    auto view = std::make_shared<FakeWindowView>();
    view->shownId = _emulator->GetId();
    ScopedWindowView registered(view);

    const ScreenshotResult r = Screenshotter::Take(_emulator->GetId(), WindowOptions());
    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(view->lastAskedId, _emulator->GetId()) << "the provider is asked for this emulator";
    EXPECT_EQ(r.width, 6);
    EXPECT_EQ(r.height, 4);
    ASSERT_TRUE(r.fromWindow);
    EXPECT_EQ(r.window.backend, "fake");
    EXPECT_EQ(r.window.viewport.width, 8);
    EXPECT_EQ(r.window.picture.x, 1);
    EXPECT_TRUE(r.window.rgba.empty()) << "the pixels are in bytes only";
    EXPECT_EQ(r.frame.width, 352) << "the frame the window shows";

    unsigned w = 0, h = 0;
    std::vector<uint8_t> rgba;
    ASSERT_TRUE(DecodePng(r.bytes, w, h, rgba));
    EXPECT_EQ(w, 6u);
    EXPECT_EQ(h, 4u);
    EXPECT_EQ(rgba[0], 0x40);
}

/// Another instance in the window, or a part of the picture: errors, not a wrong image
TEST_F(ScreenshotterMachine_Test, WindowSourceRefusesAnotherInstanceAndTheScreenArea)
{
    auto view = std::make_shared<FakeWindowView>();
    view->shownId = "some-other-instance";
    ScopedWindowView registered(view);

    const ScreenshotResult other = Screenshotter::TakeFrom(*_screen, WindowOptions());
    EXPECT_FALSE(other.ok);
    EXPECT_EQ(other.error, ScreenshotError::NoWindow);
    EXPECT_NE(other.errorMessage.find("another emulator"), std::string::npos) << other.errorMessage;

    view->shownId = _emulator->GetId();
    const ScreenshotResult area = Screenshotter::TakeFrom(*_screen, WindowOptions(ScreenshotArea::Screen));
    EXPECT_FALSE(area.ok);
    EXPECT_EQ(area.error, ScreenshotError::BadParameter);
}

/// endregion </Window source>

/// endregion </Real frames>
