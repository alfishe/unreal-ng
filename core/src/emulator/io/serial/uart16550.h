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

#include "common/network/nettypes.h"

class ISerialPeer;

/// Where an #xxEF access lands besides the eight 16550 registers (0..7): the
/// firmware and the FPGA decide (ZX-Evo with a TS-Labs AVR firmware differs)
struct ComPortRegister
{
    static constexpr int kDataRegion = -1;    ///< TS ZiFi data area: reads #FF, writes dropped (API off)
    static constexpr int kZiFiRegister = -2;  ///< TS ZiFi control registers: #FF with the API off
    static constexpr int kNothing = -3;       ///< nothing answers: reads #00
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

    /// Put the UART's times on a new clock: `now` is the same instant as the
    /// last time it saw; a character on the line keeps its remaining time
    void Rebase(uint64_t now);

    /// The chip's interrupt output (IIR bit 0 clear and MCR OUT2 set, as on a
    /// PC card). ZX-WiFi only; the Evo AVR has none
    bool InterruptActive() const;

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
        uint8_t txc, stubIir, reserved[2];
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
    uint8_t _msrLines = 0;         ///< CTS/DSR/RI/DCD last seen (for the delta bits)
    uint64_t _txDoneAt = 0;
    uint64_t _rxArriveAt = 0;
    uint64_t _lastNow = 0;
    uint64_t _bytesIn = 0, _bytesOut = 0, _overruns = 0;
};
