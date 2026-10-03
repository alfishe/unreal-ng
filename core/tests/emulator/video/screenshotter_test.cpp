#include "stdafx.h"
#include "pch.h"

#include <fstream>
#include <thread>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "emulator/video/screenshotter.h"

/// The screenshotter: what it returns for a given frame snapshot, how it fails, and that every
/// answer carries the geometry it was cut from (docs/inprogress/2026-10-03-screenshotter/design.md).
/// Frames here are synthetic, so the expected pixels are known exactly; the machine tests at the end
/// check the real frames of a Spectrum and of an FT812 picture.

namespace
{
/// A frame where pixel (x, y) is {x, y, x ^ y, 255}: every pixel tells where it came from
FrameSnapshot Gradient(uint16_t width, uint16_t height, FrameRect window)
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
    const FrameSnapshot frame = Gradient(20, 10, FrameRect{4, 2, 8, 5});
    const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Full));
    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(r.width, 20);
    EXPECT_EQ(r.height, 10);
    EXPECT_EQ(r.crop.x, 0);
    EXPECT_EQ(r.crop.y, 0);
    EXPECT_EQ(r.crop.width, 20);
    EXPECT_EQ(r.crop.height, 10);
    EXPECT_EQ(r.frame.width, 20);
    EXPECT_EQ(r.frame.screenWindow.width, 8) << "the window is reported next to the pixels";
    EXPECT_EQ(r.frame.frameNumber, 7u);

    unsigned w = 0, h = 0;
    std::vector<uint8_t> rgba;
    ASSERT_TRUE(DecodePng(r.bytes, w, h, rgba));
    EXPECT_EQ(w, 20u);
    EXPECT_EQ(h, 10u);
    EXPECT_EQ(rgba, frame.pixels) << "lossless: every pixel";
}

TEST(Screenshotter_Test, ScreenAreaIsExactlyTheFramesWindow)
{
    const FrameSnapshot frame = Gradient(20, 10, FrameRect{4, 2, 8, 5});
    const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Screen));
    ASSERT_TRUE(r.ok) << r.errorMessage;
    EXPECT_EQ(r.width, 8);
    EXPECT_EQ(r.height, 5);
    EXPECT_EQ(r.crop.x, 4);
    EXPECT_EQ(r.crop.y, 2);

    unsigned w = 0, h = 0;
    std::vector<uint8_t> rgba;
    ASSERT_TRUE(DecodePng(r.bytes, w, h, rgba));
    ASSERT_EQ(w, 8u);
    ASSERT_EQ(h, 5u);
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
    const FrameSnapshot frame = Gradient(16, 8, FrameRect{0, 0, 16, 8});
    const ScreenshotResult full = Screenshotter::Render(frame, Options(ScreenshotArea::Full));
    const ScreenshotResult screen = Screenshotter::Render(frame, Options(ScreenshotArea::Screen));
    ASSERT_TRUE(full.ok && screen.ok);
    EXPECT_EQ(full.bytes, screen.bytes);
}

TEST(Screenshotter_Test, WindowOutsideTheFrameIsAnErrorNamingTheSizes)
{
    for (const FrameRect window : {FrameRect{15, 0, 8, 5}, FrameRect{0, 8, 4, 4}, FrameRect{0, 0, 0, 0}})
    {
        const FrameSnapshot frame = Gradient(20, 10, window);
        const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Screen));
        EXPECT_FALSE(r.ok);
        EXPECT_EQ(r.error, ScreenshotError::BadGeometry);
        EXPECT_NE(r.errorMessage.find("20x10"), std::string::npos) << r.errorMessage;
        EXPECT_TRUE(r.bytes.empty()) << "never a silent substitute picture";
    }
    // The whole frame still works for the same snapshot
    EXPECT_TRUE(Screenshotter::Render(Gradient(20, 10, FrameRect{0, 0, 0, 0}), Options(ScreenshotArea::Full)).ok);
}

TEST(Screenshotter_Test, PixelsThatDoNotMatchTheirGeometryAreNoFrame)
{
    FrameSnapshot frame = Gradient(20, 10, FrameRect{0, 0, 20, 10});
    frame.pixels.resize(frame.pixels.size() - 4);
    EXPECT_EQ(Screenshotter::Render(frame, Options(ScreenshotArea::Full)).error, ScreenshotError::NoFrame);
    EXPECT_EQ(Screenshotter::Render(FrameSnapshot{}, Options(ScreenshotArea::Full)).error, ScreenshotError::NoFrame);
}

/// endregion </Crop and encode>

/// region <GIF>

TEST(Screenshotter_Test, GifIsAGifOfTheRightSize)
{
    const FrameSnapshot frame = Gradient(20, 10, FrameRect{4, 2, 8, 5});
    const ScreenshotResult r = Screenshotter::Render(frame, Options(ScreenshotArea::Screen, ScreenshotFormat::Gif));
    ASSERT_TRUE(r.ok) << r.errorMessage;
    ASSERT_GT(r.bytes.size(), 13u);
    EXPECT_EQ(std::string(r.bytes.begin(), r.bytes.begin() + 4), "GIF8");
    EXPECT_EQ(r.bytes[6] | (r.bytes[7] << 8), 8) << "logical screen width";
    EXPECT_EQ(r.bytes[8] | (r.bytes[9] << 8), 5) << "logical screen height";
    EXPECT_EQ(r.format, ScreenshotFormat::Gif);
}

/// The GIF writer wants a file. The old temp name came from the buffer address, so two captures of the
/// same buffer shared it; here many threads encode the same frame at once and each gets its own file
TEST(Screenshotter_Test, ConcurrentGifEncodesDoNotShareATempFile)
{
    const FrameSnapshot frame = Gradient(64, 48, FrameRect{0, 0, 64, 48});
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
    const FrameSnapshot frame = Gradient(20, 10, FrameRect{4, 2, 8, 5});
    const ScreenshotResult inMemory = Screenshotter::Render(frame, Options(ScreenshotArea::Screen));
    ASSERT_TRUE(inMemory.ok);

    ScreenshotOptions options = Options(ScreenshotArea::Screen);
    options.saveTo = TestPathHelper::GetUniqueTestScratchPath("screenshotter/deeper/shot.png");
    std::filesystem::remove_all(std::filesystem::path(options.saveTo).parent_path().parent_path());
    const ScreenshotResult saved = Screenshotter::Render(frame, options);
    ASSERT_TRUE(saved.ok) << saved.errorMessage;
    EXPECT_EQ(saved.savedFile, options.saveTo);
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
    const ScreenshotResult r = Screenshotter::Render(Gradient(8, 8, FrameRect{0, 0, 8, 8}), options);
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
    ASSERT_TRUE(Screenshotter::ParseRequestWords("", "", "", options, message)) << message;
    EXPECT_EQ(options.area, ScreenshotArea::Full);
    EXPECT_EQ(options.format, ScreenshotFormat::Png);
}

TEST(Screenshotter_Test, RequestWordsAreTakenAsGiven)
{
    ScreenshotOptions options;
    std::string message;
    ASSERT_TRUE(Screenshotter::ParseRequestWords("screen", "", "gif", options, message)) << message;
    EXPECT_EQ(options.area, ScreenshotArea::Screen);
    EXPECT_EQ(options.format, ScreenshotFormat::Gif);
}

TEST(Screenshotter_Test, ModeIsADeprecatedAliasOfArea)
{
    for (const char* word : {"full", "screen"})
    {
        ScreenshotOptions viaMode, viaArea;
        std::string message;
        ASSERT_TRUE(Screenshotter::ParseRequestWords("", word, "", viaMode, message)) << message;
        ASSERT_TRUE(Screenshotter::ParseRequestWords(word, "", "", viaArea, message)) << message;
        EXPECT_EQ(viaMode.area, viaArea.area) << word;
        // The same word in both is not a conflict
        ScreenshotOptions both;
        EXPECT_TRUE(Screenshotter::ParseRequestWords(word, word, "", both, message)) << message;
    }
}

TEST(Screenshotter_Test, AreaAndModeThatDisagreeAreAnErrorNamingBoth)
{
    ScreenshotOptions options;
    std::string message;
    EXPECT_FALSE(Screenshotter::ParseRequestWords("full", "screen", "", options, message));
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
        EXPECT_FALSE(Screenshotter::ParseRequestWords(c.area, c.mode, c.format, options, message)) << c.mentions;
        EXPECT_NE(message.find(c.mentions), std::string::npos) << message;
        EXPECT_NE(message.find(c.allowed), std::string::npos) << message;
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

/// endregion </Real frames>
