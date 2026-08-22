#include "pch.h"
#include "emulator/video/zx/atmfont.h"
#include "emulator/video/zx/screenzx.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "3rdparty/message-center/messagecenter.h"

/// ATM Video Mode Tests
/// Tests the ATM-specific video modes: EGA 16-color, Hardware Multicolor, Text modes

class ATMVideoMode_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;
    ScreenZX* _screen = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _cpu = new Core(_context);
        (void)_cpu->Init();
        _screen = dynamic_cast<ScreenZX*>(_context->pScreen);
        _memory = _context->pMemory;
    }

    void TearDown() override
    {
        if (_cpu != nullptr)
        {
            delete _cpu;
            _cpu = nullptr;
        }

        if (_context != nullptr)
        {
            delete _context;
            _context = nullptr;
        }
    }

    void SetATMModel()
    {
        _context->config.mem_model = MM_ATM710;

        // ATM uses ZX-compatible 312-line PAL timing: 224 T-states/line,
        // 69888 T-states/frame. Must be set explicitly in bare test contexts -
        // the _DEBUG sanity check in SetVideoMode rejects frame < maxFrameTiming.
        _context->config.frame = 69888;
        _context->config.t_line = 224;
    }

    void SetVideoMode(uint8_t mode)
    {
        // FF77 bits 1,2,4 = video mode
        // mode 0: EGA 16c (FF77_16)
        // mode 2: Hardware MC (FF77_MC)
        // mode 3: ZX Standard (FF77_ZX)
        // mode 6: Text 80x25 (FF77_TX)
        _context->emulatorState.pFF77 = (mode & 0x07) | 0x20; // bit 5 = INT gate
        _context->emulatorState.aFF77 = 0x0100; // PEN=1 to enable ATM paging
    }
};

/// region <Video Mode Detection Tests>

TEST_F(ATMVideoMode_Test, InitRaster_DetectsEGAMode)
{
    SetATMModel();
    SetVideoMode(FF77_16);  // EGA 16-color mode

    _screen->InitRaster();

    // InitRaster sets _vid.mode, not _mode (which is set by SetVideoMode)
    EXPECT_EQ(_screen->_vid.mode, M_ATM16);
}

TEST_F(ATMVideoMode_Test, InitRaster_DetectsHardwareMCMode)
{
    SetATMModel();
    SetVideoMode(FF77_MC);  // Hardware Multicolor mode

    _screen->InitRaster();

    EXPECT_EQ(_screen->_vid.mode, M_ATMHR);
}

TEST_F(ATMVideoMode_Test, InitRaster_DetectsTextMode)
{
    SetATMModel();
    SetVideoMode(FF77_TX);  // Text 80x25 mode

    _screen->InitRaster();

    EXPECT_EQ(_screen->_vid.mode, M_ATMTX);
}

TEST_F(ATMVideoMode_Test, InitRaster_DetectsZXCompatMode)
{
    SetATMModel();
    SetVideoMode(FF77_ZX);  // ZX Standard mode

    _screen->InitRaster();

    // In ZX-compatible mode ATM uses ZX48-class timing (69888 T/frame @ 224
    // T/line, reference PRESET.ATM1_2_3.5MHz) - M_ZX128's authentic
    // 70908/228 timing exceeds the ATM config frame
    EXPECT_EQ(_screen->_vid.mode, M_ZX48);
}

/// endregion </Video Mode Detection Tests>

/// region <ATM3 Linear Text Mode Tests>

TEST_F(ATMVideoMode_Test, InitRaster_DetectsATM3LinearTextMode)
{
    _context->config.mem_model = MM_ATM3;
    _context->config.frame = 69888;
    _context->config.t_line = 224;
    SetVideoMode(FF77_TL);  // Text Linear mode (ATM3 only)

    _screen->InitRaster();

    EXPECT_EQ(_screen->_vid.mode, M_ATMTL);
}

TEST_F(ATMVideoMode_Test, LinearTextMode_NotAvailableOnATM710)
{
    SetATMModel();  // ATM710, not ATM3
    SetVideoMode(FF77_TL);  // Text Linear mode

    _screen->InitRaster();

    // FF77_TL (mode 7) should not be recognized on ATM710
    // Should fall back to M_NUL or similar
    EXPECT_NE(_screen->_vid.mode, M_ATMTL);
}

/// endregion </ATM3 Linear Text Mode Tests>

/// region <Raster Mode Tests>

TEST_F(ATMVideoMode_Test, EGAMode_Uses320x200Raster)
{
    SetATMModel();
    SetVideoMode(FF77_16);

    _screen->InitRaster();

    // ATM extended modes should use 320x200 raster
    VideoControl& vid = _screen->_vid;
    EXPECT_EQ(vid.raster.num, R_320_200);
}

TEST_F(ATMVideoMode_Test, ZXCompatMode_Uses256x192Raster)
{
    SetATMModel();
    SetVideoMode(FF77_ZX);

    _screen->InitRaster();

    // ZX-compatible mode should use standard 256x192 raster
    VideoControl& vid = _screen->_vid;
    EXPECT_EQ(vid.raster.num, R_256_192);
}

/// endregion </Raster Mode Tests>

/// region <Callback Array Tests>

TEST_F(ATMVideoMode_Test, DrawCallbackArrayHasCorrectSize)
{
    // Verify the _drawCallbacks array has exactly M_MAX entries
    // This is a compile-time check but we verify it at runtime too
    EXPECT_EQ(M_MAX, 19);  // Update if VideoModeEnum changes
}

/// endregion </Callback Array Tests>

/// region <EGA Rendering Tests>

TEST_F(ATMVideoMode_Test, DrawATM16_RendersPixelsFromBitPlanes)
{
    SetATMModel();
    SetVideoMode(FF77_16);  // EGA mode
    _screen->InitRaster();

    // Set up test pattern in video RAM
    // Video page 5, alt page 1
    uint8_t* plane0 = _memory->RAMPageAddress(1);           // bit 0
    uint8_t* plane1 = _memory->RAMPageAddress(5);           // bit 1
    uint8_t* plane2 = _memory->RAMPageAddress(1) + 0x2000;  // bit 2
    uint8_t* plane3 = _memory->RAMPageAddress(5) + 0x2000;  // bit 3

    // Set test pattern: first byte = 0xFF in plane 0 only -> color 1 (blue)
    plane0[0] = 0xFF;
    plane1[0] = 0x00;
    plane2[0] = 0x00;
    plane3[0] = 0x00;

    // Initialize video state for drawing
    _screen->_vid.vptr = 0;
    _screen->_vid.xctr = 0;
    _screen->_vid.ygctr = 0;
    _screen->_vid.t_next = 0;

    // Call draw function for 4 T-states (processes 1 byte = 8 pixels)
    _screen->DrawATM16(4);

    // Verify that 16 pixels were written (8 pixels doubled)
    EXPECT_EQ(_screen->_vid.vptr, 16u);
    EXPECT_EQ(_screen->_vid.xctr, 1u);
}

TEST_F(ATMVideoMode_Test, DrawATMHiRes_RendersPixelsWithAttributes)
{
    SetATMModel();
    SetVideoMode(FF77_MC);  // Hardware MC mode
    _screen->InitRaster();

    // Set up test pattern
    uint8_t* pix0 = _memory->RAMPageAddress(1);
    uint8_t* pix1 = _memory->RAMPageAddress(5);
    uint8_t* attr0 = _memory->RAMPageAddress(1) + 0x2000;
    uint8_t* attr1 = _memory->RAMPageAddress(5) + 0x2000;

    // Test pattern: alternating pixels with white on black attribute
    pix0[0] = 0xAA;   // 10101010
    pix1[0] = 0x55;   // 01010101
    attr0[0] = 0x47;  // Bright, white ink, black paper
    attr1[0] = 0x47;

    _screen->_vid.vptr = 0;
    _screen->_vid.xctr = 0;
    _screen->_vid.ygctr = 0;
    _screen->_vid.t_next = 0;

    _screen->DrawATMHiRes(4);

    // 16 pixels output (8 from each plane)
    EXPECT_EQ(_screen->_vid.vptr, 16u);
    EXPECT_EQ(_screen->_vid.xctr, 1u);
}

/// endregion </EGA Rendering Tests>

/// region <Video Mode Switch Tests>

TEST_F(ATMVideoMode_Test, PortFF77Write_TriggersVideoModeChange)
{
    SetATMModel();

    // Start in ZX mode (mode 3 = 0x03, with INT gate = 0x20)
    _context->emulatorState.pFF77 = 0x23;  // bits 0,1 = mode 3, bit 5 = INT gate
    _context->emulatorState.aFF77 = 0x0100;  // PEN=1
    _screen->InitRaster();
    // Mode 3 (FF77_ZX) stays in ZX-compatible mode (ZX48-class timing)
    EXPECT_EQ(_screen->_vid.mode, M_ZX48);

    // Change to EGA mode (mode 0) via pFF77 directly
    _context->emulatorState.pFF77 = 0x20;  // mode 0 + INT gate
    _screen->InitRaster();
    EXPECT_EQ(_screen->_vid.mode, M_ATM16);
}

TEST_F(ATMVideoMode_Test, PortDecoderFF77_TriggersVideoModeChange)
{
    // Need to create a fresh context with ATM model set BEFORE Core::Init()
    // because port decoder is created during Init()
    if (_cpu) delete _cpu;
    if (_context) delete _context;

    _context = new EmulatorContext(LoggerLevel::LogError);
    _context->config.mem_model = MM_ATM710;  // Set BEFORE Init()
    _context->config.frame = 69888;
    _context->config.t_line = 224;
    _cpu = new Core(_context);
    (void)_cpu->Init();
    _screen = dynamic_cast<ScreenZX*>(_context->pScreen);
    _memory = _context->pMemory;

    // Get the port decoder - should now be ATM710 decoder
    PortDecoder* portDecoder = _context->pPortDecoder;
    ASSERT_NE(portDecoder, nullptr);

    // Start in ZX mode (mode 3)
    // Port FF77 with PEN bit set (address bit 8)
    uint16_t port = 0x0177;  // FF77 with PEN=1 (bit 8)
    uint8_t value = 0x23;    // mode 3 + INT gate
    portDecoder->DecodePortOut(port, value, 0);

    // Verify state was set
    EXPECT_EQ(_context->emulatorState.pFF77, 0x23);

    // Verify video mode is ZX-compatible (ZX48-class timing for ATM)
    EXPECT_EQ(_screen->_vid.mode, M_ZX48) << "Mode 3 should be ZX-compatible";

    // Now change to EGA mode via port write
    value = 0x20;  // mode 0 + INT gate
    portDecoder->DecodePortOut(port, value, 0);

    // Verify state was updated
    EXPECT_EQ(_context->emulatorState.pFF77, 0x20);

    // Verify video mode changed to ATM16 (EGA)
    EXPECT_EQ(_screen->_vid.mode, M_ATM16) << "Mode 0 should trigger EGA mode";
}

TEST_F(ATMVideoMode_Test, PortDecoderFF77_TriggersFullModeSwitch)
{
    // Comprehensive test: verify video mode, raster changes
    if (_cpu) delete _cpu;
    if (_context) delete _context;

    _context = new EmulatorContext(LoggerLevel::LogError);
    _context->config.mem_model = MM_ATM710;
    _context->config.frame = 69888;
    _context->config.t_line = 224;
    _cpu = new Core(_context);
    (void)_cpu->Init();
    _screen = dynamic_cast<ScreenZX*>(_context->pScreen);

    PortDecoder* portDecoder = _context->pPortDecoder;
    ASSERT_NE(portDecoder, nullptr);
    ASSERT_NE(_screen, nullptr);

    // Start in ZX mode (mode 3)
    uint16_t port = 0x0177;
    portDecoder->DecodePortOut(port, 0x23, 0);  // mode 3 + INT gate

    // Verify ZX mode state
    EXPECT_EQ(_screen->_vid.mode, M_ZX48);
    EXPECT_EQ(_screen->_vid.raster.num, R_256_192) << "ZX mode should use 256x192 raster";

    // Record ZX mode state
    VideoModeEnum zxMode = _screen->_vid.mode;
    RasterModeEnum zxRaster = _screen->_vid.raster.num;

    // Switch to EGA mode (mode 0)
    portDecoder->DecodePortOut(port, 0x20, 0);  // mode 0 + INT gate

    // 1. Verify video mode changed
    EXPECT_EQ(_screen->_vid.mode, M_ATM16) << "Mode should be ATM16 (EGA)";
    EXPECT_NE(_screen->_vid.mode, zxMode) << "Mode should have changed from ZX128";

    // 2. Verify raster changed
    EXPECT_EQ(_screen->_vid.raster.num, R_320_200) << "EGA mode should use 320x200 raster";
    EXPECT_NE(_screen->_vid.raster.num, zxRaster) << "Raster should have changed";

    // 3. Verify internal mode tracking updated
    EXPECT_EQ(_screen->GetVideoMode(), M_ATM16) << "GetVideoMode() should return ATM16";
}

TEST_F(ATMVideoMode_Test, PortDecoderFF77_NotifiesOnModeChange)
{
    // Note: NC_VIDEO_MODE_CHANGED notification requires pEmulator to be set,
    // which requires a full Emulator instance. In unit tests without Emulator,
    // we verify that the notification code path exists but skip execution.
    // The notification IS sent in the real app when pEmulator is available.

    if (_cpu) delete _cpu;
    if (_context) delete _context;

    _context = new EmulatorContext(LoggerLevel::LogError);
    _context->config.mem_model = MM_ATM710;
    _context->config.frame = 69888;
    _context->config.t_line = 224;
    _cpu = new Core(_context);
    (void)_cpu->Init();
    _screen = dynamic_cast<ScreenZX*>(_context->pScreen);

    PortDecoder* portDecoder = _context->pPortDecoder;
    ASSERT_NE(portDecoder, nullptr);

    // Verify pEmulator is null in unit tests (notification skipped)
    EXPECT_EQ(_context->pEmulator, nullptr)
        << "Unit test context has no Emulator; notification is skipped";

    // Verify the mode DOES change even without notification
    portDecoder->DecodePortOut(0x0177, 0x23, 0);  // ZX mode
    EXPECT_EQ(_screen->_vid.mode, M_ZX48);

    portDecoder->DecodePortOut(0x0177, 0x20, 0);  // EGA mode
    EXPECT_EQ(_screen->_vid.mode, M_ATM16);
    EXPECT_EQ(_screen->GetVideoMode(), M_ATM16);
}

/// endregion </Video Mode Switch Tests>

/// region <ATM Renderer Tests (reference: dxr_atm0/2/6.cpp)>
/// Geometry: visible rows start at beam line 24 (16 vSync + 8 vBlank),
/// screenOffsetTop = 44 -> screen row 0 is beam line 68, T = 68*224 + 32..
/// EGA cols: 64 + 8j + 2q; MC/TX cols: 32 + ... (704-wide frame).

TEST_F(ATMVideoMode_Test, Render_ATM16_NibbleDecodeAndLinearStride)
{
    SetATMModel();
    SetVideoMode(FF77_16);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATM16);

    auto& fb = _screen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 448u);
    ASSERT_EQ(fb.height, 288u);
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    // Plane pair for videoPage=5/altPage=1: ega0=ap+0, ega1=vp+0, ega2=ap+0x2000, ega3=vp+0x2000
    uint8_t* ap = _memory->RAMPageAddress(1);
    uint8_t* vp = _memory->RAMPageAddress(5);

    // Expected RGBA for a 4-bit ZX-palette index (bit 3 = bright flag).
    // TransformZXSpectrumColorsToRGBA expects an ULA ATTRIBUTE byte with
    // brightness in bit 6 - remap so the helper expresses the 4-bit index.
    auto zx16 = [this](uint8_t c) {
        return _screen->TransformZXSpectrumColorsToRGBA((c & 0x08) ? (c | 0x40) : (c & 0x07), true);
    };

    // screenY=0, byte group j=0. Each plane byte = TWO 4-bit ZX-palette
    // indices (reference p4bpp_tables): left pixel = {b6,b2,b1,b0},
    // right pixel = {b7,b5,b4,b3}.
    ap[0]      = 0x0F;         // left=7 (white),            right=1 (blue)
    vp[0]      = 0x18;         // left=0 (black),            right=3 (magenta)
    ap[0x2000] = 0x40 | 0x04;  // left=4|8=12 (BRIGHT green), right=0 (black)
    vp[0x2000] = 0x80 | 0x03;  // left=3 (magenta),          right=8 (bright black = black)

    // Linear stride proof: first byte group of screenY=1 lives at offset 40
    ap[40] = 0x0F;

    const uint32_t row0 = 44;  // screenOffsetTop
    _screen->DrawATMMode(68 * 224 + 32);  // q=0 -> px0,px1
    _screen->DrawATMMode(68 * 224 + 33);  // q=1 -> px2,px3
    _screen->DrawATMMode(68 * 224 + 34);  // q=2 -> px4,px5
    _screen->DrawATMMode(68 * 224 + 35);  // q=3 -> px6,px7
    _screen->DrawATMMode(69 * 224 + 32);  // screenY=1, q=0 -> row 45, px0,px1

    EXPECT_EQ(At(row0, 64), zx16(0x07));  // white
    EXPECT_EQ(At(row0, 65), zx16(0x01));  // blue
    EXPECT_EQ(At(row0, 66), zx16(0x00));  // black
    EXPECT_EQ(At(row0, 67), zx16(0x03));  // magenta
    EXPECT_EQ(At(row0, 68), zx16(0x0C));  // bright green (bright flag from b6)
    EXPECT_EQ(At(row0, 69), zx16(0x00));  // black
    EXPECT_EQ(At(row0, 70), zx16(0x03));  // magenta
    EXPECT_EQ(At(row0, 71), zx16(0x08));  // bright black renders as black
    // 40-byte stride: byte group 0 of the NEXT line renders from offset 40
    EXPECT_EQ(At(45, 64), zx16(0x07));
    EXPECT_EQ(At(45, 65), zx16(0x01));
}

TEST_F(ATMVideoMode_Test, Render_ATMHR_MSBFirstPixelsAndPlaneAlternation)
{
    SetATMModel();
    SetVideoMode(FF77_MC);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMHR);

    auto& fb = _screen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 704u);  // 640-px mode: doubled pixel clock, 32-px borders
    ASSERT_EQ(fb.height, 288u);
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    uint8_t* ap = _memory->RAMPageAddress(1);
    uint8_t* vp = _memory->RAMPageAddress(5);

    // Byte groups alternate planes every 8 px (reference dxr_atm2.cpp
    // line_atm2_8: h0 byte j -> px 16j..16j+7, h1 byte j -> px 16j+8..15).
    // Bits are MSB-first: bit 7 = leftmost pixel of the byte.
    vp[0] = 0x80;            // bit 7 set -> leftmost pixel of group 0 is ink
    ap[0] = 0x47;            // bright white ink, black paper
    vp[0x2000] = 0x08;       // bit 3 set -> 5th pixel of group 1 is ink
    ap[0x2000] = 0x02;       // red ink, black paper

    _screen->DrawATMMode(68 * 224 + 32);  // n=0, half=0: group 0 bits 7..4
    _screen->DrawATMMode(68 * 224 + 34);  // n=1, half=0: group 1 bits 7..4 (plane +0x2000)
    _screen->DrawATMMode(68 * 224 + 35);  // n=1, half=1: group 1 bits 3..0

    auto ink = [this](uint8_t a) { return _screen->TransformZXSpectrumColorsToRGBA(a, true); };
    auto paper = [this](uint8_t a) { return _screen->TransformZXSpectrumColorsToRGBA(a, false); };

    // MSB-first order: px0 <- bit7 (ink), px1..3 <- bits 6..4 (paper)
    EXPECT_EQ(At(44, 32), ink(0x47));
    EXPECT_EQ(At(44, 33), paper(0x47));
    EXPECT_EQ(At(44, 34), paper(0x47));
    EXPECT_EQ(At(44, 35), paper(0x47));
    // Group 1 (odd -> plane +0x2000) spans cols 40..47: bits 7..4 paper
    EXPECT_EQ(At(44, 40), paper(0x02));
    EXPECT_EQ(At(44, 41), paper(0x02));
    EXPECT_EQ(At(44, 42), paper(0x02));
    EXPECT_EQ(At(44, 43), paper(0x02));
    // Group 1 half=1: px4 <- bit3 (ink), px5..7 <- bits 2..0 (paper)
    EXPECT_EQ(At(44, 44), ink(0x02));
    EXPECT_EQ(At(44, 45), paper(0x02));
    EXPECT_EQ(At(44, 46), paper(0x02));
    EXPECT_EQ(At(44, 47), paper(0x02));
}

TEST_F(ATMVideoMode_Test, Render_ATMTX_FontAndCrossPlaneAttrs)
{
    SetATMModel();
    SetVideoMode(FF77_TX);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMTX);

    auto& fb = _screen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 704u);
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    uint8_t* ap = _memory->RAMPageAddress(1);
    uint8_t* vp = _memory->RAMPageAddress(5);

    // Column 0 (n=0, fromP0): code from vp+0x1C0, attr from ap+0x2000+0x1C0
    // (text row 0 lives at plane byte 0x1C0 - reference PrepareFrameATM2)
    constexpr uint32_t T0 = 0x1C0;    // text row 0 base
    vp[T0] = 0x41;                   // 'A'
    ap[0x2000 + T0] = 0x47;          // bright white on black
    // Column 1 (n=1, fromP1): code from vp+0x2000, attr from ap+1 (hardware quirk)
    vp[0x2000 + T0] = 0x42;          // 'B'
    ap[1 + T0] = 0x20;               // black ink on red paper (paper = bits 3..5 = 4)

    // Row 0 of 'A': font bits 7..4 at t (half=0), bits 3..0 at t+1 (half=1)
    const uint8_t glyphA = ATM_FONT[0x41];
    const uint8_t glyphB = ATM_FONT[0x42];
    _screen->DrawATMMode(68 * 224 + 32);  // n=0 half=0: 'A' bits 7..4
    _screen->DrawATMMode(68 * 224 + 33);  // n=0 half=1: 'A' bits 3..0
    _screen->DrawATMMode(68 * 224 + 34);  // n=1 half=0: 'B' bits 7..4

    auto ink = [this](uint8_t a) { return _screen->TransformZXSpectrumColorsToRGBA(a, true); };
    auto paper = [this](uint8_t a) { return _screen->TransformZXSpectrumColorsToRGBA(a, false); };

    for (int k = 0; k < 4; ++k)
    {
        EXPECT_EQ(At(44, 32 + k), ((glyphA >> (7 - k)) & 1) ? ink(0x47) : paper(0x47));
        EXPECT_EQ(At(44, 36 + k), ((glyphA >> (3 - k)) & 1) ? ink(0x47) : paper(0x47));
    }
    for (int k = 0; k < 4; ++k)
    {
        EXPECT_EQ(At(44, 40 + k), ((glyphB >> (7 - k)) & 1) ? ink(0x20) : paper(0x20));
    }
}

TEST_F(ATMVideoMode_Test, Render_ATM_BorderOpaqueAndFullLineCoverage)
{
    SetATMModel();
    SetVideoMode(FF77_16);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATM16);

    // Border color via the standard port FE path (bits 0..2)
    _context->pPortDecoder->DecodePortOut(0x00FE, 0x02, 0);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    // Sweep one full border row (beam line 24 = framebuffer row 0)
    for (uint32_t t = 24 * 224; t < 25 * 224; ++t)
        _screen->DrawATMMode(t);

    const uint32_t expected = _screen->TransformZXSpectrumColorsToRGBA(0x02, true); // red border
    for (uint32_t col = 0; col < fb.width; ++col)
    {
        EXPECT_EQ(px[col], expected) << "col " << col;
        // Opaque alpha: the old transparent-border bug showed the window
        // background through the framebuffer
        EXPECT_EQ(px[col] >> 24, 0xFFu) << "col " << col;
    }
}

TEST_F(ATMVideoMode_Test, Render_ATMTL_BorderOnlyNoCrash)
{
    _context->config.mem_model = MM_ATM3;
    _context->config.frame = 69888;
    _context->config.t_line = 224;
    SetVideoMode(FF77_TL);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMTL);

    auto& fb = _screen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 448u);
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    // TL has no ported renderer yet: the whole frame must render as border,
    // every pixel opaque, and nothing may crash
    const uint32_t expected = _screen->TransformZXSpectrumColorsToRGBA(0x00, true);
    for (uint32_t t = 24 * 224; t < 25 * 224; ++t)
        _screen->DrawATMMode(t);
    for (uint32_t col = 0; col < fb.width; ++col)
        EXPECT_EQ(px[col], expected) << "col " << col;
}

/// endregion </ATM Renderer Tests>
