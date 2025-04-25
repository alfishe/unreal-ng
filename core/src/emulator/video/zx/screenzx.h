#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"

#include "emulator/video/screen.h"

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
    /// endregion </Constants>

    /// region <Fields>
protected:
    uint16_t _screenLineOffsets[256];           // Address for each screen line start (relative to screen base offset)
    uint16_t _attrLineOffsets[256];             // Address for each attribute offset (relative to screen base offset)

    uint32_t _rgbaColors[256];                  // Colors when no Flash or Flash is in blinking=OFF state
    uint32_t _rgbaFlashColors[256];             // Colors when Flash is in blinking=ON state

    RenderTypeEnum _screenLineRenderers[288];   // Cached render types for each line in the screen area (HBlank, HSync, Left Border, Screen, Right Border)

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    ScreenZX() = delete;		            // Disable default constructor; C++ 11 feature
    ScreenZX(EmulatorContext* context);
    virtual ~ScreenZX() = default;
    /// endregion </Constructors / Destructors>

protected:
    void CreateTables() override;
    void CreateTimingTable();

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
    void UpdateScreen() override;
    void Draw(uint32_t tstate) override;

    void RenderOnlyMainScreen() override;

    /// region <Snapshot helpers>
public:
    virtual void FillBorderWithColor(uint8_t color) override;
    /// endregion </Snapshot helpers>

    /// region <Debug info>
public:
    std::string DumpRenderForTState(uint32_t tstate);
    /// endregion </Debug info>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class ScreenZXCUT : public ScreenZX
{
public:
    ScreenZXCUT(EmulatorContext* context) : ScreenZX(context) {};

public:
    using ScreenZX::_mode;
    using ScreenZX::_rasterState;
    using ScreenZX::_screenLineRenderers;
    using ScreenZX::_framebuffer;

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

    using ScreenZX::TransformTstateToFramebufferCoords;
    using ScreenZX::TransformTstateToZXCoords;
    using ScreenZX::GetLineRenderTypeByTiming;
    using ScreenZX::IsOnScreenByTiming;
};
#endif // _CODE_UNDER_TEST
