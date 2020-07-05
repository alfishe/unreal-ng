#include "pch.h"

#include "screenzx_test.h"

using namespace std;

/// region <SetUp / TearDown>

void ScreenZX_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext();
    _cpu = new CPU(_context);
    _screenzx = new ScreenZXCUT(_context);
}

void ScreenZX_Test::TearDown()
{
    if (_screenzx != nullptr)
    {
        delete _screenzx;
        _screenzx = nullptr;
    }

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

/// endregion </Setup / TearDown>

TEST_F(ScreenZX_Test, CalculateXYScreenAddress)
{
    char message[256];
    uint16_t addr = 0;

    for (uint16_t x = 0; x <= 255; x++)
    {
        for (uint8_t y = 0; y < 192; y++)
        {
            addr = _screenzx->CalculateXYScreenAddress(x, y, 0x4000);

#ifdef _DEBUG
            snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            std::cout << message << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(ScreenZX_Test, CalculateXYScreenAddressCorrectness)
{
    char message[256];
    uint16_t addr = 0;
    uint16_t addrOptimized = 0;

    for (uint16_t x = 0; x <= 255; x++)
    {
        for (uint8_t y = 0; y < 192; y++)
        {
            addr = _screenzx->CalculateXYScreenAddress(x, y, 0x4000);
            addrOptimized = _screenzx->CalculateXYScreenAddressOptimized(x, y, 0x4000);

            if (addr != addrOptimized)
            {
                //ASSERT_EQ(addr, addrOptimized);

                snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X, addrOptimized: 0x%04X", x, y, addr, addrOptimized);
                FAIL() << message << std::endl;
            }

#ifdef _DEBUG
            //snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            //std::cout << message << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(ScreenZX_Test, CalculateXYColorAttrAddress)
{
    char message[256];
    uint16_t addr = 0;

    for (uint16_t x = 0; x <= 255; x++)
    {
        for (uint8_t y = 0; y < 192; y++)
        {
            addr = _screenzx->CalculateXYColorAttrAddress(x, y, 0x4000);

#ifdef _DEBUG
            snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            std::cout << message << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(ScreenZX_Test, CalculateXYColorAddressCorrectness)
{
    char message[256];
    uint16_t addr = 0;
    uint16_t addrOptimized = 0;

    for (uint16_t x = 0; x <= 255; x++)
    {
        for (uint8_t y = 0; y < 192; y++)
        {
            addr = _screenzx->CalculateXYColorAttrAddress(x, y, 0x4000);
            addrOptimized = _screenzx->CalculateXYColorAttrAddressOptimized(x, y, 0x4000);

            if (addr != addrOptimized)
            {
                //ASSERT_EQ(addr, addrOptimized);

                snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X, addrOptimized: 0x%04X", x, y, addr, addrOptimized);
                FAIL() << message << std::endl;
            }

#ifdef _DEBUG
            //snprintf(message, sizeof message, "x: %03d, y: %03d, addr: 0x%04X", x, y, addr);
            //std::cout << message << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(ScreenZX_Test, TransformZXSpectrumColorsToRGBA)
{
    { // Black on non-bright white - default screen colors
        uint32_t inkColor = _screenzx->TransformZXSpectrumColorsToRGBA(0x38, true);
        uint32_t paperColor = _screenzx->TransformZXSpectrumColorsToRGBA(0x38, false);

        EXPECT_EQ(inkColor, 0xFF000000);
        EXPECT_EQ(paperColor, 0xFFCACACA);
    }
}