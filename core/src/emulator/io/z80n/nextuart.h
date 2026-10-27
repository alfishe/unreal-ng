#pragma once

/// @file nextuart.h
/// @brief The Next's two UARTs (zxnext.vhd, serial/uart.vhd, uart_rx.vhd, uart_tx.vhd, fifop.vhd): channel 0 is the ESP module's, channel
/// 1 the Raspberry Pi's. They share four ports and a select register:
///   #133B  write: a byte into the selected UART's Tx FIFO (64 bytes); read: the status
///   #143B  read: the next Rx byte (512-byte FIFO; 0 when empty); write: the prescaler's low 14 bits (bit 7 picks the half)
///   #153B  write: bit 6 selects the UART, bit 4 with bits 2:0 = the prescaler's top 3 bits; read: the select and those bits
///   #163B  the frame: bit 7 reset (the FIFOs and the shifter, while set), 6 break, 5 hardware flow control, 4:3 data bits - 5,
///          2 parity enable, 1 parity odd, 0 two stop bits (reset value #18: 8N1)
/// Decoded by A15:A11 = 00010, A10 xor (A9 and A8) = 1, A7:A0 = #3B (zxnext.vhd port_uart), gated by NR #82-#85 bit 12.
/// The bit time is the prescaler in 28 MHz clocks (default 243 = 115200 baud). The other end of each line is an ISerialPeer (an ESP
/// module, a loopback, a host TCP endpoint); the UART owns the timing and asks the peer only at byte boundaries, as Uart16550 does.
///
/// Status (#133B read): bit 7 break, 6 framing / parity error (sticky), 5 the next Rx byte carries an error, 4 Tx empty (FIFO empty and
/// the shifter idle), 3 Rx FIFO at least 3/4 full, 2 overflow (sticky), 1 Tx FIFO full, 0 a byte is waiting. The read clears the sticky
/// bits. A byte's error flag (bit 8 of its FIFO entry) is the sticky error state when it was stored.
/// Interrupts (NR #C6, im2_int_req): Rx = near full or (a byte waiting and bit 1 of the channel's enable is clear), enabled by bits 1:0;
/// Tx = Tx FIFO empty, enabled by bit 2; every request is a rising edge of that level.
///
/// A break from the peer (uart_rx.vhd): the peer holds its line low (ISerialPeer::BreakHeld). The receiver starts a frame, samples
/// zero bits, and at the stop bit's sample point (half a bit + data bits + parity + one bit after the line fell) sees a framing
/// error: the byte is thrown away and the sticky framing status (bit 6) is set. It stays in the error state while the line is low,
/// shifting the line into an 8-bit register that the error entry set to ones; eight bit times later the register is zero and the
/// break condition (status bit 7, held, not sticky) shows until the line rises. A line that rises before the stop sample is an
/// ordinary byte with zero bits in it (or a parity error). Held in FIFO reset (frame bit 7) the receiver pauses until the line is high.
/// The transmitter's break (frame bit 6) holds Tx low and the transmitter busy: status bit 4 (Tx empty) reads 0 then (uart_tx.vhd o_busy).
///
/// The joystick connector (zxnext.vhd 3340-3350, 3516-3539): NR #0B bit 7 with bit 5 set routes one UART to the connector, bit 0 says
/// which (0: UART 0, replacing the ESP; 1: UART 1, replacing the Pi), bit 4 which connector. Pin 7 carries Tx, the pad's C line is Rx and
/// B is CTS. The module a UART leaves sees its Rx idle and its ready-to-receive pin not ready; the connector has no RTR pin, so the
/// joystick peer gets no RTS (it is another ISerialPeer: SetJoystickPeer).

#include <array>
#include <cstdint>
#include <functional>

#include "common/network/nettypes.h"

class ISerialPeer;

class NextUart
{
public:
    static constexpr unsigned kChannels = 2;
    static constexpr unsigned kRxFifo = 512, kTxFifo = 64;
    static constexpr unsigned kNearFull = kRxFifo * 3 / 4, kAlmostFull = kRxFifo - 2;
    static constexpr uint8_t kPortLow = 0x3B;
    static constexpr uint16_t kPortTx = 0x133B, kPortRx = 0x143B, kPortSelect = 0x153B, kPortFrame = 0x163B;
    static constexpr uint16_t kDefaultPrescaler = 243;  ///< 115200 baud at 28 MHz
    static constexpr uint8_t kDefaultFrame = 0x18;

    /// Frame register bits
    static constexpr uint8_t kFrameReset = 0x80, kFrameBreak = 0x40, kFrameFlow = 0x20, kFrameParity = 0x04, kFrameOdd = 0x02,
                             kFrameTwoStops = 0x01;
    /// Status bits
    static constexpr uint8_t kStatusBreak = 0x80, kStatusFraming = 0x40, kStatusByteError = 0x20, kStatusTxEmpty = 0x10,
                             kStatusNearFull = 0x08, kStatusOverflow = 0x04, kStatusTxFull = 0x02, kStatusRxAvail = 0x01;

    /// The ports of the block (the decoder of the board)
    static bool Decodes(uint16_t port)
    {
        const unsigned a10 = (port >> 10) & 1, a9 = (port >> 9) & 1, a8 = (port >> 8) & 1;
        return static_cast<uint8_t>(port) == kPortLow && (port >> 11) == 0x02 && ((a10 ^ (a9 & a8)) == 1);
    }

    /// An interrupt request: the channel and whether it is the transmitter's (the controller maps it to its source number)
    std::function<void(unsigned channel, bool tx)> onInterrupt;

    NextUart() { Reset(true); }

    /// CLK_28, the prescaler's clock (NR #11: 28 MHz at VGA 0, 28.571429 MHz at VGA 1): the bit time in clocks stays, the rate the
    /// line is told follows it
    void SetSystemClock(uint32_t hz);
    uint32_t SystemClock() const { return _systemClockHz; }

    /// A hard reset also restores the prescalers and the frames; any reset empties the FIFOs and selects UART 0
    void Reset(bool hard);
    /// Bring both channels up to the 28 MHz clock value `now`: Tx frames finish and go to the peer, Rx frames finish and fill the FIFO
    void Advance(uint64_t now);
    uint8_t Read(uint16_t port, uint64_t now);
    void Write(uint16_t port, uint8_t value, uint64_t now);
    /// Look at the interrupt levels now; `enable` is NR #C6. Raises a request for each level that rose since the last look
    void PollInterrupts(uint8_t enable);
    /// The other end of a channel (nullptr: nothing on the line: Tx bytes are lost, nothing arrives)
    void SetPeer(unsigned channel, ISerialPeer* peer);
    /// The module a channel is fitted with (the ESP, the Pi), also while the joystick connector has taken the channel's line
    ISerialPeer* Peer(unsigned channel) const { return _ch[channel].peer; }
    /// The device plugged into the joystick connector (nullptr: nothing)
    void SetJoystickPeer(ISerialPeer* peer);
    ISerialPeer* JoystickPeer() const { return _joyPeer; }
    /// NR #0B: the connector carries a UART (`on`) and which one (`channel`). The channel's own module is cut off: it sees no data
    /// and its ready-to-receive pin not ready
    void SetJoystickRoute(bool on, unsigned channel);
    bool JoystickRouted() const { return _joyOn; }
    unsigned JoystickChannel() const { return _joyChannel; }
    /// The other end a channel talks to now: the joystick peer while the connector has the channel (zxnext.vhd 3340: the connector's
    /// pins come before the Pi's GPIO mux, so NR #A0 does not gate them), else its module when its pins are connected
    ISerialPeer* Wire(unsigned channel) const { return _joyOn && _joyChannel == channel ? _joyPeer : _ch[channel].Live(); }
    /// The pins of the line are connected to the peer (UART 1: NR #A0 bits 4 and 5, zxnext.vhd 2333: with `pi_uart_en` clear the receiver
    /// idles high and the transmitter reaches no pin): disconnected, Tx bytes are lost and the peer's bytes are not taken
    void SetLineConnected(unsigned channel, bool connected) { _ch[channel].connected = connected; }
    bool LineConnected(unsigned channel) const { return _ch[channel].connected; }

    /// Bytes for the peer that the transmitter finished, and bytes lost by a missing peer, since the last Reset (statistics)
    uint64_t BytesSent(unsigned channel) const { return _ch[channel].sent; }
    uint64_t BytesReceived(unsigned channel) const { return _ch[channel].received; }
    uint64_t BytesLost(unsigned channel) const { return _ch[channel].lost; }

    struct View
    {
        bool selected = false;
        uint32_t prescaler = kDefaultPrescaler;  ///< 17 bits
        uint32_t baud = 115200;
        uint8_t frame = kDefaultFrame;
        unsigned dataBits = 8;
        char parity = 'N';
        unsigned stopBits = 1;
        bool hardwareFlow = false, rtsAsserted = true, breakOut = false;
        unsigned rxCount = 0, txCount = 0;
        bool txBusy = false, rxBusy = false;
        bool overflow = false, framingError = false;
        bool breakIn = false;      ///< the break condition: the peer's line held low long enough (status bit 7)
        bool rxLineLow = false;    ///< the peer's line is low now
        bool joystick = false;     ///< the channel talks to the joystick connector's peer
        uint8_t status = 0;  ///< what a read of #133B returns now, without its side effect
        uint64_t sent = 0, received = 0, lost = 0;
        bool hasPeer = false;
    };
    /// Nothing moves: no peer on either line and no frame in a shifter or a byte in a Tx FIFO (the machine's per-instruction poll skips
    /// Advance then)
    bool Idle() const
    {
        for (const Channel& c : _ch)
            if (c.Live() || c.txBusy || c.rxBusy || c.txCount || c.rxLow || c.breakIn)
                return false;
        return !(_joyOn && _joyPeer);
    }
    /// A look now would change nothing but the time of the last look: no frame in a shifter, no byte queued, the lines steady, and
    /// each peer silent (asked here: a peer's byte or break ends it). The per-instruction poll then calls Look instead of Advance
    bool Quiet() const;
    /// What Advance does to a quiet UART: the clock and the channels' last look move to `now`
    void Look(uint64_t now);
    /// The state of a channel without side effects
    View GetView(unsigned channel) const;
    unsigned Selected() const { return _selected; }
    /// The frame time of one byte in 28 MHz clocks, for a channel's current settings
    uint64_t FrameClocks(unsigned channel) const;

    /// The FIFO's bytes without taking them (reports), oldest first
    unsigned PeekRx(unsigned channel, uint16_t* out, unsigned max) const;
    unsigned PeekTx(unsigned channel, uint8_t* out, unsigned max) const;

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)

    uint32_t _systemClockHz = 28000000;

    struct Channel
    {
        uint8_t prescalerMsb = 0;
        uint16_t prescalerLsb = kDefaultPrescaler;  // 14 bits
        uint8_t frame = kDefaultFrame;
        // Rx FIFO: entries are error << 8 | byte
        std::array<uint16_t, kRxFifo> rx{};
        uint16_t rxHead = 0, rxCount = 0;
        std::array<uint8_t, kTxFifo> tx{};
        uint8_t txHead = 0, txCount = 0;
        // the shifters
        bool txBusy = false;
        uint8_t txByte = 0;
        uint64_t txDone = 0;
        bool rxBusy = false;
        uint8_t rxByte = 0;
        uint64_t rxDone = 0;
        uint64_t rxFreeAt = 0, txFreeAt = 0, txReadyAt = 0, lastLook = 0;
        bool errOverflow = false, errFraming = false;
        bool rtsOut = true;       ///< what the peer was last told (RTS asserted)
        bool rxLevel = false, txLevel = false;
        uint64_t sent = 0, received = 0, lost = 0;
        ISerialPeer* peer = nullptr;
        bool connected = true;
        ISerialPeer* Live() const { return connected ? peer : nullptr; }
        SerialLine told{};
        bool toldValid = false;
        // a break on the peer's line (see the class comment)
        bool rxLow = false;        ///< the peer's line has been low since lowSince
        uint64_t lowSince = 0;
        bool lowFramed = false;    ///< the stop sample passed with the line low: the frame error is counted
        bool lowPaused = false;    ///< a FIFO reset saw the line low: the receiver waits for it to rise
        bool breakIn = false;      ///< the break condition (status bit 7)
        bool breakTold = false;    ///< what the peer was last told of our Tx break
    };

    uint32_t Prescaler(const Channel& c) const
    {
        const uint32_t value = (static_cast<uint32_t>(c.prescalerMsb) << 14) | c.prescalerLsb;
        return value ? value : 1;
    }
    uint64_t FrameClocks(const Channel& c) const;
    static unsigned DataBits(const Channel& c) { return 5 + ((c.frame >> 3) & 3); }
    bool FifoReset(const Channel& c) const { return (c.frame & kFrameReset) != 0; }
    bool RxFull(const Channel& c) const { return c.rxCount >= kRxFifo; }
    void ResetFifos(Channel& c);
    void Step(Channel& c, uint64_t now);
    void StartTx(Channel& c, uint64_t at);
    void TellPeer(Channel& c);
    uint8_t Status(const Channel& c) const;
    unsigned IndexOf(const Channel& c) const { return static_cast<unsigned>(&c - _ch.data()); }
    ISerialPeer* WireOf(const Channel& c) const { return Wire(IndexOf(c)); }
    /// Clocks from the line falling to the stop bit's sample point, and to the break condition (uart_rx.vhd)
    uint64_t StopSampleClocks(const Channel& c) const;
    uint64_t BreakClocks(const Channel& c) const;
    /// The receiver's view of a low line: the frame error and the break condition as time passes, a short pulse as a byte
    void StepLine(Channel& c, ISerialPeer* peer, uint64_t now);
    void TellBreak(Channel& c);

    std::array<Channel, kChannels> _ch;
    unsigned _selected = 0;
    uint64_t _now = 0;
    /// PollInterrupts has nothing new to look at: the FIFO counts and the level memory are as it left them and it was asked with
    /// `_polledEnable`. Everything that can change them clears it (derived, not state)
    bool _pollClean = false;
    uint8_t _polledEnable = 0;
    ISerialPeer* _joyPeer = nullptr;
    bool _joyOn = false;
    unsigned _joyChannel = 0;
};
