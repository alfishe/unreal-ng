#include "pch.h"

#include "screenzx_test.h"

using namespace std;

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
            snprintf(message, sizeof message, "x: %d, y: %d, addr: 0x%000x\n\0", x, y, addr);
            std::cout << message;
#endif // _DEBUG
        }
    }
}