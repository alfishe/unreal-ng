#pragma once

#include <cstdint>

#include "emulator/io/z84c15/z84ctc.h"
#include "emulator/io/z84c15/z84pio.h"
#include "emulator/io/z84c15/z84sio.h"
#include "emulator/io/z84c15/z84systemregs.h"

/// @file z84c15.h
/// @brief The Zilog Z84C15's on-chip devices as one package (Sprinter
/// tdd-accel-sound-input §5): a reusable piece, the Sprinter is its first user.
///
/// The chip decodes its own ports on the low address byte only, before any
/// board logic sees the cycle: #10-#13 CTC, #18-#1B SIO, #1C-#1F PIO, #EE/#EF
/// system control, #F0/#F1 watchdog, #F4 interrupt priority. So on the
/// Sprinter port #1F belongs to the PIO, not to the WD1793 or the joystick.
struct Z84C15
{
    Z84Ctc ctc;
    Z84Sio sio;
    Z84Pio pio;
    Z84SystemRegs system;

    /// Whether the chip answers this low address byte itself
    static bool Owns(uint8_t lowByte)
    {
        return (lowByte >= 0x10 && lowByte <= 0x13) || (lowByte >= 0x18 && lowByte <= 0x1F) || lowByte == 0xEE ||
               lowByte == 0xEF || lowByte == 0xF0 || lowByte == 0xF1 || lowByte == 0xF4;
    }

    uint8_t Read(uint8_t lowByte)
    {
        if (lowByte >= 0x10 && lowByte <= 0x13)
            return ctc.Read(lowByte & 3);
        if (lowByte >= 0x18 && lowByte <= 0x1B)
            return sio.Read(lowByte);
        if (lowByte >= 0x1C && lowByte <= 0x1F)
            return pio.Read(lowByte & 3);
        return system.Read(lowByte);
    }

    void Write(uint8_t lowByte, uint8_t value)
    {
        if (lowByte >= 0x10 && lowByte <= 0x13)
            ctc.Write(lowByte & 3, value);
        else if (lowByte >= 0x18 && lowByte <= 0x1B)
            sio.Write(lowByte, value);
        else if (lowByte >= 0x1C && lowByte <= 0x1F)
            pio.Write(lowByte & 3, value);
        else
            system.Write(lowByte, value);
    }

    /// The CPU's /RESET: the peripherals restart; the system control registers
    /// keep their values (MAME sets them at device start only)
    void Reset()
    {
        ctc.Reset();
        sio.Reset();
        pio.Reset();
    }

    void PowerOn()
    {
        Reset();
        system.PowerOn();
    }
};
