#pragma once

/// @file z80pio.h
/// @brief The Zilog Z80 PIO (Z8420) as a register file with its interrupt logic: two 8-bit ports, the four modes,
/// the handshake strobes, the interrupt vector and the internal daisy chain (port A above port B).
///
/// Sources: Zilog Z80 Family CPU Peripherals User Manual UM0081, "Z80 PIO" chapter (programming, mode descriptions,
/// interrupt servicing); MAME src/devices/machine/z80pio.cpp (register behaviour, reset state), the reference every
/// other emulator follows. Where the two differ the choice is noted at the member.
///
/// Control words (to the control register of a port, UM0081 "Programming the PIO"):
///   - bit 0 = 0: the interrupt vector (bits 7-1, bit 0 is 0 on the bus);
///   - xx001111 (low nibble %1111): the mode in bits 7-6 (0 output, 1 input, 2 bidirectional - port A only, 3 bit
///     control); mode 3 takes the I/O direction byte next (1 = input line);
///   - xxxx0111: the interrupt control word: bit 7 enable, 6 AND (else OR), 5 active high (else low), 4 the mask
///     follows (the next word: 1 = the line is not monitored, mode 3 only). A mask that follows clears a pending
///     interrupt and holds the enable off until the mask is written (MAME);
///   - xxxx0011: the interrupt enable flip-flop alone, bit 7.
///
/// Handshake. Mode 1 (input): /STB low latches the port's lines into the input register; its rising edge sets the
/// port's interrupt pending flag (IP) and drops RDY; a CPU read of the data register raises RDY again. Mode 0
/// (output): a data write latches the output and raises RDY; the strobe's rising edge (the peripheral took the byte)
/// sets IP and drops RDY. Mode 3: an interrupt when the monitored input lines first meet the AND / OR, high / low
/// condition. IP is set by the handshake even with interrupts disabled (MAME): enabling them later interrupts at once.
///
/// Interrupts (UM0081 "Interrupt Servicing"). The PIO pulls /INT while a port has IP with its enable on and nothing
/// above it is under service. In the acknowledge cycle (M1 with IORQ) the highest such port puts its vector on the
/// bus and its IP becomes IUS (interrupt under service), which holds IEO low: every device below it in the daisy
/// chain - and port B below port A - waits. The PIO decodes RETI (ED 4D) on the data bus: the port under service
/// whose IEI is high (the highest one) clears IUS. Reset (the PIO has no /RESET pin: M1 without RD and IORQ, or
/// power on): both ports in mode 1, interrupts off, IP / IUS clear, the mask all ones, the output register zero, RDY
/// low (MAME pio_port::reset); the vector registers keep their value (UM0081: "the vector registers are not reset").

#include <cstddef>
#include <cstdint>

class Z80Pio
{
public:
    enum Mode : uint8_t
    {
        kModeOutput = 0,
        kModeInput = 1,
        kModeBidirectional = 2,
        kModeBitControl = 3,
    };

    /// What the next control word of a port is
    enum NextWord : uint8_t
    {
        kNextAny = 0,
        kNextDirection = 1,  ///< mode 3's I/O direction byte
        kNextMask = 2,       ///< the interrupt mask
    };

    /// The daisy chain state of the whole chip (IIm2DaisyDevice::DaisyState)
    static constexpr uint8_t kDaisyInt = 0x01;  ///< a port requests an interrupt (IEI high)
    static constexpr uint8_t kDaisyIeo = 0x02;  ///< a port is under service: IEO low, the devices below wait

    struct Port
    {
        uint8_t mode = kModeInput;
        uint8_t next = kNextAny;
        uint8_t input = 0xFF;       ///< the input register (modes 1, 2: latched by /STB; mode 3: the lines)
        uint8_t output = 0;         ///< the output register
        uint8_t direction = 0;      ///< mode 3: 1 = input line
        uint8_t mask = 0xFF;        ///< mode 3: 1 = the line is not monitored
        uint8_t icw = 0;            ///< the interrupt control word as written (bit 7 the enable as last set)
        uint8_t vector = 0;
        uint8_t lines = 0xFF;       ///< the levels the peripheral drives on the port's lines
        bool ie = false;            ///< interrupt enable flip-flop
        bool ip = false;            ///< interrupt pending
        bool ius = false;           ///< interrupt under service
        bool rdy = false;           ///< the RDY output
        bool stb = true;            ///< the /STB input (true = high, idle)
        bool match = false;         ///< mode 3: the condition was true at the last evaluation
    };

    static constexpr size_t kPortStateSize = 10;
    static constexpr size_t kStateSize = 2 * kPortStateSize;

    Z80Pio()
    {
        Reset();
    }

    void Reset();

    /// region <CPU side>
    /// port 0 = A, 1 = B
    uint8_t ReadData(uint8_t port);
    void WriteData(uint8_t port, uint8_t value);
    /// A control register read: the interrupt control bits, port A's bits 7-6 and port B's bits 7-4 (MAME
    /// control_read; the NMOS manual calls the register write-only)
    uint8_t ReadControl() const
    {
        return static_cast<uint8_t>((_port[0].icw & 0xC0) | (_port[1].icw >> 4));
    }
    void WriteControl(uint8_t port, uint8_t value);
    /// The data register without the read's side effect (RDY): debugger and automation reads
    uint8_t PeekData(uint8_t port) const;
    /// endregion </CPU side>

    /// region <Peripheral side>
    /// The levels the peripheral drives on the port's data lines (mode 1 latches them at /STB; mode 3 reads them)
    void SetLines(uint8_t port, uint8_t value);
    /// /STB: low (false) or high (true). A low-high pulse is SetStrobe(false) then SetStrobe(true)
    void SetStrobe(uint8_t port, bool high);
    /// One complete strobe pulse with these levels on the lines (a mode 1 peripheral writing a byte)
    void Strobe(uint8_t port, uint8_t lines)
    {
        SetLines(port, lines);
        SetStrobe(port, false);
        SetStrobe(port, true);
    }
    bool Ready(uint8_t port) const
    {
        return _port[port & 1].rdy;
    }
    /// endregion </Peripheral side>

    /// region <Daisy chain>
    /// kDaisyInt / kDaisyIeo of the chip, with IEI high
    uint8_t DaisyState() const;
    /// The acknowledge: the vector of the highest requesting port (its IP becomes IUS); false when no port requests
    bool Acknowledge(uint8_t& vector);
    /// RETI decoded with IEI high: the highest port under service ends its service
    void Reti();
    /// endregion </Daisy chain>

    const Port& GetPort(uint8_t port) const
    {
        return _port[port & 1];
    }

    /// region <State (little-endian, kStateSize bytes)>
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);
    /// endregion </State>

private:
    void ResetPort(Port& p);
    void SetMode(uint8_t index, uint8_t mode);
    /// Mode 3: evaluate the interrupt condition on the monitored lines
    void EvaluateMatch(Port& p);

    Port _port[2];
};
