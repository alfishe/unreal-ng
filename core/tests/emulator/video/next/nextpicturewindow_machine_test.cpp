// ScreenNext on a machine: the working window and the content window latched with the presented frame follow the layers'
// registers (nextpicturewindow.h has the rules, nextpicturewindow_test.cpp the arithmetic). The case that started it: Layer 2
// at 256x192 with every window at x 8..247, y 8..183 shows 480x352 in the 640x512 grid, which sits at (32, 32) of the 720x576 HDMI frame.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/pictureview.h"
#include "emulator/video/next/screennext.h"

class NextPictureWindowMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        ASSERT_NE(dynamic_cast<ScreenNext*>(_context->pScreen), nullptr);
        // the CPU parks in RAM (DI; JR $) so that the ROM does not write registers behind the test
        Memory* memory = _context->pMemory;
        memory->DirectWriteToZ80Memory(0xC000, 0xF3);
        memory->DirectWriteToZ80Memory(0xC001, 0x18);
        memory->DirectWriteToZ80Memory(0xC002, 0xFE);
        _context->pCore->GetZ80()->pc = 0xC000;
        Nr(0x15, 0x00);
        Nr(0x68, 0x00);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    void Nr(uint8_t reg, uint8_t value)
    {
        Out(0x243B, reg);
        Out(0x253B, value);
    }
    /// NR #18 (Layer 2), #19 (sprites), #1A (ULA), #1B (tilemap): x1, x2, y1, y2 in that order
    void Clip(uint8_t reg, uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2)
    {
        for (uint8_t v : {x1, x2, y1, y2})
            Nr(reg, v);
    }
    /// The geometry that came with the newest presented frame
    PictureGeometry Presented()
    {
        for (int i = 0; i < 4; i++)
            _emulator->RunFrame(true);
        Screen* screen = _context->pScreen;
        std::vector<uint8_t> pixels(720u * 576u * 4u);
        PictureGeometry geometry;
        EXPECT_TRUE(screen->CopyPresentedFramebuffer(pixels.data(), pixels.size(), &geometry));
        return geometry;
    }
    static void ExpectRect(const PictureRect& r, int x, int y, int w, int h, const char* what)
    {
        EXPECT_EQ(r.x, x) << what;
        EXPECT_EQ(r.y, y) << what;
        EXPECT_EQ(r.width, w) << what;
        EXPECT_EQ(r.height, h) << what;
    }
};

TEST_F(NextPictureWindowMachine_Test, ASpectrumPictureIsThePaperWhole)
{
    const PictureGeometry g = Presented();
    EXPECT_EQ(g.width, 720);
    EXPECT_EQ(g.height, 576);
    ExpectRect(g.screenWindow, 96, 96, 512, 384, "screen window");
    ExpectRect(PictureView::Source(g, PictureView::Mode::Content), 96, 96, 512, 384, "content of an unclipped ULA");
}

TEST_F(NextPictureWindowMachine_Test, AGameThatClipsItsLayersHasAContentWindowInsideThePaper)
{
    Nr(0x69, 0x80);  // Layer 2 on (NR #69 bit 7), 256x192 (NR #70 bits 5:4 = 0)
    Nr(0x70, 0x00);
    Clip(0x18, 8, 247, 8, 183);
    Clip(0x1A, 8, 247, 8, 183);
    const PictureGeometry g = Presented();
    ExpectRect(g.screenWindow, 96, 96, 512, 384, "the paper stays the screen");
    ExpectRect(g.contentWindow, 112, 112, 480, 352, "240x176 inside it");
    ExpectRect(PictureView::Source(g, PictureView::Mode::Content), 112, 112, 480, 352, "what content shows");
    ExpectRect(PictureView::Source(g, PictureView::Mode::Screen), 96, 96, 512, 384, "what screen shows");
    ExpectRect(PictureView::Source(g, PictureView::Mode::Full), 0, 0, 720, 576, "what full shows");
}

TEST_F(NextPictureWindowMachine_Test, Layer2In320x256HasNoBorderSoTheWholeFrameIsTheScreen)
{
    Nr(0x69, 0x80);
    Nr(0x70, 0x10);  // 320x256
    Clip(0x18, 0, 159, 0, 255);
    const PictureGeometry g = Presented();
    ExpectRect(g.screenWindow, 32, 32, 640, 512, "no border: the whole grid");
    ExpectRect(PictureView::Source(g, PictureView::Mode::Screen), 32, 32, 640, 512, "screen is the grid");
    ExpectRect(PictureView::Source(g, PictureView::Mode::Content), 32, 32, 640, 512, "content is the grid");
}
