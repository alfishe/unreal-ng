#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/nvram.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

///
/// See: https://worldofspectrum.org/faq/reference/128kreference.htm
/// See: https://zx-pk.ru/threads/11490-paging-ports-of-zx-clones.html?langid=1
/// See: http://zx.clan.su/forum/11-46-1
class PortDecoder_Scorpion256 : public PortDecoder
{
    /// region <Fields>
protected:
    // _7FFD_Locked is now inherited from PortDecoder base class

    // SMUC (Scorpion & MOA Universal Controller) stub for the ProfROM boot
    // probes (profrom-smuc-not-found-and-driver-disassembly.md section 8):
    // 2 KB serial-link EEPROM with the DS1685 RTC behind the same NVRAM chip,
    // plus an IDE window register file
    SMUCNvram _smucNvram;
    uint8_t _smucIdeRegs[8] = {};

    // SMUC board presence. Absent by default: with no board physically on the
    // bus the whole #xxBA/#xxBE family floats (reads open-bus #FF, writes hit
    // no latch), the ProfROM presence polls fail fast and the boot skips the
    // NVRAM / RTC / IDE init chains. The stubs stay wired behind the flag for
    // verification tests (profrom-smuc-not-found-and-driver-disassembly.md, 8)
    bool _smucEnabled = false;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    PortDecoder_Scorpion256() = delete;                 // Disable default constructor; C++ 11 feature
    PortDecoder_Scorpion256(EmulatorContext* context);
    virtual ~PortDecoder_Scorpion256();
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
    bool IsPort_1FFD(uint16_t port);
    bool IsPort_7EFD(uint16_t port);  // ProfROM window latch (#7FFD pattern with A8 low)
    bool IsPort_SMUC(uint16_t port);  // SMUC board (#xxBA/#xxBE family)

    /// SMUC EEPROM backing store (verification tests / debug UI)
    SMUCNvram& GetSMUCNvram() { return _smucNvram; }

    /// SMUC board presence (absent by default - see _smucEnabled)
    void SetSmucEnabled(bool enabled) { _smucEnabled = enabled; }
    bool IsSmucEnabled() const { return _smucEnabled; }
    /// endregion </Helper methods>

protected:
    void Port_7FFD(uint8_t value, uint16_t pc);
    void Port_1FFD(uint8_t value, uint16_t pc);
    uint8_t ReadSMUCPort(uint16_t port);
    void WriteSMUCPort(uint16_t port, uint8_t value);
};
