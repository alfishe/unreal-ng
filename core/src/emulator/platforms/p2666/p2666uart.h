#pragma once

/// @file p2666uart.h
/// @brief The Pentagon 2.666 LE's UART (common/uart.vhd, wired at PaE.vhd:630-631, 1255-1262, 1539-1540, 1567;
/// hardware-reference.md §5.7): a 16550-like register set at #F8EF..#FFEF (register = A10..8) on mainclk, 16-byte
/// FIFOs, RTS / CTS, its interrupt ANDed into INT. The manual says no firmware uses it. The board as built (owner
/// decision 2026-11-11): its quirks are copied, the fixes stay as commented `P2666-FIX` blocks in the .cpp.
///
/// Where it is not a 16550:
/// - D-15: the register strobe is a level (reg_stb_n follows IORQ for the whole I/O cycle, some 80 mainclk), so one
///   OUT to the data register pushes the byte until the TX FIFO is full (16 copies) and one IN pops the RX FIFO
///   empty, returning the last byte. An IN of the data register with the FIFO empty returns the last value any
///   register read left on reg_data_out.
/// - D-16: the FIFO reset bits of FCR (1, 2) clear only on the next register write; until then that FIFO is held
///   empty (the bytes written to it are lost).
/// - D-17: LSR bit 5 (THRE) is the "TX FIFO went empty" interrupt flag: 0 after reset until a byte has been sent, and
///   cleared by an IIR read.
/// - IIR codes: RLS #0C, RX data / timeout #08, THRE #04, modem status #00, none #03 (bits 3..1 are the 16550's
///   reversed); bits 7..6 = FCR bit 0 twice. The RX timeout runs on mainclk (4 x (frame bits x 16 + 16) clocks).
/// - MSR = 1 0 1 CTS 0 0 0 DCTS: DCD and DSR read 1, RI 0, CTS is the pin's level (1 = not clear to send).
/// - RTS pin = MCR bit 1 OR the FIFO's own flow control (8 bytes waiting): set means "stop".
/// - No loopback (MCR bit 4), no OUT1 / OUT2; IER keeps all 8 bits; 5..7 bit words arrive in the high bits of RBR.
/// - Baud: one bit every 495 x divisor mainclk on TX, 496 x divisor on RX (divisor 1 = 230 kbit/s, 2 = 115200;
///   divisor 0 = 65536).

#include <array>
#include <cstdint>
#include <functional>

class ISerialPeer;

class P2666Uart
{
public:
    static constexpr int kFifo = 16;
    static constexpr int kRtsThreshold = 8;
    static constexpr uint64_t kTxBitEdges = 495;  ///< (c_tx_maxdiv = 114000000 / (2 x 115200) = 494) + 1
    static constexpr uint64_t kRxBitEdges = 496;  ///< 16 x (c_rx_maxdiv = 30 + 1)

    /// mainclk edges now (the decoder's clock)
    void SetClock(std::function<uint64_t()> now) { _now = std::move(now); }
    /// The device's /INT changed (true = held low)
    void SetIntListener(std::function<void(bool)> listener) { _onInt = std::move(listener); }
    void SetPeer(ISerialPeer* peer);
    ISerialPeer* Peer() const { return _peer; }

    /// cpu_res: every register, FIFO and flag to its reset value
    void Reset();

    uint8_t Read(uint8_t reg);
    void Write(uint8_t reg, uint8_t value);
    /// Run the line to now (frame end, the peer's new bytes)
    void Sync();

    /// region <State (tests, reports)>
    bool InterruptPending() const;
    bool RtsStop() const { return _rtsFifo || (_mcr & 0x02); }
    int TxFilled() const { return _txFilled; }
    int RxFilled() const { return _rxFilled; }
    uint32_t Divisor() const { return (static_cast<uint32_t>(_dlm) << 8 | _dll) ? (static_cast<uint32_t>(_dlm) << 8 | _dll) : 65536u; }
    uint32_t Baud() const { return static_cast<uint32_t>(114000000ull / (kTxBitEdges * Divisor())); }
    int DataBits() const { return 5 + (_lcr & 3); }
    int FrameBits() const { return 1 + DataBits() + ((_lcr & 0x08) ? 1 : 0) + ((_lcr & 0x04) ? 2 : 1); }
    uint64_t bytesIn = 0, bytesOut = 0, lost = 0;
    /// endregion

private:
    void Advance(uint64_t edge);
    void PushRx(uint8_t byte);
    bool Cts() const;  ///< the cts pin: true = high (not clear)
    void UpdateInt();
    uint64_t Now() const { return _now ? _now() : 0; }
    uint64_t TxFrameEdges() const { return static_cast<uint64_t>(FrameBits()) * kTxBitEdges * Divisor(); }
    /// The receiver strobes the byte in the middle of the (first) stop bit
    uint64_t RxFrameEdges() const
    {
        const uint64_t bits = 1u + static_cast<unsigned>(DataBits()) + ((_lcr & 0x08) ? 1u : 0u);
        return (bits * 2u + 1u) * kRxBitEdges * Divisor() / 2u;
    }
    uint64_t TimeoutEdges() const
    {
        const unsigned total = static_cast<unsigned>(DataBits() - 1) + 2u + ((_lcr >> 2) & 1u) + ((_lcr >> 3) & 1u);
        return 4u * ((total << 4) + 16u);
    }

    std::function<uint64_t()> _now;
    std::function<void(bool)> _onInt;
    ISerialPeer* _peer = nullptr;

    uint8_t _dll = 1, _dlm = 0, _ier = 0, _fcr = 0, _lcr = 0, _mcr = 0, _spr = 0;
    uint8_t _out = 0;  ///< reg_data_out_b: what the last read left on the bus
    std::array<uint8_t, kFifo> _tx{}, _rx{};
    int _txHead = 0, _txFilled = 0, _rxHead = 0, _rxFilled = 0;
    bool _txEmptyInt = false;        ///< tx_reg_empty_int (LSR bit 5)
    bool _transmitterEmpty = true;   ///< LSR bit 6
    bool _rxAvailable = false;       ///< rx_data_available (the trigger level)
    bool _overrun = false;
    bool _msInt = false;
    bool _ctsOld = false;
    bool _rtsFifo = false;           ///< rts_b
    bool _intState = false;

    bool _txBusy = false;
    uint8_t _txByte = 0;
    uint64_t _txEnd = 0;
    bool _rxBusy = false;
    uint64_t _rxEnd = 0;
    uint64_t _rxLineFree = 0;
    uint64_t _rxSince = 0;           ///< the timeout counter's start (a byte in, a data read)
    uint64_t _last = 0;
};
