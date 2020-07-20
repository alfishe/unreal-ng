#include "stdafx.h"

#include "common/logger.h"

#include "portdecoder_profi.h"

/// region <Constructors / Destructors>

PortDecoder_Profi::PortDecoder_Profi(EmulatorContext* context) : PortDecoder(context)
{
    _7FFD_Locked = false;
}

PortDecoder_Profi::~PortDecoder_Profi()
{
    LOGDEBUG("PortDecoder_Profi::~PortDecoder_Profi()");
}
/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Profi::Reset()
{
    // Reset memory paging lock latch
    _7FFD_Locked = false;
}

uint8_t PortDecoder_Profi::DecodePortIn(uint16_t addr)
{
    uint8_t result = 0xFF;

    return result;
}

void PortDecoder_Profi::DecodePortOut(uint16_t addr, uint8_t value)
{
    //    Profi 1024
    //    port: #7FFD
    //    port: #DFFD

    bool isPort_7FFD = IsPort_7FFD(addr);
    if (isPort_7FFD)
    {
        Port_7FFD(value);
    }

    bool isPort_DFFD = IsPort_DFFD(addr);
    if (isPort_DFFD)
    {
        Port_DFFD(value);
    }
}

void PortDecoder_Profi::SetRAMPage(uint8_t page)
{

}

void PortDecoder_Profi::SetROMPage(uint8_t page)
{

}

/// endregion </Interface methods>

/// region <Helper methods>
bool PortDecoder_Profi::IsPort_7FFD(uint16_t addr)
{
    //    Profi
    //    port: #7FFD
    //    Full match  :  01111111 11111101
    //    Match pattern: 01x1xxxx xx1xx101
    //    Equation: /IORQ /WR /A15 /A1
    static const uint16_t port_7FFD_full        = 0b0111'1111'1111'1101;
    static const uint16_t port_7FFD_mask        = 0b1000'0000'0000'0010;
    static const uint16_t port_7FFD_match       = 0b0000'0000'0000'0000;

    bool result = (addr & port_7FFD_mask) == port_7FFD_match;

    return result;
}

bool PortDecoder_Profi::IsPort_DFFD(uint16_t addr)
{
    //    Profi
    //    port: #DFFD
    //    Full match:    00011111 11111101
    //    Match pattern: xx0xxxxx xxxxxx0x
    //    Equation: /IORQ /WR /A13 /A1
    static const uint16_t port_DFFD_full        = 0b0001'1111'1111'1101;
    static const uint16_t port_DFFD_mask        = 0b0010'0000'0000'0010;
    static const uint16_t port_DFFD_match       = 0b0000'0000'0000'0000;

    bool result = (addr & port_DFFD_mask) == port_DFFD_match;

    return result;
}
/// endregion <Helper methods>

/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Profi::Port_7FFD(uint8_t value)
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

/// Port #DFFD (Memory) handler
/// \param value
void PortDecoder_Profi::Port_DFFD(uint8_t value)
{

}
