#include "stdafx.h"

#include "common/logger.h"

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
    LOGDEBUG("PortDecoder_Spectrum48::~PortDecoder_Spectrum48()");
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Spectrum48::Reset()
{
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
    uint8_t result = 0xFF;

    if (IsPort_FE_Out(port))
    {
        result = _keyboard->HandlePort(port);
    }

    // Determine RAM/ROM page where code executed from
    std::string currentMemoryPage = GetPCAddressLocator(pc);

    LOGWARNING("[In] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, result);

    return result;
}

void PortDecoder_Spectrum48::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    if (IsPort_FE_Out(port))
    {
        Port_FE(port, value, pc);
    }
    else
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        LOGWARNING("[Out] [PC:%04X%s] Port: %02X; Value: %02X", pc, currentMemoryPage.c_str(), port, value);
    }
}

void PortDecoder_Spectrum48::SetRAMPage(uint8_t page)
{

}

void PortDecoder_Spectrum48::SetROMPage(uint8_t page)
{

}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Spectrum48::IsPort_FE_Out(uint16_t port)
{
    //    ZX Spectrum 48
    //    Port: #FE
    //    Match pattern: xxxxxxxx xxxxxxx0
    //    Full pattern:  xxxxxxxx 11111110
    static const uint16_t port_FE_full      = 0b0000'0000'1111'1110;
    static const uint16_t port_FE_mask      = 0b0000'0000'0000'0001;
    static const uint16_t port_FE_match     = 0b0000'0000'0000'0000;

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
    bool beeperBit = value & 0b0001'0000;

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

