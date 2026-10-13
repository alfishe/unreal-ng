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
    ISerialPeer* Peer(unsigned channel) const { return _ch[channel].peer; }
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
        uint8_t status = 0;  ///< what a read of #133B returns now, without its side effect
        uint64_t sent = 0, received = 0, lost = 0;
        bool hasPeer = false;
    };
    /// Nothing moves: no peer on either line and no frame in a shifter or a byte in a Tx FIFO (the machine's per-instruction poll skips
    /// Advance then)
    bool Idle() const
    {
        for (const Channel& c : _ch)
            if (c.Live() || c.txBusy || c.rxBusy || c.txCount)
                return false;
        return true;
    }
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

    std::array<Channel, kChannels> _ch;
    unsigned _selected = 0;
    uint64_t _now = 0;
};
