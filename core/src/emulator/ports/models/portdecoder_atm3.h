#pragma once
#include "stdafx.h"

#include "emulator/memory/nvram.h"

#include "portdecoder_atm710.h"

/// ATM3 / PentEvo / ZX-Evo BaseConf Port Decoder
///
/// Extends ATM710 with:
/// - 4MB RAM support (256 pages)
/// - Extended 7FFD bits 5,6,7 for additional page addressing
/// - Partial FF77 decode (any port with low byte 0x77, e.g. 0xBC77)
/// - No INT gate (always passes interrupts)
/// - NMI handling maps RAM page 0xFF to window 0
/// - Additional registers: pBD, pBE, pBF
/// - CMOS (DS12885-style RTC + NVRAM) shared with the memory manager ports:
///   data 0xBFF7 / address 0xDFF7 when pBF.0=0 (shaden off),
///   data 0xBEF7 / address 0xDEF7 when pBF.0=1 (shaden on, DOS ports active)
/// - Memory manager ports (x7F7 / xx77 / xFF7) only respond while pBF.0=1
///   (CF_DOSPORTS in original Unreal Speccy) - see io.cpp / memory.cpp
///
/// Note: ATM3 = PentEvo = ZX-Evo BaseConf (different names, same platform)
///
/// See: Unreal Speccy io.cpp, memory.cpp, atm.cpp

class PortDecoder_ATM3 : public PortDecoder_ATM710
{
    /// region <Fields>
protected:
    // DS12885-style RTC/CMOS (BaseConf config storage). Lives with the decoder
    // so the contents survive Core::Reset() (like a battery-backed CMOS).
    CMOS _cmos;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    PortDecoder_ATM3() = delete;
    PortDecoder_ATM3(EmulatorContext* context);
    virtual ~PortDecoder_ATM3();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;
    /// endregion </Interface methods>

    /// region <Port detection>
public:
    bool IsPort_FF77(uint16_t port);  // Partial decode for ATM3
    bool IsPort_37F7(uint16_t port);  // 4MB memory manager
    bool IsPort_BF(uint16_t port);    // ATM3 control
    bool IsManagerEnabled();          // CF_DOSPORTS: pBF.0 (shaden) OR ~cpm (aFF77.9=0)
    bool IsPort_BE(uint16_t port);    // ATM3 status / window readback
    bool IsPort_FFF7(uint16_t port, uint8_t& windowIndex) override;  // Narrower decode than ATM710 (mask 0x3FFF)

    // CMOS data port: 0xBFF7 when shaden (pBF.0) off, 0xBEF7 when on
    bool IsPort_CMOS_Data(uint16_t port);
    // CMOS address port: 0xDFF7 when shaden (pBF.0) off, 0xDEF7 when on
    bool IsPort_CMOS_Address(uint16_t port);
    /// endregion </Port detection>

    /// region <Port handlers>
protected:
    void Port_FF77_Out_ATM3(uint16_t port, uint8_t value, uint16_t pc);
    void Port_37F7_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_BF_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_BE_Out(uint16_t port, uint8_t value, uint16_t pc);
    uint8_t Port_BE_In(uint8_t portHi);  // selected by A15..A8 of port #xBE

    // updateMemoryBanks(): inherited from PortDecoder_ATM710 (covers ATM3 NMI handling)
    /// endregion </Port handlers>
};
