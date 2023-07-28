#pragma once

#include <stdafx.h>
#include "emulator/ports/models/portdecoder_pentagon128.h"

/// Port decoder for Pentagon 512K model
/// The only difference comparing to Pentagon 128K is port #7FFD
/// bits [6..7] usage to address more memory pages.
/// 32 RAM pages addressed via 5 bit index ( port #7FFD bits [0..2][6..7] )
class PortDecoder_Pentagon512 : public PortDecoder_Pentagon128
{
    /// region <Constructors / Destructors>
public:
    PortDecoder_Pentagon512() = delete;
    PortDecoder_Pentagon512(EmulatorContext* context) : PortDecoder_Pentagon128(context) {};
    virtual ~PortDecoder_Pentagon512() = default;
    /// endregion </Constructors / Destructors>

protected:
    virtual void switchRAMPage(uint8_t value) override;
};
