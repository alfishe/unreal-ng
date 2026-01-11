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

void PortDecoder_Profi::reset()
{
    // Profi 1024 ROM setup
    // Similar to Spectrum 128K
    // Bit 4 of port 0x7FFD: 0 = SOS ROM, 1 = 128K ROM

    // Explicitly reset port states to ensure consistent reset behavior
    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;     // Reset port 0x7FFD to default (Screen 0, RAM bank 0, SOS ROM, paging enabled)
    state.pDFFD = 0x00;     // Reset port 0xDFFD (extended paging)
    state.pBFFD = 0x00;     // Reset AY register select port
    state.pFFFD = 0x00;     // Reset AY data port
    state.pFE = 0xFF;       // Reset ULA port (border white, no sound)

    // Set default 128K memory pages
    Memory& memory = *_context->pMemory;
    memory.SetROMMode(RM_SOS);      // SOS ROM at reset
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    memory.SetRAMPageToBank3(0);

    // Set default border color to white
    _screen->SetBorderColor(COLOR_WHITE);

    // Reset memory paging lock latch
    _7FFD_Locked = false;

    // Explicitly force screen to SCREEN_NORMAL
    _screen->SetActiveScreen(SCREEN_NORMAL);

    // Set default memory paging state
    Port_7FFD(0x00, 0x0000);
}

uint8_t PortDecoder_Profi::DecodePortIn(uint16_t port, uint16_t pc)
{
    (void)port;
    (void)pc;

    // Handle common part (like breakpoints)
    PortDecoder::DecodePortIn(port, pc);

    uint8_t result = 0xFF;

    return result;
}

void PortDecoder_Profi::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    //    Profi 1024
    //    port: #7FFD
    //    port: #DFFD

    // Handle common part (like breakpoints)
    PortDecoder::DecodePortOut(port, value, pc);

    bool isPort_7FFD = IsPort_7FFD(port);
    if (isPort_7FFD)
    {
        Port_7FFD(value, pc);
    }

    bool isPort_DFFD = IsPort_DFFD(port);
    if (isPort_DFFD)
    {
        Port_DFFD(value, pc);
    }
}

void PortDecoder_Profi::SetRAMPage(uint8_t page)
{
    (void)page;
}

void PortDecoder_Profi::SetROMPage(uint8_t page)
{
    (void)page;
}

/// endregion </Interface methods>

/// region <Helper methods>
bool PortDecoder_Profi::IsPort_7FFD(uint16_t port)
{
    //    Profi
    //    port: #7FFD
    //    Full match  :  01111111 11111101
    //    Match pattern: 01x1xxxx xx1xx101
    //    Equation: /IORQ /WR /A15 /A1
    static const uint16_t port_7FFD_full        = 0b0111'1111'1111'1101;
    static const uint16_t port_7FFD_mask        = 0b1000'0000'0000'0010;
    static const uint16_t port_7FFD_match       = 0b0000'0000'0000'0000;

    // Compile-time check
    static_assert((port_7FFD_full & port_7FFD_mask) == port_7FFD_match && "Mask pattern incorrect");

    bool result = (port & port_7FFD_mask) == port_7FFD_match;

    return result;
}

bool PortDecoder_Profi::IsPort_DFFD(uint16_t port)
{
    //    Profi
    //    port: #DFFD
    //    Full match:    00011111 11111101
    //    Match pattern: xx0xxxxx xxxxxx0x
    //    Equation: /IORQ /WR /A13 /A1
    static const uint16_t port_DFFD_full        = 0b0001'1111'1111'1101;
    static const uint16_t port_DFFD_mask        = 0b0010'0000'0000'0010;
    static const uint16_t port_DFFD_match       = 0b0000'0000'0000'0000;

    // Compile-time check
    static_assert((port_DFFD_full & port_DFFD_mask) == port_DFFD_match && "Mask pattern incorrect");

    bool result = (port & port_DFFD_mask) == port_DFFD_match;

    return result;
}
/// endregion <Helper methods>

/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Profi::Port_7FFD(uint8_t value, [[maybe_unused]] uint16_t pc)
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

    Memory& memory = *_context->pMemory;

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

    SpectrumScreenEnum screen = screenNumber ? SCREEN_SHADOW : SCREEN_NORMAL;
    _screen->SetActiveScreen(screen);

    LOGDEBUG(memory.DumpMemoryBankInfo());
}

/// Port #DFFD (Memory) handler
/// \param value
void PortDecoder_Profi::Port_DFFD(uint8_t value, uint16_t pc)
{
    (void)value;
    (void)pc;
}
