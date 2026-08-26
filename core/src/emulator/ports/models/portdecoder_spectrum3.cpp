#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_spectrum3.h"

#include "common/collectionhelper.h"

/// region <Constructors / Destructors>

PortDecoder_Spectrum3::PortDecoder_Spectrum3(EmulatorContext* context) : PortDecoder(context)
{
    _7FFD_Locked = false;
}

PortDecoder_Spectrum3::~PortDecoder_Spectrum3()
{
    LOGDEBUG("PortDecoder_Spectrum3::~PortDecoder_Spectrum3()");
}
/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Spectrum3::reset()
{
    // ZX-Spectrum +2A/+2B/+3 ROM pages
    // 0 - SOS128 <-- Set after reset
    // 1 - SOS48

    // Explicitly reset port states to ensure consistent reset behavior
    EmulatorState& state = _context->emulatorState;
    state.p7FFD = 0x00;     // Reset port 0x7FFD to default (Screen 0, RAM bank 0, ROM 0, paging enabled)
    state.p1FFD = 0x00;     // Reset port 0x1FFD (special paging)
    state.pBFFD = 0x00;     // Reset AY register select port
    state.pFFFD = 0x00;     // Reset AY data port
    state.pFE = 0xFF;       // Reset ULA port (border white, no sound)
    state.border_attr = 0x07;  // Sync border_attr with pFE bits 0-2 (white)

    // Set default 120K memory pages
    Memory& memory = *_context->pMemory;
    memory.SetROMPage(0);
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

uint8_t PortDecoder_Spectrum3::DecodePortIn(uint16_t port, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
    /// endregion </Override submodule>

    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    // AY #FFFD: A15=1, A14=1, A1=0. The AY-3-8910 does not decode the other
    // address bits, so mirrored ports (#FF05, #FF00, #C000...) select it on IN
    // too. Resolve mirrors to the canonical port BEFORE the weak FE (A0-only)
    // check - otherwise register readback via a mirror reaches the keyboard or
    // returns 0xFF and TurboSound players cannot detect the second chip
    // (same order as the OUT dispatch and the Pentagon decode table)
    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    if ((port & 0xC002) == 0xC000)
    {
        result = PeripheralPortIn(0xFFFD);
        disp.decodedPort = 0xFFFD;
    }
    // AY #BFFD: A15=1, A14=0, A1=0
    else if ((port & 0xC002) == 0x8000)
    {
        result = PeripheralPortIn(0xBFFD);
        disp.decodedPort = 0xBFFD;
    }
    else if (IsPort_FE(port))
    {
        // Call default implementation
        result = Default_Port_FE_In(port, pc);
        _lastPortDecoded = true;
        disp.decodedPort = 0x00FE;
        disp.wasHandledInline = true;
    }
    else
    {
        result = PeripheralPortIn(port);
        // Identity decode: mark decoded only when a device actually responded
        if (_lastPortDecoded)
            disp.decodedPort = port;
    }
    disp.wasDecoded = _lastPortDecoded;

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (!key_exists(_loggingMutePorts, port))
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGINFO("[In] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, result);
    }
    /// endregion </Debug logging>

    // Universal handler for breakpoints, tracking, analyzers
    OnPortInComplete(port, result, pc, disp);

    return result;
}

void PortDecoder_Spectrum3::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    //    ZX Spectrum 128 +2A/+2B/+3
    //    port: #7FFD
    //    port: #1FFD

    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    bool isPort_7FFD = IsPort_7FFD(port);
    if (isPort_7FFD)
    {
        Port_7FFD(value, pc);
        disp.decodedPort = 0x7FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }

    bool isPort_1FFD = IsPort_1FFD(port);
    if (isPort_1FFD)
    {
        Port_1FFD(value, pc);
        disp.decodedPort = 0x1FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }

    // AY #FFFD: A15=1, A14=1, A1=0 (register select / TurboSound chip select)
    // Mask: 0b1100'0000'0000'0010, Match: 0b1100'0000'0000'0000
    if ((port & 0xC002) == 0xC000)
    {
        _state->pFFFD = value;
        PeripheralPortOut(0xFFFD, value);
        disp.decodedPort = 0xFFFD;
        disp.wasDecoded = true;
    }
    // AY #BFFD: A15=1, A14=0, A1=0 (data write)
    // Mask: 0b1100'0000'0000'0010, Match: 0b1000'0000'0000'0000
    else if ((port & 0xC002) == 0x8000)
    {
        _state->pBFFD = value;
        PeripheralPortOut(0xBFFD, value);
        disp.decodedPort = 0xBFFD;
        disp.wasDecoded = true;
    }

    /// region <Debug logging>

    // Check if port was not explicitly muted
    if (_logger && _logger->GetLevel() <= LoggerLevel::LogInfo)
    {
        if (!key_exists(_loggingMutePorts, port))
        {
            // Determine RAM/ROM page where code executed from
            std::string currentMemoryPage = GetPCAddressLocator(pc);
            MLOGINFO("[Out] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, value);
        }
    }
    /// endregion </Debug logging>

    // Universal handler for breakpoints, tracking, analyzers
    OnPortOutComplete(port, value, pc, disp);
}

void PortDecoder_Spectrum3::SetRAMPage(uint8_t page)
{
    (void)page;
}

void PortDecoder_Spectrum3::SetROMPage(uint8_t page)
{
    (void)page;
}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Spectrum3::IsPort_FE(uint16_t port)
{
    //    ZX Spectrum 128 / +2A
    //    Port: #FE
    //    Match pattern: xxxxxxxx xxxxxxx0
    //    Full pattern:  xxxxxxxx 11111110
    static const uint16_t port_FE_full      = 0b0000'0000'1111'1110;
    static const uint16_t port_FE_mask      = 0b0000'0000'0000'0001;
    static const uint16_t port_FE_match     = 0b0000'0000'0000'0000;

    // Compile-time check
    static_assert((port_FE_full & port_FE_mask) == port_FE_match && "Mask pattern incorrect");

    bool result = (port & port_FE_mask) == port_FE_match;

    return result;
}

bool PortDecoder_Spectrum3::IsPort_7FFD(uint16_t port)
{
    //    ZX Spectrum +2A / +3
    //    port: #7FFD
    //    Match pattern: 01xxxxxx xxxxxx0x
    //    Full pattern:  01111111 11111101
    //    The additional memory features of the 128K/+2 are controlled to by writes to port 0x7ffd.
    //    As normal on Sinclair hardware, the port address is in fact only partially decoded and the hardware will respond
    //    to any port address with bits 1 and 15 reset.
    static const uint16_t port_7FFD_full    = 0b0111'1111'1111'1101;
    static const uint16_t port_7FFD_mask    = 0b1100'0000'0000'0010;
    static const uint16_t port_7FFD_match   = 0b0100'0000'0000'0000;

    // Compile-time check
    static_assert((port_7FFD_full & port_7FFD_mask) == port_7FFD_match && "Mask pattern incorrect");

    bool result = (port & port_7FFD_mask) == port_7FFD_match;

    return result;
}

bool PortDecoder_Spectrum3::IsPort_1FFD(uint16_t port)
{
    //    ZX Spectrum +2A / +3
    //    port: #1FFD
    //    Match pattern: 0001xxxx xxxxxx0x
    //    Full pattern:  00011111 11111101
    static const uint16_t port_1FFD_full    = 0b0001'1111'1111'1101;
    static const uint16_t port_1FFD_mask    = 0b1111'0000'0000'0010;
    static const uint16_t port_1FFD_match   = 0b0001'0000'0000'0000;

    // Compile-time check
    static_assert((port_1FFD_full & port_1FFD_mask) == port_1FFD_match && "Mask pattern incorrect");

    bool result = (port & port_1FFD_mask) == port_1FFD_match;

    return result;
}
/// endregion <Helper methods>


/// Port #7FFD (Memory) handler
/// \param value
void PortDecoder_Spectrum3::Port_7FFD(uint8_t value, uint16_t pc)
{
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
void PortDecoder_Spectrum3::Port_1FFD(uint8_t value, uint16_t pc)
{
    (void)value;
    (void)pc;
}
