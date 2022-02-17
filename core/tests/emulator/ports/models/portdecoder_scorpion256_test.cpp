#include "stdafx.h"
#include "pch.h"

#include "portdecoder_scorpion256_test.h"

/// region <SetUp / TearDown>

void PortDecoder_Scorpion256_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);
    _portDecoder = new PortDecoder_Scorpion256(_context);
}

void PortDecoder_Scorpion256_Test::TearDown()
{
    if (_portDecoder != nullptr)
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(PortDecoder_Scorpion256_Test, IsPort_7FFD)
{
    // Port: #7FFD
    // Sensitivity: 01x1xxxx xx1xx101
    // Equation: /IORQ /WR M1 /A15 A14 A12 A5 A2 /A1 A0
    static const uint16_t bit15_inv = 0b1000'0000'0000'0000;
    static const uint16_t bit14     = 0b0100'0000'0000'0000;
    static const uint16_t bit12     = 0b0001'0000'0000'0000;
    static const uint16_t bit5      = 0b0000'0000'0010'0000;
    static const uint16_t bit2      = 0b0000'0000'0000'0100;
    static const uint16_t bit1_inv  = 0b0000'0000'0000'0010;
    static const uint16_t bit0      = 0b0000'0000'0000'0001;
    static char message[256];

    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;
        bool referenceIs_7FFD = (~port & bit15_inv) && (port & bit14) && (port & bit12) && (port & bit5) && (port & bit2) && (~port & bit1_inv) && (port & bit0);
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

TEST_F(PortDecoder_Scorpion256_Test, IsPort_1FFD)
{
    // Port: #1FFD
    // Sensitivity: 00x1xxxx xx1xx101
    // Equation: /WR M1 /A15 /A14 A12 A5 A2 /A1 A0
    static const uint16_t bit15_inv = 0b1000'0000'0000'0000;
    static const uint16_t bit14_inv = 0b0100'0000'0000'0000;
    static const uint16_t bit12     = 0b0001'0000'0000'0000;
    static const uint16_t bit5      = 0b0000'0000'0010'0000;
    static const uint16_t bit2      = 0b0000'0000'0000'0100;
    static const uint16_t bit1_inv  = 0b0000'0000'0000'0010;
    static const uint16_t bit0      = 0b0000'0000'0000'0001;
    static char message[256];

    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;
        bool referenceIs_1FFD = (~port & bit15_inv) && (~port & bit14_inv) && (port & bit12) && (port & bit5) && (port & bit2) && (~port & bit1_inv) && (port & bit0);
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