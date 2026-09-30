#pragma once
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/video/map/videowritelog.h"
#include "emulator/video/temporaleffects.h"
#include "stdafx.h"

class Z80;

/// region <Constants>

// Video framebuffer and rendering related
#define MAX_WIDTH_P (64 * 2)
#define MAX_WIDTH 512
#define MAX_HEIGHT 320
#define MAX_BUFFERS 8

#define VID_TACTS 224   // CPU ticks per PAL video scan line (@3.5MHz)
#define VID_LINES 320   // Full-screen height in lines for standard ZX-Spectrum
#define VID_WIDTH 448   // Full-screen width for standard ZX-Spectrum
#define VID_HEIGHT 320  // Full-screen height in pixels (same as in scan lines)

/// endregion </Constants>

/// region <Enumerations>

enum SpectrumScreenEnum : uint8_t
{
    SCREEN_NORMAL = 0,
    SCREEN_SHADOW = 1
};

enum VideoModeEnum : uint8_t
{
    M_NUL = 0,       // Non-existing mode / headless
    M_ZX48,          // Sinclair ZX-Spectrum 48k
    M_ZX128,         // Sinclair ZX-Spectrum 128k / +2 / +3
    M_PENTAGON128K,  // Pentagon 128k timings
    M_PMC,           // Pentagon Multicolor
    M_P16,           // Pentagon 16c
    M_P384,          // Pentagon 384x304
    M_PHR,           // Pentagon HiRes

    M_TIMEX,  // Timex with 32 x 192 attributes (2 colors per line)

    M_TS16,   // TS 16c
    M_TS256,  // TS 256c
    M_TSTX,   // TS Text
    M_ATM16,  // ATM 16c
    M_ATMHR,  // ATM HiRes
    M_ATMTX,  // ATM Text
    M_ATMTL,  // ATM Text Linear
    M_PROFI,  // Profi
    M_GMX,    // GMX

    M_BRD,  // Border only

    M_SCORPION,  // Scorpion ZS-256 (Sinclair-matching 312-line x 224T raster)

    M_PROFIHR,  // Profi 512x240 hi-res (DFFD.7): standard 312-line x 224T beam, 4 px/T in the paper window

    M_TSZX,  // TS ZX mode (TS-Conf's ZX-layout graphics in its own raster and palette)

    M_MAX
};

enum RasterModeEnum
{
    R_256_192 = 0,  // Sinclair
    R_320_200 = 1,  // ATM, TS
    R_320_240 = 2,  // TS
    R_360_288 = 3,  // TS
    R_384_304 = 4,  // AlCo
    R_512_240 = 5,  // Profi

    R_MAX
};

enum RenderTypeEnum : uint8_t
{
    RT_BLANK = 0,  // Invisible area (VBlank, HSync, etc.)
    RT_BORDER,     // Top/Bottom/Left/Right border
    RT_SCREEN      // Screen area
};

enum ZXColorEnum : uint8_t
{
    COLOR_BLACK = 0,
    COLOR_BLUE,
    COLOR_RED,
    COLOR_MAGENTA,
    COLOR_GREEN,
    COLOR_CYAN,
    COLOR_YELLOW,
    COLOR_WHITE,
    COLOR_BRIGHT_BLACK,
    COLOR_BRIGHT_BLUE,
    COLOR_BRIGHT_RED,
    COLOR_BRIGHT_MAGENTA,
    COLOR_BRIGHT_GREEN,
    COLOR_BRIGHT_CYAN,
    COLOR_BRIGHT_YELLOW,
    COLOR_BRIGHT_WHITE,
};

/// endregion </Enumerations>

/// region <Structures>

struct RASTER
{
    RasterModeEnum num;
    uint32_t u_brd;  // first pixel line
    uint32_t d_brd;  // first lower border line
    uint32_t l_brd;  // first pixel tact
    uint32_t r_brd;  // first right border tact
};

struct VideoControl
{
    uint32_t clut[256];       // palette LUT in truecolor: ZX defaults in 0-15 (read by the ATM drawers and their tests)
    RASTER raster;            // raster parameters
    VideoModeEnum mode;       // renderer mode
    VideoModeEnum mode_next;  // renderer mode, delayed to the start of the line
    uint32_t t_next;          // next tact to be rendered
    uint32_t vptr;            // address in videobuffer
    uint32_t xctr;            // videocontroller X counter
    uint32_t yctr;            // videocontroller absolute Y counter (used for TS)
    uint32_t ygctr;           // videocontroller graphics Y counter (used for graphics)
    uint32_t buf;             // active video buffer
    uint32_t flash;           // flash counter
    uint16_t line;            // current rendered line
    uint16_t line_pos;        // current rendered position in line
};

///
///
/// Note: Each t-state ULA renders 2 pixels. All pixel dimensions are translated to t-states by dividing by 2
//        i.e. Pixel width 256 = 128 t-states.
struct RasterDescriptor
{
    uint16_t fullFrameWidth;
    uint16_t fullFrameHeight;

    uint16_t screenWidth;
    uint16_t screenHeight;

    uint16_t screenOffsetLeft;
    uint16_t screenOffsetTop;

    uint16_t pixelsPerLine;

    uint16_t hSyncPixels;
    uint16_t hBlankPixels;
    uint16_t vSyncLines;
    uint16_t vBlankLines;
};

///
/// Calculated from RasterDescriptor runtime values.
/// Should be refreshed after changing raster / screen mode
///
struct RasterState
{
    /// region <Config values>
    uint32_t configFrameDuration;  // Full frame duration between two INTs (in t-states). Can be any but longer than
                                   // raster-defined frame duration
    /// endregion </Config values>

    /// region <Frame timings>

    const uint8_t pixelsPerTState = 2;  // Fixed value

    uint16_t pixelsPerLine;
    uint16_t tstatesPerLine;
    uint32_t maxFrameTiming;

    /// endregion </Frame timings>

    /// region <Vertical timings>

    // Invisible blank area on top
    uint32_t blankAreaStart;
    uint32_t blankAreaEnd;

    // Top border
    uint32_t topBorderAreaStart;
    uint32_t topBorderAreaEnd;

    // Screen + side borders
    uint32_t screenAreaStart;
    uint32_t screenAreaEnd;

    // Bottom border
    uint32_t bottomBorderAreaStart;
    uint32_t bottomBorderAreaEnd;

    /// endregion </Vertical timings>

    /// region <Horizontal timings>
    // T-states within a line, renderer origin: T 0 is the first left-border T,
    // horizontal blank/sync is at the END of the line (ZX: T 176..223)

    uint8_t leftBorderAreaStart;
    uint8_t leftBorderAreaEnd;

    uint8_t screenLineAreaStart;
    uint8_t screenLineAreaEnd;

    uint8_t rightBorderAreaStart;
    uint8_t rightBorderAreaEnd;

    uint8_t blankLineAreaStart;
    uint8_t blankLineAreaEnd;

    uint8_t paperDotsPerT = 2;  // pixels drawn per T inside the paper window (4 for 640/512-wide modes)

    /// endregion </Horizontal timings>

    /// region <Model-specific ULA behavior>

    // Border color update granularity in t-states.
    // Pentagon: updates every 1 t-state (immediate)
    // ZX-48K/128K: updates every 4 t-states (latched at 8-HC boundaries)
    uint8_t borderUpdateTStates = 1;

    // Whether ULA memory contention is active for this model.
    // Pentagon: no contention. ZX-48K/128K: contention on 0x4000-0x7FFF.
    bool contentionEnabled = false;

    // Video controller fetch architecture type.
    // Controls floating bus phase behavior (8T Ferranti vs 4T discrete).
    // Pentagon/Scorpion: discrete logic (continuous fetch, no shift gaps).
    // ZX-48K/128K: Ferranti ULA (8T pipeline with shift phases).
    uint8_t fetchType = 0;  // UlaFetchType enum value

    /// endregion </Model-specific ULA behavior>
};

/// Horizontal beam geometry of a video mode, in T-states from the start of
/// the left border (the renderer's line origin)
struct LineGeometry
{
    uint16_t paperStartT;    // first T of the mode's display window
    uint16_t paperTCount;    // T-states the display window spans
    uint16_t visibleTCount;  // left border + window + right border; blanking follows
    uint8_t paperDotsPerT;   // pixels per T inside the window
};

/// Picture format of a video mode (Screen::GetVideoModeInfo). Zero / nullptr
/// fields do not apply to the mode.
struct VideoModeInfo
{
    const char* colorDepth = "";
    uint16_t colors = 0;
    uint8_t bpp = 0;                    // bits per pixel of the bitmap; 0 for text / per-line attribute modes
    const char* attributeSize = nullptr;
    uint8_t textColumns = 0;
    uint8_t textRows = 0;
    uint8_t planes = 0;
    uint32_t pixelDataBytes = 0;
    uint32_t attributeBytes = 0;
    uint32_t totalBytes = 0;
};

/// Screen state as every automation module reports it (Screen::DescribeScreenState)
struct ScreenState
{
    MEM_MODEL model = MM_PENTAGON;
    VideoModeEnum mode = M_NUL;
    std::string videoMode;               // Screen::GetVideoModeName
    uint16_t width = 0, height = 0;      // mode picture size
    VideoModeInfo format;
    uint8_t borderColor = 0;
    bool shadowScreenCapable = false;    // 7FFD bit 3 selects a second screen
    uint8_t activeScreen = 0;            // 0 = normal (page 5), 1 = shadow (page 7)
    uint16_t activeRamPage = 5;          // video page selected by 7FFD bit 3
    std::vector<uint16_t> activeRamPages;  // every page the mode reads
    bool contention = false;             // Sinclair ULA memory contention active
    bool flashInverted = false;          // FLASH phase (toggles every 16 frames)
    uint8_t framesUntilFlashToggle = 16;
    uint8_t p7FFD = 0, pEFF7 = 0, pDFFD = 0, pFF77 = 0;
};

/// Beam position described in the active mode's geometry (Screen::DescribeBeam).
/// Line origin as the renderer: T 0 = first left-border T.
struct BeamPosition
{
    bool valid = false;
    uint32_t tInFrame = 0;
    uint32_t line = 0;
    uint32_t tInLine = 0;
    uint32_t beamX = 0;                           // dots (2 per T) from the line origin
    const char* verticalZone = "beyond_raster";   // vsync, vblank, top_border, screen, bottom_border
    const char* horizontalZone = "-";             // left_border, paper, right_border, hblank (screen rows only)
    const char* zone = "beyond_raster";           // paper / border / hblank, or the vertical zone
    bool inVisibleArea = false;
    bool inPaper = false;
    uint32_t paperX = 0, paperXEnd = 0, paperY = 0;  // mode pixels under the beam (valid when inPaper)
};

struct FramebufferDescriptor
{
    VideoModeEnum videoMode = M_NUL;

    uint16_t width = 0;
    uint16_t height = 0;

    uint8_t* memoryBuffer = nullptr;
    size_t memoryBufferSize = 0;
};

/// Display viewport configuration for cropping framebuffer to display
/// Used with M_P384 overscan mode to allow symmetric display output
struct DisplayViewport
{
    uint16_t cropLeft = 0;    // Pixels to crop from left
    uint16_t cropRight = 0;   // Pixels to crop from right
    uint16_t cropTop = 0;     // Lines to crop from top
    uint16_t cropBottom = 0;  // Lines to crop from bottom

    /// Get resulting display width after cropping
    uint16_t GetDisplayWidth(uint16_t framebufferWidth) const
    {
        return framebufferWidth - cropLeft - cropRight;
    }

    /// Get resulting display height after cropping
    uint16_t GetDisplayHeight(uint16_t framebufferHeight) const
    {
        return framebufferHeight - cropTop - cropBottom;
    }
};

/// Preset viewports for M_P384 overscan mode
/// M_P384 framebuffer layout (from raster descriptor screenOffsetLeft=48):
///   Left border: 48px, Paper: 256px, Right border: 80px = 384px total
///   Top border: 56px, Paper: 192px, Bottom border: 56px = 304px total
/// Extra pixels vs Pentagon (352x288): 32 on right, 8 top + 8 bottom
namespace ViewportPresets
{
    // Raster descriptor values
    static constexpr uint16_t SCREEN_OFFSET_LEFT = 48;   // Paper starts at x=48
    static constexpr uint16_t SCREEN_OFFSET_TOP = 56;    // Paper starts at y=56
    static constexpr uint16_t SCREEN_WIDTH = 256;
    static constexpr uint16_t SCREEN_HEIGHT = 192;
    static constexpr uint16_t P384_WIDTH = 384;
    static constexpr uint16_t P384_HEIGHT = 304;
    static constexpr uint16_t PENTAGON_WIDTH = 352;
    static constexpr uint16_t PENTAGON_HEIGHT = 288;

    // Derived: right/bottom borders
    static constexpr uint16_t RIGHT_BORDER = P384_WIDTH - SCREEN_OFFSET_LEFT - SCREEN_WIDTH;   // 80
    static constexpr uint16_t BOTTOM_BORDER = P384_HEIGHT - SCREEN_OFFSET_TOP - SCREEN_HEIGHT; // 56

    // Full overscan (384x304) - show everything including extra border areas
    constexpr DisplayViewport FULL_OVERSCAN = {0, 0, 0, 0};

    // Symmetric horizontal (352x304) - crop right border to match left (48px each)
    constexpr DisplayViewport SYMMETRIC_HORIZONTAL = {
        0,
        static_cast<uint16_t>(RIGHT_BORDER - SCREEN_OFFSET_LEFT),  // 80 - 48 = 32
        0, 0
    };

    // Standard (352x288) - match standard Pentagon display (48px borders all around)
    constexpr DisplayViewport STANDARD = {
        0,
        static_cast<uint16_t>(RIGHT_BORDER - SCREEN_OFFSET_LEFT),  // 32
        static_cast<uint16_t>(SCREEN_OFFSET_TOP - 48),             // 56 - 48 = 8
        static_cast<uint16_t>(BOTTOM_BORDER - 48)                  // 56 - 48 = 8
    };

    // Screen only (256x192) - paper area only
    constexpr DisplayViewport SCREEN_ONLY = {
        SCREEN_OFFSET_LEFT,  // 48
        RIGHT_BORDER,        // 80
        SCREEN_OFFSET_TOP,   // 56
        BOTTOM_BORDER        // 56
    };
}

/// endregion </Structures>

// ULA+ color models:
//
// val  red/grn     blue1       blue2
// 0    00000000    00000000    00000000
// 1    00100100
// 2    01001001
// 3    01101101    01101101    01101101
// 4    10010010                10010010
// 5    10110110    10110110
// 6    11011011
// 7    11111111    11111111    11111111

// ULA+ palette cell select:
// bit5 - FLASH
// bit4 - BRIGHT
// bit3 - 0 - INK / 1 - PAPER
// bits0..2 - INK / PAPER

// Extract colors
#define col_def(a) (((a) << 5) | ((a) << 2) | ((a) >> 1))
#define col_r(a) (col_def(a) << 16)
#define col_g(a) (col_def(a) << 8)
#define col_b(a) (col_def(a))

typedef void (Screen::*DrawCallback)(uint32_t n);

class Screen
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_VIDEO;
    const uint16_t _SUBMODULE = PlatformVideoSubmodulesEnum::SUBMODULE_VIDEO_GENERIC;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    static constexpr uint32_t rb2_offs = MAX_HEIGHT * MAX_WIDTH_P;
    static constexpr uint32_t sizeof_rbuf = rb2_offs * (MAX_BUFFERS + 2);
    static constexpr uint32_t sizeof_vbuf = VID_HEIGHT * VID_WIDTH * 2;

#ifdef CACHE_ALIGNED
    CACHE_ALIGNED uint8_t rbuf[sizeof_rbuf];
    CACHE_ALIGNED uint32_t vbuf[2][sizeof_vbuf];
#else
    uint8_t rbuf[sizeof_rbuf];
    uint32_t vbuf[2][sizeof_vbuf];
#endif

    // Video raster mode descriptors
    const RASTER raster[R_MAX] = {
        {R_256_192, 80, 272, 70, 70 + 128},  // Genuine ZX-Spectrum screen
        //{ R_256_192, 80, 272, 58, 186 },
        {R_320_200, 76, 276, 54, 214},
        {R_320_240, 56, 296, 54, 214},
        {R_360_288, 32, 320, 44, 224},
        {R_384_304, 16, 320, 32, 224},
        {R_512_240, 56, 296, 70, 198},
    };

    /// Raster descriptors for each video mode
    /// All values are in pixel units!
    //    uint16_t fullFrameWidth;
    //    uint16_t fullFrameHeight;
    //
    //    uint16_t screenWidth;
    //    uint16_t screenHeight;
    //
    //    uint16_t screenOffsetLeft;
    //    uint16_t screenOffsetTop;
    //
    //    uint16_t pixelsPerLine;
    //
    //    uint16_t hSyncPixels;
    //    uint16_t hBlankPixels;
    //    uint16_t vSyncLines;
    //    uint16_t vBlankLines;
    const RasterDescriptor rasterDescriptors[M_MAX] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},                  // M_NUL
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 8, 16},   // M_ZX48k
        {352, 288, 256, 192, 48, 48, 456, 64, 32, 8, 15},   // M_ZX128 (311 lines: 228*311=70908)
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16},  // M_PENTAGON128K
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16},  // M_PMC
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16},  // M_P16
        // M_P384: Pentagon Overscan - larger framebuffer with same timing as Pentagon
        // Timing must be IDENTICAL to M_PENTAGON128K for correct border effects
        // Only fullFrameWidth/Height differ for larger framebuffer allocation
        // Screen position (48,48) same as Pentagon - extra border rendered around it
        // Frame: 16 vSync + 16 vBlank + 288 visible = 320 lines, same 71680 T-states
        {384, 304, 256, 192, 48, 48, 448, 64, 32, 16, 16},   // M_P384 (Pentagon 384x304 overscan)
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16},  // M_PHR
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 8, 16},   // M_TIMEX
        // TS-Conf modes (ScreenTSConf; TSConf hardware-spec §4.1): one geometry for every
        // mode - the 360x288 visible dots (dots 88-447, lines 32-319 of the 448-dot x
        // 320-line raster) stored at 2 px per dot (TXT pixels are 14 MHz). The
        // graphics window inside it follows V_CONFIG's geometry, the rest is border.
        // 32 blank lines + 288 visible = 320 lines x 224 T = 71680 T
        {720, 288, 720, 288, 0, 0, 448, 64, 24, 16, 16},  // M_TS16
        {720, 288, 720, 288, 0, 0, 448, 64, 24, 16, 16},  // M_TS256
        {720, 288, 720, 288, 0, 0, 448, 64, 24, 16, 16},  // M_TSTX
        // ATM modes: ZX-compatible 312-line PAL timing at base clock
        // Beam: 448 pixels/line = 224 T-states; 16 vSync + 8 vBlank + 288 visible = 312 lines
        // maxFrameTiming = 224 x 312 = 69888 = config.frame (synchronized)
        // 200-line screen vertically centered (44-line top/bottom border, like the
        // reference renderer's (scy-200)/2 centering in dxr_atm0.cpp).
        // NO side border: cross-checked against 3 independent ZXMAK2 renderer
        // classes (Atm320Renderer/Atm640Renderer/AtmTxtRenderer CreateParams,
        // all c_ulaBorderLeftT=c_ulaBorderRightT=0) - ATM extended modes are
        // edge-to-edge horizontally, fullFrameWidth == screenWidth. Only
        // top/bottom border exists. pixelsPerLine stays the ZX-compatible beam
        // timing (448 px/224T); fullFrameWidth is storage, now equal to the
        // active picture width with zero side margin.
        {320, 288, 320, 200, 0, 44, 448, 64, 32, 16, 8},  // M_ATM16 (EGA 16-color)
        {640, 288, 640, 200, 0, 44, 448, 64, 32, 16, 8},  // M_ATMHR (HW Multicolor 640x200)
        {640, 288, 640, 200, 0, 44, 448, 64, 32, 16, 8},  // M_ATMTX (Text 80x25, 640x200)
        {640, 288, 640, 200, 0, 44, 448, 64, 32, 16, 8},  // M_ATMTL (ZX-Evo Text Linear 80x25, 640x200 - same geometry as TX)
        // M_PROFI: standard Profi mode. 312 lines x 224T = 69888T frame (UnrealSpeccy PRESET.PROFI,
        // ZXMAK2, Xpeccy - the corpus consensus, not verified on real hardware)
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 8, 16},  // M_PROFI
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16},  // M_GMX
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 16, 16},  // M_BRD
        // M_SCORPION: 312 lines x 224T = 69888T frame - same 312-line geometry as
        // M_ZX48 (Pentagon's row differs only in vSyncLines 16 vs 8)
        {352, 288, 256, 192, 48, 48, 448, 64, 32, 8, 16},  // M_SCORPION
        // M_PROFIHR: same beam and timing as M_PROFI (no emulator changes the frame in hi-res).
        // 512x240 paper drawn at 4 px/T inside the 128 T paper window; the paper starts 24 lines
        // above the standard one (240 lines centred on the 192-line window). Storage is wider than
        // the beam: 48 px side borders at 2 px/T.
        {608, 288, 512, 240, 48, 24, 448, 64, 32, 8, 16},  // M_PROFIHR
        {720, 288, 720, 288, 0, 0, 448, 64, 24, 16, 16},   // M_TSZX (see M_TS16)
    };

    // Default color table: 0RRrrrGG gggBBbbb
    uint16_t spec_colors[16] = {0x0000, 0x0010, 0x4000, 0x4010, 0x0200, 0x0210, 0x4200, 0x4210,
                                0x0000, 0x0018, 0x6000, 0x6018, 0x0300, 0x0318, 0x6300, 0x6318};

    const uint32_t cr[8] = {col_r(0), col_r(1), col_r(2), col_r(3), col_r(4), col_r(5), col_r(6), col_r(7)};

    const uint32_t cg[8] = {col_g(0), col_g(1), col_g(2), col_g(3), col_g(4), col_g(5), col_g(6), col_g(7)};

    const uint32_t cb[2][4] = {{col_b(0), col_b(3), col_b(5), col_b(7)}, {col_b(0), col_b(3), col_b(4), col_b(7)}};

protected:
    EmulatorContext* _context = nullptr;
    EmulatorState* _state = nullptr;
    Core* _system = nullptr;
    Z80* _cpu = nullptr;
    Memory* _memory = nullptr;
    ModuleLogger* _logger;

    uint8_t _activeScreen;

    /// Latches after every video port write of the current and previous frame (cold: port handlers only)
    videomap::VideoWriteLog _videoWriteLog;
    void NoteVideoWrite();
    uint8_t* _activeScreenMemoryOffset;
    uint8_t _borderColor;

    // Frame-level screen switch tracking (zero overhead when HUD disabled)
    bool _feature_hud_enabled = false;
    uint8_t _screenSwitchCount = 0;

    VideoModeEnum _mode;
    RasterState _rasterState;
    FramebufferDescriptor _framebuffer;
    DisplayViewport _displayViewport;  // Current viewport for display cropping

    uint32_t _prevTstate = 0;  // Previous Draw call t-state value (since emulation is not concurrent as in hardware -
                               // we need to know what time period to replay)

bool _turboRenderSkip = false;  // Turbo render decimation: set only for the CPU cycle of a skipped turbo frame

    /// region <Obsolete>
    // NOTE (2026-09-08 audit, Scorpion task): this table is positionally under-filled -
    // 17 initializers for M_MAX slots - so M_GMX, M_BRD and the appended M_SCORPION slot
    // stay null. Intentionally left as-is: the table belongs to the obsolete draw path.
    DrawCallback _currentDrawCallback;
    DrawCallback _nullCallback;
    DrawCallback _drawCallback;
    DrawCallback _borderCallback;

    DrawCallback _drawCallbacks[M_MAX] = {
        &Screen::DrawNull,      // M_NUL
        &Screen::DrawNull,      // M_ZX48 - drawn by ScreenZX
        &Screen::DrawNull,      // M_ZX128
        &Screen::DrawNull,      // M_PENTAGON128K
        &Screen::DrawPMC,       // M_PMC
        &Screen::DrawP16,       // M_P16
        &Screen::DrawP384,      // M_P384
        &Screen::DrawPHR,       // M_PHR
        &Screen::DrawTimex,     // M_TIMEX
        &Screen::DrawNull,      // M_TS16 - TSConf modes are ScreenTSConf's (PLAN #41 phase 3)
        &Screen::DrawNull,      // M_TS256
        &Screen::DrawNull,      // M_TSTX
        &Screen::DrawATM16,     // M_ATM16
        &Screen::DrawATMHiRes,  // M_ATMHR
        &Screen::DrawATM2Text,  // M_ATMTX
        &Screen::DrawATM3Text,  // M_ATMTL
        &Screen::DrawNull,      // M_PROFI - own renderer is ScreenZX (M_PROFI) / ScreenProfi (M_PROFIHR), never this obsolete table
        &Screen::DrawGMX,       // M_GMX
        &Screen::DrawNull       // M_BRD
    };

public:
    VideoControl _vid;

    /// endregion </Obsolete>

    /// region <Static methods>
    static std::string GetColorName(uint8_t color);
    /// endregion </Static methods>

    /// region <Constructors / Destructors>
public:
    Screen() = delete;  // Disable default constructor; C++ 11 feature
    Screen(EmulatorContext* context);
    virtual ~Screen();
    /// endregion </Constructors / Destructors>

    /// region <Initialization>
public:
    virtual void CreateTables() = 0;

    virtual void Reset();

    virtual void InitFrame();
    virtual void InitRaster();
    virtual void InitMemoryCounters();
    /// endregion </Initialization>

    /// region <Video mode detection>
protected:
    /// Result of the per-model video mode detection: port/config state -> (mode, raster)
    struct ModeSelection
    {
        VideoModeEnum mode;
        RasterModeEnum raster;
    };

    /// Single routing point: maps the machine model to its per-family
    /// detector. InitRaster calls only this; adding a model family means
    /// one case here plus one DetectMode* implementation.
    ModeSelection DetectVideoMode(MEM_MODEL model) const;

    /// Per-model-family detection - each method fully owns its family's
    /// mode + raster decision based on the current emulator port state
    ModeSelection DetectModeZX48(const EmulatorState& state) const;
    ModeSelection DetectModeZX128(const EmulatorState& state) const;
    ModeSelection DetectModePentagon(const EmulatorState& state) const;
    ModeSelection DetectModeATM1(const EmulatorState& state) const;
    ModeSelection DetectModeATM2(const EmulatorState& state) const;
    ModeSelection DetectModeATM3(const EmulatorState& state) const;
    ModeSelection DetectModeProfi(const EmulatorState& state) const;
    ModeSelection DetectModeScorpion(const EmulatorState& state) const;
    ModeSelection DetectModeGMX(const EmulatorState& state) const;
    ModeSelection DetectModeLegacy(const EmulatorState& state) const;
    /// endregion </Video mode detection>

    /// region <Frame lifecycle>
public:
    void handleFrameStart();
    void handleFrameEnd();
    /// endregion </Frame lifecycle>

public:
    virtual void SetVideoMode(VideoModeEnum mode);
    virtual void SetActiveScreen(SpectrumScreenEnum screen);
    virtual void SetBorderColor(uint8_t color);

    virtual VideoModeEnum GetVideoMode();
    virtual uint8_t GetActiveScreen();
    virtual uint8_t GetBorderColor();
    virtual uint32_t GetCurrentTstate();

    /// Video debug translation (PLAN #42 phase 3): the latches the picture's
    /// geometry and memory depend on now, and their history over the current
    /// and the previous frame (videowritelog.h)
    videomap::VideoLatches CaptureVideoLatches() const;
    const videomap::VideoWriteLog& GetVideoWriteLog() const { return _videoWriteLog; }

    /// @brief Read-only access to the calculated raster zone boundaries
    /// (t-state ranges for blank/border/screen areas, vertical and horizontal)
    const RasterState& GetRasterState() const { return _rasterState; }

    /// Test observability: raster timing of the active mode. _rasterState is
    /// refreshed by SetVideoMode, i.e. after InitRaster applied a mode change.
    uint32_t GetMaxFrameTiming() const { return _rasterState.maxFrameTiming; }
    uint32_t GetTstatesPerLine() const { return _rasterState.tstatesPerLine; }

    virtual void UpdateScreen() = 0;
    virtual void DrawPeriod(uint32_t fromTstate, uint32_t toTstate);
    virtual void Draw(uint32_t tstate);

    /// Render the inclusive frame T-state range [from, to]. DrawPeriod calls it
    /// once per catch-up; renderers override it to loop without per-T dispatch.
    virtual void DrawRange(uint32_t fromTstate, uint32_t toTstate);

    /// @brief Reset the previous t-state tracker used by DrawPeriod
    /// Must be called after AdjustFrameCounters() wraps z80.t to prevent
    /// DrawPeriod from seeing fromTstate > toTstate across the frame boundary
    void ResetPrevTstate() { _prevTstate = 0; }
    /// Draw cursor save/restore for TTD live-state snapshots taken mid-frame
    uint32_t GetPrevTstate() const { return _prevTstate; }
    void SetPrevTstate(uint32_t tstate) { _prevTstate = tstate; }

    /// @brief Suspend contingent per-t-state rendering for the current CPU
    /// frame cycle (turbo render decimation - see MainLoop::RunFrame).
    /// While set, DrawPeriod returns immediately; _prevTstate tracking in
    /// UpdateScreen callers still advances, so the beam position stays fresh.
    /// Must only be set for the duration of a skipped turbo frame's CPU cycle
    /// - never across frame boundaries, so manual debug stepping and the
    /// frame-end batch/latch paths are never affected.
    void SetTurboRenderSkip(bool skip) { _turboRenderSkip = skip; }

    /// @brief Frame T-state of the first pixel of the active mode's display window
    uint32_t GetPaperStartTstate() const
    {
        return _rasterState.screenAreaStart + _rasterState.screenLineAreaStart;
    }

    virtual void RenderOnlyMainScreen();

    /// region <ZX DLSS plane B>
    /// Per-pixel meaning of the rendered frame (feature zxdlss), written by the
    /// renderer in the same pass as the RGBA pixel, same size and layout as the
    /// framebuffer. One uint16 per pixel:
    ///   bits 0-7   attribute byte the beam used for this pixel (0 on the border)
    ///   bits 8-11  color index 0..15 (bright * 8 + color)
    ///   bit  12    ink (1) / paper (0)
    ///   bits 13-14 role: 0 not drawn, 1 screen, 2 border
    /// Only the per-T ZX renderer (ScreenHQ) writes it; other modes leave 0.
    static constexpr uint16_t kPlaneBInk = 1u << 12;
    static constexpr uint16_t kPlaneBRoleScreen = 1u << 13;
    static constexpr uint16_t kPlaneBRoleBorder = 2u << 13;
    static constexpr uint16_t kPlaneBRoleMask = 3u << 13;

    /// Threading: the live buffer belongs to the thread that renders (the
    /// emulation thread, or a TTD replay while the emulation thread is paused).
    /// The zxdlss feature can change on any thread, so UpdateFeatureCache only
    /// records the wanted state; InitFrame applies it at the next frame start
    /// on the rendering thread. Other threads read plane B through
    /// CopyPresentedPlaneB, latched with the framebuffer under _presentMutex.
    bool IsPlaneBEnabled() const { return _planeBEnabled; }
    /// Allocates (enabled) or frees (disabled) the buffer; renderers pick their
    /// plane-B variant here, so the disabled path runs exactly the old code.
    /// Rendering thread only (or with the emulation thread paused).
    virtual void SetPlaneBEnabled(bool enabled);
    /// @return the live plane B (nullptr when disabled); count = pixels.
    /// Rendering thread only (or with the emulation thread paused).
    uint16_t* GetPlaneB(size_t* count);
    /// Any thread: plane B of the frame CopyPresentedFramebuffer serves.
    /// @return false when plane B is off or nothing is latched yet
    bool CopyPresentedPlaneB(std::vector<uint16_t>& dst);
    /// endregion </ZX DLSS plane B>

    /// @brief Render entire screen at frame end when ScreenHQ=OFF (batch rendering mode)
    /// Called by MainLoop::OnFrameEnd() instead of per-t-state Draw() calls.
    /// Override in ScreenZX to use RenderScreen_Batch8 for 25x faster rendering.
    virtual void RenderFrameBatch();

    /// region <Feature cache - ScreenHQ>
    /// @brief Update cached feature flag state (called by FeatureManager::onFeatureChanged)
    /// Components cache their feature flags for performance to avoid map lookups in hot paths.
    void UpdateFeatureCache();

    /// @brief Refresh cached memory pointers after memory migration
    /// Called by Memory when transitioning between heap and shared memory.
    /// This ensures _activeScreenMemoryOffset points to current memory.
    void RefreshMemoryPointers();

    /// @brief Check if ScreenHQ mode is enabled (per-t-state rendering for demo compatibility)
    /// When false, batch 8-pixel rendering is used for performance
    bool IsScreenHQEnabled() const
    {
        return _feature_screenhq_enabled;
    }

protected:
    // Cached feature flag (updated by UpdateFeatureCache)
    bool _feature_screenhq_enabled = true;  // Default ON for demo compatibility

    // ZX DLSS plane B (see SetPlaneBEnabled); sized with the framebuffer
    bool _planeBEnabled = false;
    std::atomic<bool> _planeBWanted{false};     // set by UpdateFeatureCache on any thread
    std::vector<uint16_t> _planeB;
    void ResizePlaneB();
    void ApplyPlaneBRequest();                  // InitFrame: wanted -> enabled, rendering thread
    /// endregion </Feature cache>

    virtual void SaveScreen();
    virtual void SaveZXSpectrumNativeScreen();

    /// region <Framebuffer related>
protected:
    void AllocateFramebuffer(VideoModeEnum mode);
    void DeallocateFramebuffer();

    // Presentation (latched) framebuffer QUEUE: complete-frame snapshots
    // taken at frame end on the emulation thread. GUI consumers read these
    // copies instead of the live _framebuffer, which the emulator overwrites
    // concurrently (the source of mid-frame tearing).
    //
    // A/V sync (audio-sync design): audio is presented ~DRC_TARGET_MS + HW
    // buffer (~50 ms ~= 2 frames) behind the emulated frame that produced
    // it - the ring depth is structural (production is bursty per-frame,
    // the DAC drains continuously). Instead of shrinking the ring into
    // underrun territory, VIDEO presentation is delayed by
    // _presentDelayFrames so both land at the same constant latency and
    // the net A/V offset collapses to ~0. Recording is unaffected: it taps
    // emulated time upstream of both presentation paths.
    //
    // Temporal effects (ZX DLSS) delay the video further: the algorithm's
    // look-ahead + 1 frame for its worker (TemporalEffects::VideoDelayFrames,
    // 7 for mod-tpgwafsd). The queue holds that many frames plus the write slot.
    static constexpr size_t PRESENT_SLOTS = 12;  // > max delay (11) + write slot
    uint8_t* _presentSlots[PRESENT_SLOTS] = {};
    // Serial of the frame each slot holds (never reset, unlike the latch counter):
    // temporal effects write their output back into the slot of its frame
    uint64_t _presentSlotSerial[PRESENT_SLOTS] = {};
    // Per slot: the temporal effect's output is in it / a reader was served it
    // (under _presentMutex; "shown" is set by the const reader path)
    bool _presentSlotProcessed[PRESENT_SLOTS] = {};
    mutable bool _presentSlotShown[PRESENT_SLOTS] = {};
    mutable std::atomic<bool> _showingProcessed{false};  // the slot readers get now holds processed output
    zxdlss::FrameReport _presentSlotReport[PRESENT_SLOTS];   // what the effect did to each slot's frame
    mutable zxdlss::FrameReport _shownReport;               // ... to the one served last (under _presentMutex)
    uint64_t _presentSerial = 0;
    uint64_t _presentLatchCounter = 0;  // Total frames latched (next write index)
    size_t _presentBufferSize = 0;  // Authoritative size for readers; set under _presentMutex
    std::atomic<uint8_t> _presentDelayFrames{2};  // Frames of video delay (0..PRESENT_SLOTS-1)
    std::mutex _presentMutex;
    // Plane B latched with each present slot (under _presentMutex); empty while
    // plane B is off, so the feature-off latch copies nothing
    std::vector<uint16_t> _presentPlaneB[PRESENT_SLOTS];
    const uint8_t* PresentedSlotLocked(size_t* index) const;  // the slot CopyPresentedFramebuffer serves

    // Temporal effects (created on first use: no worker thread otherwise)
    std::unique_ptr<TemporalEffects> _temporal;
    std::mutex _temporalCreateMutex;
    std::atomic<uint8_t> _temporalDelayFrames{0};  // video delay the effect needs (0 = off)
    int _audioExtraDelayFrames = 0;                // emulation thread: what SoundManager was told
    bool _temporalRestoreScreenHQ = false;         // switched on for the effect: off again with it
    bool _temporalRestoreZXDLSS = false;
    TemporalEffects::WriteResult WriteTemporalOutput(uint64_t serial, const uint8_t* rgb, int width, int height,
                                                     const zxdlss::FrameReport& report);  // worker thread
    void UpdateAudioDelay();                                                              // emulation thread

    // User-forced Pentagon overscan (see SetOverscanForced)
    bool _overscanForced = false;

    // Wall-clock (steady) timestamp of the last LatchFramebuffer, in us.
    // GUI consumers compute video presentation latency = paint time - this.
    std::atomic<uint64_t> _lastLatchTimestampUs{0};

public:
    /// @brief Latch the completed frame into the presentation buffer.
    /// Call on the emulation thread at frame end, after rendering is finished.
    /// Holds _presentMutex only for one SIMD frame copy (~40us for 352x288).
    void LatchFramebuffer();

    /// @brief Flush the present queue and publish the current framebuffer.
    ///
    /// The present queue exists for A/V sync: CopyPresentedFramebuffer() serves
    /// the frame latched _presentDelayFrames ago so video trails audio by a
    /// constant latency. That is right while frames keep arriving and wrong
    /// after a seek, where the machine repaints once and stops — a plain latch
    /// would leave the UI showing a queued older frame with nothing coming to
    /// push it through.
    ///
    /// Discarding the queue rather than back-filling it is what matches the
    /// hardware analogy: the delay line is emptied, the new frame becomes the
    /// only content, and normal playback repopulates it on resume.
    void FlushAndPresentFramebuffer();

    /// Steady-clock timestamp (us) of the last completed latch (0 = never)
    uint64_t GetLastLatchTimestampUs() const { return _lastLatchTimestampUs.load(std::memory_order_acquire); }

    /// Video presentation delay in frames (A/V sync: match the audio path's
    /// ring + HW buffer latency, ~2 frames). 0 = present immediately
    /// (lowest input latency, audio trails by the full ring depth).
    void SetPresentDelayFrames(uint8_t frames)
    {
        _presentDelayFrames.store(frames < PRESENT_SLOTS ? frames : PRESENT_SLOTS - 1, std::memory_order_release);
    }
    uint8_t GetPresentDelayFrames() const { return _presentDelayFrames.load(std::memory_order_acquire); }

    /// The delay actually applied: the configured one, or more while a temporal
    /// effect needs its look-ahead (the audio is delayed by the difference)
    uint8_t GetEffectivePresentDelayFrames() const
    {
        const uint8_t base = _presentDelayFrames.load(std::memory_order_acquire);
        const uint8_t temporal = _temporalDelayFrames.load(std::memory_order_acquire);
        return temporal > base ? temporal : base;
    }

    /// @brief Temporal effect run on every frame before presentation (ZX DLSS
    /// de-flicker, core/src/emulator/video/zxdlss): the registry name of the
    /// algorithm, "" = off. Any thread. The algorithm needs plane B: switching it
    /// on switches the features zxdlss + screenhq on, switching it off restores
    /// them. On a mode without a ZX raster the effect stays inactive (see stats).
    /// @return false for an unknown algorithm name
    bool SetTemporalAlgorithm(const std::string& name);
    std::string GetTemporalAlgorithm();
    TemporalEffects::Stats GetTemporalStats();

    /// Present delay in microseconds at the current frame duration (for the
    /// video presentation latency readout: paint-to-latch delta measures the
    /// NEWEST latch, but the presented frame is GetPresentDelayFrames older)
    uint32_t GetPresentDelayUs() const
    {
        const uint32_t frameTStates = (_context && _context->config.frame) ? _context->config.frame : 71680;
        return static_cast<uint32_t>(GetEffectivePresentDelayFrames() *
                                     (static_cast<uint64_t>(frameTStates) * 10 / 35));
    }

    /// @brief Copy the latched (tear-free) frame into a caller-provided buffer.
    /// Safe to call from any thread.
    /// @param dst Destination buffer
    /// @param dstSize Destination size in bytes; must be >= framebuffer size
    /// @return true if a frame was copied
    bool CopyPresentedFramebuffer(uint8_t* dst, size_t dstSize);

    FramebufferDescriptor& GetFramebufferDescriptor();
    void GetFramebufferData(uint32_t** buffer, size_t* size);

    /// Display viewport for cropping framebuffer to display
    void SetDisplayViewport(const DisplayViewport& viewport);
    const DisplayViewport& GetDisplayViewport() const;

    /// Check if current mode is overscan (M_P384)
    bool IsOverscanMode() const { return _mode == M_P384; }

    /// User-forced Pentagon overscan (UI toggle, not guest-visible hardware).
    /// InitRaster re-detects the video mode from config/ports every frame;
    /// without this flag a manual SetVideoMode(M_P384) is reverted to the
    /// model's base mode on the next frame. Guest-programmed AlCo modes
    /// (EFF7 bits) still take priority over the override.
    void SetOverscanForced(bool forced) { _overscanForced = forced; }
    bool IsOverscanForced() const { return _overscanForced; }

    /// Get display dimensions after viewport cropping
    uint16_t GetDisplayWidth() const;
    uint16_t GetDisplayHeight() const;

    /// The 16 ZX colors exactly as the renderer draws them (the first 16 entries of
    /// the live palette), in the framebuffer format RGBA8888 (LE uint32 0xAABBGGRR).
    /// The ZX-Poly composer draws with them
    /// @param colors Output array of 16 color values
    virtual void GetRGBAPalette16(uint32_t* colors);

    /// endregion </Framebuffer related>

    // Draw helpers
public:
    static std::string GetVideoModeName(VideoModeEnum mode);

    /// Physical RAM pages that make up the surface the given video mode actually
    /// displays - the source for the /state/screen/digest "mode=active" selection
    /// (P1-3). ZX-family modes keep the classic screen pages (5, plus shadow page
    /// 7 on banked models, matching the digest default). The ATM hardware modes
    /// (16c / HiRes / Text / TextLinear) interleave the 7FFD-selected video page
    /// and the page four below it - the DrawATM* bit-plane layout (atm branch):
    /// plane pairs live at {videoPage - 4, videoPage} with offsets 0x0000/0x2000.
    /// Other extended modes (Profi/GMX/TS, renderers still stubbed) fall back to
    /// the classic pages until their renderers define a surface.
    /// @param mode Current video mode (Screen::GetVideoMode())
    /// @param p7FFD Port 7FFD latch value (bit 3 selects the video page on ATM)
    /// @param bankedZX Model exposes a shadow screen (128K-class paging)
    static std::vector<uint16_t> GetActiveSurfaceRAMPages(VideoModeEnum mode, uint8_t p7FFD, bool bankedZX);

    /// Machine has a second (shadow) screen selected by 7FFD bit 3: every
    /// supported machine except the 48K
    static bool HasShadowScreen(MEM_MODEL model) { return model != MM_SPECTRUM48; }

    /// RAM page selected as the video page: 7 when 7FFD bit 3 selects the
    /// shadow screen, 5 otherwise
    static uint16_t GetVideoRAMPage(MEM_MODEL model, uint8_t p7FFD)
    {
        return (HasShadowScreen(model) && (p7FFD & 0x08)) ? 7 : 5;
    }

    /// RAM pages the current mode reads to form the picture: the video page
    /// alone for ZX-layout modes, every plane/attribute/text page otherwise
    static std::vector<uint16_t> GetDisplayedRAMPages(VideoModeEnum mode, MEM_MODEL model, uint8_t p7FFD);

    /// Picture format of a mode (colour depth, bpp, attribute cell, memory layout)
    static const VideoModeInfo& GetVideoModeInfo(VideoModeEnum mode);

    /// Current screen state - the single source for every automation module
    ScreenState DescribeScreenState() const;

    /// Horizontal beam geometry (display window, pixel clock) of a mode.
    /// timing is the descriptor the mode's timing comes from (SetVideoMode).
    static LineGeometry GetLineGeometry(VideoModeEnum mode, const RasterDescriptor& timing);

    /// Descriptor the mode's timing comes from (P384 uses Pentagon timing,
    /// ATM3 AlCo modes keep the ATM 312-line raster)
    const RasterDescriptor& GetTimingDescriptor(VideoModeEnum mode) const;

    /// Beam position, zones and the mode pixel under the beam for a frame T
    BeamPosition DescribeBeam(uint32_t tInFrame) const;


    void DrawNull(uint32_t n);      // Non-existing mode (skip draw)
    void DrawPMC(uint32_t n);       // Pentagon Multicolor
    void DrawP16(uint32_t n);       // Pentagon 16c
    void DrawP384(uint32_t n);      // Pentagon 384x304
    void DrawPHR(uint32_t n);       // Pentagon HiRes
    void DrawTimex(uint32_t n);     // Timex
    void DrawATM16(uint32_t n);     // ATM 16c
    void DrawATMHiRes(uint32_t n);  // ATM HiRes
    void DrawATM2Text(uint32_t n);  // ATM Text
    void DrawATM3Text(uint32_t n);  // ATM Text linear
    void DrawGMX(uint32_t n);       // GMX

    /// region <Helper methods
public:
    static std::string GetVideoVideoModeName(VideoModeEnum mode);
    static std::string GetRenderTypeName(RenderTypeEnum type);
    /// endregion </Helper methods

    /// region <Snapshot helpers>
public:
    virtual void FillBorderWithColor(uint8_t color) = 0;
    /// endregion </Snapshot helpers>

    /// region <Debug methods>
#ifdef _DEBUG
public:
    std::string DumpFramebufferInfo();
    void DumpFramebufferInfo(char* buffer, size_t len);

    std::string DumpRasterState();
    void DumpRasterState(char* buffer, size_t len);

#endif  // _DEBUG
    /// endregion </Debug methods>
};
