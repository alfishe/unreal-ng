#pragma once
#include "stdafx.h"

#include "common/logger.h"

#include "emulator/video/screen.h"

class ScreenZX : public Screen
{
    /// region <Fields>
protected:
    uint16_t _screenLineOffsets[256];           // Address for each screen line start (relative to screen base offset)
    uint16_t _attrLineOffsets[256];             // Address for each attribute offset (relative to screen base offset)

    uint32_t _rgbaColors[256];                  // Colors when no Flash or Flash is in blinking=OFF state
    uint32_t _rgbaFlashColors[256];             // Colors when Flash is in blinking=ON state

    RenderTypeEnum _screenLineRenderers[288];   // Cached render types for each line in screen area (HBlank, HSync, Left Border, Screen, Right Border)

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    ScreenZX() = delete;		            // Disable default constructor; C++ 11 feature
    ScreenZX(EmulatorContext* context);
    virtual ~ScreenZX();
    /// endregion </Constructors / Destructors>

protected:
    void CreateTables() override;
    void CreateTimingTable();

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
