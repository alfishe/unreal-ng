#pragma once
#include "stdafx.h"

#include "portdecoder_atm710.h"

/// ATM3 / PentEvo / ZX-Evo BaseConf Port Decoder
///
/// Extends ATM710 with:
/// - 4MB RAM support (256 pages)
/// - Extended 7FFD bits 5,6,7 for additional page addressing
/// - Partial FF77 decode (allows x7F7 variants)
/// - No INT gate (always passes interrupts)
/// - NMI handling maps RAM page 0xFF to window 0
/// - Additional registers: pBD, pBE, pBF
///
/// Note: ATM3 = PentEvo = ZX-Evo BaseConf (different names, same platform)
///
/// See: Unreal Speccy zxevo.cpp, atm.cpp

class PortDecoder_ATM3 : public PortDecoder_ATM710
{
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
    bool IsPort_FFF7(uint16_t port, uint8_t& windowIndex) override;  // Narrower decode than ATM710 (mask 0x3FFF)
    /// endregion </Port detection>

    /// region <Port handlers>
protected:
    void Port_FF77_Out_ATM3(uint16_t port, uint8_t value, uint16_t pc);
    void Port_37F7_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_BF_Out(uint16_t port, uint8_t value, uint16_t pc);

    // updateMemoryBanks(): inherited from PortDecoder_ATM710 (covers ATM3 NMI handling)
    /// endregion </Port handlers>
};
