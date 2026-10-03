#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "emulator/video/zx/zxgeometry.h"

/// Frame geometry: where the working picture sits inside the frame a consumer receives.
///
/// The screenshotter and every other consumer take the picture's rectangle from the frame's
/// own geometry (docs/inprogress/2026-10-03-screenshotter/design.md). These tests measure it:
/// a machine is frozen in a DI / HALT loop, its border and paper are painted in two colors,
/// and the rectangle of paper-colored pixels found in the presented frame is compared with
/// the geometry the frame claims.

namespace
{
constexpr uint32_t kBorderColor = 1;  // blue
constexpr uint32_t kPaperColor = 2;   // red (attribute paper bits 3-5)

struct Box
{
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;  // inclusive
    int Width() const { return x1 - x0 + 1; }
    int Height() const { return y1 - y0 + 1; }
};

/// Bounding box of the pixels equal to `color`
Box FindColor(const std::vector<uint8_t>& rgba, int width, int height, uint32_t color)
{
    Box box;
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(rgba.data());
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            if (pixels[y * width + x] == color)
            {
                box.x0 = std::min(box.x0, x);
                box.y0 = std::min(box.y0, y);
                box.x1 = std::max(box.x1, x);
                box.y1 = std::max(box.y1, y);
            }
        }
    }
    return box;
}
}  // namespace

class FrameGeometry_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Screen* _screen = nullptr;

    void Boot(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator " << model;
        _context = _emulator->GetContext();
        _screen = _context->pScreen;
        ASSERT_NE(_screen, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Freeze the machine and paint: border blue, paper red, no ink
    void PaintAndFreeze()
    {
        Z80* z80 = _context->pCore->GetZ80();
        z80->DirectWrite(0x8000, 0xF3);  // DI
        z80->DirectWrite(0x8001, 0x76);  // HALT
        z80->pc = 0x8000;
        for (uint16_t a = 0x4000; a < 0x5800; a++)
            z80->DirectWrite(a, 0x00);
        for (uint16_t a = 0x5800; a < 0x5B00; a++)
            z80->DirectWrite(a, static_cast<uint8_t>(kPaperColor << 3));
        _context->pPortDecoder->DecodePortOut(0xFE, static_cast<uint8_t>(kBorderColor), 0);
        _screen->SetPresentDelayFrames(0);
        _emulator->RunNFrames(3);
    }

    std::vector<uint8_t> PresentedFrame(int& width, int& height)
    {
        const auto& fb = _screen->GetFramebufferDescriptor();
        width = fb.width;
        height = fb.height;
        std::vector<uint8_t> rgba(fb.memoryBufferSize);
        EXPECT_TRUE(_screen->CopyPresentedFramebuffer(rgba.data(), rgba.size()));
        return rgba;
    }
};

/// The geometry a frame claims must be where the picture really is
TEST_F(FrameGeometry_Test, WorkingWindowIsWherePaperIsDrawn)
{
    struct Case
    {
        const char* model;
        bool overscan;
        VideoModeEnum mode;
        int width, height;
    };
    const Case cases[] = {
        {"48K", false, M_ZX48, 352, 288},
        {"128k", false, M_ZX128, 352, 288},
        {"PENTAGON", false, M_PENTAGON128K, 352, 288},
        {"PENTAGON", true, M_P384, 384, 304},  // the table was believed wrong for this one; measured, it is right
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(testing::Message() << c.model << (c.overscan ? " overscan" : ""));
        Boot(c.model);
        if (c.overscan)
            ASSERT_TRUE(_emulator->SetOverscanMode(true));
        PaintAndFreeze();

        FrameSnapshot snap;
        ASSERT_TRUE(_screen->SnapshotPresented(snap));
        const PictureGeometry& g = snap.geometry;
        EXPECT_EQ(g.videoMode, c.mode);
        EXPECT_EQ(g.width, c.width);
        EXPECT_EQ(g.height, c.height);
        EXPECT_EQ(g.stride, static_cast<uint32_t>(c.width) * 4);
        EXPECT_EQ(snap.pixels.size(), static_cast<size_t>(g.stride) * g.height);
        EXPECT_EQ(g.source, FrameSource::Native);
        EXPECT_GT(g.frameNumber, 0u);

        const Box paper = FindColor(snap.pixels, g.width, g.height, ZxGeometry::Colour(kPaperColor));
        EXPECT_EQ(g.screenWindow.x, paper.x0);
        EXPECT_EQ(g.screenWindow.y, paper.y0);
        EXPECT_EQ(g.screenWindow.width, paper.Width());
        EXPECT_EQ(g.screenWindow.height, paper.Height());
        TearDown();
    }
}

/// Pixels and geometry are one pair: after a size-changing switch the snapshot is the new size, with
/// pixels of exactly that size, never the old geometry over new pixels (or the reverse)
TEST_F(FrameGeometry_Test, SnapshotStaysCoherentAcrossAModeSwitch)
{
    Boot("PENTAGON");
    PaintAndFreeze();
    FrameSnapshot before;
    ASSERT_TRUE(_screen->SnapshotPresented(before));
    EXPECT_EQ(before.geometry.width, 352);

    ASSERT_TRUE(_emulator->SetOverscanMode(true));
    // The switch resizes the present queue: until a frame of the new size is latched there is no frame,
    // and a stale 352x288 one is never served with a 384x304 geometry
    FrameSnapshot between;
    if (_screen->SnapshotPresented(between))
        EXPECT_EQ(between.pixels.size(), static_cast<size_t>(between.geometry.stride) * between.geometry.height);
    PaintAndFreeze();
    FrameSnapshot after;
    ASSERT_TRUE(_screen->SnapshotPresented(after));
    EXPECT_EQ(after.geometry.width, 384);
    EXPECT_EQ(after.geometry.height, 304);
    EXPECT_EQ(after.pixels.size(), static_cast<size_t>(384) * 304 * 4);
    EXPECT_GT(after.geometry.frameNumber, before.geometry.frameNumber);
}

/// An external picture (the FT812 of the VDAC2 card) has no table row and no border: its geometry says so,
/// whatever its size, even smaller than a Spectrum screen (the old 256x192 crop could not handle that)
TEST_F(FrameGeometry_Test, ExternalPictureIsDescribedByItself)
{
    Boot("PENTAGON");
    PaintAndFreeze();
    for (const auto& size : {std::pair<int, int>{40, 20}, std::pair<int, int>{1024, 768}})
    {
        SCOPED_TRACE(testing::Message() << size.first << "x" << size.second);
        std::vector<uint8_t> picture(static_cast<size_t>(size.first) * size.second * 4);
        for (size_t i = 0; i < picture.size(); i++)
            picture[i] = static_cast<uint8_t>(i * 7 + 3);
        _screen->SetExternalPicture(picture.data(), static_cast<uint16_t>(size.first),
                                    static_cast<uint16_t>(size.second), 20000);
        _screen->LatchExternalFrame();

        FrameSnapshot snap;
        ASSERT_TRUE(_screen->SnapshotPresented(snap));
        const PictureGeometry& g = snap.geometry;
        EXPECT_EQ(g.source, FrameSource::External);
        EXPECT_EQ(g.videoMode, M_NUL);
        EXPECT_EQ(g.width, size.first);
        EXPECT_EQ(g.height, size.second);
        EXPECT_EQ(g.screenWindow.x, 0);
        EXPECT_EQ(g.screenWindow.y, 0);
        EXPECT_EQ(g.screenWindow.width, size.first);
        EXPECT_EQ(g.screenWindow.height, size.second);
        EXPECT_EQ(snap.pixels, picture);
        _screen->ClearExternalPicture();
    }
}
