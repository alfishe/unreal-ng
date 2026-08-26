#include "stdafx.h"

#include "common/modulelogger.h"

#include "portdecoder_spectrum48.h"

#include "common/stringhelper.h"
#include <map>
#include <vector>

/// region <Constructors / Destructors>

PortDecoder_Spectrum48::PortDecoder_Spectrum48(EmulatorContext* context) : PortDecoder(context)
{
}

PortDecoder_Spectrum48::~PortDecoder_Spectrum48()
{
    MLOGDEBUG("PortDecoder_Spectrum48::~PortDecoder_Spectrum48()");
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Spectrum48::reset()
{
    // Explicitly reset port states to ensure consistent reset behavior
    EmulatorState& state = _context->emulatorState;
    state.pFE = 0xFF;       // Reset ULA port (border white, no sound)
    state.border_attr = 0x07;  // Sync border_attr with pFE bits 0-2 (white)

    // Set default 48K memory pages
    Memory& memory = *_context->pMemory;
    memory.SetROMPage(0);
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    memory.SetRAMPageToBank3(0);

    // Set default border color to white
    _screen->SetBorderColor(COLOR_WHITE);
}

uint8_t PortDecoder_Spectrum48::DecodePortIn(uint16_t port, uint16_t pc)
{
    /// region <Override submodule>
    [[maybe_unused]]
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
    /// endregion </Override submodule>

    uint8_t result = 0xFF;
    _lastPortDecoded = false;

    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    // AY #FFFD: A15=1, A14=1, A1=0. The AY-3-8910 does not decode the other
    // address bits, so mirrored ports (#FF05, #FF00, #C000...) select it on IN
    // too. Resolve mirrors to the canonical port BEFORE the weak FE (A0-only)
    // check - otherwise register readback via a mirror reaches the keyboard or
    // returns 0xFF and TurboSound players cannot detect the second chip
    // (same order as the OUT dispatch and the Pentagon decode table)
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
        // Identity decode: mark decoded only when a device actually responded,
        // otherwise the port stays unmapped (decodedPort == 0) in the trace
        if (_lastPortDecoded)
            disp.decodedPort = port;
    }
    disp.wasDecoded = _lastPortDecoded;

    if (_logger && _logger->GetLevel() <= LoggerLevel::LogWarning)
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGWARNING("[In] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, result);
    }

    // Universal handler for breakpoints, tracking, analyzers
    OnPortInComplete(port, result, pc, disp);

    return result;
}

void PortDecoder_Spectrum48::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    [[maybe_unused]]
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    // Port trace decode attribution (if-chain decoder: no mask/match table)
    PortDecodeDisposition disp;
    disp.decodeRuleIndex = PortTraceRule::kNoTable;

    // AY mirrors must be resolved before the FE (A0-only) check: the AY decodes
    // A15/A14/A1 only, so an even mirror like #C000 would otherwise be claimed
    // by the keyboard/border handler and never reach the sound chip
    // AY #FFFD: A15=1, A14=1, A1=0 (register select / TurboSound chip select)
    // Mask: 0b1100'0000'0000'0010, Match: 0b1100'0000'0000'0000
    // Stock 48K has no AY, but TurboSound interfaces use this decode
    if ((port & 0xC002) == 0xC000)
    {
        PeripheralPortOut(0xFFFD, value);
        disp.decodedPort = 0xFFFD;
        disp.wasDecoded = true;
    }
    // AY #BFFD: A15=1, A14=0, A1=0 (data write)
    // Mask: 0b1100'0000'0000'0010, Match: 0b1000'0000'0000'0000
    else if ((port & 0xC002) == 0x8000)
    {
        PeripheralPortOut(0xBFFD, value);
        disp.decodedPort = 0xBFFD;
        disp.wasDecoded = true;
    }
    else if (IsPort_FE(port))
    {
        Port_FE(port, value, pc);
        disp.decodedPort = 0x00FE;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
    }
    else
    {
        if (_logger && _logger->GetLevel() <= LoggerLevel::LogWarning)
        {
            // Determine RAM/ROM page where code executed from
            std::string currentMemoryPage = GetPCAddressLocator(pc);
            LOGWARNING("[Out] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, value);
        }
    }

    // Universal handler for breakpoints, tracking, analyzers
    OnPortOutComplete(port, value, pc, disp);
}

void PortDecoder_Spectrum48::SetRAMPage(uint8_t page)
{
    (void)page;
}

void PortDecoder_Spectrum48::SetROMPage(uint8_t page)
{
    (void)page;
}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Spectrum48::IsPort_FE(uint16_t port)
{
    //    ZX Spectrum 48
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

/// endregion </Helper methods>

/// region <Port handlers>

/// Port #FE (Border, Beeper)
/// \param port
/// \param value
/// \param pc
void PortDecoder_Spectrum48::Port_FE(uint16_t port, uint8_t value, uint16_t pc)
{
    uint8_t borderColor = value & 0b000'00111;
    //bool beeperBit = value & 0b0001'0000;

    _screen->SetBorderColor(borderColor);

    LOGDEBUG(DumpPortValue(0xFE, port, value, pc, Dump_FE_value(value).c_str()));
}

/// endregion </Port handlers>

/// region <Debug information>

std::string PortDecoder_Spectrum48::Dump_FE_value(uint8_t value)
{
    uint8_t borderColor = value & 0b000'00111;
    bool beeperBit = value & 0b0001'0000;
    std::string colorText = Screen::GetColorName(borderColor);

    std::string result = StringHelper::Format("Border color: %d (%s); Beeper: %d", borderColor, colorText.c_str(), beeperBit);

    return result;
}

/// endregion </Debug information>

