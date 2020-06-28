#pragma once
#include "stdafx.h"

#include "common/logger.h"

#include "emulator/video/screen.h"

class ScreenZX : public Screen
{
public:
    ScreenZX() = delete;		            // Disable default constructor; C++ 11 feature
    ScreenZX(EmulatorContext* context);
    virtual ~ScreenZX();

protected:
    void CreateTables();
    uint16_t CalculateXYScreenAddress(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);
    uint16_t CalculateXYColorAttrAddress(uint8_t x, uint8_t y, uint16_t baseAddress = 0x4000);
    uint32_t TransformZXSpectrumColorsToRGBA(uint8_t color, bool isPixelSet);

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
    using ScreenZX::CalculateXYColorAttrAddress;
    using ScreenZX::TransformZXSpectrumColorsToRGBA;

};
#endif // _CODE_UNDER_TEST
