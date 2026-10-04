#pragma once

/// @file uart16550.h
/// @brief A 16550-compatible UART as the Z80 sees it (network adapters TDD
/// §7.1): registers, 16-byte FIFOs, one character per character time in
/// emulated T-states, RTS / CTS, loopback. Two flavors behind the same ports
/// (#F8EF..#FFEF, register = A10..A8):
///  - ZX-Evo BaseConf: the AVR emulates the chip (flavor EvoAvr, see Params)
///  - ZX-WiFi: a real 16550 on the ZX-Bus (flavor ZxWifi)
///
/// Time: absolute base-clock T-states (emulatorState.t_states + Z80 t scaled
/// back by the turbo multiplier), passed in by the caller. Nothing runs on its
/// own: every register access and the frame boundary call Advance(now), which
/// moves the bytes whose time has come. The result depends only on the times
/// of those calls and on what the peer holds, so a TTD replay repeats it.

#include <array>
#include <cstdint>
#include <functional>

#include "common/network/nettypes.h"

class ISerialPeer;

/// Where an #xxEF access lands besides the eight 16550 registers (0..7): the
/// firmware and the FPGA decide (ZX-Evo with a TS-Labs AVR firmware differs)
struct ComPortRegister
{
    static constexpr int kDataRegion = -1;    ///< TS ZiFi data area (AVR index #00): ZiFi or the RS-232 rings
    static constexpr int kNothing = -3;       ///< nothing answers: reads #00
    static constexpr int kZiFiBase = -32;     ///< kZiFiBase + n: TS ZiFi register #C0 + n (n = 0..15)
    static constexpr bool IsZiFi(int reg) { return reg >= kZiFiBase && reg < kZiFiBase + 16; }
};

class Uart16550
{
public:
    enum class Flavor : uint8_t
    {
        EvoAvr,      ///< ZX-Evo: the AVR firmware emulates the register set
        Chip16550,   ///< a real 16550 / 16C550 (the ZX-WiFi card)
    };

    /// ZX-Evo AVR firmwares, each with its own UART emulation (research:
    /// reference-evo-com-port.md §9). Dates are the releases that changed it
    enum class AvrFirmware : uint8_t
    {
        Base2010,      ///< NedoPC 2010-10 .. 2011-04-02: a register file, no transfer
        Base2011Apr,   ///< 2011-04-26 (r374): first working UART
        Base2011May,   ///< 2011-05-11 (r391): DLM bit 7 = the AVR's own divisor
        Base2011Sep,   ///< 2011-09-29 (r478): RTS polarity fixed
        Base2013,      ///< 2013-11-08 (r565): an FCR RX reset clears OE
        Base2023,      ///< 2023-10-08 (r1097+, current): LSR bit 7 = RX FIFO half full
        Ts2013,        ///< TS-Labs 2013-05 .. 2016-02: 256-byte FIFOs
        Ts2016Feb,     ///< TS-Labs 2016-02-27 .. 04-11: full high byte decoding (TS-Conf FPGA)
        Ts2016Apr,     ///< TS-Labs 2016-04-12 .. now: interrupt-driven 511 / 255-byte rings
    };
    static constexpr AvrFirmware kLatestAvr = AvrFirmware::Base2023;

    /// Register offsets (A10..A8)
    static constexpr uint8_t kRbrThr = 0, kIer = 1, kIirFcr = 2, kLcr = 3, kMcr = 4, kLsr = 5, kMsr = 6, kScr = 7;

    // LSR bits
    static constexpr uint8_t kLsrDr = 0x01, kLsrOe = 0x02, kLsrThre = 0x20, kLsrTemt = 0x40, kLsrHalfFull = 0x80;
    // MCR bits
    static constexpr uint8_t kMcrDtr = 0x01, kMcrRts = 0x02, kMcrOut1 = 0x04, kMcrOut2 = 0x08, kMcrLoop = 0x10,
                             kMcrAfe = 0x20;
    // MSR bits
    static constexpr uint8_t kMsrDcts = 0x01, kMsrDdsr = 0x02, kMsrTeri = 0x04, kMsrDdcd = 0x08, kMsrCts = 0x10,
                             kMsrDsr = 0x20, kMsrRi = 0x40, kMsrDcd = 0x80;

    static constexpr int kFifoSize = 16;     ///< a 16550's FIFOs
    static constexpr int kMaxRx = 512;       ///< the largest receive ring (TS 2016-04: 511 usable)
    static constexpr int kMaxTx = 256;       ///< the largest transmit ring (TS 2013: 256)

    /// What differs between the chip and the AVR firmwares
    struct Params
    {
        Flavor flavor = Flavor::Chip16550;
        AvrFirmware avr = kLatestAvr;
        uint32_t uartClockHz = 1843200;  ///< baud = clock / (16 * divisor)
        uint8_t mcrMask = 0x3F;          ///< MCR bits that exist (AVR: & #1F, no auto flow control)
        bool interrupts = true;          ///< IER / IIR work (AVR: none)
        /// Only CTS reaches the chip; DSR' and DCD' are tied asserted, RI' tied inactive (the ATM2IOESP card:
        /// reference-atm2ioesp.md open question 5). The peer's DSR / DCD / RI are not seen
        bool ctsOnly = false;
        /// Which modem inputs reach the chip from the peer (MSR layout: CTS #10, DSR #20, RI #40, DCD #80), and the
        /// level the others read (1 = asserted). A pin nobody drives (SprinterSerial's COM1 behind a CH340 that
        /// wires them input to input) reads its unwired level. Default: all four from the peer
        uint8_t msrWired = 0xF0;
        uint8_t msrUnwired = 0x00;
        /// The PC16552D's Alternate Function Register: with DLAB set, register 2 is AFR (bit 0 concurrent write,
        /// bits 2-1 the MF pin's function) instead of IIR / FCR
        bool afr = false;
        /// The receiver's frame length in bits, 0: the programmed line's. A line whose two ends differ (the TS AVR's
        /// ZiFi USART sends 8N2, the ESP 8N1: reference-zifi.md §2.1)
        uint8_t rxFrameBits = 0;

        // AVR firmware behavior
        bool dataPath = true;            ///< false: the 2010 register file (no byte moves)
        uint16_t rxDepth = kFifoSize;    ///< receive FIFO / ring capacity
        uint16_t txDepth = kFifoSize;
        bool oeClearedByFcr = true;      ///< an FCR RX reset clears OE (AVR before 2013: only a restart)
        bool halfFullBit = false;        ///< LSR bit 7 = 8+ bytes waiting (NedoPC 2023)
        bool threNotFull = false;        ///< THRE = TX ring not full (TS 2016-04); else "empty"
        bool temtIsTxc = false;          ///< TEMT = the USART's TXC: 0 until the first byte went out, then 1
        uint32_t divisor0Baud = 345600;  ///< AVR: divisor 0 ("256000" in the source, 345600 in fact; TS 2016-04: 230400)
        bool rawUbrr = true;             ///< DLM bit 7: the AVR's own divisor (from 2011-05)
        bool divisorResets = true;       ///< DLL 1 / DLM 0 at reset (from r377); before: 0 / 0
        bool rtsInverted = false;        ///< before 2011-09: MCR bit 1 set drove RTS inactive

        // AVR timing (the Z80 waits on /WAIT while the AVR serves an access;
        // research: reference-evo-com-port.md §3)
        uint32_t avrClockHz = 11059200;  ///< the ATmega128's crystal (Q2 11.059 MHz)
        uint16_t isrCycles = 37;         ///< INT6: the wait flag noted
        uint16_t loopCycles = 260;       ///< one main-loop pass: the flag is looked at once per pass
        /// Wait-flag tests per pass: BaseConf tests it once, after the last task; the TS firmware since
        /// 2016-03 (9a3b541b "ISRed ZiFi-UART") calls waittask() after each of its 8 tasks (TS-AVR main.c:414-431),
        /// so a wait is picked up at the next task boundary [inferred: the pass split evenly]
        uint8_t waitChecksPerLoop = 1;
        uint16_t serviceWrite = 258;     ///< SPI #42 + #40 and the register write
        uint16_t serviceRead = 278;      ///< a register read
        uint16_t serviceRbr = 308;       ///< a receive-buffer read
    };
    static Params DefaultParams(Flavor flavor);
    static Params EvoAvrParams(AvrFirmware firmware);
    /// Config names: BASE2010, BASE2011-04, BASE2011-05, BASE2011-09, BASE2013,
    /// BASE2023 (= BASECONF, the default), TS2013, TS2016-02, TS2016-04 (= TS)
    static bool ParseAvrFirmware(const char* text, AvrFirmware& out);
    static const char* AvrFirmwareName(AvrFirmware firmware);

    Uart16550(const Params& params, uint32_t baseClockHz);

    /// Attach the other end; it is told the current line format at once
    void SetPeer(ISerialPeer* peer);
    ISerialPeer* Peer() const { return _peer; }
    const Params& GetParams() const { return _params; }

    /// Power-on / ZX-Bus reset: registers to their reset values, FIFOs empty
    void Reset();

    uint8_t Read(uint8_t reg, uint64_t now);
    void Write(uint8_t reg, uint8_t value, uint64_t now);

    /// Move the bytes whose time has come (the caller's clock, base T-states).
    /// A clock that went back (the machine's counter restarts on a reset or a
    /// snapshot load) moves the UART onto the new time base (Rebase)
    void Advance(uint64_t now);

    /// The earliest time (the caller's clock) at which Advance would change something by itself: a received
    /// character lands, a transmitted one leaves the shifter (THRE / the next byte). UINT64_MAX: nothing on the
    /// line. A byte the peer holds but has not started yet starts at the next Advance, whenever that is
    uint64_t NextEventAt() const;
    /// Called at the end of every Advance (also the ones inside Read / Write): the interrupt output or
    /// NextEventAt may have changed. A card that wires INTR to a bus listens (the Sprinter's ISA slot)
    std::function<void()> onAdvance;

    /// Put the UART's times on a new clock: `now` is the same instant as the
    /// last time it saw; a character on the line keeps its remaining time
    void Rebase(uint64_t now);

    /// The chip's interrupt output (IIR bit 0 clear and MCR OUT2 set, as on a
    /// PC card). ZX-WiFi only; the Evo AVR has none
    bool InterruptActive() const;
    /// The INTR pin itself (an interrupt pending, whatever OUT2 says): a card that wires INTR straight to its bus
    /// (the SprinterESP: INTR to ISA IRQ3, OUT2 drives the ESP's GPIO0)
    bool IntrPin() const;

    /// The -OUT1 / -OUT2 pins changed (true = asserted: the MCR bit set, outside loopback mode, where the chip
    /// holds both pins inactive). A card wires them as it likes (the SprinterESP: OUT1 resets the ESP, OUT2 pulls
    /// its GPIO0 low); a state restore does not call it
    std::function<void(bool out1, bool out2)> onAuxLines;
    /// The PC16552D's AFR (Params::afr): one register both channels share - the card writes it into both
    uint8_t Afr() const { return _afr; }
    void SetAfr(uint8_t value) { _afr = static_cast<uint8_t>(value & 0x07); }
    /// The pins as they are now (same rule)
    bool Out1() const { return !Evo() && (_mcr & (kMcrOut1 | kMcrLoop)) == kMcrOut1; }
    bool Out2() const { return !Evo() && (_mcr & (kMcrOut2 | kMcrLoop)) == kMcrOut2; }

    /// The TS firmware's direct ring access (the ZiFi data register on the RS-232 rings, the ZiFi line itself:
    /// reference-zifi.md §2.3): no register side effects
    uint16_t RxUsed() const { return _rxCount; }
    uint16_t TxFree() const { return static_cast<uint16_t>(TxDepth() > _txCount ? TxDepth() - _txCount : 0); }
    /// Pop a received byte, #FF when none
    uint8_t DataRead(uint64_t now);
    /// Queue a byte to send; dropped when the ring is full (THRE / TEMT clear when it fills)
    void DataWrite(uint8_t value, uint64_t now);
    /// Empty the rings (a byte already on the line still goes out); LSR is not touched
    void ClearRx();
    void ClearTx();

    /// Called for every received byte with its arrival time (base T-states), after it is in the ring
    std::function<void(uint64_t at)> onRxByte;

    /// Current line: baud rate from the divisor, frame bits (start + data +
    /// parity + stop)
    uint32_t Baud() const;
    uint32_t FrameBits() const;
    /// T-states one character takes on the line
    uint64_t CharacterT() const;

    /// AVR: how long the Z80 waits for this access, in AVR clock cycles (0 for
    /// the chip): the interrupt, the main loop reaching the flag (1..loop
    /// cycles, counted from the previous release), the service. `now` in base T-states
    uint32_t AccessCycles(uint8_t reg, bool read, uint64_t now);

    /// The line format as programmed (a host serial device follows it). Evo:
    /// stick parity is not supported (plain parity), 8N2 until LCR is written
    SerialLine Line() const;

    /// Registers without side effects (status views, TTD)
    struct View
    {
        uint8_t ier = 0, iir = 0, fcr = 0, lcr = 0, mcr = 0, lsr = 0, msr = 0, scr = 0;
        uint16_t divisor = 0;
        uint8_t afr = 0;   ///< PC16552D (Params::afr)
        uint16_t rxCount = 0, txCount = 0;
        bool txBusy = false;
        uint64_t bytesIn = 0, bytesOut = 0, overruns = 0;
    };
    View GetView() const;

    /// TTD state (fixed size, trivial)
    struct State
    {
        uint8_t ier, fcr, lcr, mcr, lsr, msr, scr, dll, dlm;
        uint8_t txShift, txBusy, rxShift, rxInFlight, msrLines, thrInt, lcrWritten;
        uint8_t txc, stubIir, afr, reserved[1];
        uint16_t rxCount, rxHead, txCount, txHead;
        uint8_t rx[kMaxRx];
        uint8_t tx[kMaxTx];
        uint64_t txDoneAt, rxArriveAt, lastNow, avrRelease;
        uint64_t bytesIn, bytesOut, overruns;
    };
    void SaveState(State& out) const;
    void LoadState(const State& in);

private:
    void PushRx(uint8_t byte);
    uint8_t PopRx();
    void UpdateModemStatus();
    bool RtsAsserted() const;
    bool CtsForTx() const;
    bool Evo() const { return _params.flavor == Flavor::EvoAvr; }
    uint16_t RxDepth() const;
    uint16_t TxDepth() const;
    uint8_t ReadStub(uint8_t reg) const;
    void WriteStub(uint8_t reg, uint8_t value);
    uint8_t Iir() const;
    uint8_t LsrValue() const;   ///< LSR as read (DR, HF, THRE / TEMT by firmware)
    uint8_t RxTriggerLevel() const;
    void NotifyLine();   ///< tell the peer the line format when it changed

    Params _params;
    uint32_t _baseClockHz = 3500000;
    ISerialPeer* _peer = nullptr;

    uint8_t _ier = 0, _fcr = 0, _lcr = 0, _mcr = 0, _lsr = 0, _msr = 0, _scr = 0, _dll = 0, _dlm = 0;
    std::array<uint8_t, kMaxRx> _rx{};
    std::array<uint8_t, kMaxTx> _tx{};
    uint16_t _rxCount = 0, _rxHead = 0, _txCount = 0, _txHead = 0;
    uint8_t _txShift = 0;
    bool _txBusy = false;          ///< a character is on the TX line, done at _txDoneAt
    uint8_t _rxShift = 0;
    bool _rxInFlight = false;      ///< the peer started a character, it arrives at _rxArriveAt
    bool _thrInterrupt = false;    ///< THRE interrupt pending (cleared by reading IIR or writing THR)
    bool _lcrWritten = false;      ///< Evo: the AVR's USART runs 8N2 until the first LCR write
    bool _txc = false;             ///< the USART's transmit-complete flag (TS 2016-04 TEMT)
    uint64_t _avrRelease = 0;      ///< AVR cycle (absolute) when the last access was released
    uint8_t _stubIir = 0x01;       ///< the 2010 register file: what reg 2 last held
    uint8_t _afr = 0;              ///< PC16552D Alternate Function Register (Params::afr)
    uint8_t _msrLines = 0;         ///< CTS/DSR/RI/DCD last seen (for the delta bits)
    uint64_t _txDoneAt = 0;
    uint64_t _rxArriveAt = 0;
    uint64_t _lastNow = 0;
    uint64_t _bytesIn = 0, _bytesOut = 0, _overruns = 0;
};
