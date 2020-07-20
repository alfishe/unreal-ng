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
class PortDecoder_Spectrum128 : public PortDecoder
{
    /// region <Fields>
protected:
    bool _7FFD_Locked = false;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    PortDecoder_Spectrum128() = delete;                 // Disable default constructor; C++ 11 feature
    PortDecoder_Spectrum128(EmulatorContext* context);
    virtual ~PortDecoder_Spectrum128();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void Reset() override;
    uint8_t DecodePortIn(uint16_t addr) override;
    void DecodePortOut(uint16_t addr, uint8_t value) override;

    void SetRAMPage(uint8_t oage) override;
    void SetROMPage(uint8_t page) override;
    /// endregion </Interface methods>

    /// region <Helper methods>
public:
    bool IsPort_7FFD(uint16_t addr);
    bool IsPort_1FFD(uint16_t addr);
    /// endregion <Helper methods>

protected:
    void Port_7FFD(uint16_t port, uint8_t value);

    std::string Dump_7FFD_value(uint8_t value);
};
