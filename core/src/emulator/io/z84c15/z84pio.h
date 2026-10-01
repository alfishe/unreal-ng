#pragma once

#include <cstdint>

/// @file z84pio.h
/// @brief The Z84C15's PIO as a register file (Sprinter tdd-accel-sound-input §5; Zilog Z80 PIO data sheet).
///
/// Ports (any high byte, MAME read_alt order): #1C port A data, #1D port A
/// control, #1E port B data, #1F port B control. Control words: mode
/// (bits 3-0 = %1111, mode in bits 7-6; mode 3 takes a direction byte next),
/// interrupt control (bits 3-0 = %0111, a mask follows when bit 4 = 1), the
/// vector (bit 0 = 0). A data read returns the output latch on output lines
/// and the inputs (#FF: nothing wired in v1 - ISA IRQ / DRQ inactive) on input
/// lines. The BIOS shows its POST progress codes on port A (OUT (#1C)).
///
/// Worked example (BIOS 3.04 #0154): OUT (#1D),#CF = mode 3 (bit control),
/// OUT (#1D),#00 = all port A lines outputs, OUT (#1C),#EA = POST code: a read
/// of #1C returns #EA.
class Z84Pio
{
public:
    struct Port
    {
        uint8_t mode = 1;            ///< 0 output, 1 input, 2 bidirectional, 3 bit control
        uint8_t direction = 0xFF;    ///< mode 3: 1 = input line
        uint8_t output = 0;
        uint8_t vector = 0;
        uint8_t intControl = 0;
        uint8_t mask = 0xFF;
        uint8_t next = 0;            ///< 1 = a direction byte follows, 2 = a mask follows
        uint8_t inputs = 0xFF;       ///< the lines' input levels
    };

    void Reset();

    uint8_t Read(uint8_t port);
    void Write(uint8_t port, uint8_t value);

    const Port& GetPort(uint8_t p) const { return _port[p & 1]; }
    void SetInputs(uint8_t p, uint8_t value) { _port[p & 1].inputs = value; }

private:
    uint8_t ReadData(uint8_t p) const;
    void WriteControl(uint8_t p, uint8_t value);

    Port _port[2];
};
