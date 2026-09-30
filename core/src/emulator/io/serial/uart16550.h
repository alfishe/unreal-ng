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

class Uart16550
{
public:
    enum class Flavor : uint8_t
    {
        EvoAvr,   ///< ZX-Evo: AVR firmware behind the FPGA
        ZxWifi,   ///< ZX-WiFi card: a real 16550
    };

    /// Register offsets (A10..A8)
    static constexpr uint8_t kRbrThr = 0, kIer = 1, kIirFcr = 2, kLcr = 3, kMcr = 4, kLsr = 5, kMsr = 6, kScr = 7;

    // LSR bits
    static constexpr uint8_t kLsrDr = 0x01, kLsrOe = 0x02, kLsrThre = 0x20, kLsrTemt = 0x40;
    // MCR bits
    static constexpr uint8_t kMcrDtr = 0x01, kMcrRts = 0x02, kMcrOut1 = 0x04, kMcrOut2 = 0x08, kMcrLoop = 0x10,
                             kMcrAfe = 0x20;
    // MSR bits
    static constexpr uint8_t kMsrDcts = 0x01, kMsrDdsr = 0x02, kMsrTeri = 0x04, kMsrDdcd = 0x08, kMsrCts = 0x10,
                             kMsrDsr = 0x20, kMsrRi = 0x40, kMsrDcd = 0x80;

    static constexpr int kFifoSize = 16;

    /// What differs between the flavors
    struct Params
    {
        Flavor flavor = Flavor::ZxWifi;
        uint32_t uartClockHz = 1843200;  ///< baud = clock / (16 * divisor)
        uint8_t mcrMask = 0x3F;          ///< MCR bits that exist (Evo: & #1F, no auto flow)
        bool interrupts = true;          ///< IER / IIR work (Evo: none)
        uint32_t accessWaitT = 0;        ///< extra Z80 T-states per access (Evo: the AVR serves it)
    };
    static Params DefaultParams(Flavor flavor);

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

    /// The line format as programmed (a host serial device follows it). Evo:
    /// stick parity is not supported (plain parity), 8N2 until LCR is written
    SerialLine Line() const;

    /// Registers without side effects (status views, TTD)
    struct View
    {
        uint8_t ier = 0, iir = 0, fcr = 0, lcr = 0, mcr = 0, lsr = 0, msr = 0, scr = 0;
        uint16_t divisor = 0;
        uint8_t rxCount = 0, txCount = 0;
        bool txBusy = false;
        uint64_t bytesIn = 0, bytesOut = 0, overruns = 0;
    };
    View GetView() const;

    /// TTD state (fixed size, trivial; netstate::Com holds it)
    struct State
    {
        uint8_t ier, fcr, lcr, mcr, lsr, msr, scr, dll, dlm;
        uint8_t rxCount, rxHead, txCount, txHead;
        uint8_t txShift, txBusy, rxShift, rxInFlight, msrLines, thrInt, lcrWritten;
        uint8_t reserved[2];
        uint8_t rx[kFifoSize];
        uint8_t tx[kFifoSize];
        uint64_t txDoneAt, rxArriveAt, lastNow;
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
    uint8_t Iir() const;
    uint8_t RxTriggerLevel() const;
    void NotifyLine();   ///< tell the peer the line format when it changed

    Params _params;
    uint32_t _baseClockHz = 3500000;
    ISerialPeer* _peer = nullptr;

    uint8_t _ier = 0, _fcr = 0, _lcr = 0, _mcr = 0, _lsr = 0, _msr = 0, _scr = 0, _dll = 0, _dlm = 0;
    std::array<uint8_t, kFifoSize> _rx{};
    std::array<uint8_t, kFifoSize> _tx{};
    uint8_t _rxCount = 0, _rxHead = 0, _txCount = 0, _txHead = 0;
    uint8_t _txShift = 0;
    bool _txBusy = false;          ///< a character is on the TX line, done at _txDoneAt
    uint8_t _rxShift = 0;
    bool _rxInFlight = false;      ///< the peer started a character, it arrives at _rxArriveAt
    bool _thrInterrupt = false;    ///< THRE interrupt pending (cleared by reading IIR or writing THR)
    bool _lcrWritten = false;      ///< Evo: the AVR's USART runs 8N2 until the first LCR write
    uint8_t _msrLines = 0;         ///< CTS/DSR/RI/DCD last seen (for the delta bits)
    uint64_t _txDoneAt = 0;
    uint64_t _rxArriveAt = 0;
    uint64_t _lastNow = 0;
    uint64_t _bytesIn = 0, _bytesOut = 0, _overruns = 0;
};
