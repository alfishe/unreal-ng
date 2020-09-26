#include "stdafx.h"
#include "portdecoder.h"

#include "common/modulelogger.h"

#include "common/stringhelper.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/ports/models/portdecoder_spectrum48.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"
#include <cassert>

/// region <Constructors / Destructors>
PortDecoder::PortDecoder(EmulatorContext* context)
{
    _context = context;

    _state = &context->state;
    _keyboard = context->pKeyboard;
    _memory = context->pMemory;
    _screen = context->pScreen;
    _logger = context->pModuleLogger;
}
/// endregion </Constructors / Destructors>

/// region <Static methods>

PortDecoder* PortDecoder::GetPortDecoderForModel(MEM_MODEL model, EmulatorContext* context)
{
    PortDecoder* result = nullptr;

    switch (model)
    {
        case MM_SPECTRUM48:
            result = new PortDecoder_Spectrum48(context);
            break;
        case MM_PENTAGON:
            result = new PortDecoder_Pentagon128(context);
            break;
        case MM_SPECTRUM128:
            result = new PortDecoder_Spectrum128(context);
            break;
        case MM_PLUS3:
            result = new PortDecoder_Spectrum3(context);
            break;
        case MM_PROFI:
            result = new PortDecoder_Profi(context);
            break;
        case MM_SCORP:
            result = new PortDecoder_Scorpion256(context);
            break;
        default:
            LOGERROR("PortDecoder::GetPortDecoderForModel - Unknown model: %d", model);
            throw std::logic_error(StringHelper::Format("PortDecoder::GetPortDecoderForModel - unknown model %d", model));
            break;
    }

    return result;
}

/// endregion </Static methods>

/// region <Interface methods>

/// Keyboard ports:
/// #FEFE
/// #FDFE
/// #FBFE
/// #F7FE
/// #EFFE
/// #DFFE
/// #BFFE
/// #7FFE
/// \param port Port to check for match
/// \return If port matched as #FE
bool PortDecoder::IsFEPort(uint16_t port)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
    /// endregion </Override submodule>

    // Any even port will be decoded as #FE
    static const uint16_t port_FE_full    = 0b0000'0000'1111'1110;
    static const uint16_t port_FE_mask    = 0b0000'0000'0000'0001;
    static const uint16_t port_FE_match   = 0b0000'0000'0000'0000;

    bool result = (port & port_FE_mask) == port_FE_match;

    return result;
}

/// Default implementation for 'out (#FE)'
/// Bits [0:2]  - Border color
/// Bit [4]     - Beeper output bit
/// See: https://worldofspectrum.org/faq/reference/48kreference.htm
/// \param port
/// \param value
/// \param pc
/// \return
bool PortDecoder::PortFE_Out(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    bool result = false;

    uint8_t borderColor = value & 0b000'00111;
    bool beeperBit = value & 0b0001'0000;

    _screen->SetBorderColor(borderColor);

    MLOGDEBUG(DumpPortValue(0xFE, port, value, pc, Dump_FE_value(value).c_str()));

    return result;
}

std::string PortDecoder::GetPCAddressLocator(uint16_t pc)
{
    std::string result;

    if (pc < 0x4000)
    {
        if (_memory->IsBank0ROM())
        {
            uint8_t romPage = _memory->GetROMPage();
            result = StringHelper::Format(" ROM_%d", romPage);
        }
        else
        {
            uint8_t ramPage = _memory->GetRAMPageForBank0();
            result = StringHelper::Format(" RAM_%d", ramPage);
        }
    }
    else if (pc >= 0xC000)
    {
        uint8_t ramPage = _memory->GetRAMPageForBank3();
        result = StringHelper::Format(" RAM_%d", ramPage);
    }

    return result;
}

/// endregion </Interface methods>

/// region <Debug information>

std::string PortDecoder::DumpPortValue(uint16_t refPort, uint16_t port, uint8_t value, uint16_t pc, const char* comment)
{
    std::string result;

    std::string pcString;
    if (pc == 0x0000)
    {
        // Port triggered during reset / debug
        pcString = "<Init>";
    }
    else
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);

        pcString = StringHelper::Format("PC:0x%04X%s", pc, currentMemoryPage.c_str());
    }

    if (comment != nullptr)
    {
        result = StringHelper::Format("[Out] [%s] Port #%04X, decoded as #%04X value: 0x%02X (%s)", pcString.c_str(), port, refPort, value, comment);
    }
    else
    {
        result = StringHelper::Format("[Out] [%s] Port #%04X, decoded as #%04X value: 0x%02X", pcString.c_str(), port, refPort, value);
    }

    return result;
}

std::string PortDecoder::Dump_FE_value(uint8_t value)
{
    uint8_t borderColor = value & 0b000'00111;
    bool beeperBit = value & 0b0001'0000;
    std::string colorText = Screen::GetColorName(borderColor);

    std::string result = StringHelper::Format("Border color: %d (%s); Beeper: %d", borderColor, colorText.c_str(), beeperBit);

    return result;
}

/// endregion </Debug information>