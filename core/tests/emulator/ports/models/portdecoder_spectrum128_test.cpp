#include "stdafx.h"
#include "pch.h"

#include "portdecoder_spectrum128_test.h"

/// region <SetUp / TearDown>

void PortDecoder_Spectrum128_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);
    _portDecoder = new PortDecoder_Spectrum128(_context);
}

void PortDecoder_Spectrum128_Test::TearDown()
{
    if (_portDecoder != nullptr)
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(PortDecoder_Spectrum128_Test, IsPort_7FFD)
{
    // Port: #7FFD
    // Sensitivity: 0xxxxxxx xxxxxx0x
    // Equation: /IORQ /WR /A15 /A1
    static const uint16_t bit15_inv = 0b1000'0000'0000'0000;
    static const uint16_t bit1_inv  = 0b0000'0000'0000'0010;
    static char message[256];

    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;
        bool referenceIs_7FFD = ((~port & bit15_inv) && (~port & bit1_inv));
        bool is_7FFD = _portDecoder->IsPort_7FFD(port);

        if (referenceIs_7FFD != is_7FFD)
        {
            snprintf(message, sizeof message, "port: #%04X. Expected: %d, returned: %d", port, referenceIs_7FFD, is_7FFD);
            FAIL() << message << std::endl;
        }

#ifdef _DEBUG
        if (is_7FFD)
        {
            snprintf(message, sizeof message, "port: #%04X. Expected: %d, returned: %d", port, referenceIs_7FFD, is_7FFD);
            std::cout << message << std::endl;
        }
#endif // _DEBUG
    }
}
