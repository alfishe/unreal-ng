#include "stdafx.h"

#include "common/logger.h"

#include "portdecoder_scorpion256.h"

/// region <Constructors / Destructors>

PortDecoder_Scorpion256::PortDecoder_Scorpion256(EmulatorContext* context) : PortDecoder(context)
{
    _7FFD_Locked = false;
}

PortDecoder_Scorpion256::~PortDecoder_Scorpion256()
{
    LOGDEBUG("PortDecoder_Scorpion256::~PortDecoder_Scorpion256()");
}
/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Scorpion256::Reset()
{
    // Reset memory paging lock latch
    _7FFD_Locked = false;
}

uint8_t PortDecoder_Scorpion256::DecodePortIn(uint16_t port, uint16_t pc)
{
    uint8_t result = 0xFF;

    return result;
}

void PortDecoder_Scorpion256::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    //    ZX Spectrum 128 +2A/+2B/+3
    //    port: #7FFD
    //    port: #1FFD

    bool isPort_7FFD = IsPort_7FFD(port);
    if (isPort_7FFD)
    {
        Port_7FFD(value, pc);
    }

    bool isPort_1FFD = IsPort_1FFD(port);
    if (isPort_1FFD)
    {
        Port_1FFD(value, pc);
    }
}

void PortDecoder_Scorpion256::SetRAMPage(uint8_t page)
{

}

void PortDecoder_Scorpion256::SetROMPage(uint8_t page)
{

}

/// endregion </Interface methods>

/// region <Helper methods>
bool PortDecoder_Scorpion256::IsPort_7FFD(uint16_t port)
{
    //    Scorpion ZS256
    //    port: #7FFD
    //    Full match  :  01111111 11111101
    //    Match pattern: 01x1xxxx xx1xx101
    //    Equation: /IORQ /WR M1 /A15 A14 A12 A5 A2 /A1 A0
    static const uint16_t port_7FFD_full    = 0b0111'1111'1111'1101;
    static const uint16_t port_7FFD_mask    = 0b1101'0000'0010'0111;
    static const uint16_t port_7FFD_match   = 0b0101'0000'0010'0101;

    bool result = (port & port_7FFD_mask) == port_7FFD_match;

    return result;
}

bool PortDecoder_Scorpion256::IsPort_1FFD(uint16_t port)
{
    //    Scorpion ZS256
    //    port: #1FFD
    //    Full match:    00011111 11111101
    //    Match pattern: 00x1xxxx xx1xx101
    //    /IORQ /WR M1 /A15 /A14 A12 A5 A2 /A1 A0
    static const uint16_t port_1FFD_full    = 0b0001'1111'1111'1101;
    static const uint16_t port_1FFD_mask    = 0b1101'0000'0010'0111;
    static const uint16_t port_1FFD_match   = 0b0001'0000'0010'0101;

    bool result = (port & port_1FFD_mask) == port_1FFD_match;

    return result;
}
/// endregion <Helper methods>

/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Scorpion256::Port_7FFD(uint8_t value, uint16_t pc)
{
    //  Port: #7FFD
    //  Bits:
    //      D0 = RAM - bit0 ;128 kB memory
    //      D1 = RAM - bit1 ;128 kB memory
    //      D2 = RAM - bit2 ;128 kB memory
    //      D3 = Screen (Normal (Bank5) | Shadow (Bank 7))
    //      D4 = ROM (ROM0 = 128k ROM | ROM1 = 48k ROM)
    //      D5 = Disable memory paging (both ROM and RAM) until reset
    //      D6 = unused
    //      D7 = unused

    static Memory& memory = *_context->pMemory;
    static Screen& screen = *_context->pScreen;

    uint8_t bankRAM = value & 0b00000111;
    uint8_t screenNumber = (value & 0b00001000) >> 3;  // 0 = Normal (Bank 5), 1 = Shadow (Bank 7)
    bool isROM0 = value & 0b00010000;
    bool isPagingDisabled = value & 0b00100000;

    // Disabling latch is kept until reset
    if (!_7FFD_Locked)
    {
        memory.SetRAMPageToBank3(bankRAM);
        memory.SetROMMode(isROM0 ? RM_128 : RM_SOS);

        _7FFD_Locked = isPagingDisabled;
    }

    screen.SetActiveScreen(screenNumber);

    LOGDEBUG(memory.DumpMemoryBankInfo());
}

/// Port #1FFD (Memory) handler
/// \param value
void PortDecoder_Scorpion256::Port_1FFD(uint8_t value, uint16_t pc)
{

}