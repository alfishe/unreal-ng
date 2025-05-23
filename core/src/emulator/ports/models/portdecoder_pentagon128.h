#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"


///
/// See: https://worldofspectrum.org/faq/reference/128kreference.htm
/// See: https://zx-pk.ru/threads/11490-paging-ports-of-zx-clones.html?langid=1
/// See: http://zx.clan.su/forum/11-46-1
// ----------
// Memory Map
// ----------
// ROM 0 or 1 resides at $0000-$3FFF
// RAM bank 5 resides at $4000-$7FFF always
// RAM bank 2 resides at $8000-$BFFF always
// Any RAM bank may reside at $C000-$FFFF
class PortDecoder_Pentagon128 : public PortDecoder
{
    /// region <Fields>
protected:
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
    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    void SetRAMPage(uint8_t oage) override;
    void SetROMPage(uint8_t page) override;
    /// endregion </Interface methods>

    /// region <Helper methods>
public:
    bool IsPort_FE(uint16_t port);

    bool IsPort_7FFD(uint16_t port);

    bool IsPort_BFFD(uint16_t port);
    bool IsPort_FFFD(uint16_t port);

    uint16_t decodePort(uint16_t port);

protected:
    void Port_7FFD_Out(uint16_t port, uint8_t value, uint16_t pc);
    virtual void switchRAMPage(uint8_t value);
    /// endregion <Helper methods>

    /// region <Debug methods>
protected:
    std::string Dump_7FFD_value(uint8_t value);
    std::string Dump_BFFD_value(uint8_t value);
    std::string Dump_FFFD_value(uint8_t value);
    /// endregion </Debug methods>
};
