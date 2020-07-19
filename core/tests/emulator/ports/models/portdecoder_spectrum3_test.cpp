#include "stdafx.h"
#include "pch.h"

#include "portdecoder_spectrum3_test.h"

/// region <SetUp / TearDown>

void PortDecoder_Spectrum3_Test::SetUp()
{
    _portDecoder = new PortDecoder_Spectrum3(_context);
}

void PortDecoder_Spectrum3_Test::TearDown()
{
    if (_portDecoder != nullptr)
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(PortDecoder_Spectrum3_Test, IsPort_7FFD)
{
    // Port: #7FFD
    // Sensitivity: 01xxxxxx xxxxxx0x
    // Equation: /IORQ /WR /A15 A14 /A1
    static const uint16_t bit15_inv = 0b1000000000000000;
    static const uint16_t bit14     = 0b0100000000000000;
    static const uint16_t bit1_inv  = 0b0000000000000010;
    static char message[256];

    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;
        bool referenceIs_7FFD = ((~port & bit15_inv) && (port & bit14) && (~port & bit1_inv));
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

TEST_F(PortDecoder_Spectrum3_Test, IsPort_1FFD)
{
    // Port: #1FFD
    // Sensitivity: 0001xxxx xxxxxx0x
    // Equation: /IORQ /WR /A15 /A14 /A13 A12 /A1
    static const uint16_t bit15_inv = 0b1000000000000000;
    static const uint16_t bit14_inv = 0b0100000000000000;
    static const uint16_t bit13_inv = 0b0010000000000000;
    static const uint16_t bit12     = 0b0001000000000000;
    static const uint16_t bit1_inv  = 0b0000000000000010;
    static char message[256];

    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;
        bool referenceIs_1FFD = (~port & bit15_inv) && (~port & bit14_inv) && (~port & bit13_inv) && (port & bit12) &&
                                (~port & bit1_inv);
        bool is_1FFD = _portDecoder->IsPort_1FFD(port);

        if (referenceIs_1FFD != is_1FFD)
        {
            snprintf(message, sizeof message, "port: #%04X. Expected: %d, returned: %d", port, referenceIs_1FFD, is_1FFD);
            FAIL() << message << std::endl;
        }

#ifdef _DEBUG
        if (is_1FFD)
        {
            snprintf(message, sizeof message, "port: #%04X. Expected: %d, returned: %d", port, referenceIs_1FFD, is_1FFD);
            std::cout << message << std::endl;
        }
#endif // _DEBUG
    }
}