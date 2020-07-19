#include "stdafx.h"
#include "pch.h"

#include "portdecoder_profi_test.h"

/// region <SetUp / TearDown>

void PortDecoder_Profi_Test::SetUp()
{
    _portDecoder = new PortDecoder_Profi(_context);
}

void PortDecoder_Profi_Test::TearDown()
{
    if (_portDecoder != nullptr)
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(PortDecoder_Profi_Test, IsPort_7FFD)
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
        bool referenceIs_7FFD = (~port & bit15_inv) && (~port & bit1_inv);
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

TEST_F(PortDecoder_Profi_Test, IsPort_DFFD)
{
    // Port: #DFFD
    // Sensitivity: xx0xxxxx xxxxxx0x
    // Equation: /IORQ /WR /A13 /A1
    static const uint16_t bit13_inv = 0b0010'0000'0000'0000;
    static const uint16_t bit1_inv  = 0b0000'0000'0000'0010;
    static char message[256];

    for (int i = 0; i <= 0xFFFF; i++)
    {
        uint16_t port = i & 0xFFFF;
        bool referenceIs_DFFD = (~port & bit13_inv) && (~port & bit1_inv);
        bool is_DFFD = _portDecoder->IsPort_DFFD(port);

        if (referenceIs_DFFD != is_DFFD)
        {
            snprintf(message, sizeof message, "port: #%04X. Expected: %d, returned: %d", port, referenceIs_DFFD, is_DFFD);
            FAIL() << message << std::endl;
        }

#ifdef _DEBUG
        if (is_DFFD)
        {
            snprintf(message, sizeof message, "port: #%04X. Expected: %d, returned: %d", port, referenceIs_DFFD, is_DFFD);
            std::cout << message << std::endl;
        }
#endif // _DEBUG
    }
}