#pragma once

#include <cstdint>

/// @file z84systemregs.h
/// @brief The Z84C15's system control registers and watchdog (Sprinter
/// tdd-accel-sound-input §5; MAME z84c015.cpp, tmpz84c015.cpp).
///
/// Ports (any high byte): #EE SCRP (register pointer), #EF SCDP (data) for
/// WCR (wait states, 0), MWBR (memory wait boundary, 1), CSBR (chip-select
/// boundary, 2), MCR (CS enables, 3); #F0 WDTMR, #F1 WDTCR (watchdog), #F4
/// interrupt priority.
///
/// The chip selects matter only while the PLD loads its configuration: CS0
/// covers #0000 up to (CSBR[3:0] + 1) x #1000 when MCR bit 0 is set, the rest
/// is the fast RAM (the loader sets CSBR = #FE: #F000-#FFFF; a reload request
/// sets #F0: #1000-#FFFF, where the BIOS left the new bitstream).
///
/// The watchdog is stored, not run: BIOS 3.04 never programs #F0/#F1 (S1
/// check of the page 8 / page 0 / SETUP listings) and MAME leaves the WDT
/// output unconnected on the Sprinter.
class Z84SystemRegs
{
public:
    /// Power-on values (MAME z84c015 device_start, tmpz84c015 device_reset)
    void PowerOn()
    {
        scrp = 0;
        wcr = 0x00;
        mwbr = 0xF0;
        csbr = 0xFF;
        mcr = 0x01;
        wdtmr = 0xFB;
        wdtcr = 0;
        irqPriority = 0;
    }

    uint8_t Read(uint8_t port) const
    {
        switch (port)
        {
            case 0xEE: return scrp;
            case 0xEF:
            {
                const uint8_t regs[4] = {wcr, mwbr, csbr, mcr};
                return scrp < 4 ? regs[scrp] : 0xFF;
            }
            case 0xF0: return wdtmr;
            default: return 0xFF;
        }
    }

    void Write(uint8_t port, uint8_t value)
    {
        switch (port)
        {
            case 0xEE: scrp = value; break;
            case 0xEF:
                if (scrp == 0) wcr = value;
                else if (scrp == 1) mwbr = value;
                else if (scrp == 2) csbr = value;
                else if (scrp == 3) mcr = value;
                break;
            case 0xF0: wdtmr = value; break;
            case 0xF1: wdtcr = value; break;
            case 0xF4: irqPriority = value; break;
            default: break;
        }
    }

    /// First address outside CS0 (#10000 = CS0 covers everything)
    uint32_t Cs0End() const
    {
        if (!(mcr & 0x01))
            return 0;
        return ((csbr & 0x0Fu) + 1u) * 0x1000u;
    }

    uint8_t scrp = 0;
    uint8_t wcr = 0x00;
    uint8_t mwbr = 0xF0;
    uint8_t csbr = 0xFF;
    uint8_t mcr = 0x01;
    uint8_t wdtmr = 0xFB;
    uint8_t wdtcr = 0;
    uint8_t irqPriority = 0;
};
