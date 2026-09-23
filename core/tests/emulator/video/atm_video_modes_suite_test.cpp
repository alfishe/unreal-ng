#include "pch.h"
#include "emulator/video/atm/atmfont.h"
#include "emulator/video/zx/screenzx.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

#include <cstring>
#include <vector>

/// ATM Video Modes - comprehensive suite
/// Complements atm_videomode_test.cpp (basic detection + renderer spot checks) with:
///  - full mode matrices: FF77 (ATM710/ATM3 x modes 0-7), aFE (ATM450),
///    EFF7 AlCo (Pentagon) and EFF7-is-control-only (ATM3)
///  - raster geometry + timing invariants (69888 T/frame @ 224 T/line)
///  - 16-entry clut integrity (opaque ZX palette, transparent tail)
///  - FF77 port-decoder semantics: full byte into pFF77, address into aFF77,
///    alias decode (ATM710 low-byte vs ATM3 x0F77), mode-change gating
///  - 7FFD shadow-screen plane switching in the ATM renderer
///  - deep renderer coverage: TX text-row addressing (0x1C0 + 64*row) across
///    all 25 rows, per-scanline font lines, attr-quirk byte alignment,
///    TL dedicated-page linear text rows (ZX-Evo), MC/EGA linear 40-byte
///    plane stride, horizontal/vertical border geometry and RenderFrameBatch
///    vs per-t-state equivalence for every ATM mode
///
/// Reference: other/unrealspeccy draw.cpp (PrepareFrameATM2), dxr_atm0/2/6.cpp

class ATMVideoModesSuite_Test : public ::testing::Test
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
        ASSERT_NE(_screen, nullptr);
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

    /// (Re)create the whole stack with the model set BEFORE Core::Init() so
    /// the matching port decoder is installed (needed by port-semantics tests)
    void ReinitAs(MEM_MODEL model)
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

        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->config.mem_model = model;
        SetATMTiming();
        _cpu = new Core(_context);
        (void)_cpu->Init();
        _screen = dynamic_cast<ScreenZX*>(_context->pScreen);
        _memory = _context->pMemory;
        ASSERT_NE(_screen, nullptr);
        ASSERT_NE(_memory, nullptr);
    }

    /// ATM uses ZX-compatible 312-line PAL timing: 224 T/line, 69888 T/frame
    /// (reference unreal.ini PRESET.ATM1_2_3.5MHz / ApplyModelTimingDefaults)
    void SetATMTiming()
    {
        _context->config.frame = 69888;
        _context->config.t_line = 224;
    }

    void SetFF77Mode(uint8_t mode, uint8_t extra = 0x20)
    {
        _context->emulatorState.pFF77 = (mode & 0x07) | extra;  // bit 5 = INT gate
        _context->emulatorState.aFF77 = 0x0100;                 // PEN=1
    }

    /// Beam T-state inside the screen window: screen row 0 is beam line 68
    /// (24 vSync/vBlank lines + 44 screenOffsetTop), window starts at T 32
    static uint32_t BeamT(uint32_t screenY, uint32_t tInLine) { return (68 + screenY) * 224 + tInLine; }

    uint32_t InkColor(uint8_t attr) const { return _screen->TransformZXSpectrumColorsToRGBA(attr, true); }
    uint32_t PaperColor(uint8_t attr) const { return _screen->TransformZXSpectrumColorsToRGBA(attr, false); }
};

/// region <Mode detection matrices>

namespace
{

struct ModeExpectation
{
    uint8_t ff77Mode;      // FF77 bits 0..2 value
    VideoModeEnum mode;    // expected detected mode (after M_NUL fallback)
    RasterModeEnum raster; // expected raster mode
};

const ModeExpectation kATMFF77Matrix[] = {
    {FF77_16, M_ATM16, R_320_200},  // 0: EGA 16-color 320x200
    {0x01,    M_ZX48,  R_320_200},  // 1: unused -> fallback (raster stays 320x200)
    {FF77_MC, M_ATMHR, R_320_200},  // 2: HW multicolor 640x200
    {FF77_ZX, M_ZX48,  R_256_192},  // 3: ZX compat (ZX48-class timing)
    {0x04,    M_ZX48,  R_320_200},  // 4: unused -> fallback
    {0x05,    M_ZX48,  R_320_200},  // 5: unused -> fallback
    {FF77_TX, M_ATMTX, R_320_200},  // 6: text 80x25
    {FF77_TL, M_ZX48,  R_320_200},  // 7: TL is ATM3-only -> fallback on ATM710
};

} // namespace

TEST_F(ATMVideoModesSuite_Test, ModeMatrix_ATM710_FF77AllValues)
{
    _context->config.mem_model = MM_ATM710;
    SetATMTiming();

    for (const auto& e : kATMFF77Matrix)
    {
        SCOPED_TRACE(testing::Message() << "pFF77&7 = " << int(e.ff77Mode));
        SetFF77Mode(e.ff77Mode);
        _screen->InitRaster();
        EXPECT_EQ(_screen->_vid.mode, e.mode);
        EXPECT_EQ(_screen->_vid.raster.num, e.raster);
        EXPECT_EQ(_screen->GetVideoMode(), e.mode);  // applied to the renderer
    }
}

TEST_F(ATMVideoModesSuite_Test, ModeMatrix_ATM3_FF77AllValues)
{
    _context->config.mem_model = MM_ATM3;
    SetATMTiming();

    for (const auto& e : kATMFF77Matrix)
    {
        SCOPED_TRACE(testing::Message() << "pFF77&7 = " << int(e.ff77Mode));
        const VideoModeEnum expected = (e.ff77Mode == FF77_TL) ? M_ATMTL : e.mode;
        SetFF77Mode(e.ff77Mode);
        _screen->InitRaster();
        EXPECT_EQ(_screen->_vid.mode, expected);
        EXPECT_EQ(_screen->GetVideoMode(), expected);
    }
}

TEST_F(ATMVideoModesSuite_Test, ModeMatrix_ATM450_AFEBits)
{
    _context->config.mem_model = MM_ATM450;
    SetATMTiming();

    // ATM1 selects extended modes via aFE bits 5-6 ((aFE >> 5) & 3)
    const struct
    {
        uint8_t afe;
        VideoModeEnum mode;
        RasterModeEnum raster;
    } cases[] = {
        {uint8_t(aFE_16 << 5), M_ATM16, R_320_200},
        {uint8_t(aFE_MC << 5), M_ATMHR, R_320_200},
        {uint8_t(0x02 << 5), M_ZX48, R_320_200},   // mode 2 unused -> fallback
        {uint8_t(FF77_ZX << 5), M_ZX48, R_256_192}, // mode 3 = ZX compat
    };
    for (const auto& e : cases)
    {
        SCOPED_TRACE(testing::Message() << "aFE bits 5-6 = " << int(e.afe >> 5));
        _context->emulatorState.aFE = e.afe;
        _screen->InitRaster();
        EXPECT_EQ(_screen->_vid.mode, e.mode);
        EXPECT_EQ(_screen->_vid.raster.num, e.raster);
        EXPECT_EQ(_screen->GetVideoMode(), e.mode);
    }
}

TEST_F(ATMVideoModesSuite_Test, ModeMatrix_ATM3_EFF7ZBits_HierarchicalDecode)
{
    // BaseConf FPGA (alfishe/pentevo video_modedecode.v): FF77 selects the
    // mode family FIRST; pent_vmode = {pEFF7.0, pEFF7.5} only applies within
    // FF77 = 3 - z0 alone = ALCO (M_P16), z5 alone = hardware multicolor
    // (M_PMC), both or neither = plain ZX (2'b11 is undefined on hardware and
    // falls back to ZX). With FF77 != 3 the ATM mode wins and the z-bits are
    // ignored. (xpeccy's flat `(pEFF7 & 0x20) | ((pEFF7 & 0x01) << 1) |
    // (pFF77 & 7)` composition makes its own ALCO case unreachable - see
    // Screen::DetectModeATM3.)
    _context->config.mem_model = MM_ATM3;
    SetATMTiming();

    const struct
    {
        uint8_t ff77;
        uint8_t eff7;
        VideoModeEnum mode;
        RasterModeEnum raster;
    } cases[] = {
        // ZX base mode + z-bit combinations
        {FF77_ZX, 0x00,                            M_ZX48,  R_256_192},
        {FF77_ZX, EFF7_4BPP,                       M_P16,   R_256_192},  // z0: ALCO 16-color
        {FF77_ZX, EFF7_HWMC,                       M_PMC,   R_256_192},  // z5: hardware multicolor
        {FF77_ZX, uint8_t(EFF7_4BPP | EFF7_HWMC), M_ZX48,  R_256_192},  // 2'b11 undefined -> ZX
        {FF77_ZX, 0xFF,                            M_ZX48,  R_256_192},  // control bits sneak no mode in
        // Extended FF77 modes ignore the z-bits (atm_vmode wins)
        {FF77_16, EFF7_4BPP,                       M_ATM16, R_320_200},
        {FF77_16, EFF7_HWMC,                       M_ATM16, R_320_200},
        {FF77_MC, EFF7_HWMC,                       M_ATMHR, R_320_200},
        {FF77_TX, EFF7_4BPP,                       M_ATMTX, R_320_200},
        // z-bits don't rescue the undefined FF77 values (1/4/5)
        {0x01,    EFF7_4BPP,                       M_ZX48,  R_320_200},
    };
    for (const auto& e : cases)
    {
        SCOPED_TRACE(testing::Message() << "pFF77=" << int(e.ff77) << " pEFF7=0x" << std::hex << int(e.eff7));
        SetFF77Mode(e.ff77);
        _context->emulatorState.pEFF7 = e.eff7;
        _screen->InitRaster();
        EXPECT_EQ(_screen->_vid.mode, e.mode);
        EXPECT_EQ(_screen->_vid.raster.num, e.raster);
        EXPECT_EQ(_screen->GetVideoMode(), e.mode);
    }
}

TEST_F(ATMVideoModesSuite_Test, ModeMatrix_Pentagon_EFF7AlCo)
{
    _context->config.mem_model = MM_PENTAGON;
    _context->config.frame = 71680;  // Pentagon: 224 T/line x 320 lines
    _context->config.t_line = 224;

    const struct
    {
        uint8_t eff7;
        VideoModeEnum mode;
        RasterModeEnum raster;
    } cases[] = {
        {0x00, M_PENTAGON128K, R_256_192},
        {EFF7_4BPP, M_P16, R_256_192},
        {EFF7_HWMC, M_PMC, R_256_192},
        {EFF7_512, M_PHR, R_256_192},
        {EFF7_384, M_P384, R_384_304},
        {uint8_t(EFF7_4BPP | EFF7_512), M_ZX48, R_256_192},  // combo -> M_NUL -> fallback
    };
    for (const auto& e : cases)
    {
        SCOPED_TRACE(testing::Message() << "pEFF7 = 0x" << std::hex << int(e.eff7));
        _context->emulatorState.pEFF7 = e.eff7;
        _screen->InitRaster();
        EXPECT_EQ(_screen->_vid.mode, e.mode);
        EXPECT_EQ(_screen->_vid.raster.num, e.raster);
    }
}

/// endregion </Mode detection matrices>

/// region <Geometry and timing>

TEST_F(ATMVideoModesSuite_Test, Geometry_ATMDescriptorsAndTiming)
{
    _context->config.mem_model = MM_ATM3;  // covers TL too; descriptors match ATM710
    SetATMTiming();

    const struct
    {
        uint8_t ff77;
        VideoModeEnum m;
        uint16_t w, h, sw, sh, ol, ot;
    } cases[] = {
        {FF77_16, M_ATM16, 448, 288, 320, 200, 64, 44},
        {FF77_MC, M_ATMHR, 704, 288, 640, 200, 32, 44},
        {FF77_TX, M_ATMTX, 704, 288, 640, 200, 32, 44},
        {FF77_TL, M_ATMTL, 704, 288, 640, 200, 32, 44},
    };
    for (const auto& c : cases)
    {
        SCOPED_TRACE(testing::Message() << "mode " << c.m);
        SetFF77Mode(c.ff77);
        _screen->InitRaster();
        ASSERT_EQ(_screen->GetVideoMode(), c.m);

        auto& fb = _screen->GetFramebufferDescriptor();
        EXPECT_EQ(fb.width, c.w);
        EXPECT_EQ(fb.height, c.h);

        const RasterDescriptor& rd = _screen->rasterDescriptors[c.m];
        EXPECT_EQ(rd.screenWidth, c.sw);
        EXPECT_EQ(rd.screenHeight, c.sh);
        EXPECT_EQ(rd.screenOffsetLeft, c.ol);
        EXPECT_EQ(rd.screenOffsetTop, c.ot);

        // ATM timing invariant: 224 T/line, 16 vSync + 8 vBlank + 288 = 312 lines
        EXPECT_EQ(_screen->GetTstatesPerLine(), 224u);
        EXPECT_EQ(_screen->GetMaxFrameTiming(), 69888u);
        EXPECT_EQ((rd.vSyncLines + rd.vBlankLines + rd.fullFrameHeight) * 224u, 69888u);

        // Screen window fits into the frame storage
        EXPECT_LE(uint32_t(rd.screenOffsetLeft) + rd.screenWidth, rd.fullFrameWidth);
        EXPECT_LE(uint32_t(rd.screenOffsetTop) + rd.screenHeight, rd.fullFrameHeight);
    }
}

TEST_F(ATMVideoModesSuite_Test, Geometry_ZXCompatMode_TimingAndFramebuffer)
{
    _context->config.mem_model = MM_ATM710;
    SetATMTiming();

    // Force a mode application first: only a real mode CHANGE refreshes
    // _rasterState (SetVideoMode) - ZX-compat keeps M_ZX48
    SetFF77Mode(FF77_16);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATM16);

    SetFF77Mode(FF77_ZX);
    _screen->InitRaster();
    EXPECT_EQ(_screen->GetVideoMode(), M_ZX48);
    EXPECT_EQ(_screen->GetMaxFrameTiming(), 69888u);  // ZX48: 224 x (8+16+288)
    EXPECT_EQ(_screen->GetTstatesPerLine(), 224u);

    auto& fb = _screen->GetFramebufferDescriptor();
    EXPECT_EQ(fb.width, 352u);
    EXPECT_EQ(fb.height, 288u);
}

TEST_F(ATMVideoModesSuite_Test, Clut_ZX16DefaultsOpaque_TransparentTail)
{
    // 16-entry ZX palette defaults (RGBA8888 LE = 0xAABBGGRR); entries 16..255
    // are transparent (unused by the 4-bit EGA indices)
    const uint32_t expected[16] = {
        0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4,
        0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
        0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF,
        0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF,
    };
    for (uint32_t i = 0; i < 16; ++i)
        EXPECT_EQ(_screen->_vid.clut[i], expected[i]) << "clut[" << i << "]";
    for (uint32_t i = 16; i < 256; ++i)
        EXPECT_EQ(_screen->_vid.clut[i], 0x00000000u) << "clut[" << i << "]";
}

TEST_F(ATMVideoModesSuite_Test, Framebuffer_ModeSwitch_ClearsToOpaqueBlack)
{
    // The framebuffer is RGBA8888 and MUST never expose transparency: the GUI
    // composites it over the window background. A 0x00-alpha clear (the old
    // memset) flashed the background through on every mode switch - the ATM
    // BIOS reprograms FF77 several times at boot, so each switch re-clears.
    _context->config.mem_model = MM_ATM3;  // TL/TX share the 704x288 frame
    SetATMTiming();

    // Fresh allocation: ZX default (352x288) -> TL (704x288) reallocates
    SetFF77Mode(FF77_TL);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMTL);

    auto& fb = _screen->GetFramebufferDescriptor();
    const size_t count = static_cast<size_t>(fb.width) * fb.height;
    auto* px = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
    for (size_t i = 0; i < count; ++i)
        ASSERT_EQ(px[i], 0xFF000000u) << "fresh alloc pixel " << i;

    // Same-size switch: TL (704x288) -> TX (704x288) keeps buffers, re-clears
    SetFF77Mode(FF77_TX);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMTX);
    px = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
    for (size_t i = 0; i < count; ++i)
        ASSERT_EQ(px[i], 0xFF000000u) << "same-size switch pixel " << i;
}

/// endregion </Geometry and timing>

/// region <Port decoder semantics>

TEST_F(ATMVideoModesSuite_Test, PortFF77_StoresFullByteInPFF77_AddressInAFF77)
{
    ReinitAs(MM_ATM710);
    auto* pd = _context->pPortDecoder;
    ASSERT_NE(pd, nullptr);

    // Full 8-bit value (turbo/memswap/intgate bits included); address captured
    pd->DecodePortOut(0xFF77, 0xC3, 0);
    EXPECT_EQ(_context->emulatorState.pFF77, 0xC3);
    EXPECT_EQ(_context->emulatorState.aFF77, 0xFF77);
    EXPECT_EQ(_screen->_vid.mode, M_ZX48);  // mode bits 3 = ZX compat
}

TEST_F(ATMVideoModesSuite_Test, PortFF77_ATM710Aliases_LowByteDecode)
{
    ReinitAs(MM_ATM710);
    auto* pd = _context->pPortDecoder;

    // ATM710 decodes the LOW byte only: any port x77 hits FF77
    for (uint16_t port : {uint16_t(0x0077), uint16_t(0x0177), uint16_t(0x0F77), uint16_t(0xFF77)})
    {
        SCOPED_TRACE(testing::Message() << "port 0x" << std::hex << port);
        pd->DecodePortOut(port, 0x23, 0);
        EXPECT_EQ(_context->emulatorState.pFF77, 0x23);
        EXPECT_EQ(_context->emulatorState.aFF77, port);
    }

    // Non-matching ports must not touch FF77 state (0x17F7 = FFF7 bank select)
    _context->emulatorState.pFF77 = 0x42;
    for (uint16_t port : {uint16_t(0x0075), uint16_t(0x0179), uint16_t(0x17F7)})
    {
        SCOPED_TRACE(testing::Message() << "port 0x" << std::hex << port);
        pd->DecodePortOut(port, 0x00, 0);
        EXPECT_EQ(_context->emulatorState.pFF77, 0x42) << "port must not decode as FF77";
    }
}

TEST_F(ATMVideoModesSuite_Test, PortFF77_ATM3PartialDecode_MatchesXF77)
{
    ReinitAs(MM_ATM3);
    auto* pd = _context->pPortDecoder;

    // xx77 responses live behind the DOS-port gate (original io.cpp CF_DOSPORTS
    // block). Writing FF77 first latches aFF77 = 0xFF77 (cpm set) which would
    // CLOSE the gate for the aliases - hold it open with shaden (pBF.0)
    _context->emulatorState.pBF = 0x01;

    // ATM3 decodes low byte 0x77 (original io.cpp `p1 == 0x77`): FF77, any
    // high-byte alias, and the BaseConf manager-enable port 0xBC77
    for (uint16_t port : {uint16_t(0xFF77), uint16_t(0x1F77), uint16_t(0x0F77), uint16_t(0xBC77)})
    {
        SCOPED_TRACE(testing::Message() << "port 0x" << std::hex << port);
        pd->DecodePortOut(port, 0x23, 0);
        EXPECT_EQ(_context->emulatorState.pFF77, 0x23);
    }

    // Plain low-byte x77 ports decode through the same handler
    pd->DecodePortOut(0x0077, 0x05, 0);
    EXPECT_EQ(_context->emulatorState.pFF77, 0x05)
        << "low-byte 0x77 ports land in the ATM3 FF77 handler";
}

TEST_F(ATMVideoModesSuite_Test, PortFF77_ModeBitsUnchanged_NoRedetection)
{
    ReinitAs(MM_ATM710);
    auto* pd = _context->pPortDecoder;

    // Write through the SYSEN-open xx77 alias (full low-byte 0x77 decode,
    // same handler): this bare fixture has no DOS/SYS ROMs, so no CF_DOSPORTS
    // session can latch - after a #FF77 write latches CPM into aFF77, the
    // next #FF77 write would be gated out by the DOSEN || SYSEN gate
    pd->DecodePortOut(0x0077, 0x23, 0);  // mode 3 (ZX) + INT gate
    EXPECT_EQ(_screen->_vid.mode, M_ZX48);

    // Sentinel: only InitRaster rewrites _vid.mode
    _screen->_vid.mode = M_ATMTX;

    // Same mode bits, different control bits (turbo 0x08): no re-detection
    pd->DecodePortOut(0x0077, 0x2B, 0);
    EXPECT_EQ(_context->emulatorState.pFF77, 0x2B);
    EXPECT_EQ(_screen->_vid.mode, M_ATMTX) << "no video-mode-bit change must not re-detect";

    // Mode bits 3 -> 0: re-detection runs and applies EGA
    pd->DecodePortOut(0x0077, 0x20, 0);
    EXPECT_EQ(_screen->_vid.mode, M_ATM16);
    EXPECT_EQ(_screen->GetVideoMode(), M_ATM16);
}

TEST_F(ATMVideoModesSuite_Test, PortEFF7_ControlBitsStored_ZBitsTriggerRedetection)
{
    ReinitAs(MM_ATM3);
    auto* pd = _context->pPortDecoder;

    // Hold the DOS-port gate open for the xx77 write: #FF77 latches aFF77
    // with cpm set, which would close it afterwards
    _context->emulatorState.pBF = 0x01;

    pd->DecodePortOut(0xFF77, 0x23, 0);  // ZX compat base
    EXPECT_EQ(_screen->_vid.mode, M_ZX48);

    _screen->_vid.mode = M_ATMTX;  // sentinel: only InitRaster rewrites it

    // Control-only EFF7 bits (turbo / lockmem / rocache): stored, no redetect
    pd->DecodePortOut(0xEFF7, 0x1C, 0);
    EXPECT_EQ(_context->emulatorState.pEFF7, 0x1C);
    EXPECT_EQ(_screen->_vid.mode, M_ATMTX) << "control-only EFF7 write must not re-detect";

    // z0 set: video bits changed -> InitRaster runs and picks ALCO
    pd->DecodePortOut(0xEFF7, 0x1D, 0);
    EXPECT_EQ(_screen->GetVideoMode(), M_P16);
    // The ATM 312-line raster is held: 69888 T, not the Pentagon-class 71680
    EXPECT_EQ(_screen->GetMaxFrameTiming(), 69888u);

    // z0 + z5: the undefined combo falls back to plain ZX (still redetects)
    pd->DecodePortOut(0xEFF7, 0x3D, 0);
    EXPECT_EQ(_screen->GetVideoMode(), M_ZX48);
    EXPECT_EQ(_screen->GetMaxFrameTiming(), 69888u);

    // z5 alone over the ZX base: hardware multicolor
    pd->DecodePortOut(0xEFF7, 0x20, 0);
    EXPECT_EQ(_screen->GetVideoMode(), M_PMC);
    EXPECT_EQ(_screen->GetMaxFrameTiming(), 69888u);
}

TEST_F(ATMVideoModesSuite_Test, Port7FFD_ShadowBit_SwitchesRendererPlanes)
{
    ReinitAs(MM_ATM710);
    auto* pd = _context->pPortDecoder;
    // Reset default pFF77 = 0x00 already encodes mode 0 (EGA): a lone 0x20
    // write changes no mode bits, so no re-detection is triggered. Establish
    // the mode via a real transition (ZX -> EGA), as software does at boot.
    // SYSEN-open xx77 alias: no DOS/SYS ROMs in this fixture, so the 2nd
    // #FF77 write would be gated out (DOSEN || SYSEN)
    pd->DecodePortOut(0x0077, 0x23, 0);
    pd->DecodePortOut(0x0077, 0x20, 0);  // EGA
    ASSERT_EQ(_screen->GetVideoMode(), M_ATM16);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    uint8_t* p1 = _memory->RAMPageAddress(1);
    uint8_t* p5 = _memory->RAMPageAddress(5);
    uint8_t* p3 = _memory->RAMPageAddress(3);
    uint8_t* p7 = _memory->RAMPageAddress(7);
    ASSERT_NE(p3, nullptr);
    ASSERT_NE(p7, nullptr);

    // Normal screen (video page 5 / alt 1): plane0 nibbles 7 | 1 -> white/blue
    p1[0] = 0x0F;
    p5[0] = 0x00;
    // Shadow screen (video page 7 / alt 3): zero planes -> black
    p3[0] = 0x00;
    p7[0] = 0x00;

    const uint32_t* clut = _screen->_vid.clut;
    _screen->Draw(BeamT(0, 32));  // q=0, j=0 -> plane ap+0
    EXPECT_EQ(At(44, 64), clut[7]);
    EXPECT_EQ(At(44, 65), clut[1]);

    // 7FFD bit 3 selects the shadow screen (video page 7 / alt 3)
    pd->DecodePortOut(0x7FFD, 0x08, 0);
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x08);

    _screen->Draw(BeamT(0, 32));
    EXPECT_EQ(At(44, 64), clut[0]);
    EXPECT_EQ(At(44, 65), clut[0]);
}

/// endregion </Port decoder semantics>

/// region <Renderer: TX text mode>

TEST_F(ATMVideoModesSuite_Test, Render_ATMTX_All25TextRows_AddressedAt1C0Plus64PerRow)
{
    _context->config.mem_model = MM_ATM710;
    SetATMTiming();
    SetFF77Mode(FF77_TX);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMTX);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    uint8_t* ap = _memory->RAMPageAddress(1);
    uint8_t* vp = _memory->RAMPageAddress(5);

    // Bright white ink on black for every char of both attr planes
    memset(ap + 0x2000 + 0x1C0, 0x47, 25 * 64);
    memset(ap + 1 + 0x1C0, 0x47, 25 * 64);

    // Text row r, char column 0: code 0x41+r at vp + 0x1C0 + 64r
    // (the old renderer bug read row 0's bytes for every row)
    for (uint32_t r = 0; r < 25; ++r)
        vp[0x1C0 + 64 * r] = static_cast<uint8_t>(0x41 + r);

    const uint32_t ink = InkColor(0x47);
    const uint32_t paper = PaperColor(0x47);

    for (uint32_t r = 0; r < 25; ++r)
    {
        SCOPED_TRACE(testing::Message() << "text row " << r);
        // Font row 0 of the glyph, both halves of the char cell (t, t+1)
        _screen->Draw(BeamT(8 * r, 32));
        _screen->Draw(BeamT(8 * r, 33));
        const uint8_t glyph = ATM_FONT[0x41 + r];
        const uint32_t row = 44 + 8 * r;
        for (uint32_t k = 0; k < 8; ++k)
            EXPECT_EQ(At(row, 32 + k), ((glyph >> (7 - k)) & 1) ? ink : paper) << "px " << k;
    }
}

TEST_F(ATMVideoModesSuite_Test, Render_ATMTX_PerScanlineFontLines)
{
    _context->config.mem_model = MM_ATM710;
    SetATMTiming();
    SetFF77Mode(FF77_TX);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMTX);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    uint8_t* ap = _memory->RAMPageAddress(1);
    uint8_t* vp = _memory->RAMPageAddress(5);
    memset(ap + 0x2000 + 0x1C0, 0x47, 25 * 64);
    memset(ap + 1 + 0x1C0, 0x47, 25 * 64);

    const uint8_t code = 0x41;  // 'A' in every row
    for (uint32_t r = 0; r < 25; ++r)
        vp[0x1C0 + 64 * r] = code;

    const uint32_t ink = InkColor(0x47);
    const uint32_t paper = PaperColor(0x47);

    // All 8 scanlines of text row 2 (screenY 16..23): scanline s must use
    // font line (screenY % 8) of the glyph, not a repeated line
    for (uint32_t s = 0; s < 8; ++s)
    {
        SCOPED_TRACE(testing::Message() << "scanline " << s);
        _screen->Draw(BeamT(16 + s, 32));
        _screen->Draw(BeamT(16 + s, 33));
        const uint8_t glyph = ATM_FONT[s * 256 + code];
        const uint32_t row = 44 + 16 + s;
        for (uint32_t k = 0; k < 8; ++k)
            EXPECT_EQ(At(row, 32 + k), ((glyph >> (7 - k)) & 1) ? ink : paper) << "px " << k;
    }
}

TEST_F(ATMVideoModesSuite_Test, Render_ATMTX_AttrQuirkByteAlignment)
{
    _context->config.mem_model = MM_ATM710;
    SetATMTiming();
    SetFF77Mode(FF77_TX);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMTX);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    uint8_t* ap = _memory->RAMPageAddress(1);
    uint8_t* vp = _memory->RAMPageAddress(5);
    constexpr uint32_t T0 = 0x1C0;

    ap[0x2000 + T0] = 0x47;               // p0 attrs (even columns)
    vp[T0] = 0x00;                        // p0 codes: blank glyph
    vp[T0 + 1] = 0x00;
    // Odd columns pair p1 codes with ap+1 attrs - the +1 quirk must follow
    // the byte index: col n=1 uses ap[1+T0+0], col n=3 uses ap[1+T0+1]
    vp[0x2000 + T0] = 0x42;               // 'B' at char column 1
    ap[1 + T0] = 0x20;                    // black ink on red paper
    vp[0x2000 + T0 + 1] = 0x44;           // 'D' at char column 3
    ap[1 + T0 + 1] = 0x30;                // black ink on yellow paper

    _screen->Draw(BeamT(0, 34));   // n=1, half=0 -> cols 40..43, bits 7..4
    _screen->Draw(BeamT(0, 38));   // n=3, half=0 -> cols 56..59, bits 7..4

    const uint8_t gB = ATM_FONT[0x42];
    const uint8_t gD = ATM_FONT[0x44];
    for (uint32_t k = 0; k < 4; ++k)
    {
        EXPECT_EQ(At(44, 40 + k), ((gB >> (7 - k)) & 1) ? InkColor(0x20) : PaperColor(0x20)) << "px " << k;
        EXPECT_EQ(At(44, 56 + k), ((gD >> (7 - k)) & 1) ? InkColor(0x30) : PaperColor(0x30)) << "px " << k;
    }
}

/// endregion </Renderer: TX text mode>

/// region <Renderer: TL text mode (ZX-Evo Text Linear)>

TEST_F(ATMVideoModesSuite_Test, Render_ATMTL_LinearRowStrideAndColumnParity)
{
    _context->config.mem_model = MM_ATM3;
    SetATMTiming();
    SetFF77Mode(FF77_TL);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMTL);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    // TL reads ONE dedicated page (videoPage==5 here -> RAM page 8) with
    // linear 64-byte text rows - no +0x2000 plane halves like TX (reference
    // ZXMAK2 EvoTxtRenderer):
    //   row r, even columns: codes at 0x01C0 + 64r, attrs at 0x31C0 + 64r
    //   row r, odd columns:  codes at 0x11C0 + 64r, attrs at 0x21C0 + 64r (+1)
    uint8_t* page = _memory->RAMPageAddress(8);
    memset(page, 0, 0x4000);

    for (uint32_t r = 0; r < 25; ++r)
    {
        page[0x01C0 + 64 * r] = static_cast<uint8_t>(0x41 + r);  // col 0 (even) code
        page[0x11C0 + 64 * r] = static_cast<uint8_t>(0x41 + r);  // col 1 (odd) code
        page[0x31C0 + 64 * r] = 0x47;                            // col 0 attr (+((0+1)>>1)=0)
        page[0x21C0 + 64 * r + 1] = 0x47;                        // col 1 attr (+((1+1)>>1)=1)
    }

    const uint32_t ink = InkColor(0x47);
    const uint32_t paper = PaperColor(0x47);

    // Scanline 0 of every text row: the same glyph at columns 0 and 1 proves
    // the 64-byte row stride for both parity regions
    for (uint32_t r = 0; r < 25; ++r)
    {
        SCOPED_TRACE(testing::Message() << "text row " << r);
        _screen->Draw(BeamT(8 * r, 32));  // col 0, bits 7..4
        _screen->Draw(BeamT(8 * r, 33));  // col 0, bits 3..0
        _screen->Draw(BeamT(8 * r, 34));  // col 1, bits 7..4
        const uint8_t glyph = ATM_FONT[0x41 + r];
        const uint32_t row = 44 + 8 * r;
        for (uint32_t k = 0; k < 8; ++k)
            EXPECT_EQ(At(row, 32 + k), ((glyph >> (7 - k)) & 1) ? ink : paper) << "col 0 px " << k;
        for (uint32_t k = 0; k < 4; ++k)
            EXPECT_EQ(At(row, 40 + k), ((glyph >> (7 - k)) & 1) ? ink : paper) << "col 1 px " << k;
    }

    // All 8 scanlines of text row 2 select font line (screenY % 8)
    for (uint32_t s = 0; s < 8; ++s)
    {
        SCOPED_TRACE(testing::Message() << "scanline " << s);
        _screen->Draw(BeamT(16 + s, 32));
        _screen->Draw(BeamT(16 + s, 33));
        const uint8_t glyph = ATM_FONT[s * 256 + 0x43];
        const uint32_t row = 44 + 16 + s;
        for (uint32_t k = 0; k < 8; ++k)
            EXPECT_EQ(At(row, 32 + k), ((glyph >> (7 - k)) & 1) ? ink : paper) << "px " << k;
    }
}

/// endregion </Renderer: TL text mode (ZX-Evo Text Linear)>

/// region <Renderer: plane strides and palette>

TEST_F(ATMVideoModesSuite_Test, Render_ATMHR_Linear40ByteStride_SecondLine)
{
    _context->config.mem_model = MM_ATM710;
    SetATMTiming();
    SetFF77Mode(FF77_MC);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMHR);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    uint8_t* ap = _memory->RAMPageAddress(1);
    uint8_t* vp = _memory->RAMPageAddress(5);

    vp[0] = 0x80;   ap[0] = 0x47;               // screenY 0: bit 7 -> px0 ink
    vp[40] = 0x10;  ap[40] = 0x47;              // screenY 1 (offset 40): bit 4 -> px3 ink
    vp[0x2000 + 40] = 0x80;  ap[0x2000 + 40] = 0x02;  // odd byte group, screenY 1

    _screen->Draw(BeamT(0, 32));         // row 44
    _screen->Draw(BeamT(1, 32));         // row 45
    _screen->Draw(BeamT(1, 34));         // row 45, odd byte group n=1

    EXPECT_EQ(At(44, 32), InkColor(0x47));
    EXPECT_EQ(At(45, 32), PaperColor(0x47));
    EXPECT_EQ(At(45, 35), InkColor(0x47)) << "bit 4 of the byte at offset 40";
    EXPECT_EQ(At(45, 40), InkColor(0x02)) << "odd byte group n=1 reads plane +0x2000";
    EXPECT_EQ(At(45, 41), PaperColor(0x02));
}

TEST_F(ATMVideoModesSuite_Test, Render_ATM16_PlaneHalvesStride_SecondLine)
{
    _context->config.mem_model = MM_ATM710;
    SetATMTiming();
    SetFF77Mode(FF77_16);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATM16);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    uint8_t* ap = _memory->RAMPageAddress(1);
    uint8_t* vp = _memory->RAMPageAddress(5);

    // screenY 1 (offset 40): only plane 2 (ap + 0x2000) byte 0 is non-zero
    ap[40] = 0x00;
    vp[40] = 0x00;
    vp[0x2000 + 40] = 0x00;
    ap[0x2000 + 40] = 0x0F;  // left nibble 7 (white), right 1 (blue)

    _screen->Draw(BeamT(1, 34));  // t'=2 -> j=0, q=2 -> plane ap+0x2000
    EXPECT_EQ(At(45, 68), _screen->_vid.clut[7]);
    EXPECT_EQ(At(45, 69), _screen->_vid.clut[1]);
}

TEST_F(ATMVideoModesSuite_Test, Render_ATM16_CLUTBrightFlagRegression)
{
    // The 4-bit EGA index carries BRIGHT in bit 3 - colors must come from the
    // 16-entry clut, not from _rgbaColors (which expects ULA attribute bytes
    // with brightness in bit 6 and would silently drop the flag)
    _context->config.mem_model = MM_ATM710;
    SetATMTiming();
    SetFF77Mode(FF77_16);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATM16);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    uint8_t* ap = _memory->RAMPageAddress(1);
    // bt = 0x8C: left = 4 (green, b6=0), right = 1|8 = 9 (BRIGHT blue, b7=1)
    ap[0] = 0x8C;

    _screen->Draw(BeamT(0, 32));
    EXPECT_EQ(px[44 * fb.width + 64], 0xFF25C500u);  // clut[4] green
    EXPECT_EQ(px[44 * fb.width + 65], 0xFFFB2B00u);  // clut[9] BRIGHT blue
    EXPECT_NE(px[44 * fb.width + 65], 0xFFC72200u);  // not the non-bright blue
}

/// endregion </Renderer: plane strides and palette>

/// region <Renderer: programmable palette (port #FF) and 4-bit border>

TEST_F(ATMVideoModesSuite_Test, Render_ATM16_PalettePortProgramsColorsAndBorder)
{
    // A single OUT (#9F),A reprograms the palette cell the 4-bit border
    // points at; EGA pixels AND the border then render through it (xpeccy
    // atm2OutFF / evoOutFF -> vid->pal is shared by every mode)
    ReinitAs(MM_ATM710);
    auto* pd = _context->pPortDecoder;
    EmulatorState& state = _context->emulatorState;

    // EGA via the SYSEN-open xx77 alias (no DOS/SYS ROMs in this fixture, so
    // a #FF77 write would close the DOSEN || SYSEN gate after the first one)
    pd->DecodePortOut(0x0077, 0x23, 0);
    pd->DecodePortOut(0x0077, 0x20, 0);
    ASSERT_EQ(_screen->GetVideoMode(), M_ATM16);

    // value 0x55 -> DAC lines (v = 0xAA): red = 10, green = 5, blue = 5
    // -> ABGR 0xFF5555AA (packing 0xAABBGGRR, red in the low byte).
    // #009F is a palette alias the Beta128 does not decode
    pd->DecodePortOut(0x009F, 0x55, 0);
    EXPECT_EQ(state.atmPalette[0], 0xFF5555AAu) << "border_attr = 0, bright = 0 -> cell 0";
    EXPECT_EQ(state.atmPaletteRegs[0], 0x55) << "raw byte stored pre-inversion";

    uint8_t* ap = _memory->RAMPageAddress(1);  // EGA plane q0 = ap + 0
    ap[0] = 0x0F;                              // pair (0,1): colors 7 and 1

    _screen->Draw(BeamT(0, 32));        // first pixel pair of row 44
    _screen->Draw(BeamT(0, 0));         // left border

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    EXPECT_EQ(px[44 * fb.width + 64], state.atmPalette[7]);
    EXPECT_EQ(px[44 * fb.width + 65], state.atmPalette[1]);
    EXPECT_EQ(px[44 * fb.width + 0], 0xFF5555AAu) << "border renders through the reprogrammed cell";
}

TEST_F(ATMVideoModesSuite_Test, Border_FEAddressBit3_SelectsBrightPaletteCell)
{
    // ATM 4-bit border: A3 of the #FE port address is the bright bit of the
    // palette cell pointer, re-latched by every write (xpeccy atm2OutFE /
    // evoOutFE: nextbrd |= (port ^ 8) & 8; BaseConf FPGA zports.v: border <=
    // {~a[3], din[2:0]} - port #FE gives bright 0, #F6 gives bright 1)
    ReinitAs(MM_ATM710);
    auto* pd = _context->pPortDecoder;
    EmulatorState& state = _context->emulatorState;

    pd->DecodePortOut(0x0077, 0x23, 0);
    pd->DecodePortOut(0x0077, 0x20, 0);
    ASSERT_EQ(_screen->GetVideoMode(), M_ATM16);

    pd->DecodePortOut(0x00FE, 0x02, 0);  // red border, A3 = 1 -> bright 0
    EXPECT_EQ(state.border_attr, 0x02);
    EXPECT_EQ(state.atmBorderBright, 0);

    pd->DecodePortOut(0x00F6, 0x02, 0);  // same color, A3 = 0 -> bright 1
    EXPECT_EQ(state.border_attr, 0x02);
    EXPECT_EQ(state.atmBorderBright, 1);

    // White into cell 2 | (1 << 3) = 10; the non-bright cell 2 stays preset
    pd->DecodePortOut(0x009F, 0x00, 0);
    EXPECT_EQ(state.atmPalette[10], 0xFFFFFFFFu);
    EXPECT_EQ(state.atmPalette[2], 0xFF1628D6u) << "cell 2 keeps the ZX red preset";

    _screen->Draw(BeamT(0, 0));
    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    EXPECT_EQ(px[44 * fb.width], 0xFFFFFFFFu) << "border picks the bright cell 10";

    // A3 is re-latched per write: back on #FE the border falls to cell 2
    pd->DecodePortOut(0x00FE, 0x02, 0);
    EXPECT_EQ(state.atmBorderBright, 0);
    _screen->Draw(BeamT(0, 0));
    EXPECT_EQ(px[44 * fb.width], 0xFF1628D6u);
}

TEST_F(ATMVideoModesSuite_Test, Render_ATMHR_AttributeBit7IsPaperBright_NoFlash)
{
    // vidATMDoubleDot decode (xpeccy, shared by HWM / TX / TL): attr bit 6 =
    // ink bright, bit 7 = PAPER bright - there is no flash. The old renderer
    // routed paper through _rgbaFlashColors, misreading bit 7 as flash
    _context->config.mem_model = MM_ATM710;
    SetATMTiming();
    SetFF77Mode(FF77_MC);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMHR);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    EmulatorState& state = _context->emulatorState;
    uint8_t* ap = _memory->RAMPageAddress(1);
    uint8_t* vp = _memory->RAMPageAddress(5);

    vp[0] = 0x80;  // bit 7 set -> first pixel ink, second paper
    ap[0] = 0xC7;  // ink = 7 | 8 = 15 (bright white), paper = 0 | 8 = 8 (bright black)

    _screen->Draw(BeamT(0, 32));  // byte group n=0, bits 7..4 -> cols 32..35
    EXPECT_EQ(At(44, 32), state.atmPalette[15]);
    EXPECT_EQ(At(44, 33), state.atmPalette[8]);
}

/// endregion </Renderer: programmable palette (port #FF) and 4-bit border>

/// region <Renderer: ATM3 EFF7 z-modes (ALCO / HWMC)>

TEST_F(ATMVideoModesSuite_Test, Render_ATM3Alco_FourPlanes_PagePair)
{
    // vidDrawAlco (xpeccy video.c): ZX screen addressing, planes at the
    // video page pair {vidPage ^ 1, vidPage} x {+0, +0x2000} - the ^1 pair,
    // NOT the ^4 pair the ATM extended modes use. Each byte holds two
    // adjacent pixels as 4-bit palette indices packed ZX-attribute-style
    ReinitAs(MM_ATM3);
    auto* pd = _context->pPortDecoder;

    // z0 over the ZX base -> ALCO (#EFF7 needs no manager gate)
    pd->DecodePortOut(0xFF77, 0x23, 0);
    pd->DecodePortOut(0xEFF7, EFF7_4BPP, 0);
    ASSERT_EQ(_screen->GetVideoMode(), M_P16);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    EmulatorState& state = _context->emulatorState;
    uint8_t* p4 = _memory->RAMPageAddress(4);  // videoPage 5 ^ 1
    uint8_t* p5 = _memory->RAMPageAddress(5);

    p4[0] = 0x0F;          // pair 0 (q0): left 7, right 1
    p5[0] = 0xC7;          // pair 1 (q1): left 15, right 8
    p4[0x2000] = 0x47;     // pair 2 (q2): left 15, right 0
    p5[0x2000] = 0x18;     // pair 3 (q3): left 0, right 3

    // LUT geometry: the ZX window starts at beam line 72, tInLine 24; one
    // t-state renders one pixel pair (zxX, zxX+1) at framebuffer (48 + zxX)
    for (uint32_t pair = 0; pair < 4; ++pair)
        _screen->DrawAlcoMode(72 * 224 + 24 + pair);

    EXPECT_EQ(At(48, 48 + 0), state.atmPalette[7]);
    EXPECT_EQ(At(48, 48 + 1), state.atmPalette[1]);
    EXPECT_EQ(At(48, 48 + 2), state.atmPalette[15]);
    EXPECT_EQ(At(48, 48 + 3), state.atmPalette[8]);
    EXPECT_EQ(At(48, 48 + 4), state.atmPalette[15]);
    EXPECT_EQ(At(48, 48 + 5), state.atmPalette[0]);
    EXPECT_EQ(At(48, 48 + 6), state.atmPalette[0]);
    EXPECT_EQ(At(48, 48 + 7), state.atmPalette[3]);
}

TEST_F(ATMVideoModesSuite_Test, Render_ATM3Hwmc_AttrFromPixelAddress_FlashBit)
{
    // vidDrawHwmc (xpeccy video.c): the bitmap byte AND the attribute byte
    // are both fetched from the PIXEL address of the video page (same byte).
    // Attr decode: ink = bits 0-2 + bit 6, paper = bits 3-6 (bit 6 brights
    // both), bit 7 = flash - inverts the bitmap on the 16-frame phase
    ReinitAs(MM_ATM3);
    auto* pd = _context->pPortDecoder;

    pd->DecodePortOut(0xFF77, 0x23, 0);
    pd->DecodePortOut(0xEFF7, EFF7_HWMC, 0);  // z5 -> hardware multicolor
    ASSERT_EQ(_screen->GetVideoMode(), M_PMC);

    auto& fb = _screen->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    EmulatorState& state = _context->emulatorState;
    uint8_t* p5 = _memory->RAMPageAddress(5);

    p5[0] = 0x47;  // ink = 7 | 8 = 15, paper = 8; bitmap bits 7,6 = 0, 1
    _screen->DrawAlcoMode(72 * 224 + 24 + 0);
    EXPECT_EQ(At(48, 48), state.atmPalette[8]) << "bit 7 clear -> paper";
    EXPECT_EQ(At(48, 49), state.atmPalette[15]) << "bit 6 set -> ink";

    // Bit 7 = flash: on the flash phase the bitmap inverts, attrs untouched
    p5[0] = 0xC7;  // ink = 15, paper = 8; bitmap 0xC7 -> ~ = 0x38
    _screen->_vid.flash = 1;
    _screen->DrawAlcoMode(72 * 224 + 24 + 0);
    EXPECT_EQ(At(48, 48), state.atmPalette[8]);
    EXPECT_EQ(At(48, 49), state.atmPalette[8]);

    _screen->_vid.flash = 0;
    _screen->DrawAlcoMode(72 * 224 + 24 + 0);
    EXPECT_EQ(At(48, 48), state.atmPalette[15]) << "flash off restores the raw bitmap";
    EXPECT_EQ(At(48, 49), state.atmPalette[15]);
}

TEST_F(ATMVideoModesSuite_Test, Timing_ATM3ZModes_KeepAtm312LineFrame)
{
    // The ZX-Evo BaseConf sync generator never leaves the ATM 312-line /
    // 69888 T raster (xpeccy evoSetVideoMode swaps the pixel fetcher only,
    // never the sync), so M_P16/M_PMC on ATM3 must NOT adopt the
    // Pentagon-class 320-line / 71680 T descriptors their ids carry
    _context->config.mem_model = MM_ATM3;
    SetATMTiming();

    for (uint8_t eff7 : {uint8_t(EFF7_4BPP), uint8_t(EFF7_HWMC)})
    {
        SCOPED_TRACE(testing::Message() << "pEFF7 = 0x" << std::hex << int(eff7));
        SetFF77Mode(FF77_ZX);
        _context->emulatorState.pEFF7 = eff7;
        _screen->InitRaster();
        ASSERT_TRUE(_screen->GetVideoMode() == M_P16 || _screen->GetVideoMode() == M_PMC);

        EXPECT_EQ(_screen->GetTstatesPerLine(), 224u);
        EXPECT_EQ(_screen->GetMaxFrameTiming(), 69888u);

        // 69888 T is exactly one frame: 69700 (last line) is in, 69888 is out
        uint16_t x, y;
        EXPECT_TRUE(_screen->TransformTstateToFramebufferCoords(69700, &x, &y));
        EXPECT_FALSE(_screen->TransformTstateToFramebufferCoords(69888, &x, &y))
            << "69888 T starts the next frame on ATM3";
    }

    // Contrast: Pentagon M_P16 really is the 320-line / 71680 T raster
    _context->config.mem_model = MM_PENTAGON;
    _context->config.frame = 71680;
    _context->emulatorState.pEFF7 = EFF7_4BPP;
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_P16);
    EXPECT_EQ(_screen->GetMaxFrameTiming(), 71680u);
    uint16_t x, y;
    EXPECT_TRUE(_screen->TransformTstateToFramebufferCoords(69888, &x, &y))
        << "69888 T is still inside the Pentagon 320-line frame";
}

/// endregion </Renderer: ATM3 EFF7 z-modes (ALCO / HWMC)>

/// region <Renderer: border geometry and batch equivalence>

TEST_F(ATMVideoModesSuite_Test, Render_ScreenWindowHorizontalGeometry_PerMode)
{
    const struct
    {
        MEM_MODEL model;
        uint8_t ff77;
        VideoModeEnum m;
        uint32_t offL, screenW;
    } cases[] = {
        {MM_ATM710, FF77_16, M_ATM16, 64, 320},
        {MM_ATM710, FF77_MC, M_ATMHR, 32, 640},
        {MM_ATM710, FF77_TX, M_ATMTX, 32, 640},
        {MM_ATM3, FF77_TL, M_ATMTL, 32, 640},
    };
    for (const auto& c : cases)
    {
        SCOPED_TRACE(testing::Message() << "mode " << c.m);
        _context->config.mem_model = c.model;
        SetATMTiming();
        SetFF77Mode(c.ff77);
        _screen->InitRaster();
        ASSERT_EQ(_screen->GetVideoMode(), c.m);

        // All-zero planes -> the whole screen window renders black in every
        // mode (TL reads its dedicated page 8/10, zeroed here too)
        memset(_memory->RAMPageAddress(1), 0, 0x4000);
        memset(_memory->RAMPageAddress(5), 0, 0x4000);
        memset(_memory->RAMPageAddress(8), 0, 0x4000);

        // Red border via the standard port FE path
        _context->pPortDecoder->DecodePortOut(0x00FE, 0x02, 0);
        const uint32_t border = InkColor(0x02);

        // Sweep one full screen row (beam line 68 = screen row 0)
        for (uint32_t t = 68 * 224; t < 69 * 224; ++t)
            _screen->Draw(t);

        auto& fb = _screen->GetFramebufferDescriptor();
        auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
        constexpr uint32_t row = 44;
        EXPECT_EQ(px[row * fb.width + 0], border);
        EXPECT_EQ(px[row * fb.width + (c.offL - 1)], border) << "last left-border col";
        EXPECT_EQ(px[row * fb.width + c.offL], 0xFF000000u) << "first screen col";
        EXPECT_EQ(px[row * fb.width + (c.offL + c.screenW - 1)], 0xFF000000u) << "last screen col";
        EXPECT_EQ(px[row * fb.width + (c.offL + c.screenW)], border) << "first right-border col";
        EXPECT_EQ(px[row * fb.width + (fb.width - 1)], border) << "last frame col";
    }
}

TEST_F(ATMVideoModesSuite_Test, Render_VerticalGeometry_FullFrameBatch_EGA)
{
    ReinitAs(MM_ATM710);
    auto* pd = _context->pPortDecoder;
    // SYSEN-open xx77 alias: no DOS/SYS ROMs in this fixture, so the 2nd
    // #FF77 write would be gated out (DOSEN || SYSEN)
    pd->DecodePortOut(0x0077, 0x23, 0);
    pd->DecodePortOut(0x0077, 0x20, 0);  // EGA
    ASSERT_EQ(_screen->GetVideoMode(), M_ATM16);

    memset(_memory->RAMPageAddress(1), 0, 0x4000);
    memset(_memory->RAMPageAddress(5), 0, 0x4000);
    pd->DecodePortOut(0x00FE, 0x02, 0);  // red border
    const uint32_t border = InkColor(0x02);
    const uint32_t black = 0xFF000000u;

    _screen->RenderFrameBatch();

    auto& fb = _screen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 448u);
    ASSERT_EQ(fb.height, 288u);
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    // Top border rows 0..43 and bottom rows 244..287: full-width border
    for (uint32_t row : {0u, 43u, 244u, 287u})
        for (uint32_t col = 0; col < fb.width; ++col)
            ASSERT_EQ(px[row * fb.width + col], border) << "row " << row << " col " << col;

    // Screen rows 44..243: 64-px side borders, black screen window
    for (uint32_t row : {44u, 150u, 243u})
    {
        EXPECT_EQ(px[row * fb.width + 0], border) << "row " << row;
        EXPECT_EQ(px[row * fb.width + 63], border) << "row " << row;
        EXPECT_EQ(px[row * fb.width + 64], black) << "row " << row;
        EXPECT_EQ(px[row * fb.width + 383], black) << "row " << row;
        EXPECT_EQ(px[row * fb.width + 384], border) << "row " << row;
        EXPECT_EQ(px[row * fb.width + 447], border) << "row " << row;
    }
}

TEST_F(ATMVideoModesSuite_Test, RenderFrameBatch_EquivalentToPerTstate_AllModes)
{
    const struct
    {
        MEM_MODEL model;
        uint8_t ff77;
        VideoModeEnum m;
    } cases[] = {
        {MM_ATM710, FF77_16, M_ATM16},
        {MM_ATM710, FF77_MC, M_ATMHR},
        {MM_ATM710, FF77_TX, M_ATMTX},
        {MM_ATM3, FF77_TL, M_ATMTL},
    };
    for (const auto& c : cases)
    {
        SCOPED_TRACE(testing::Message() << "mode " << c.m);
        _context->config.mem_model = c.model;
        SetATMTiming();
        SetFF77Mode(c.ff77);
        _screen->InitRaster();
        ASSERT_EQ(_screen->GetVideoMode(), c.m);

        // Deterministic pseudo-random plane content
        auto fill = [](uint8_t* page) {
            for (uint32_t i = 0; i < 0x4000; ++i)
                page[i] = static_cast<uint8_t>((i * 2654435761u) >> 24);
        };
        fill(_memory->RAMPageAddress(1));
        fill(_memory->RAMPageAddress(5));
        fill(_memory->RAMPageAddress(8));  // TL dedicated page (unused elsewhere)

        _screen->RenderFrameBatch();

        auto& fb = _screen->GetFramebufferDescriptor();
        std::vector<uint8_t> batch(fb.memoryBuffer, fb.memoryBuffer + fb.memoryBufferSize);
        memset(fb.memoryBuffer, 0xAA, fb.memoryBufferSize);
        for (uint32_t t = 0; t < 69888; ++t)
            _screen->Draw(t);

        EXPECT_EQ(memcmp(batch.data(), fb.memoryBuffer, fb.memoryBufferSize), 0)
            << "RenderFrameBatch must match per-t-state rendering";
    }
}

TEST_F(ATMVideoModesSuite_Test, RenderFrameBatch_ATMTL_TextFromDedicatedPage)
{
    _context->config.mem_model = MM_ATM3;
    SetATMTiming();
    SetFF77Mode(FF77_TL);
    _screen->InitRaster();
    ASSERT_EQ(_screen->GetVideoMode(), M_ATMTL);

    // TL must read the DEDICATED page (videoPage==5 -> RAM page 8): fill
    // only page 8 and leave page 5 all-ink - any leak of the standard video
    // plane would light up the whole screen
    uint8_t* page = _memory->RAMPageAddress(8);
    memset(page, 0, 0x4000);
    memset(_memory->RAMPageAddress(5), 0xFF, 0x4000);

    // Text row 0: 'A' at the even-column slot (+0x01C0), 'B' at the
    // odd-column slot (+0x11C0); attrs 0x47 in both parity slots
    // (+0x31C0 for even n, +0x21C0+1 for odd n=1)
    page[0x01C0] = 0x41;
    page[0x11C0] = 0x42;
    page[0x31C0] = 0x47;
    page[0x21C0 + 1] = 0x47;

    _context->pPortDecoder->DecodePortOut(0x00FE, 0x05, 0);  // cyan border
    const uint32_t border = InkColor(0x05);

    _screen->RenderFrameBatch();

    auto& fb = _screen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 704u);
    ASSERT_EQ(fb.height, 288u);
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    // Border geometry: 32-px side borders, 44-row top/bottom borders
    for (uint32_t col : {0u, 31u, 672u, 703u})
        EXPECT_EQ(At(44, col), border) << "side border col " << col;
    for (uint32_t row : {0u, 43u, 244u, 287u})
        EXPECT_EQ(At(row, 32), border) << "top/bottom border row " << row;

    // Char columns 0 ('A') and 1 ('B') at framebuffer cols 32..47, row 44
    // (scanline 0 -> font line 0), MSB-first bits
    const uint8_t glyphA = ATM_FONT[0x41];
    const uint8_t glyphB = ATM_FONT[0x42];
    for (uint32_t k = 0; k < 8; ++k)
    {
        EXPECT_EQ(At(44, 32 + k), ((glyphA >> (7 - k)) & 1) ? InkColor(0x47) : PaperColor(0x47)) << "'A' px " << k;
        EXPECT_EQ(At(44, 40 + k), ((glyphB >> (7 - k)) & 1) ? InkColor(0x47) : PaperColor(0x47)) << "'B' px " << k;
    }
}

/// endregion </Renderer: border geometry and batch equivalence>
