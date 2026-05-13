#pragma once
#include "common/modulelogger.h"
#include "emulator/video/screen.h"
#include "stdafx.h"

/// ZX Spectrum Screen Layout (Per Frame)
// +--------------------------+-------+-------------------------------------------------------+
// | Region                   | Lines | Description                                           |
// +--------------------------+-------+-------------------------------------------------------+
// | Top Border               |   64  | Blank area above the screen (varies slightly per TV)  |
// +--------------------------+-------+-------------------------------------------------------+
// | Screen Area              |  192  | Actual pixel/attribute data (24 rows × 8 pixels each) |
// +--------------------------+-------+-------------------------------------------------------+
// | Bottom Border            |   32  | Blank area below the screen.                          |
// +--------------------------+-------+-------------------------------------------------------+
// | Total Visible            |  288  | What most TVs display (including borders).            |
// +--------------------------+-------+-------------------------------------------------------+
// | Vertical Sync & Blanking | ~24.5 | Hidden by TV overscan (not visible).                  |
// +--------------------------+-------+-------------------------------------------------------+
// | Full Frame               | 312.5 | Total PAL lines (non-interlaced, 50Hz).               |
// +--------------------------+-------+-------------------------------------------------------+
class ScreenZX : public Screen
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_VIDEO;
    const uint16_t _SUBMODULE = PlatformVideoSubmodulesEnum::SUBMODULE_VIDEO_ULA;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Constants>
public:
    const uint16_t _SCREEN5_BASE_ADDRESS = 0x4000;
    const uint16_t _SCREEN7_BASE_ADDRESS = 0xC000;
    const uint16_t _SCREEN_ATTRIBUTES_OFFSET = 0x1800;

    const uint16_t _SCREEN_VISIBLE_WIDTH_PX = 256;
    const uint16_t _SCREEN_VISIBLE_HEIGHT_PX = 192;

    const uint16_t _SCREEN_48K_TSTATES_PER_LINE = 224;
    const uint16_t _SCREEN_128K_TSTATES_PER_LINE = 228;

    // Maximum frame t-states for LUT allocation (69888 for ZX48k, 70908 for ZX128k)
    static constexpr uint32_t MAX_FRAME_TSTATES = 71680;  // Round up to cover all models
    /// endregion </Constants>

    /// region <LUT Types>
    /// Pre-computed coordinate lookup for each t-state
    /// Eliminates runtime division/modulo operations in hot path
    struct TstateCoordLUT
    {
        uint16_t framebufferX;      // Framebuffer X coordinate (UINT16_MAX if invisible)
        uint16_t framebufferY;      // Framebuffer Y coordinate
        uint16_t zxX;               // ZX screen X (UINT16_MAX if border/invisible)
        uint8_t zxY;                // ZX screen Y (255 if border/invisible)
        uint8_t symbolX;            // Pre-computed x / 8
        uint8_t pixelXBit;          // Pre-computed x % 8
        RenderTypeEnum renderType;  // RT_BLANK, RT_BORDER, or RT_SCREEN
        uint16_t screenOffset;      // Pre-computed _screenLineOffsets[y]
        uint16_t attrOffset;        // Pre-computed _attrLineOffsets[y]
    };
    /// endregion </LUT Types>

    /// region <Fields>
protected:
    uint16_t _screenLineOffsets[256];  // Address for each screen line start (relative to screen base offset)
    uint16_t _attrLineOffsets[256];    // Address for each attribute offset (relative to screen base offset)

    uint32_t _rgbaColors[256];       // Colors when no Flash or Flash is in blinking=OFF state
    uint32_t _rgbaFlashColors[256];  // Colors when Flash is in blinking=ON state

    RenderTypeEnum _screenLineRenderers[288];  // Cached render types for each line in the screen area (HBlank, HSync,
                                               // Left Border, Screen, Right Border)

    // T-state coordinate LUT - pre-computed for current video mode
    // Regenerated on mode change in CreateTimingTable()
    TstateCoordLUT _tstateLUT[MAX_FRAME_TSTATES];

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    ScreenZX() = delete;  // Disable default constructor; C++ 11 feature
    ScreenZX(EmulatorContext* context);
    virtual ~ScreenZX() = default;
    /// endregion </Constructors / Destructors>

protected:
    void CreateTables() override;
    void CreateTimingTable();
    void CreateTstateLUT();  // Pre-compute coordinate LUT for current mode

public:
    uint16_t CalculateXYScreenAddress(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);
    uint16_t CalculateXYScreenAddressOptimized(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);

    uint16_t CalculateXYColorAttrAddress(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);
    uint16_t CalculateXYColorAttrAddressOptimized(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);

    uint32_t TransformZXSpectrumColorsToRGBA(uint8_t attribute, bool isPixelSet);

    uint32_t GetZXSpectrumPixel(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);
    uint32_t GetZXSpectrumPixelOptimized(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);

    bool TransformTstateToFramebufferCoords(uint32_t tstate, uint16_t* x, uint16_t* y);
    bool TransformTstateToZXCoords(uint32_t tstate, uint16_t* zxX, uint16_t* zxY);
    uint32_t GetPixelOrBorderColorForTState(uint32_t tstate);
    bool IsOnScreenByTiming(uint32_t tstate);

    RenderTypeEnum GetLineRenderTypeByTiming(uint32_t tstate);
    RenderTypeEnum GetRenderType(uint16_t line, uint16_t col);

    // Screen class methods override
public:
    /// @brief Set video mode and regenerate timing LUTs
    /// Regenerates TstateCoordLUT after base class updates raster state
    void SetVideoMode(VideoModeEnum mode) override;

    void UpdateScreen() override;

    /// @brief Optimized Draw using pre-computed LUT
    /// Uses TstateCoordLUT to eliminate runtime division/modulo operations
    /// @param tstate T-state timing position
    void Draw(uint32_t tstate) override;

    /// @brief Original Draw implementation with runtime coordinate calculation
    /// @deprecated Kept for benchmarking comparison; will be removed after verification
    /// Uses TransformTstateToFramebufferCoords/TransformTstateToZXCoords with division/modulo
    /// @param tstate T-state timing position
    void DrawOriginal(uint32_t tstate);

    /// @brief LUT-based Draw with ternary color selection (Phase 2 baseline for Phase 3 comparison)
    /// @deprecated CLEANUP: Remove after Phase 3 verification complete
    /// @param tstate T-state timing position
    void DrawLUT_Ternary(uint32_t tstate);

    /// region <ScreenHQ=OFF optimizations - Phase 4-5>
    /// These methods batch 8 pixels together, breaking demo multicolor compatibility
    /// but providing significant performance gains for non-demo usage

    /// @brief Render 8 pixels at once (scalar version) - ScreenHQ=OFF only
    /// @warning Breaks demo multicolor effects that modify attributes mid-scanline
    /// @param zxY ZX screen Y coordinate (0-191)
    /// @param symbolX Character column (0-31)
    /// @param destPtr Destination framebuffer pointer (must have 8 uint32_t space)
    void DrawBatch8_Scalar(uint8_t zxY, uint8_t symbolX, uint32_t* destPtr);

#ifdef __ARM_NEON
    /// @brief Render 8 pixels at once using ARM NEON SIMD - ScreenHQ=OFF only
    /// @warning Breaks demo multicolor effects that modify attributes mid-scanline
    /// @param zxY ZX screen Y coordinate (0-191)
    /// @param symbolX Character column (0-31)
    /// @param destPtr Destination framebuffer pointer (must have 8 uint32_t space, 16-byte aligned preferred)
    void DrawBatch8_NEON(uint8_t zxY, uint8_t symbolX, uint32_t* destPtr);
#endif

    /// @brief Render entire screen using batch 8-pixel method - ScreenHQ=OFF only
    /// @warning Breaks demo multicolor effects - only use when ScreenHQ feature is disabled
    void RenderScreen_Batch8();

    /// endregion </ScreenHQ=OFF optimizations>

    void RenderOnlyMainScreen() override;

    /// @brief Render entire screen using optimized batch method at frame end
    /// Called by MainLoop::OnFrameEnd() when ScreenHQ=OFF.
    /// Uses RenderScreen_Batch8() for 25x faster rendering than per-t-state.
    void RenderFrameBatch() override;

    /// region <Snapshot helpers>
public:
    virtual void FillBorderWithColor(uint8_t color) override;

    /// @brief Optimized FillBorderWithColor using row-based std::fill_n
    /// Much faster than pixel-by-pixel loops - fills entire rows at once
    void FillBorderWithColor_Optimized(uint8_t color);

    /// @brief Original FillBorderWithColor implementation for benchmarking
    /// @deprecated Kept for performance comparison only
    void FillBorderWithColor_Original(uint8_t color);

    /// @brief Optimized RenderOnlyMainScreen using batch 8-pixel method
    /// Reuses RenderScreen_Batch8() for SIMD-accelerated rendering
    void RenderOnlyMainScreen_Optimized();

    /// @brief Original RenderOnlyMainScreen implementation for benchmarking
    /// @deprecated Kept for performance comparison only
    void RenderOnlyMainScreen_Original();
    /// endregion </Snapshot helpers>

    /// region <Debug info>
public:
    std::string DumpRenderForTState(uint32_t tstate);
    /// endregion </Debug info>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing /
// benchmark purposes
//
#if defined(_CODE_UNDER_TEST) || defined(_CODE_UNDER_BENCHMARK)

class ScreenZXCUT : public ScreenZX
{
public:
    ScreenZXCUT(EmulatorContext* context) : ScreenZX(context) {};

public:
    using ScreenZX::_framebuffer;
    using ScreenZX::_mode;
    using ScreenZX::_rasterState;
    using ScreenZX::_screenLineRenderers;

public:
    using ScreenZX::CreateTables;
    using ScreenZX::CreateTimingTable;

    using ScreenZX::CalculateXYScreenAddress;
    using ScreenZX::CalculateXYScreenAddressOptimized;

    using ScreenZX::CalculateXYColorAttrAddress;
    using ScreenZX::CalculateXYColorAttrAddressOptimized;

    using ScreenZX::TransformZXSpectrumColorsToRGBA;

    using ScreenZX::GetZXSpectrumPixel;
    using ScreenZX::GetZXSpectrumPixelOptimized;

    using ScreenZX::GetLineRenderTypeByTiming;
    using ScreenZX::IsOnScreenByTiming;
    using ScreenZX::TransformTstateToFramebufferCoords;
    using ScreenZX::TransformTstateToZXCoords;

    // Draw methods for benchmarking comparison
    using ScreenZX::Draw;             // LUT + branch-free (Phase 3)
    using ScreenZX::DrawLUT_Ternary;  // LUT + ternary (Phase 2 baseline)
    using ScreenZX::DrawOriginal;     // Original with runtime calculation

    // Batch 8-pixel methods (ScreenHQ=OFF, Phase 4-5)
    using ScreenZX::DrawBatch8_Scalar;
#ifdef __ARM_NEON
    using ScreenZX::DrawBatch8_NEON;
#endif
    using ScreenZX::RenderOnlyMainScreen;
    using ScreenZX::RenderScreen_Batch8;

    // LUT access for testing
    using ScreenZX::_attrLineOffsets;
    using ScreenZX::_screenLineOffsets;
    using ScreenZX::_tstateLUT;
    using ScreenZX::CreateTstateLUT;

    // Snapshot helpers - optimized vs original for benchmarking
    using ScreenZX::FillBorderWithColor;
    using ScreenZX::FillBorderWithColor_Optimized;
    using ScreenZX::FillBorderWithColor_Original;
    using ScreenZX::RenderOnlyMainScreen_Optimized;
    using ScreenZX::RenderOnlyMainScreen_Original;
};
#endif  // _CODE_UNDER_TEST || _CODE_UNDER_BENCHMARK
