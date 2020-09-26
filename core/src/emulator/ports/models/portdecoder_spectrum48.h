#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

class PortDecoder_Spectrum48 : public PortDecoder
{
    /// region <Constructors / Destructors>
public:
    PortDecoder_Spectrum48() = delete;                 // Disable default constructor; C++ 11 feature
    PortDecoder_Spectrum48(EmulatorContext* context);
    virtual ~PortDecoder_Spectrum48();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void Reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    void SetRAMPage(uint8_t oage) override;
    void SetROMPage(uint8_t page) override;
    /// endregion </Interface methods>

    /// region <Helper methods>
public:
    bool IsPort_FE(uint16_t port);

    /// endregion <Helper methods>

protected:
    void Port_FE(uint16_t port, uint8_t value, uint16_t pc);

    std::string Dump_FE_value(uint8_t value);
};
