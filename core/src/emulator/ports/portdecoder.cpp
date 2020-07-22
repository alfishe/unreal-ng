#include "stdafx.h"
#include "portdecoder.h"

#include "common/logger.h"

#include "common/stringhelper.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"
#include <cassert>

/// region <Constructors / Destructors>
PortDecoder::PortDecoder(EmulatorContext* context)
{
    _context = context;

    _state = &context->state;
    _screen = context->pScreen;
}
/// endregion </Constructors / Destructors>

/// region <Static methods>

PortDecoder* PortDecoder::GetPortDecoderForModel(MEM_MODEL model, EmulatorContext* context)
{
    PortDecoder* result = nullptr;

    switch (model)
    {
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
            assert("Unknown model");
    }

    return result;
}

/// endregion </Static methods>

/// region <Interface methods>
std::string PortDecoder::DumpPortValue(uint16_t refPort, uint16_t port, uint8_t value, uint16_t pc, const char* comment)
{
    std::string result;
    if (comment != nullptr)
    {
        result = StringHelper::Format("[Out] [PC:0x%04X] Port #%04X, decoded as #%04X value: 0x%02X (%s)", pc, port, refPort, value, comment);
    }
    else
    {
        result = StringHelper::Format("[Out] [PC:0x%04X] Port #%04X, decoded as #%04X value: 0x%02X", pc, port, refPort, value);
    }

    return result;
}
/// endregion </Interface methods>