#pragma once
#include "stdafx.h"

#include "common/logger.h"

#include "emulator/video/screen.h"

class ScreenZX : public Screen
{
    /// region <Fields>
protected:
    uint16_t _screenLineOffsets[256];
    uint16_t _attrLineOffsets[256];

    uint32_t _rgbaColors[256];      // Colors when no Flash or Flash is in blinking=OFF state
    uint32_t _rgbaFlashColors[256]; // Colors when Flash is in blinking=ON state

    /// endregion </Fields>

public:
    ScreenZX() = delete;		            // Disable default constructor; C++ 11 feature
    ScreenZX(EmulatorContext* context);
    virtual ~ScreenZX();

protected:
    void CreateTables() override;
    uint16_t CalculateXYScreenAddress(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);
    uint16_t CalculateXYScreenAddressOptimized(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);

    uint16_t CalculateXYColorAttrAddress(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);
    uint16_t CalculateXYColorAttrAddressOptimized(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);

    uint32_t TransformZXSpectrumColorsToRGBA(uint8_t color, bool isPixelSet);

    uint32_t GetZXSpectrumPixel(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);
    uint32_t GetZXSpectrumPixelOptimized(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);

    // Screen class methods override
public:
    void UpdateScreen() override;
    void RenderOnlyMainScreen() override;
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
    using ScreenZX::CalculateXYScreenAddress;
    using ScreenZX::CalculateXYScreenAddressOptimized;

    using ScreenZX::CalculateXYColorAttrAddress;
    using ScreenZX::CalculateXYColorAttrAddressOptimized;

    using ScreenZX::TransformZXSpectrumColorsToRGBA;

    using ScreenZX::GetZXSpectrumPixel;
    using ScreenZX::GetZXSpectrumPixelOptimized;
};
#endif // _CODE_UNDER_TEST
