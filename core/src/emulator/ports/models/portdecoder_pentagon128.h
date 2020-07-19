#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"


///
/// See: https://worldofspectrum.org/faq/reference/128kreference.htm
/// See: https://zx-pk.ru/threads/11490-paging-ports-of-zx-clones.html?langid=1
/// See: http://zx.clan.su/forum/11-46-1
class PortDecoder_Pentagon128 : public PortDecoder
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    bool _7FFD_Locked = false;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    PortDecoder_Pentagon128() = delete;                 // Disable default constructor; C++ 11 feature
    PortDecoder_Pentagon128(EmulatorContext* context);
    virtual ~PortDecoder_Pentagon128();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void Reset() override;
    uint8_t DecodePortIn(uint16_t addr) override;
    void DecodePortOut(uint16_t addr, uint8_t value) override;
    /// endregion </Interface methods>

protected:
    void Port_7FFD(uint8_t value);
};
