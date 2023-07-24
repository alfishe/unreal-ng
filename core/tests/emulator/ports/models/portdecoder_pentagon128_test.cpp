#include "stdafx.h"
#include "pch.h"

#include "portdecoder_pentagon128_test.h"
#include <common/stringhelper.h>

/// region <SetUp / TearDown>

void PortDecoder_Pentagon128_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);
    _portDecoder = new PortDecoder_Pentagon128(_context);
}

void PortDecoder_Pentagon128_Test::TearDown()
{
    if (_portDecoder != nullptr)
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(PortDecoder_Pentagon128_Test, IsPort_7FFD)
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

TEST_F(PortDecoder_Pentagon128_Test, DecodePort_FF)
{
    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;

        if ((i & 0x00FF) == 0x00FF)
        {
            uint16_t result = _portDecoder->decodePort(i);

            EXPECT_EQ(result, 0x00FF) << StringHelper::Format("Expected 0x00FF, found 0x%04X for i: %d", result, i);
        }
    }
}