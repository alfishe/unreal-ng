#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/cpu/z80.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/spectrum128ayioport.h"
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
class PortDecoder_Spectrum128 : public PortDecoder, public IReadCycleLatch
{
    /// region <Fields>
protected:
    /// The board wiring on AY port A (keypad / AUX and RS-232 lines), attached to the socket AY
    Spectrum128AyIoPort _ayIoPort;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    PortDecoder_Spectrum128() = delete;                 // Disable default constructor; C++ 11 feature
    PortDecoder_Spectrum128(EmulatorContext* context);
    virtual ~PortDecoder_Spectrum128();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    void SetRAMPage(uint8_t oage) override;
    void SetROMPage(uint8_t page) override;

    /// The paging latch is clocked by read cycles too: the 128K's decode (a HAL10H8, BANK = IORQ & (RD | WR)
    /// & !A15 & !A1, service manual and a PAL readout) does not tell them apart, so an IN from the #7FFD decode
    /// writes the byte on the bus - the floating bus, #FF in the border - into the 74LS174, bits 0-5 only. The
    /// lock bit stops it like a write (the diode clamps the latch clock). The +2A / +3 gate array decodes
    /// writes only. Z80::readCycleLatch, docs/inprogress/2026-09-30-fusetest-core-defects/research.md claim 2
    void OnReadCycle(uint16_t port, uint8_t value) override;
    /// endregion </Interface methods>

    /// The board wiring on AY port A (devices on the keypad / AUX and RS-232 sockets set its receiver levels)
    Spectrum128AyIoPort& AyIoPort()
    {
        return _ayIoPort;
    }

    /// region <Helper methods>
public:
    bool IsPort_FE(uint16_t port);

    /// The #7FFD decode: A15 = 0, A2 = 1, A1 = 0 (A2 keeps the SounDrive ports #F1 / #F9 out)
    static constexpr uint16_t kPort7FFDMask = 0b1000'0000'0000'0110;
    static constexpr uint16_t kPort7FFDMatch = 0b0000'0000'0000'0100;
    bool IsPort_7FFD(uint16_t port);

    bool IsPort_BFFD(uint16_t port);
    bool IsPort_FFFD(uint16_t port);

    /// endregion <Helper methods>

protected:
    void Port_7FFD_Out(uint16_t port, uint8_t value, uint16_t pc);

public:
    /// Re-derive the #C000 RAM page from the #7FFD latch (bank rebuild after a
    /// restore that bypassed the port write). Locked paging keeps its mapping.
    void UpdateModelMemoryBanks() override;

protected:

    uint8_t Port_BFFD_In(uint16_t port, uint16_t pc);
    void Port_BFFD_Out(uint16_t port, uint8_t value, uint16_t pc);

    uint8_t Port_FFFD_In(uint16_t port, uint16_t pc);
    void Port_FFFD_Out(uint16_t port, uint8_t value, uint16_t pc);

    std::string Dump_7FFD_value(uint8_t value);
    std::string Dump_BFFD_value(uint8_t value);
    std::string Dump_FFFD_value(uint8_t value);
};
