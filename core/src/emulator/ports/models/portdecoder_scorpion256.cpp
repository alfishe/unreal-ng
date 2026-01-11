#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_scorpion256.h"

#include "common/collectionhelper.h"

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

void PortDecoder_Scorpion256::reset()
{
    // Scorpion 256 ROM setup
    // Similar to Spectrum 128K
    // Bit 4 of port 0x7FFD: 0 = SOS ROM, 1 = 128K ROM

    // Explicitly reset port states to ensure consistent reset behavior
    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;     // Reset port 0x7FFD to default (Screen 0, RAM bank 0, SOS ROM, paging enabled)
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

uint8_t PortDecoder_Scorpion256::DecodePortIn(uint16_t port, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
    /// endregion </Override submodule>

    uint8_t result = 0xFF;

    // Handle common part (like breakpoints)
    PortDecoder::DecodePortIn(port, pc);

    if (IsPort_FE(port))
    {
        // Call default implementation
        result = Default_Port_FE_In(port, pc);
    }

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGINFO("[In] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, result);
    }
    /// endregion </Debug logging>

    return result;
}

void PortDecoder_Scorpion256::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    //    ZX Spectrum 128 +2A/+2B/+3
    //    port: #7FFD
    //    port: #1FFD

    // Handle common part (like breakpoints)
    PortDecoder::DecodePortOut(port, value, pc);

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

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGINFO("[Out] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, value);
    }
    /// endregion </Debug logging>
}

void PortDecoder_Scorpion256::SetRAMPage(uint8_t page)
{
    (void)page;
}

void PortDecoder_Scorpion256::SetROMPage(uint8_t page)
{
    (void)page;
}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Scorpion256::IsPort_FE(uint16_t port)
{
    //    Scorpion ZS256
    //    Port: #FE
    //    Match pattern: xxxxxxxx xx1xxx10
    //    Full pattern:  xxxxxxxx 11111110
    static const uint16_t port_FE_full      = 0b0000'0000'1111'1110;
    static const uint16_t port_FE_mask      = 0b0000'0000'0010'0011;
    static const uint16_t port_FE_match     = 0b0000'0000'0010'0010;

    // Compile-time check
    static_assert((port_FE_full & port_FE_mask) == port_FE_match && "Mask pattern incorrect");

    bool result = (port & port_FE_mask) == port_FE_match;

    return result;
}

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

    // Compile-time check
    static_assert((port_7FFD_full & port_7FFD_mask) == port_7FFD_match && "Mask pattern incorrect");

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

    // Compile-time check
    static_assert((port_1FFD_full & port_1FFD_mask) == port_1FFD_match && "Mask pattern incorrect");

    bool result = (port & port_1FFD_mask) == port_1FFD_match;

    return result;
}
/// endregion <Helper methods>

/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Scorpion256::Port_7FFD(uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

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

    static const uint16_t port = 0x7FFD;
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

    MLOGDEBUG(memory.DumpMemoryBankInfo());

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        MLOGDEBUG(DumpPortValue(0x7FFD, port, value, pc));
        MLOGDEBUG(memory.DumpMemoryBankInfo());
    }

    /// endregion </Debug logging>
}

/// Port #1FFD (Memory) handler
/// \param value
void PortDecoder_Scorpion256::Port_1FFD(uint8_t value, uint16_t pc)
{
    (void)value;
    (void)pc;
}
