#include "screen.h"
#include "ulacontention.h"

#include <common/image/imagehelper.h>

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "common/video/videoutils.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/notifications.h"
#include "stdafx.h"

/// region <Static methods>
std::string Screen::GetColorName(uint8_t color)
{
    static const char* colorNames[] = {"Black", "Blue", "Red", "Magenta", "Green", "Cyan", "Yellow", "White"};

    std::string result = colorNames[color & 0b0000'0111];

    return result;
}
/// endregion </Static methods>

/// region <Constructors / Destructors>

Screen::Screen(EmulatorContext* context)
{
    // Initialize pointers and basic state
    _context = context;
    _state = &_context->emulatorState;
    _system = _context->pCore;
    _cpu = _system ? _system->GetZ80() : nullptr;
    _memory = _context->pMemory;
    _logger = _context->pModuleLogger;

    // Initialize video control structure
    memset(&_vid, 0, sizeof(_vid));
    _vid.raster = raster[R_256_192];  // Default to standard ZX Spectrum raster
    _vid.mode = M_ZX48;               // Default to ZX48 mode
    _vid.mode_next = M_ZX48;
    _vid.t_next = 0;
    _vid.vptr = 0;
    _vid.xctr = 0;
    _vid.yctr = 0;
    _vid.ygctr = 0;
    _vid.buf = 0;
    _vid.flash = 0;
    _vid.line = 0;
    _vid.line_pos = 0;
    _vid.ts_pos = 0;
    _vid.memcyc_lcmd = 0;

    // Initialize the 256-entry programmable palette with ZX 16-color defaults.
    // Framebuffer format is RGBA8888 (LE uint32 = 0xAABBGGRR) - values MUST be
    // opaque. The previous 0x00RRGGBB defaults were transparent AND R/B-swapped,
    // which showed as green-only garbage (green survives both errors).
    // ATM extended modes themselves render via the fixed ZX palette
    // (_rgbaColors), exactly like the reference renderer's sctab tables;
    // clut is the hook for future ATM3/TSConf palette port writes.
    static const uint32_t defaultPalette[16] = {
        0xFF000000,  // 0: Black
        0xFFC72200,  // 1: Blue
        0xFF1628D6,  // 2: Red
        0xFFC733D4,  // 3: Magenta
        0xFF25C500,  // 4: Green
        0xFFC9C700,  // 5: Cyan
        0xFF2AC8CC,  // 6: Yellow
        0xFFCACACA,  // 7: White (normal)
        0xFF000000,  // 8: Black (bright)
        0xFFFB2B00,  // 9: Bright Blue
        0xFF1C33FF,  // A: Bright Red
        0xFFFC40FF,  // B: Bright Magenta
        0xFF2FF900,  // C: Bright Green
        0xFFFEFB00,  // D: Bright Cyan
        0xFF36FCFF,  // E: Bright Yellow
        0xFFFFFFFF   // F: White (bright)
    };
    memcpy(_vid.clut, defaultPalette, sizeof(defaultPalette));
    memset(_vid.clut + 16, 0x00, sizeof(_vid.clut) - sizeof(defaultPalette));

    // Initialize memory counters
    InitMemoryCounters();

    // Initialize TS line buffers
    memset(_vid.tsline, 0, sizeof(_vid.tsline));

    // Initialize remaining members
    _borderColor = 0;
    _mode = M_ZX48;
    _nullCallback = nullptr;
    _drawCallback = nullptr;
    _borderCallback = nullptr;
    _currentDrawCallback = nullptr;
    _prevTstate = 0;

    // Set Normal screen (Bank 5) mode by default
    _activeScreen = 0;
    _activeScreenMemoryOffset = _memory->RAMPageAddress(5);
}

Screen::~Screen()
{
    if (_framebuffer.memoryBuffer != nullptr)
    {
        DeallocateFramebuffer();
    }
}

/// endregion </Constructors / Destructors>

/// region <Initialization>

void Screen::Reset()
{
    // Set Normal screen (Bank 5) mode by default
    Memory* memory = _context->pMemory;
    if (memory)
    {
        _activeScreenMemoryOffset = memory->RAMPageAddress(5);
    }

    _activeScreen = 0;

    // Reset t-state cached
    _prevTstate = 0;
}

void Screen::InitFrame()
{
    _vid.buf ^= 0x00000001;  // Swap current video buffer
    _vid.t_next = 0;
    _vid.vptr = 0;
    _vid.yctr = 0;
    _vid.ygctr = _state->ts.g_yoffs - 1;
    _vid.line = 0;      // Reset current render line
    _vid.line_pos = 0;  // Reset current render line position

    _state->ts.g_yoffs_updated = 0;
    _vid.flash = _state->frame_counter & 0x10;  // Flash attribute changes each 16 frames

    InitRaster();
    InitMemoryCounters();
}

//
// Set appropriate video mode based on ports for current platform.
// Routing only: the per-model detection logic lives in the DetectMode*
// methods below (routed through DetectVideoMode), one per machine family.
//
void Screen::InitRaster()
{
    // Check for null pointers before proceeding
    if (!_context || !_state)
    {
        // Log error and set to safe defaults
        if (_logger)
        {
            _logger->Error(_MODULE, _SUBMODULE,
                           "Screen::InitRaster called with null _context or _state, using defaults");
        }

        // Set to default ZX Spectrum 48K mode
        _vid.mode = M_ZX48;
        _vid.raster = raster[R_256_192];
        return;
    }

    VideoControl& video = _vid;

    /// region <Set current video mode>

    // Per-model detection: port/config state -> (mode, raster)
    ModeSelection selection = DetectVideoMode(_context->config.mem_model);

    // If after all the configuration checks, we still have an invalid mode (M_NUL),
    // we need to set a valid default mode to prevent framebuffer allocation errors
    if (selection.mode == M_NUL)
    {
        // Default to ZX48 mode if we have an invalid mode
        // This ensures we always have a valid video mode for framebuffer allocation
        selection.mode = M_ZX48;
    }

    video.mode = selection.mode;
    video.raster = raster[selection.raster];

    /// endregion </Set current video mode>

    // Select renderer for the mode
    // Apply when the detected mode differs from the ACTIVE raster mode (_mode),
    // not merely from the previous detection result: the constructor defaults
    // (_mode/_vid.mode) can disagree with the model's base mode, and comparing
    // detection-to-detection left 48K/128K machines running with the Pentagon
    // raster and contention disabled.
    if (video.mode != _mode)
    {
        SetVideoMode(video.mode);

        /// region <Sanity checks>
#ifdef _DEBUG
        // 1. Frame duration from config should be longer than raster-defined frame duration
        // (configFrameDuration == 0 means 'unconfigured' - bare test contexts - and is skipped)
        if (_rasterState.configFrameDuration != 0 &&
            _rasterState.configFrameDuration < _rasterState.maxFrameTiming)
        {
            std::string error = StringHelper::Format(
                "Screen::SetVideoMode config.frame: %d cannot be less than _rasterState.maxFrameTiming: %d",
                _rasterState.configFrameDuration, _rasterState.maxFrameTiming);
            throw std::logic_error(error);
        }
#endif  // _DEBUG
        /// endregion </Sanity checks>
    }
}

Screen::ModeSelection Screen::DetectVideoMode(MEM_MODEL model) const
{
    const EmulatorState& state = _context->emulatorState;

    switch (model)
    {
        case MM_SPECTRUM48:
            return DetectModeZX48(state);
        case MM_SPECTRUM128:
        case MM_PLUS3:
            return DetectModeZX128(state);
        case MM_PENTAGON:
            return DetectModePentagon(state);
        case MM_ATM450:
            return DetectModeATM1(state);
        case MM_ATM710:
            return DetectModeATM2(state);
        case MM_ATM3:
            return DetectModeATM3(state);
        case MM_PROFI:
            return DetectModeProfi(state);
        case MM_GMX:
            return DetectModeGMX(state);
        default:
            // Other models keep their current/legacy mode selection
            return DetectModeLegacy(state);
    }
}

Screen::ModeSelection Screen::DetectModeZX48(const EmulatorState& /*state*/) const
{
    return { M_ZX48, R_256_192 };
}

Screen::ModeSelection Screen::DetectModeZX128(const EmulatorState& /*state*/) const
{
    return { M_ZX128, R_256_192 };
}

// Pentagon 128K: user-forced overscan (UI toggle) must survive the per-frame
// re-detection - without the flag a manual M_P384 selection is reverted to
// the model's base mode on the next frame. Guest-programmed AlCo modes
// (EFF7 bits) still take priority over the override.
Screen::ModeSelection Screen::DetectModePentagon(const EmulatorState& state) const
{
    VideoModeEnum mode = M_PENTAGON128K;
    RasterModeEnum rasterMode = R_256_192;

    if (_overscanForced)
    {
        mode = M_P384;
        rasterMode = R_384_304;
    }

    const uint8_t alco = state.pEFF7 & (EFF7_4BPP | EFF7_512 | EFF7_384 | EFF7_HWMC);
    if (alco != 0)
    {
        switch (alco)
        {
            case EFF7_4BPP: mode = M_P16; break;
            case EFF7_HWMC: mode = M_PMC; break;
            case EFF7_512:  mode = M_PHR; break;
            case EFF7_384:  mode = M_P384; rasterMode = R_384_304; break;

            default:
                // Several AlCo bits at once - unsupported combination
                mode = M_NUL;
                break;
        }
    }

    return { mode, rasterMode };
}

// ATM ZX-compatible base mode (all ATM detectors): standard ZX rendering
// (same DrawZX callback and 352x288 geometry as M_ZX128) but with ZX48-class
// timing. The ATM ULA preset is 69888 T/frame @ 224 T/line (reference
// unreal.ini PRESET.ATM1_2_3.5MHz), matching ApplyModelTimingDefaults;
// M_ZX128's authentic 70908/228 timing would exceed the ATM config frame and
// trip the SetVideoMode sanity check. Screen bank selection is mode-agnostic
// (state.ts.vpage), so the 128K shadow screen still works.

// ATM 1 (ATM-TURBO v4.50): extended modes via aFE bits 5-6
Screen::ModeSelection Screen::DetectModeATM1(const EmulatorState& state) const
{
    VideoModeEnum mode = M_ZX48;
    RasterModeEnum rasterMode = R_256_192;

    const uint8_t atmMode = (state.aFE >> 5) & 3;
    if (atmMode != FF77_ZX)
    {
        rasterMode = R_320_200;
        if (atmMode == aFE_16)
            mode = M_ATM16;
        else if (atmMode == aFE_MC)
            mode = M_ATMHR;
        else
            mode = M_NUL;
    }

    return { mode, rasterMode };
}
// ATM 2 (ATM-TURBO 2+ v7.10): extended modes via FF77 bits 0-2. FF77_TL is
// ATM3-only and yields M_NUL here (InitRaster falls it back to a valid mode).
Screen::ModeSelection Screen::DetectModeATM2(const EmulatorState& state) const
{
    VideoModeEnum mode = M_ZX48;
    RasterModeEnum rasterMode = R_256_192;

    const uint8_t atmMode = state.pFF77 & 7;
    if (atmMode != FF77_ZX)
    {
        rasterMode = R_320_200;
        switch (atmMode)
        {
            case FF77_16: mode = M_ATM16; break;
            case FF77_MC: mode = M_ATMHR; break;
            case FF77_TX: mode = M_ATMTX; break;
            default:      mode = M_NUL; break;
        }
    }

    return { mode, rasterMode };
}

// ATM 3 (ZX-Evo / PentEvo): FF77 extended modes incl. Text Linear. Unlike
// Pentagon, EFF7 on ATM is extended CONTROL only (turbo / lockmem / rocache)
// - its bits select no AlCo video modes. Mapping them to the Pentagon M_P16 /
// M_PMC descriptors would also pull in 320-line / 71680 T timing that
// conflicts with the ATM frame (69888 T, forced by ApplyModelTimingDefaults).
Screen::ModeSelection Screen::DetectModeATM3(const EmulatorState& state) const
{
    VideoModeEnum mode = M_ZX48;
    RasterModeEnum rasterMode = R_256_192;

    const uint8_t atmMode = state.pFF77 & 7;
    if (atmMode != FF77_ZX)
    {
        rasterMode = R_320_200;
        switch (atmMode)
        {
            case FF77_16: mode = M_ATM16; break;
            case FF77_MC: mode = M_ATMHR; break;
            case FF77_TX: mode = M_ATMTX; break;
            case FF77_TL: mode = M_ATMTL; break;
            default:      mode = M_NUL; break;
        }
    }

    return { mode, rasterMode };
}

// Profi: DFFD bit 7 selects the 512x240 hi-res mode; without it the model
// keeps its current/legacy mode selection
Screen::ModeSelection Screen::DetectModeProfi(const EmulatorState& state) const
{
    if (state.pDFFD & 0x80)
        return { M_PROFI, R_512_240 };

    return { _vid.mode, R_256_192 };
}

// GMX: 7EFD bit 3 selects the extended 320x200 mode; without it the model
// keeps its current/legacy mode selection
Screen::ModeSelection Screen::DetectModeGMX(const EmulatorState& state) const
{
    if (state.p7EFD & 0x08)
        return { M_GMX, R_320_200 };

    return { _vid.mode, R_256_192 };
}

// Unknown/legacy models: keep the current mode, standard ZX raster
Screen::ModeSelection Screen::DetectModeLegacy(const EmulatorState& /*state*/) const
{
    return { _vid.mode, R_256_192 };
}

void Screen::InitMemoryCounters()
{
    // Initialize all memory cycle counters to zero
    for (int i = 0; i < 320; i++)
    {
        _vid.memvidcyc[i] = 0;
        _vid.memcpucyc[i] = 0;
        _vid.memtsscyc[i] = 0;
        _vid.memtstcyc[i] = 0;
        _vid.memdmacyc[i] = 0;
    }

    // Reset video memory changed flag if state is available
    if (_state)
    {
        _state->video_memory_changed = false;
    }
}

/// endregion </Initialization>

void Screen::SetVideoMode(VideoModeEnum mode)
{
    _mode = mode;
    _nullCallback = _drawCallbacks[M_NUL];
    _drawCallback = _drawCallbacks[_mode];
    _borderCallback = _drawCallbacks[M_BRD];

    /// region <Calculate raster values>

    /// Note!: all timings are in t-states, although raster descriptor has pixels as UOM. So recalculation is required
    const RasterDescriptor& rasterDescriptor = rasterDescriptors[_mode];

    // For M_P384 overscan mode, use Pentagon timing for all calculations
    // Only the framebuffer size differs - timing must be identical to Pentagon
    const RasterDescriptor& timingDescriptor = (_mode == M_P384) ? rasterDescriptors[M_PENTAGON128K] : rasterDescriptor;

    /// region <Config values>
    _rasterState.configFrameDuration = _context->config.frame;
    /// endregion </Config values>

    /// region <Frame timings>

    _rasterState.pixelsPerLine = timingDescriptor.pixelsPerLine;
    _rasterState.tstatesPerLine = _rasterState.pixelsPerLine / _rasterState.pixelsPerTState;
    _rasterState.maxFrameTiming =
        _rasterState.tstatesPerLine *
        (timingDescriptor.vSyncLines + timingDescriptor.vBlankLines + timingDescriptor.fullFrameHeight);

    /// endregion </Frame timings>

    /// region <Vertical timings>

    // Invisible blank area on top
    _rasterState.blankAreaStart = 0;
    _rasterState.blankAreaEnd =
        _rasterState.tstatesPerLine * (timingDescriptor.vSyncLines + timingDescriptor.vBlankLines) - 1;

    // Top border
    _rasterState.topBorderAreaStart = _rasterState.blankAreaEnd + 1;
    _rasterState.topBorderAreaEnd =
        _rasterState.topBorderAreaStart + _rasterState.tstatesPerLine * timingDescriptor.screenOffsetTop - 1;

    // Screen + side borders
    _rasterState.screenAreaStart = _rasterState.topBorderAreaEnd + 1;
    _rasterState.screenAreaEnd =
        _rasterState.screenAreaStart + _rasterState.tstatesPerLine * timingDescriptor.screenHeight - 1;

    // Bottom border
    _rasterState.bottomBorderAreaStart = _rasterState.screenAreaEnd + 1;
    _rasterState.bottomBorderAreaEnd =
        _rasterState.bottomBorderAreaStart +
        _rasterState.tstatesPerLine *
            (timingDescriptor.fullFrameHeight - timingDescriptor.screenHeight - timingDescriptor.screenOffsetTop) -
        1;

    /// endregion </Vertical timings>

    /// region <Horizontal timings>

    _rasterState.blankLineAreaStart = 0;
    _rasterState.blankLineAreaEnd =
        ((timingDescriptor.hSyncPixels + timingDescriptor.hBlankPixels) / _rasterState.pixelsPerTState) - 1;

    _rasterState.leftBorderAreaStart = _rasterState.blankLineAreaEnd + 1;
    _rasterState.leftBorderAreaEnd =
        _rasterState.leftBorderAreaStart + (timingDescriptor.screenOffsetLeft / _rasterState.pixelsPerTState) - 1;

    _rasterState.screenLineAreaStart = _rasterState.leftBorderAreaEnd + 1;
    _rasterState.screenLineAreaEnd =
        _rasterState.screenLineAreaStart + (timingDescriptor.screenWidth / _rasterState.pixelsPerTState) - 1;

    _rasterState.rightBorderAreaStart = _rasterState.screenLineAreaEnd + 1;
    _rasterState.rightBorderAreaEnd =
        _rasterState.rightBorderAreaStart +
        ((timingDescriptor.fullFrameWidth - timingDescriptor.screenOffsetLeft - timingDescriptor.screenWidth) /
         _rasterState.pixelsPerTState) -
        1;

    /// endregion </Horizontal timings>

    /// endregion </Calculate raster values>

    /// region <Model-specific ULA behavior>
    // Set border update granularity and contention per model.
    // Derived from MiSTer HDL ula.sv:
    //   Pentagon (!mZX): border updated every HC cycle (1T), no contention
    //   ZX-48K/128K (mZX): border latched every 8 HC (4T), contention active
    switch (mode)
    {
        case M_PENTAGON128K:
        case M_PMC:
        case M_P16:
        case M_P384:  // Pentagon overscan - same ULA behavior as standard Pentagon
        case M_PHR:
            _rasterState.borderUpdateTStates = 1;
            _rasterState.contentionEnabled = false;
            _rasterState.fetchType = ULA_DISCRETE_LOGIC;
            break;
        case M_ZX48:
        case M_ZX128:
        default:
            _rasterState.borderUpdateTStates = 4;
            _rasterState.contentionEnabled = true;
            _rasterState.fetchType = ULA_FERRANTI;
            break;
    }
    /// endregion </Model-specific ULA behavior>

    // Push raster timing + contention flag to the standalone ULA contention component
    if (_context && _context->pUlaContention)
    {
        ContentionRaster cr;
        cr.configFrameDuration = _rasterState.configFrameDuration;
        cr.screenAreaStart = _rasterState.screenAreaStart;
        cr.screenAreaEnd = _rasterState.screenAreaEnd;
        cr.tstatesPerLine = _rasterState.tstatesPerLine;
        cr.screenLineAreaStart = _rasterState.screenLineAreaStart;
        cr.screenLineAreaEnd = _rasterState.screenLineAreaEnd;
        _context->pUlaContention->UpdateRaster(cr);
        _context->pUlaContention->SetContentionEnabled(_rasterState.contentionEnabled);
        _context->pUlaContention->SetFetchType((UlaFetchType)_rasterState.fetchType);
    }

    // Allocate framebuffer
    AllocateFramebuffer(_mode);

    // Notify consumers that the video mode changed. JUSTIFICATION: mode
    // switches are not only UI-driven - guest software switches modes by
    // port writes (Pentagon AlCo via EFF7, Profi via DFFD, ATM via FF77,
    // GMX), detected by InitRaster mid-emulation on the emulation thread.
    // Framebuffer geometry (and, for size-changing switches, the buffer
    // address) is different afterwards; a GUI consumer with cached
    // dimensions has CopyPresentedFramebuffer rejecting every copy (dst too
    // small) and freezes on the last frame. Consumers must re-attach.
    // Skipped during construction (pEmulator not wired yet).
    if (_context && _context->pEmulator)
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.Post(NC_VIDEO_MODE_CHANGED,
                           new EmulatorFramePayload(_context->pEmulator->GetUUID(), 0));
    }

#ifdef _DEBUG
    MLOGINFO("%s", DumpRasterState().c_str());
#endif  // _DEBUG
}

/// Set active ZX-Spectrum screen
/// @param screen Normal screen (RAM page 5) or Shadow screen (RAM page 7)
void Screen::SetActiveScreen(SpectrumScreenEnum screen)
{
    Memory& memory = *_context->pMemory;

    uint8_t* activeScreenMemoryOffset = memory.RAMPageAddress(5);  // RAM Page 5 is used for default / normal screen
    switch (screen)
    {
        case SCREEN_NORMAL:  // Normal screen (RAM Page 5)
            activeScreenMemoryOffset = memory.RAMPageAddress(5);
            break;
        case SCREEN_SHADOW:  // Shadow screen (RAM Page 7)
            activeScreenMemoryOffset = memory.RAMPageAddress(7);
            break;
        default:
            MLOGERROR("Screen::SetActiveScreen - Invalid screen mode specified %d. Only 0=Normal, 1=Shadow are valid",
                      screen);
            assert("Invalid screen");
            break;
    }

    _activeScreen = screen;
    _activeScreenMemoryOffset = activeScreenMemoryOffset;
}

/// Set current border color
/// \param color
void Screen::SetBorderColor(uint8_t color)
{
    // Flush/Render all pending pixels using the CURRENT (old) border color 
    // up to the exact CPU T-state of the I/O port write.
    // This fixes pixel-perfect multicolor effects (raster bars) across all models.
    UpdateScreen();

    // Only bits [0:2] contain border color
    _borderColor = color & 0b0000'0111;
}

VideoModeEnum Screen::GetVideoMode()
{
    return _mode;
}

uint8_t Screen::GetActiveScreen()
{
    return _activeScreen;
}

uint8_t Screen::GetBorderColor()
{
    return _borderColor;
}

uint32_t Screen::GetCurrentTstate()
{
    Z80* cpu = _context->pCore->GetZ80();
    EmulatorState& state = _context->emulatorState;

    // Z80 runs at scaled speed (multiplied by speed multiplier: 1x, 2x, 4x, 8x, 16x)
    // but ULA/screen expects unscaled t-states based on base 3.5MHz clock
    // Video signal timing is independent of CPU speed
    uint32_t scaledTstate = cpu->t;
    uint32_t unscaledTstate = scaledTstate / state.current_z80_frequency_multiplier;

    return unscaledTstate;
}

///
/// Convert whole ZX-Spectrum screen to RGBA framebuffer
///
void Screen::RenderOnlyMainScreen()
{
    // No default implementation
    return;
}

/// @brief Render entire screen in a single pass when ScreenHQ=OFF
/// Called by MainLoop::OnFrameEnd() to provide batch rendering when per-t-state
/// rendering is bypassed. Base implementation uses RenderOnlyMainScreen;
/// ScreenZX overrides this with RenderScreen_Batch8 for optimal performance.
void Screen::RenderFrameBatch()
{
    // Default: fall back to simple full-screen render
    // ScreenZX overrides with optimized RenderScreen_Batch8()
    RenderOnlyMainScreen();
}

/// @brief Update cached feature flag state from FeatureManager
/// This is called automatically by FeatureManager::onFeatureChanged() whenever
/// any feature state changes. Components cache flags for performance.
void Screen::UpdateFeatureCache()
{
    if (_context && _context->pFeatureManager)
    {
        _feature_screenhq_enabled = _context->pFeatureManager->isEnabled(Features::kScreenHQ);
    }
}

void Screen::RefreshMemoryPointers()
{
    // Refresh _activeScreenMemoryOffset after memory migration
    // This pointer becomes stale when Memory migrates between heap and shared memory
    if (_memory)
    {
        // Re-fetch pointer for currently active screen (Bank 5 or 7)
        uint8_t ramPage = (_activeScreen == SCREEN_SHADOW) ? 7 : 5;
        _activeScreenMemoryOffset = _memory->RAMPageAddress(ramPage);
    }
}

void Screen::SaveScreen()
{
    ImageHelper::SaveFrameToPNG_Async(_framebuffer.memoryBuffer, _framebuffer.memoryBufferSize, _framebuffer.width,
                                      _framebuffer.height);
}

void Screen::SaveZXSpectrumNativeScreen()
{
    uint8_t* buffer = _memory->MapZ80AddressToPhysicalAddress(0x4000);
    uint8_t bank = _memory->GetRAMPageFromAddress(buffer);
    int frameNumber = _state->frame_counter;

    Logger::UnmuteSilent();
    MLOGDEBUG("Saving ZX Native screen: RAN%d (0x%08x)", bank, buffer);
    Logger::MuteSilent();

    ImageHelper::SaveZXSpectrumNativeScreen(buffer, frameNumber);
}

/// region <Framebuffer related>

/// Framebuffer clear: OPAQUE black. The format is RGBA8888 (0xAABBGGRR) and
/// must never expose transparency - consumers (DeviceScreen, videowall tiles)
/// wrap the raw buffer with alpha honored. A 0x00-alpha clear (memset) left
/// freshly allocated/switched buffers fully transparent and the window
/// background flashed through until each pixel was redrawn (the ATM BIOS
/// reprograms FF77 several times at boot, so every mode switch re-cleared).
static void ClearFramebufferOpaque(uint8_t* buffer, size_t sizeBytes)
{
    uint32_t* pixels = reinterpret_cast<uint32_t*>(buffer);
    const size_t count = sizeBytes / RGBA_SIZE;
    for (size_t i = 0; i < count; i++)
        pixels[i] = 0xFF000000u;
}

void Screen::AllocateFramebuffer(VideoModeEnum mode)
{
    // Apply the configured A/V sync video delay (auto -1 = 2 frames: the
    // audio path's ring target + HW buffer expressed in frame periods)
    if (_context)
    {
        const int cfg = _context->config.videoPresentDelayFrames;
        SetPresentDelayFrames(cfg < 0 ? 2 : static_cast<uint8_t>(cfg));
    }

    // Buffer already allocated for the selected video mode
    if (_framebuffer.memoryBuffer != nullptr && _framebuffer.videoMode == mode)
    {
        return;
    }

    // Same-size mode switch (e.g. M_ZX48 <-> M_ZX128 <-> M_PENTAGON128K, all
    // 352x288): KEEP the existing buffers. JUSTIFICATION: consumers hold raw
    // framebuffer pointers without locks (DeviceScreen's live QImage wrap,
    // videowall tiles); reallocating identical-size buffers frees memory the
    // GUI thread may be reading mid-paint - a use-after-free with zero upside.
    // Different-size switches still reallocate (unavoidable) and are covered
    // by the NC_VIDEO_MODE_CHANGED re-attach.
    if (_framebuffer.memoryBuffer != nullptr && mode < M_MAX)
    {
        const RasterDescriptor& rd = rasterDescriptors[mode];
        size_t newSize = (size_t)rd.fullFrameWidth * rd.fullFrameHeight * RGBA_SIZE;

        if (newSize != 0 && newSize == _framebuffer.memoryBufferSize)
        {
            _framebuffer.videoMode = mode;
            _framebuffer.width = rd.fullFrameWidth;
            _framebuffer.height = rd.fullFrameHeight;
            ClearFramebufferOpaque(_framebuffer.memoryBuffer, _framebuffer.memoryBufferSize);

            std::lock_guard<std::mutex> lock(_presentMutex);
            for (size_t i = 0; i < PRESENT_SLOTS; i++)
            {
                if (_presentSlots[i])
                    ClearFramebufferOpaque(_presentSlots[i], _presentBufferSize);
            }
            _presentLatchCounter = 0;
            return;
        }
    }

    // Deallocate existing framebuffer memory
    DeallocateFramebuffer();

    bool isUnknownVideoMode = false;
    switch (mode)
    {
        case M_ZX48:
        case M_ZX128:
        case M_PENTAGON128K:
        case M_PMC:
        case M_P16:
        case M_P384:  // Pentagon 384x304 overscan mode
        case M_PHR:
        case M_ATM16:   // ATM EGA 16-color 320x200
        case M_ATMHR:   // ATM Hardware Multicolor 640x200
        case M_ATMTX:   // ATM Text 80x25 (640x200)
        case M_ATMTL:   // ATM3 Linear Text
            break;
        default:
            MLOGWARNING("AllocateFramebuffer: Unknown video mode");

            isUnknownVideoMode = true;
            break;
    }

    if (!isUnknownVideoMode)
    {
        const RasterDescriptor& rasterDescriptor = rasterDescriptors[mode];

        _framebuffer.videoMode = mode;
        _framebuffer.width = rasterDescriptor.fullFrameWidth;
        _framebuffer.height = rasterDescriptor.fullFrameHeight;

        // Calculate required buffer size and allocate memory
        _framebuffer.memoryBufferSize = _framebuffer.width * _framebuffer.height * RGBA_SIZE;
        _framebuffer.memoryBuffer = new uint8_t[_framebuffer.memoryBufferSize];

        // Clear the whole framebuffer (opaque black - see ClearFramebufferOpaque)
        ClearFramebufferOpaque(_framebuffer.memoryBuffer, _framebuffer.memoryBufferSize);

        // Allocate the matching presentation (latched) buffer.
        // _presentBufferSize is the authoritative size for cross-thread readers
        // and changes only together with the buffer, under the mutex - readers
        // must never trust _framebuffer.memoryBufferSize, which is published
        // outside the lock during mode switches.
        {
            std::lock_guard<std::mutex> lock(_presentMutex);
            for (size_t i = 0; i < PRESENT_SLOTS; i++)
            {
                delete[] _presentSlots[i];
                _presentSlots[i] = new uint8_t[_framebuffer.memoryBufferSize];
                ClearFramebufferOpaque(_presentSlots[i], _framebuffer.memoryBufferSize);
            }
            _presentBufferSize = _framebuffer.memoryBufferSize;
            _presentLatchCounter = 0;
        }

#ifdef _DEBUG
        MLOGINFO("Framebuffer allocated");

        static char videoModeInfo[200];
        DumpFramebufferInfo(videoModeInfo, sizeof(videoModeInfo));
        MLOGINFO(videoModeInfo);
#endif
    }
    else
    {
        MLOGERROR("Unable to allocate framebuffer, unknown video mode");
        throw new std::logic_error("Unable to allocate framebuffer, unknown video mode");
    }
}

void Screen::DeallocateFramebuffer()
{
    if (_framebuffer.memoryBuffer != nullptr)
    {
        delete[] _framebuffer.memoryBuffer;
        _framebuffer.memoryBuffer = nullptr;
        _framebuffer.memoryBufferSize = 0;
    }

    {
        std::lock_guard<std::mutex> lock(_presentMutex);
        for (size_t i = 0; i < PRESENT_SLOTS; i++)
        {
            delete[] _presentSlots[i];
            _presentSlots[i] = nullptr;
        }
        _presentBufferSize = 0;
        _presentLatchCounter = 0;
    }
}

void Screen::LatchFramebuffer()
{
    // Runs on the emulation thread, which is also the only mutator of
    // _framebuffer - those fields are stable here
    if (_framebuffer.memoryBuffer == nullptr)
        return;

    std::lock_guard<std::mutex> lock(_presentMutex);
    if (_presentSlots[0] && _presentBufferSize == _framebuffer.memoryBufferSize)
    {
        uint8_t* slot = _presentSlots[_presentLatchCounter % PRESENT_SLOTS];
        VideoUtils::CopyFrameBuffer(slot, _framebuffer.memoryBuffer, _presentBufferSize);
        _presentLatchCounter++;

        _lastLatchTimestampUs.store(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_release);
    }
}

bool Screen::CopyPresentedFramebuffer(uint8_t* dst, size_t dstSize)
{
    if (dst == nullptr)
        return false;

    // Size check and copy length must both use _presentBufferSize under the
    // mutex: _framebuffer.memoryBufferSize is published outside the lock
    // during mode-switch reallocation, and trusting it here could overread
    // a present buffer from the previous video mode
    std::lock_guard<std::mutex> lock(_presentMutex);
    if (_presentSlots[0] == nullptr || _presentBufferSize == 0 || dstSize < _presentBufferSize)
        return false;
    if (_presentLatchCounter == 0)
    {
        // Nothing latched yet: serve the zeroed slot (black frame) - callers
        // treat a false return as "no present buffer", not "not yet"
        VideoUtils::CopyFrameBuffer(dst, _presentSlots[0], _presentBufferSize);
        return true;
    }

    // A/V sync: present the frame latched _presentDelayFrames ago so video
    // trails by the same constant latency as the audio path. During the
    // first frames after start/reset the delay is clamped to what exists -
    // the queue "fills" naturally, exactly the 1-2 frame startup buffering.
    const uint64_t newest = _presentLatchCounter - 1;
    uint64_t delay = _presentDelayFrames.load(std::memory_order_acquire);
    if (delay > newest)
        delay = newest;

    const uint8_t* slot = _presentSlots[(newest - delay) % PRESENT_SLOTS];
    VideoUtils::CopyFrameBuffer(dst, slot, _presentBufferSize);
    return true;
}

FramebufferDescriptor& Screen::GetFramebufferDescriptor()
{
    return _framebuffer;
}

void Screen::GetFramebufferData(uint32_t** buffer, size_t* size)
{
    if (buffer && size && _framebuffer.memoryBuffer && _framebuffer.memoryBufferSize)
    {
        *buffer = (uint32_t*)_framebuffer.memoryBuffer;
        *size = _framebuffer.memoryBufferSize;
    }
}

void Screen::GetRGBAPalette16(uint32_t* colors)
{
    // Default ZX Spectrum 16-color palette (same as DrawZX uses)
    // Format is ABGR on little-endian systems
    static const uint32_t zxPalette[16] = {
        // Normal intensity (brightness OFF)
        0xFF000000,  // 0: Black
        0xFF0022C7,  // 1: Blue      (R=0x00, G=0x22, B=0xC7)
        0xFFD62816,  // 2: Red       (R=0xD6, G=0x28, B=0x16)
        0xFFD433C7,  // 3: Magenta   (R=0xD4, G=0x33, B=0xC7)
        0xFF00C525,  // 4: Green     (R=0x00, G=0xC5, B=0x25)
        0xFF00C7C9,  // 5: Cyan      (R=0x00, G=0xC7, B=0xC9)
        0xFFCCC82A,  // 6: Yellow    (R=0xCC, G=0xC8, B=0x2A)
        0xFFCACACA,  // 7: White     (R=0xCA, G=0xCA, B=0xCA)
        // Bright intensity (brightness ON)
        0xFF000000,  // 8: Bright Black  (same as black)
        0xFF002BFB,  // 9: Bright Blue   (R=0x00, G=0x2B, B=0xFB)
        0xFFFF331C,  // 10: Bright Red   (R=0xFF, G=0x33, B=0x1C)
        0xFFFF40FC,  // 11: Bright Magenta (R=0xFF, G=0x40, B=0xFC)
        0xFF00F92F,  // 12: Bright Green  (R=0x00, G=0xF9, B=0x2F)
        0xFF00FBFE,  // 13: Bright Cyan   (R=0x00, G=0xFB, B=0xFE)
        0xFFFFFC36,  // 14: Bright Yellow (R=0xFF, G=0xFC, B=0x36)
        0xFFFFFFFF,  // 15: Bright White  (R=0xFF, G=0xFF, B=0xFF)
    };

    if (colors)
    {
        memcpy(colors, zxPalette, sizeof(zxPalette));
    }
}

/// endregion </Framebuffer related>

/// region <Display viewport>

void Screen::SetDisplayViewport(const DisplayViewport& viewport)
{
    _displayViewport = viewport;
}

const DisplayViewport& Screen::GetDisplayViewport() const
{
    return _displayViewport;
}

uint16_t Screen::GetDisplayWidth() const
{
    return _displayViewport.GetDisplayWidth(_framebuffer.width);
}

uint16_t Screen::GetDisplayHeight() const
{
    return _displayViewport.GetDisplayHeight(_framebuffer.height);
}

/// endregion </Display viewport>

std::string Screen::GetVideoModeName(VideoModeEnum mode)
{
    std::string result;

    switch (mode)
    {
        case M_NUL:
            result = "Nul";
            break;
        case M_ZX48:
            result = "ZX";
            break;
        case M_PMC:
            result = "PMC";
            break;
        case M_P16:
            result = "P16";
            break;
        case M_P384:
            result = "P384";
            break;
        case M_PHR:
            result = "PHR";
            break;
        case M_TIMEX:
            result = "Timex";
            break;
        case M_TS16:
            result = "TS16";
            break;
        case M_TS256:
            result = "TS256";
            break;
        case M_TSTX:
            result = "TSTX";
            break;
        case M_ATM16:
            result = "ATM16";
            break;
        case M_ATMHR:
            result = "ATMHR";
            break;
        case M_ATMTX:
            result = "ATMTX";
            break;
        case M_ATMTL:
            result = "ATMTL";
            break;
        case M_PROFI:
            result = "PROFI";
            break;
        case M_GMX:
            result = "GMX";
            break;
        case M_BRD:
            result = "Border";
            break;
        default:
            result = "Unknown";
            break;
    }

    return result;
}

void Screen::DrawScreenBorder(uint32_t n)
{
    [[maybe_unused]] Z80& cpu = *_cpu;
    EmulatorState& state = _context->emulatorState;
    [[maybe_unused]] CONFIG& config = _context->config;
    VideoControl& video = _context->pScreen->_vid;

    video.t_next += n;
    uint32_t vptr = video.vptr;

    for (; n > 0; n--)
    {
        uint32_t pixelColorRGBA = video.clut[state.ts.border];
        vbuf[video.buf][vptr] = vbuf[video.buf][vptr + 1] = vbuf[video.buf][vptr + 2] = vbuf[video.buf][vptr + 3] =
            pixelColorRGBA;
        vptr += 4;
    }

    video.vptr = vptr;
}

/// Replay ULA render for the whole period since last call (same as prev. CPU command complete)
/// \param fromTstate
/// \param toTstate
/// \param borderColor
void Screen::DrawPeriod(uint32_t fromTstate, uint32_t toTstate)
{
    // =============================================================================
    // SCREENHQ OPTIMIZATION BYPASS
    // =============================================================================
    // When ScreenHQ feature is disabled (OFF), we skip the per-t-state rendering
    // loop entirely. This bypasses ~70,000 Draw() calls per frame (one per t-state).
    //
    // WHAT IS BYPASSED:
    //   - The for loop below that calls Draw(tstate) for each t-state in the period
    //   - Draw() -> TstateCoordLUT lookup + pixel color calculation + framebuffer write
    //   - All per-t-state attribute reads for "racing the beam" multicolor effects
    //
    // WHAT HAPPENS INSTEAD:
    //   - MainLoop::OnFrameEnd() calls Screen::RenderFrameBatch()
    //   - Which calls ScreenZX::RenderScreen_Batch8() for 25x faster batch rendering
    //   - Entire 256x192 screen is rendered in one pass using 8-pixel symbols
    //
    // TRADE-OFF:
    //   - ScreenHQ=ON (default): Per-t-state, demo multicolor effects work, ~343μs/frame
    //   - ScreenHQ=OFF: Batch 8-pixel, multicolor breaks, ~13μs/frame (25x faster)
    //
    // See: docs/inprogress/2026-01-11-performance-optimizations/phase-4-5-execution-log.md
    // =============================================================================
    if (!_feature_screenhq_enabled)
    {
        return;  // Skip per-t-state rendering; batch render happens at frame end
    }

    /// region <Sanity checks>
    constexpr int MAX_FRAME_DURATION_TOLERANCE = 100;  // We can allow up to 100 t-state cycles after frame ends since
                                                       // CPU can be in the middle of current command proceesing

    [[maybe_unused]] EmulatorState& state = _context->emulatorState;
    CONFIG& config = _context->config;

    // Screen timings work with unscaled 3-byte t-state counter
    // Z80 runs at scaled speed, but screen sees unscaled t-state values
    uint32_t maxFrameDuration = config.frame + MAX_FRAME_DURATION_TOLERANCE;

    // Next frame started during current CPU command processing. Adjust to Tstate
    if (fromTstate > toTstate)
    {
        if (toTstate < MAX_FRAME_DURATION_TOLERANCE)
        {
            toTstate = fromTstate + toTstate;
        }
        else
        {
            MLOGERROR("Screen::DrawPeriod - Incorrect fromTstate: %d and/or toTstate: %d. Tolerance: %d. Skipping period.",
                      fromTstate, toTstate, MAX_FRAME_DURATION_TOLERANCE);
            return;
        }
    }

    if (fromTstate >= maxFrameDuration || toTstate >= maxFrameDuration)
    {
        MLOGERROR("Screen::DrawPeriod - Incorrect fromTstate: %d and/or toTstate: %d. MAX_FRAME_DURATION: %d. Skipping period.",
                  fromTstate, toTstate, maxFrameDuration);
        return;
    }

    // Do not capture previously handled t-state (at the end of previous period. i.e. (from: to]
    if (toTstate > fromTstate)
    {
        fromTstate += 1;
    }

    /// endregion </Sanity checks>

    for (uint32_t i = fromTstate; i <= toTstate; i++)
    {
        Draw(i);
    }
}

/// ULA video frame render simulation.
/// Called after each CPU command cycle
/// Note: default implementation calls registered callback. Platform specific overrides allowed.
void Screen::Draw(uint32_t tstate)
{
    (this->*_currentDrawCallback)(tstate);
}

// Skip render
void Screen::DrawNull(uint32_t n)
{
    (void)n;
}

// Genuine Sinclair ZX Spectrum
void Screen::DrawZX(uint32_t n)
{
    static uint32_t palette[2][8] = {{
                                         // Brightness OFF
                                         0x00000000,  // Black
                                         0x000022C7,  // Blue
                                         0x00D62816,  // Red
                                         0x00D433C7,  // Magenta
                                         0x0000C525,  // Green,
                                         0x0000C7C9,  // Cyan
                                         0x00CCC82A,  // Yellow
                                         0x00CACACA   // White
                                     },
                                     {
                                         // Brightness ON
                                         0x00000000,  // Black
                                         0x00002BFB,  // Blue
                                         0x00FF331C,  // Red
                                         0x00FF40FC,  // Magenta
                                         0x0000F92F,  // Green
                                         0x0000FBFE,  // Cyan
                                         0x00FFFC36,  // Yellow
                                         0x00FFFFFF   // White
                                     }};

    EmulatorState& state = _context->emulatorState;
    CONFIG& config = _context->config;
    VideoControl& video = _vid;

    if (n > sizeof vbuf[0])
    {
        MLOGERROR("Standard ZX-Spectrum cannot have more than %d video lines", sizeof vbuf[0]);
        return;
    }

    uint32_t g =
        ((video.ygctr & 0x07) << 8) + ((video.ygctr & 0x38) << 2) + ((video.ygctr & 0xC0) << 5) + (video.xctr & 0x1F);
    uint32_t a = ((video.ygctr & 0xF8) << 2) + (video.xctr & 0x1F) + 0x1800;
    uint8_t* zx_screen_mem = _system->GetMemory()->RAMPageAddress(state.ts.vpage);
    uint32_t vptr = video.vptr;
    uint16_t vcyc = video.memvidcyc[video.line];
    uint8_t upmod = config.ulaplus;
    [[maybe_unused]] uint8_t tsgpal = state.ts.gpal << 4;

    for (int i = n; i > 0; i -= 4, video.t_next += 4, video.xctr++, g++, a++)
    {
        uint32_t color_paper, color_ink;
        uint8_t pixel =
            zx_screen_mem[g];  // Line of 8 pixels from ZX-Spectrum screen memory (Encoded as bits in single byte)
        uint8_t attrib = zx_screen_mem[a];  // Color attributes for the whole 8x8 character block

        vcyc++;
        video.memcyc_lcmd++;

        if ((upmod != UPLS_NONE) && state.ulaplus_mode)
        {
            // Decode color information as ULA+
            uint32_t psel = (attrib & 0xC0) >> 2;
            uint32_t ink = state.ulaplus_cram[psel + (attrib & 7)];
            uint32_t paper = state.ulaplus_cram[psel + ((attrib >> 3) & 7) + 8];

            color_paper = cr[(paper & 0x1C) >> 2] | cg[(paper & 0xE0) >> 5] | cb[upmod][paper & 0x03];
            color_ink = cr[(ink & 0x1C) >> 2] | cg[(ink & 0xE0) >> 5] | cb[upmod][ink & 0x03];
        }
        else
        {
            // Decode color information as standard ULA
            // Bit 7 - Flash, Bit 6 - Brightness, Bits 5-3 - Paper color, Bits 2-0 - Ink color
            if ((attrib & 0x80) && (state.frame_counter & 0x10))  // Flash attribute for the 8x8 block
                pixel ^= 0xFF;                                    // Invert every N frames

            uint8_t brightness = (attrib & 0x40) >> 3;  // BRIGHTNESS attribute
            uint8_t paper = (attrib >> 3) & 0x07;       // Color for 'PAPER'
            uint8_t ink = attrib & 0x07;                // Color for 'INK'

            color_paper = palette[brightness][paper];  // Resolve PAPER color to RGB
            color_ink = palette[brightness][ink];      // Resolve INK color to RGB
        }

        // Write RGBA 1x8 (scaled to 2x16) line to framebuffer
        vbuf[video.buf][vptr] = vbuf[video.buf][vptr + 1] = ((pixel << 1) & 0x100) ? color_ink : color_paper;
        vbuf[video.buf][vptr + 2] = vbuf[video.buf][vptr + 3] = ((pixel << 2) & 0x100) ? color_ink : color_paper;
        vbuf[video.buf][vptr + 4] = vbuf[video.buf][vptr + 5] = ((pixel << 3) & 0x100) ? color_ink : color_paper;
        vbuf[video.buf][vptr + 6] = vbuf[video.buf][vptr + 7] = ((pixel << 4) & 0x100) ? color_ink : color_paper;
        vbuf[video.buf][vptr + 8] = vbuf[video.buf][vptr + 9] = ((pixel << 5) & 0x100) ? color_ink : color_paper;
        vbuf[video.buf][vptr + 10] = vbuf[video.buf][vptr + 11] = ((pixel << 6) & 0x100) ? color_ink : color_paper;
        vbuf[video.buf][vptr + 12] = vbuf[video.buf][vptr + 13] = ((pixel << 7) & 0x100) ? color_ink : color_paper;
        vbuf[video.buf][vptr + 14] = vbuf[video.buf][vptr + 15] = ((pixel << 8) & 0x100) ? color_ink : color_paper;
        vptr += 16;
    }

    video.vptr = vptr;
    video.memvidcyc[video.line] = vcyc;
}

void Screen::DrawPMC(uint32_t n)
{
    (void)n;
}

void Screen::DrawP16(uint32_t n)
{
    (void)n;
}

void Screen::DrawP384(uint32_t n)
{
    (void)n;
}

void Screen::DrawPHR(uint32_t n)
{
    (void)n;
}

void Screen::DrawTimex(uint32_t n)
{
    (void)n;
}

void Screen::DrawTS16(uint32_t n)
{
    (void)n;
}

void Screen::DrawTS256(uint32_t n)
{
    (void)n;
}

void Screen::DrawTSText(uint32_t n)
{
    (void)n;
}

void Screen::DrawATM16(uint32_t n)
{
    // ATM 16-color EGA mode (320x200)
    // Memory layout: 4 bit-planes, each byte encodes 8 pixels' bit N
    //   Plane 0: video_page - 4, offset 0x0000 (bit 0 of each pixel)
    //   Plane 1: video_page,     offset 0x0000 (bit 1 of each pixel)
    //   Plane 2: video_page - 4, offset 0x2000 (bit 2 of each pixel)
    //   Plane 3: video_page,     offset 0x2000 (bit 3 of each pixel)
    // Each bit position in 4 planes combines to form a 4-bit color index

    EmulatorState& state = _context->emulatorState;
    VideoControl& video = _vid;

    // Get video page (5 or 7 based on 7FFD bit 3)
    uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
    uint8_t altPage = videoPage - 4;  // Page 1 or 3

    uint8_t* plane0 = _memory->RAMPageAddress(altPage);            // bit 0
    uint8_t* plane1 = _memory->RAMPageAddress(videoPage);          // bit 1
    uint8_t* plane2 = _memory->RAMPageAddress(altPage) + 0x2000;   // bit 2
    uint8_t* plane3 = _memory->RAMPageAddress(videoPage) + 0x2000; // bit 3

    // ATM palette lookup (uses programmable palette, but defaults to EGA-like)
    const uint32_t* palette = video.clut;

    uint32_t vptr = video.vptr;

    // ATM 320x200 mode: 40 bytes per line per plane, 200 lines
    uint32_t y = video.ygctr;
    uint32_t screenOffset = y * 40;

    for (uint32_t i = n; i > 0; i -= 4, video.t_next += 4, video.xctr++)
    {
        uint32_t x = video.xctr;
        if (x >= 40) continue;

        uint32_t offset = (screenOffset + x) & 0x1FFF;

        // Read one byte from each plane
        uint8_t b0 = plane0[offset];
        uint8_t b1 = plane1[offset];
        uint8_t b2 = plane2[offset];
        uint8_t b3 = plane3[offset];

        // Extract 8 pixels from bit-planes (MSB first)
        for (int bit = 7; bit >= 0; bit--)
        {
            uint8_t color = ((b0 >> bit) & 1) |
                           (((b1 >> bit) & 1) << 1) |
                           (((b2 >> bit) & 1) << 2) |
                           (((b3 >> bit) & 1) << 3);

            uint32_t c = palette[color];
            // Double pixels for 640 width output
            vbuf[video.buf][vptr++] = c;
            vbuf[video.buf][vptr++] = c;
        }
    }

    video.vptr = vptr;
}

void Screen::DrawATMHiRes(uint32_t n)
{
    // ATM Hardware Multicolor mode (640x200, per-line attributes)
    // Memory layout (relative to video page):
    //   Pixels 0: video_page - 4, offset 0x0000 (page 1/3)
    //   Pixels 1: video_page,     offset 0x0000 (page 5/7)
    //   Attrs 0:  video_page - 4, offset 0x2000 (page 1/3 + 8KB)
    //   Attrs 1:  video_page,     offset 0x2000 (page 5/7 + 8KB)
    // Each byte from 2 pixel planes gives 8 pixels, with per-byte attributes

    EmulatorState& state = _context->emulatorState;
    VideoControl& video = _vid;

    // Standard ZX palette (same as DrawZX)
    static const uint32_t palette[2][8] = {{
        0x00000000, 0x000022C7, 0x00D62816, 0x00D433C7,
        0x0000C525, 0x0000C7C9, 0x00CCC82A, 0x00CACACA
    }, {
        0x00000000, 0x00002BFB, 0x00FF331C, 0x00FF40FC,
        0x0000F92F, 0x0000FBFE, 0x00FFFC36, 0x00FFFFFF
    }};

    // Get video page (5 or 7 based on 7FFD bit 3)
    uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
    uint8_t altPage = videoPage - 4;  // Page 1 or 3

    uint8_t* pix0 = _memory->RAMPageAddress(altPage);         // pixel plane 0
    uint8_t* pix1 = _memory->RAMPageAddress(videoPage);       // pixel plane 1
    uint8_t* attr0 = _memory->RAMPageAddress(altPage) + 0x2000;    // attribute plane 0
    uint8_t* attr1 = _memory->RAMPageAddress(videoPage) + 0x2000;  // attribute plane 1

    uint32_t vptr = video.vptr;

    // ATM HiRes: 80 bytes per line (40 bytes * 2 planes), 200 lines
    uint32_t y = video.ygctr;  // Current line (0-199)
    uint32_t screenOffset = y * 40;

    for (uint32_t i = n; i > 0; i -= 4, video.t_next += 4, video.xctr++)
    {
        uint32_t x = video.xctr;
        if (x >= 40) continue;

        uint32_t offset = (screenOffset + x) & 0x1FFF;

        // Read pixels and attributes from both planes
        uint8_t pixels0 = pix0[offset];
        uint8_t pixels1 = pix1[offset];
        uint8_t attrib0 = attr0[offset];
        uint8_t attrib1 = attr1[offset];

        // Decode attribute (same as ZX: bit 6=bright, bits 5-3=paper, bits 2-0=ink)
        uint8_t bright0 = (attrib0 & 0x40) ? 1 : 0;
        uint8_t paper0 = (attrib0 >> 3) & 0x07;
        uint8_t ink0 = attrib0 & 0x07;
        uint32_t color_paper0 = palette[bright0][paper0];
        uint32_t color_ink0 = palette[bright0][ink0];

        uint8_t bright1 = (attrib1 & 0x40) ? 1 : 0;
        uint8_t paper1 = (attrib1 >> 3) & 0x07;
        uint8_t ink1 = attrib1 & 0x07;
        uint32_t color_paper1 = palette[bright1][paper1];
        uint32_t color_ink1 = palette[bright1][ink1];

        // Render 8 pixels from plane 0, then 8 from plane 1 (16 total, no doubling needed)
        vbuf[video.buf][vptr++] = (pixels0 & 0x80) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (pixels0 & 0x40) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (pixels0 & 0x20) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (pixels0 & 0x10) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (pixels0 & 0x08) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (pixels0 & 0x04) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (pixels0 & 0x02) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (pixels0 & 0x01) ? color_ink0 : color_paper0;

        vbuf[video.buf][vptr++] = (pixels1 & 0x80) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (pixels1 & 0x40) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (pixels1 & 0x20) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (pixels1 & 0x10) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (pixels1 & 0x08) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (pixels1 & 0x04) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (pixels1 & 0x02) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (pixels1 & 0x01) ? color_ink1 : color_paper1;
    }

    video.vptr = vptr;
}

void Screen::DrawATM2Text(uint32_t n)
{
    // ATM Text mode (80x25, 640x200 effective)
    // Memory layout:
    //   Chars 0:  video_page - 4, offset 0x0000 (page 1/3)
    //   Chars 1:  video_page,     offset 0x0000 (page 5/7)
    //   Attrs 0:  video_page - 4, offset 0x2000 (page 1/3 + 8KB)
    //   Attrs 1:  video_page,     offset 0x2000 (page 5/7 + 8KB)
    // Font is stored in SYS ROM at offset 0x1C00 (standard ZX charset)

    EmulatorState& state = _context->emulatorState;
    VideoControl& video = _vid;

    // Standard ZX palette
    static const uint32_t palette[2][8] = {{
        0x00000000, 0x000022C7, 0x00D62816, 0x00D433C7,
        0x0000C525, 0x0000C7C9, 0x00CCC82A, 0x00CACACA
    }, {
        0x00000000, 0x00002BFB, 0x00FF331C, 0x00FF40FC,
        0x0000F92F, 0x0000FBFE, 0x00FFFC36, 0x00FFFFFF
    }};

    // Get video page (5 or 7 based on 7FFD bit 3)
    uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
    uint8_t altPage = videoPage - 4;

    uint8_t* chars0 = _memory->RAMPageAddress(altPage);
    uint8_t* chars1 = _memory->RAMPageAddress(videoPage);
    uint8_t* attrs0 = _memory->RAMPageAddress(altPage) + 0x2000;
    uint8_t* attrs1 = _memory->RAMPageAddress(videoPage) + 0x2000;

    // Font from SYS ROM (page 3, offset 0x1C00)
    // Note: In ATM, font is typically at 0x3D00 in ROM page 3
    uint8_t* font = _memory->ROMPageHostAddress(3) + 0x1D00;

    uint32_t vptr = video.vptr;

    // 80x25 text mode: 64 bytes per row (80 chars interleaved between 2 planes)
    // Screen has 200 lines, each character is 8 lines high
    uint32_t y = video.ygctr;
    uint32_t charRow = y / 8;        // Which text row (0-24)
    uint32_t charLine = y % 8;       // Which line within character (0-7)
    uint32_t rowOffset = charRow * 64;

    for (uint32_t i = n; i > 0; i -= 4, video.t_next += 4, video.xctr++)
    {
        uint32_t x = video.xctr;
        if (x >= 40) continue;

        uint32_t offset = (rowOffset + x) & 0x1FFF;

        // Read characters and attributes from both planes
        uint8_t char0 = chars0[offset];
        uint8_t char1 = chars1[offset];
        uint8_t attr0 = attrs0[offset];
        uint8_t attr1 = attrs1[offset];

        // Get font data for this character line
        uint8_t fontBits0 = font[char0 * 8 + charLine];
        uint8_t fontBits1 = font[char1 * 8 + charLine];

        // Decode attributes
        uint8_t bright0 = (attr0 & 0x40) ? 1 : 0;
        uint8_t paper0 = (attr0 >> 3) & 0x07;
        uint8_t ink0 = attr0 & 0x07;
        uint32_t color_paper0 = palette[bright0][paper0];
        uint32_t color_ink0 = palette[bright0][ink0];

        uint8_t bright1 = (attr1 & 0x40) ? 1 : 0;
        uint8_t paper1 = (attr1 >> 3) & 0x07;
        uint8_t ink1 = attr1 & 0x07;
        uint32_t color_paper1 = palette[bright1][paper1];
        uint32_t color_ink1 = palette[bright1][ink1];

        // Render 8 pixels from char0, then 8 from char1 (16 total, no doubling)
        vbuf[video.buf][vptr++] = (fontBits0 & 0x80) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (fontBits0 & 0x40) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (fontBits0 & 0x20) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (fontBits0 & 0x10) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (fontBits0 & 0x08) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (fontBits0 & 0x04) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (fontBits0 & 0x02) ? color_ink0 : color_paper0;
        vbuf[video.buf][vptr++] = (fontBits0 & 0x01) ? color_ink0 : color_paper0;

        vbuf[video.buf][vptr++] = (fontBits1 & 0x80) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (fontBits1 & 0x40) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (fontBits1 & 0x20) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (fontBits1 & 0x10) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (fontBits1 & 0x08) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (fontBits1 & 0x04) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (fontBits1 & 0x02) ? color_ink1 : color_paper1;
        vbuf[video.buf][vptr++] = (fontBits1 & 0x01) ? color_ink1 : color_paper1;
    }

    video.vptr = vptr;
}

void Screen::DrawATM3Text(uint32_t n)
{
    // ATM3 Text Linear mode (FF77 mode 7) - undocumented Sinclair-style text
    // Similar to ATM2 text but with linear memory layout
    // Uses simple 32-column layout like ZX-Spectrum

    EmulatorState& state = _context->emulatorState;
    VideoControl& video = _vid;

    // Standard ZX palette
    static const uint32_t palette[2][8] = {{
        0x00000000, 0x000022C7, 0x00D62816, 0x00D433C7,
        0x0000C525, 0x0000C7C9, 0x00CCC82A, 0x00CACACA
    }, {
        0x00000000, 0x00002BFB, 0x00FF331C, 0x00FF40FC,
        0x0000F92F, 0x0000FBFE, 0x00FFFC36, 0x00FFFFFF
    }};

    // Get video page
    uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;

    // Linear text mode uses a simpler layout
    uint8_t* charMem = _memory->RAMPageAddress(videoPage) + 0x1840;
    uint8_t* font = _memory->ROMPageHostAddress(3) + 0x1D00;

    uint32_t vptr = video.vptr;

    // 32 columns, similar to ZX
    uint32_t y = video.ygctr;
    uint32_t charRow = y / 8;
    uint32_t charLine = y % 8;

    for (uint32_t i = n; i > 0; i -= 4, video.t_next += 4, video.xctr++)
    {
        uint32_t x = video.xctr;
        if (x >= 32) continue;

        // Linear layout: characters in sequence
        uint32_t offset = ((charRow * 32 + x) + 1) & 0x1F;
        uint8_t chr = charMem[offset];

        // Get font bits
        uint8_t fontBits = font[chr * 8 + charLine];

        // Simple white on black for linear text mode
        uint32_t color_paper = palette[0][0];  // Black
        uint32_t color_ink = palette[1][7];    // Bright white

        // Render 8 pixels doubled to 16
        vbuf[video.buf][vptr++] = (fontBits & 0x80) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x80) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x40) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x40) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x20) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x20) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x10) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x10) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x08) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x08) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x04) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x04) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x02) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x02) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x01) ? color_ink : color_paper;
        vbuf[video.buf][vptr++] = (fontBits & 0x01) ? color_ink : color_paper;
    }

    video.vptr = vptr;
}

void Screen::DrawProfi(uint32_t n)
{
    (void)n;
}

void Screen::DrawGMX(uint32_t n)
{
    (void)n;
}

void Screen::DrawBorder(uint32_t n)
{
    [[maybe_unused]] EmulatorState& state = _context->emulatorState;
    [[maybe_unused]] const CONFIG& config = _context->config;
    [[maybe_unused]] VideoControl& video = _context->pScreen->_vid;

    video.t_next += n;
    uint32_t vptr = video.vptr;

    for (; n > 0; n--)
    {
        uint32_t p = video.clut[state.ts.border];
        vbuf[video.buf][vptr] = vbuf[video.buf][vptr + 1] = vbuf[video.buf][vptr + 2] = vbuf[video.buf][vptr + 3] = p;
        vptr += 4;
    }

    video.vptr = vptr;
}

/// region <Helper methods

std::string Screen::GetVideoVideoModeName(VideoModeEnum mode)
{
    static const char* const videoModeName[] = {
        "Null",                 // M_NUL
        "ZX-Spectrum 48k",      // M_ZX48
        "ZX-Spectrum 128k",     // M_ZX128
        "Pentagon 128k",        // M_PENTAGON128K
        "Pentagon multicolor",  // M_PMC
        "Pentagon 16c",         // M_P16
        "Pentagon 384x384",     // M_P384
        "Pentagon HiRes",       // M_PHR
        "Timex ULA+",           // M_TIMEX
        "TSConf 16c",           // M_TS16
        "TSConf 256c",          // M_TS256
        "TSConf Text",          // M_TSTX
        "ATM 16c",              // M_ATM16
        "ATM HiRes",            // M_ATMHR
        "ATM Text",             // M_ATMTX
        "ATM Text Linear",      // M_ATMTL
        "Profi",                // M_PROFI
        "GMX",                  // M_GMX
        "Border only",          // M_BRD
    };
    static_assert(std::size(videoModeName) == M_MAX, "videoModeName array size mismatch with VideoModeEnum");

    if (mode < M_MAX)
        return videoModeName[mode];

    return "";
}

std::string Screen::GetRenderTypeName(RenderTypeEnum type)
{
    // Using switch instead of C99-style designated array initializers (not supported by MSVC)
    switch (type)
    {
        case RT_BLANK:
            return "BLANK";
        case RT_BORDER:
            return "BORDER";
        case RT_SCREEN:
            return "SCREEN";
        default:
            return "";
    }
}

/// endregion </Helper methods

/// region <Debug methods>

#ifdef _DEBUG

#include <common/stringhelper.h>

#include <cstdio>

std::string Screen::DumpFramebufferInfo()
{
    std::string videoModeName = GetVideoModeName(_framebuffer.videoMode);
    std::string result =
        StringHelper::Format("VideoMode: %s; Width: %dpx; Height: %dpx; Buffer: %d bytes", videoModeName.c_str(),
                             _framebuffer.width, _framebuffer.height, _framebuffer.memoryBufferSize);

    return result;
}

void Screen::DumpFramebufferInfo(char* buffer, size_t len)
{
    std::string value = DumpFramebufferInfo();
    snprintf(buffer, len, "%s", value.c_str());
}

std::string Screen::DumpRasterState()
{
    const RasterState& state = _rasterState;

    std::string result = StringHelper::Format("RasterState: ");
    result += StringHelper::Format("VideoMode: %s; Frame: %d; Lines: %d; PerLine: %d",
                                   Screen::GetVideoVideoModeName(_mode).c_str(), state.maxFrameTiming,
                                   state.maxFrameTiming / state.tstatesPerLine, state.tstatesPerLine);

    return result;
}

void Screen::DumpRasterState(char* buffer, size_t len)
{
    std::string value = DumpFramebufferInfo();
    snprintf(buffer, len, "%s", value.c_str());
}

#endif  // _DEBUG

/// endregion </Debug methods>