#pragma once
#include "stdafx.h"

#include "emulator/platform.h"

class EmulatorContext;

/// Base class for all model port decoders
class PortDecoder
{
    /// region <Static methods>
public:
    static PortDecoder* GetPortDecoderForModel(MEM_MODEL model, EmulatorContext* context);
    /// endregion </Static methods>

    /// region <Constructors / destructors>
public:
    virtual ~PortDecoder() {};
    /// endregion </Constructors / destructors>

    /// region <Interface methods>
public:
    virtual void Reset() = 0;
    virtual uint8_t DecodePortIn(uint16_t addr) = 0;
    virtual void DecodePortOut(uint16_t addr, uint8_t value) = 0;
    /// endregion </Interface methods>
};