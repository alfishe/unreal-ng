#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/displaygeometry.h"
#include "emulator/video/screen.h"

/// DisplayGeometry: the one answer to "how does this framebuffer look on a screen". The window, the video wall and
/// the recording ask it; the tests pin the sizes every machine produces

namespace
{
struct Case
{
    uint16_t width, height;
    uint16_t expectedWidth, expectedHeight;
    const char* what;
};
}  // namespace

TEST(DisplayGeometry_Test, TheKnownFramebufferSizes)
{
    const Case cases[] = {
        {352, 288, 352, 288, "ZX family, Pentagon, Profi, Scorpion"},
        {384, 304, 384, 304, "Pentagon overscan"},
        {320, 288, 320, 288, "ATM 320 EGA"},
        {640, 288, 640, 576, "ATM 640 hi-res / text"},
        {608, 288, 608, 576, "Profi hi-res"},
        {720, 288, 720, 576, "TS-Conf, ZX-Poly module"},
        {736, 288, 736, 576, "Sprinter"},
        {640, 512, 640, 512, "ZX Spectrum Next (lines already doubled)"},
    };
    for (const Case& c : cases)
    {
        const DisplayGeometry::Size size = DisplayGeometry::Compute(c.width, c.height);
        EXPECT_EQ(size.width, c.expectedWidth) << c.what;
        EXPECT_EQ(size.height, c.expectedHeight) << c.what;
        EXPECT_EQ(size.height, c.height * size.lineRepeat) << c.what;
    }
}

TEST(DisplayGeometry_Test, TheDisplayFrameIsTheReferenceShapeAroundThePicture)
{
    const Case cases[] = {
        {352, 288, 352, 288, "ZX family"},
        {384, 304, 384, 315, "Pentagon overscan"},
        {320, 288, 352, 288, "ATM 320 EGA grows sideways"},
        {640, 288, 640, 524, "ATM 640 hi-res"},
        {608, 288, 608, 498, "Profi hi-res"},
        {720, 288, 720, 590, "TS-Conf"},
        {736, 288, 736, 603, "Sprinter"},
        {640, 512, 640, 524, "ZX Spectrum Next"},
    };
    for (const Case& c : cases)
    {
        const DisplayGeometry::Size frame = DisplayGeometry::DisplayFrame(c.width, c.height);
        EXPECT_EQ(frame.width, c.expectedWidth) << c.what;
        EXPECT_EQ(frame.height, c.expectedHeight) << c.what;
        EXPECT_GE(frame.width, c.width) << c.what << ": nothing squeezed";
        EXPECT_GE(frame.height, c.height) << c.what;
        EXPECT_NEAR(frame.Aspect(), 352.0 / 288.0, 0.01) << c.what;
    }
}

/// The shape a rectangle of a frame has on the CRT: what a video wall tile keeps inside its 4:3 plate
TEST(DisplayGeometry_Test, TheWorkingPictureKeepsItsRasterShape)
{
    struct Window
    {
        uint16_t frameWidth, frameHeight, width, height;
        double aspect;
        const char* what;
    };
    const Window windows[] = {
        {352, 288, 256, 192, 4.0 / 3.0, "a Spectrum's paper is 4:3"},
        {352, 288, 352, 288, 352.0 / 288.0, "the whole ZX frame"},
        {720, 288, 720, 288, 352.0 / 288.0, "the whole TS-Conf frame"},
        {640, 288, 640, 200, 352.0 / 200.0, "ATM 640x200 inside its 288 lines"},
        {608, 288, 512, 240, (512.0 * 288 * 352) / (608.0 * 240 * 288), "Profi hi-res paper"},
        {640, 512, 512, 384, (512.0 * 512 * 352) / (640.0 * 384 * 288), "Next paper"},
        {736, 288, 640, 256, (640.0 * 288 * 352) / (736.0 * 256 * 288), "Sprinter 640x256"},
    };
    for (const Window& w : windows)
        EXPECT_NEAR(DisplayGeometry::WindowAspect(w.frameWidth, w.frameHeight, w.width, w.height), w.aspect, 1e-9) << w.what;
    EXPECT_EQ(DisplayGeometry::WindowAspect(0, 288, 256, 192), 0.0);
    EXPECT_EQ(DisplayGeometry::WindowAspect(352, 288, 256, 0), 0.0);
}

TEST(DisplayGeometry_Test, NothingIsDrawnWithoutAPicture)
{
    EXPECT_EQ(DisplayGeometry::Compute(0, 288).width, 0);
    EXPECT_EQ(DisplayGeometry::Compute(352, 0).height, 0);
    EXPECT_FLOAT_EQ(DisplayGeometry::Size{}.Aspect(), 0.0f);
}

namespace
{
void ExpectOrdinaryPicture(const DisplayGeometry::Size& size, uint16_t width, uint16_t height, const std::string& what)
{
    EXPECT_GE(size.width, width) << what << ": the picture is never squeezed";
    EXPECT_GE(size.height, height) << what;
    EXPECT_GT(size.Aspect(), 1.0f) << what;
    EXPECT_LT(size.Aspect(), 1.4f) << what << ": ultra-wide";
    EXPECT_EQ(size.width % 2, 0) << what << ": video codecs want even sizes";
    EXPECT_EQ(size.height % 2, 0) << what;
}
}  // namespace

/// Every machine the build knows, in the video mode it boots with
TEST(DisplayGeometry_Test, EveryMachineShowsAnOrdinaryPicture)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    ASSERT_NE(manager, nullptr);
    int checked = 0;
    for (const TMemModel& model : manager->GetAvailableModels())
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model.ShortName, LoggerLevel::LogError, RamPowerOn::Zero);
        if (emulator == nullptr)
            continue;
        const Screen* screen = emulator->GetContext()->pScreen;
        if (screen != nullptr)
        {
            ExpectOrdinaryPicture(screen->GetDisplayGeometry(), screen->GetDisplayWidth(), screen->GetDisplayHeight(),
                                  model.ShortName);
            checked++;
        }
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
    EXPECT_GT(checked, 3);
}

/// The rectangle a video wall tile cuts out of a frame is the machine's own, taken from the frame it was latched with:
/// not empty and inside the frame (its shape on the CRT is WindowAspect's: a 640x200 window is 3.2:1 as stored, 1.76 shown)
TEST(DisplayGeometry_Test, EveryMachinesWorkingPictureLiesInsideItsFrame)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    ASSERT_NE(manager, nullptr);
    int checked = 0;
    for (const TMemModel& model : manager->GetAvailableModels())
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model.ShortName, LoggerLevel::LogError, RamPowerOn::Zero);
        if (emulator == nullptr)
            continue;
        emulator->RunNFrames(3);

        Screen* screen = emulator->GetContext()->pScreen;
        std::vector<uint8_t> rgba(static_cast<size_t>(screen->GetFramebufferDescriptor().memoryBufferSize));
        PictureGeometry geometry;
        if (screen->CopyPresentedFramebuffer(rgba.data(), rgba.size(), &geometry) && geometry.width > 0)
        {
            const PictureRect& window = geometry.screenWindow;
            EXPECT_GT(window.width, 0) << model.ShortName;
            EXPECT_GT(window.height, 0) << model.ShortName;
            EXPECT_LE(window.x + window.width, geometry.width) << model.ShortName << ": inside the frame";
            EXPECT_LE(window.y + window.height, geometry.height) << model.ShortName;
            checked++;
        }
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
    EXPECT_GT(checked, 3);
}

/// Every video mode's framebuffer, from the raster table
TEST(DisplayGeometry_Test, EveryVideoModeShowsAnOrdinaryPicture)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    const Screen* screen = emulator->GetContext()->pScreen;
    for (int mode = M_ZX48; mode < M_MAX; mode++)
    {
        const RasterDescriptor& raster = screen->GetTimingDescriptor(static_cast<VideoModeEnum>(mode));
        if (raster.fullFrameWidth == 0)
            continue;
        EXPECT_NEAR(DisplayGeometry::DisplayFrame(raster.fullFrameWidth, raster.fullFrameHeight).Aspect(), 352.0 / 288.0, 0.01);
        ExpectOrdinaryPicture(DisplayGeometry::Compute(raster.fullFrameWidth, raster.fullFrameHeight),
                              raster.fullFrameWidth, raster.fullFrameHeight, Screen::GetVideoModeName(static_cast<VideoModeEnum>(mode)));
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}
