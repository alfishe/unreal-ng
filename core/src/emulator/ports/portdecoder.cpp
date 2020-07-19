#include "stdafx.h"
#include "portdecoder.h"

#include "common/logger.h"

#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include <cassert>

/// region <Static methods>

PortDecoder* PortDecoder::GetPortDecoderForModel(MEM_MODEL model, EmulatorContext* context)
{
    PortDecoder* result = nullptr;

    switch (model)
    {
        case MM_PENTAGON:
            result = new PortDecoder_Pentagon128(context);
            break;
        case MM_PLUS3:
            result = new PortDecoder_Spectrum128(context);
            break;
        default:
            LOGERROR("PortDecoder::GetPortDecoderForModel - Unknown model: %d", model);
            assert("Unknown model");
    }

    return result;
}

/// endregion </Static methods>