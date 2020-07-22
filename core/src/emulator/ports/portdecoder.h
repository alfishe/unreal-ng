#pragma once
#include "stdafx.h"

#include "emulator/platform.h"

class EmulatorContext;
class Screen;

/// Base class for all model port decoders
class PortDecoder
{
    /// region <Static methods>
public:
    static PortDecoder* GetPortDecoderForModel(MEM_MODEL model, EmulatorContext* context);
    /// endregion </Static methods>

    /// region <Fields>
protected:
    COMPUTER* _state = nullptr;
    Screen* _screen = nullptr;

    EmulatorContext* _context = nullptr;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    PortDecoder() = delete;     // Disable default constructor; C++ 11 feature
    PortDecoder(EmulatorContext* context);
    virtual ~PortDecoder() {};
    /// endregion </Constructors / destructors>

    /// region <Interface methods>
public:
    virtual void Reset() = 0;
    virtual uint8_t DecodePortIn(uint16_t addr, uint16_t pc) = 0;
    virtual void DecodePortOut(uint16_t addr, uint8_t value, uint16_t pc) = 0;

    virtual void SetRAMPage(uint8_t oage) = 0;
    virtual void SetROMPage(uint8_t page) = 0;

protected:
    virtual std::string GetPCAddressLocator(uint16_t pc);
    virtual std::string DumpPortValue(uint16_t refPort, uint16_t port, uint8_t value, uint16_t pc, const char* comment = nullptr);
    /// endregion </Interface methods>
};