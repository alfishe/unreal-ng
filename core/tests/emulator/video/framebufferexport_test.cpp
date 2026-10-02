// The picture as raw pixels (framebufferexport.h; automation audit G14): rgba (the presented frame) on any
// machine, index (pens) where the machine names its palette - the Sprinter.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "../machines/sprinter/sprinterfixture.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/video/framebufferexport.h"
#include "emulator/video/sprinter/sprintervideoram.h"

class FramebufferExportSprinter_Test : public SprinterFixture
{
};

// Index: the pen of every visible pixel; graphics 320 square (0, 0) with palette 2 at the picture origin
TEST_F(FramebufferExportSprinter_Test, IndexIsThePenOfEveryPixel)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().rgMod = 0;
    vram.Write(SprinterVideoRam::ModeAddress(0, 0, 0), 0xA0);  // graphics 320, palette 2, source column 0, row 0
    vram.Write(0x0000, 0x37);                                  // its first byte
    FramebufferExport::Frame frame;
    std::string error;
    ASSERT_TRUE(FramebufferExport::Capture(_context, "index", frame, error)) << error;
    EXPECT_EQ(frame.width, 736);
    EXPECT_EQ(frame.height, 288);
    ASSERT_EQ(frame.bytes.size(), 736u * 288u * 2u);
    const size_t at = (16u * 736u + 48u) * 2u;  // the picture's first pixel (48, 16)
    EXPECT_EQ(frame.bytes[at] | (frame.bytes[at + 1] << 8), 2 * 256 + 0x37);
    EXPECT_FALSE(FramebufferExport::Capture(_context, "png", frame, error));
}

TEST(FramebufferExport_Test, RgbaOnAnyMachineIndexOnlyWhereNamed)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorTestHelper::RunFramesFast(emulator, 1);
    FramebufferExport::Frame frame;
    std::string error;
    ASSERT_TRUE(FramebufferExport::Capture(emulator->GetContext(), "rgba", frame, error)) << error;
    EXPECT_EQ(frame.bytes.size(), static_cast<size_t>(frame.width) * frame.height * 4);
    EXPECT_GT(frame.width, 0);
    EXPECT_FALSE(FramebufferExport::Capture(emulator->GetContext(), "index", frame, error));
    EXPECT_NE(error.find("no indexed form"), std::string::npos) << error;
    EmulatorTestHelper::CleanupEmulator(emulator);
}
