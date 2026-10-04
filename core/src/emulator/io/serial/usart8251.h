#pragma once
/// @file usart8251.h
/// @brief Intel 8251A / KR580VV51A USART, asynchronous mode, and the peer on its line.
///
/// Two registers: data (C/D = 0) and control (C/D = 1: mode / sync characters / command words on write, status on
/// read). After a reset (the RESET pin, or the command word's IR bit) the control register takes a mode word: baud
/// factor (bits 1-0: 01 x1, 10 x16, 11 x64; 00 = synchronous), character length (bits 3-2: 5..8), parity enable /
/// even (bits 4 / 5), stop bits (bits 7-6: 01 one, 10 one and a half, 11 two). A synchronous mode word is followed
/// by one or two sync characters (bit 7 SCS); then every write is a command word: TxEN (d0), DTR (d1), RxE (d2),
/// SBRK (d3), ER (d4, clears PE / OE / FE), RTS (d5), IR (d6, internal reset), EH (d7). The usual internal reset
/// sequence, 3 x #00 then #40, works from either state: a mode word #00 (synchronous, two sync characters) eats the
/// next two #00 and #40 is then a command with IR.
///
/// Status (read): TxRDY d0 (the transmit buffer is empty - the status bit is not conditioned by CTS / TxEN, the pin
/// is), RxRDY d1, TxEMPTY d2 (buffer and shifter empty), PE d3, OE d4, FE d5, SYNDET / BRKDET d6, DSR d7 (the
/// input asserted).
///
/// Line: a character takes (start + data + parity + stop) bits x the baud factor x the TxC / RxC period; both
/// clocks come from one source (ClockSource: the ZX Profi's 8253 counter 0). A byte written moves to the shifter
/// while TxEN is set and CTS is asserted and goes to the peer when its last stop bit has left; the peer starts a
/// byte while RTS is asserted (or it ignores RTS) and the receiver has it a character time later - RxRDY, or OE
/// when the previous byte was not read (the new byte replaces it). The peer's bytes have no errors, so PE / FE
/// never set. Not modeled: the synchronous mode (no byte moves; the mode words are taken), the break (SBRK, break
/// detect), the TxRDY / RxRDY / TxEMPTY pins.
///
/// Time: absolute base-clock T-states passed in by the caller (the emulator's 3.5 MHz T-states). Nothing runs on its
/// own: every register access and the frame boundary call Advance(now), which moves the bytes whose time has come.
/// The result depends only on the times of those calls and on what the peer holds, so a TTD replay repeats it.

#include <cstdint>
#include <functional>

#include "common/network/nettypes.h"

class ISerialPeer;

class Usart8251
{
public:
    /// Register select (C/D)
    static constexpr uint8_t kData = 0;
    static constexpr uint8_t kControl = 1;

    // Status bits
    static constexpr uint8_t kTxRdy = 0x01, kRxRdy = 0x02, kTxEmpty = 0x04, kPe = 0x08, kOe = 0x10, kFe = 0x20,
                             kSynDet = 0x40, kDsr = 0x80;
    // Command bits
    static constexpr uint8_t kTxEn = 0x01, kDtr = 0x02, kRxE = 0x04, kSbrk = 0x08, kEr = 0x10, kRts = 0x20, kIr = 0x40,
                             kEh = 0x80;

    /// The TxC / RxC clock: hz / divisor (divisor 0: no clock, nothing moves)
    struct ClockRate
    {
        uint32_t hz = 0;
        uint32_t divisor = 0;
    };
    using ClockSource = std::function<ClockRate()>;

    /// What the control register expects next
    enum class Expect : uint8_t
    {
        Mode = 0,
        Sync1 = 1,
        Sync2 = 2,
        Command = 3,
    };

    /// The chip (the TTD blob, PeripheralId::Usart8251): fixed size, trivially copyable
    struct State
    {
        uint8_t mode = 0;          ///< the mode word
        uint8_t command = 0;       ///< the command word (IR and ER are actions, not kept)
        uint8_t expect = 0;        ///< Expect
        uint8_t sync1 = 0, sync2 = 0;
        uint8_t errors = 0;        ///< PE / OE / FE in their status positions
        uint8_t rxData = 0;        ///< the receive buffer
        uint8_t rxReady = 0;
        uint8_t txBuffer = 0;      ///< the transmit buffer
        uint8_t txFull = 0;
        uint8_t txShift = 0;       ///< the character on the TX line
        uint8_t txBusy = 0;
        uint8_t rxShift = 0;       ///< the character coming in on the RX line
        uint8_t rxBusy = 0;
        uint8_t boardLatch = 0;    ///< a board register kept with the chip (the ZX Profi's COM control register #B3)
        uint8_t reserved = 0;
        uint64_t txDoneAt = 0;     ///< the TX character's last stop bit leaves (base T)
        uint64_t rxDoneAt = 0;     ///< the RX character is complete (base T)
        uint64_t lastNow = 0;
        uint64_t bytesIn = 0, bytesOut = 0, overruns = 0;
    };
    static_assert(sizeof(State) == 16 + 6 * 8, "Usart8251::State layout changed");

    explicit Usart8251(uint32_t baseClockHz = 3500000);

    void SetClockSource(ClockSource source) { _clock = std::move(source); }
    /// Attach the other end (nullptr: nothing on the connector, every modem input inactive); it is told the line
    /// format and the modem lines at once
    void SetPeer(ISerialPeer* peer);
    ISerialPeer* Peer() const { return _peer; }

    /// The RESET pin: idle, the control register expects a mode word, the command cleared (DTR / RTS inactive)
    void Reset(uint64_t now);

    void Write(uint8_t reg, uint8_t value, uint64_t now);
    uint8_t Read(uint8_t reg, uint64_t now);

    /// Move the bytes whose time has come (the caller's clock, base T-states). A clock that went back moves the chip
    /// onto the new time base: a character on the line keeps its remaining time
    void Advance(uint64_t now);

    /// The status register as a read would return it (no side effects)
    uint8_t Status() const;
    bool Rts() const { return (_state.command & kRts) != 0; }
    bool Dtr() const { return (_state.command & kDtr) != 0; }
    bool IsAsync() const { return (_state.mode & 0x03) != 0; }
    /// Data bits (5..8), the baud factor (1 / 16 / 64), stop bits in halves (2 / 3 / 4)
    uint8_t DataBits() const { return static_cast<uint8_t>(5 + ((_state.mode >> 2) & 0x03)); }
    uint32_t BaudFactor() const;
    uint8_t StopHalfBits() const;
    /// The line rate (0: no clock or not asynchronous) and one character's length in base T-states (0: none)
    uint32_t Baud() const;
    uint32_t FrameBits() const;
    uint64_t CharacterT() const;
    /// The line format as programmed (a host serial device follows it); 1.5 stop bits are reported as 2
    SerialLine Line() const;

    /// The peer's modem lines as the board sees them (a loopback plug's wires: DTR to DSR / DCD, RTS to CTS / RI)
    bool CtsIn() const;
    bool DsrIn() const;
    bool DcdIn() const;
    bool RiIn() const;

    const State& GetState() const { return _state; }
    void SetState(const State& state);
    void SetBoardLatch(uint8_t value) { _state.boardLatch = value; }
    uint8_t BoardLatch() const { return _state.boardLatch; }

private:
    void InternalReset();
    void WriteControl(uint8_t value, uint64_t now);
    void StartTx(uint64_t at);
    void StartRx(uint64_t at);
    void TellLine();
    void TellModemLines();
    uint8_t DataMask() const { return static_cast<uint8_t>(0xFF >> (8 - DataBits())); }

    State _state;
    uint32_t _baseHz;
    ClockSource _clock;
    ISerialPeer* _peer = nullptr;
    SerialLine _toldLine{0, 0, 0, 0};   ///< what the peer was told last (not state: the peer is told again on restore)
    bool _toldRts = false, _toldDtr = false;
};
