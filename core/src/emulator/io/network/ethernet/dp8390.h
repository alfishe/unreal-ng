#pragma once

/// @file dp8390.h
/// @brief National Semiconductor DP8390 Network Interface Controller core, as the NE2000 family carries it
/// (DP8390 datasheet in National's 1988 Data Communications handbook; Realtek RTL8019AS datasheet 2005-08-26 §5;
/// docs/inprogress/2026-10-02-sprinter-network/tdd.md §6). Machine-independent: an NE2000 board (Ne2000Board) wraps
/// it with its I/O layout, PROM and buffer RAM, a host bus reaches the board (the Sprinter's ISA slots today).
///
/// What the core does:
///   - the register pages 0-2 (page 3 is the variant's: the board answers it) and the command register;
///   - remote DMA through the board's data port: read, write, "send packet", RDC when the count runs out;
///     reads wrap from PSTOP to PSTART (drivers read a frame that wraps the ring in one go);
///   - the receive ring: the address filter (own address, broadcast, the multicast hash, promiscuous), the
///     4-byte header (status, next page, byte count = frame + 4 CRC bytes), CURR, overflow (OVW, missed packet
///     tally), the stored CRC;
///   - transmit from TPSR / TBCR: the frame goes to the link at TXP; PTX and TSR follow after the wire time
///     ((bytes + 8 preamble + 12 gap) x 0.8 us at 10 Mbit/s), seen on the first access after it (lazy time);
///   - internal / external loopback as the datasheet has it: the frame never reaches the wire and never enters
///     the ring - the receive status (RSR) and the FIFO register report it (network open question Q4);
///   - ISR / IMR, the interrupt level, the three tally counters (cleared by reading, ISR.CNT at bit 7).
///
/// Time: every call carries `now`, the machine's time in base T-states (3.5 MHz units, ComPort::Now's clock), so
/// the CPU's turbo never changes the wire speed. Nothing runs between accesses: no per-instruction cost.
///
/// Worked example (the RTL8019AS kit sends a DHCP DISCOVER): the driver copies 342 bytes to page #40 with a
/// remote write (RSAR = #4000, RBCR = 342, CR = #12, 342 data-port writes, ISR.RDC), sets TPSR = #40,
/// TBCR = 342 and CR = #26 (TXP): Transmit() hands the frame to the link at once; 0.8 us x 370 = 296 us later
/// (1036 T) the next ISR read shows PTX.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class Dp8390
{
public:
    /// What the core needs from the board: its buffer memory (the 16-bit local bus; PROM and RAM decoded by the
    /// board), the wire (a transmitted frame, without the CRC) and whether it is the RTL8019AS (its page-0 ID)
    class IBoard
    {
    public:
        virtual ~IBoard() = default;
        virtual uint8_t BufferRead(uint16_t address) const = 0;
        virtual void BufferWrite(uint16_t address, uint8_t value) = 0;
        virtual void Transmit(const uint8_t* frame, size_t length) = 0;
    };

    // CR bits
    static constexpr uint8_t kCrStp = 0x01;
    static constexpr uint8_t kCrSta = 0x02;
    static constexpr uint8_t kCrTxp = 0x04;
    static constexpr uint8_t kCrRdMask = 0x38;
    static constexpr uint8_t kCrRdRead = 0x08;
    static constexpr uint8_t kCrRdWrite = 0x10;
    static constexpr uint8_t kCrRdSend = 0x18;
    static constexpr uint8_t kCrRdAbort = 0x20;
    // ISR / IMR bits
    static constexpr uint8_t kIsrPrx = 0x01;
    static constexpr uint8_t kIsrPtx = 0x02;
    static constexpr uint8_t kIsrRxe = 0x04;
    static constexpr uint8_t kIsrTxe = 0x08;
    static constexpr uint8_t kIsrOvw = 0x10;
    static constexpr uint8_t kIsrCnt = 0x20;
    static constexpr uint8_t kIsrRdc = 0x40;
    static constexpr uint8_t kIsrRst = 0x80;
    // RCR bits
    static constexpr uint8_t kRcrSep = 0x01;
    static constexpr uint8_t kRcrAr = 0x02;
    static constexpr uint8_t kRcrAb = 0x04;
    static constexpr uint8_t kRcrAm = 0x08;
    static constexpr uint8_t kRcrPro = 0x10;
    static constexpr uint8_t kRcrMon = 0x20;
    // RSR bits
    static constexpr uint8_t kRsrPrx = 0x01;
    static constexpr uint8_t kRsrMpa = 0x10;
    static constexpr uint8_t kRsrPhy = 0x20;
    static constexpr uint8_t kRsrDis = 0x40;
    // TSR
    static constexpr uint8_t kTsrPtx = 0x01;
    // TCR loopback field, DCR word transfer
    static constexpr uint8_t kTcrLbMask = 0x06;
    static constexpr uint8_t kTcrCrc = 0x01;
    static constexpr uint8_t kDcrWts = 0x01;

    /// The wire: 10 Mbit/s = 0.8 us a byte = 2.8 base T-states (3.5 MHz)
    static constexpr uint64_t WireTime(size_t frameBytes) { return ((frameBytes + 20) * 28 + 9) / 10; }

    explicit Dp8390(IBoard& board);

    /// The chip's hardware reset (RSTDRV, the NE2000 reset port): CR = #21, ISR = #80 (RST), IMR = 0, DCR, TCR, RCR
    /// to their reset values, a transmit in progress is dropped; buffer memory, PAR, MAR, PSTART / PSTOP stay
    void Reset();

    /// Register access at offset 0-15 of the current page (pages 0-2; page 3 is the board's)
    uint8_t ReadRegister(uint8_t offset, uint64_t now) { return ReadRegisterAs(Page(), offset, now); }
    void WriteRegister(uint8_t offset, uint8_t value, uint64_t now) { WriteRegisterAs(Page(), offset, value, now); }
    /// The same on a given page (a clone that mirrors page 1 into page 3 decodes it so)
    uint8_t ReadRegisterAs(uint8_t page, uint8_t offset, uint64_t now);
    void WriteRegisterAs(uint8_t page, uint8_t offset, uint8_t value, uint64_t now);
    /// What a read would return, no side effect (tally counters stay, debugger)
    uint8_t PeekRegister(uint8_t page, uint8_t offset) const;

    /// The NE2000 data port: one remote DMA byte
    uint8_t DataRead(uint64_t now);
    void DataWrite(uint8_t value, uint64_t now);
    uint8_t DataPeek() const;

    /// A frame from the wire (no CRC: the core computes and stores it). False = not taken now: the receiver
    /// runs and the frame passes the address filter, but the ring has no room for it (the network keeps it for
    /// the next frame boundary). A frame the chip drops (stopped, filtered, runt) is taken (true)
    bool Receive(const uint8_t* frame, size_t length, uint64_t now);
    /// Whether Receive would take a frame of this length now (CanAccept && Receive are what a switch port asks)
    bool CanAccept(const uint8_t* frame, size_t length) const;

    /// Catch up with time (a transmit completes): every access does it; the frame boundary too
    void Advance(uint64_t now);

    /// The interrupt output: ISR & IMR (bits 0-6)
    bool InterruptActive() const { return (_s.isr & _s.imr & 0x7F) != 0; }
    void SetInterruptListener(std::function<void()> changed) { _irqChanged = std::move(changed); }

    uint8_t Page() const { return static_cast<uint8_t>(_s.cr >> 6); }
    uint8_t Command() const { return _s.cr; }
    bool Running() const { return (_s.cr & (kCrSta | kCrStp)) == kCrSta; }
    const uint8_t* Mac() const { return _s.par; }

    /// Everything a checkpoint holds (plain fields; equal states give equal bytes)
    struct State
    {
        uint8_t cr, isr, imr, dcr, tcr, rcr, rsr, tsr;
        uint8_t pstart, pstop, bnry, curr, tpsr, ncr;
        uint8_t par[6];
        uint8_t mar[8];
        uint8_t cntr[3];
        uint8_t fifoLength, fifoRead, reserved0;
        uint8_t fifo[8];
        uint16_t rsar, rbcr, crda, remaining, tbcr, clda;
        uint8_t dmaActive;    ///< 0 none, 1 read, 2 write (remote DMA in progress)
        uint8_t txPending;    ///< a transmit is on the wire, PTX at txDoneAt
        uint8_t reserved1[2];
        uint64_t txDoneAt;
        uint64_t framesIn, framesOut, framesFiltered, framesMissed;
    };
    const State& GetState() const { return _s; }
    void LoadState(const State& state);

    /// Observation (not state): frames that never got in because the ring was full when a switch offered them
    uint64_t RingFullWaits() const { return _ringFullWaits; }

    /// Ethernet FCS (CRC-32, IEEE) of a frame; the multicast hash index is its top 6 bits
    static uint32_t Crc32(const uint8_t* data, size_t length);
    /// The MAR bit (0-63) a multicast destination hashes to (MAR byte = index / 8, bit = index % 8)
    static unsigned MulticastHashIndex(const uint8_t* mac);

private:
    bool AddressAccepted(const uint8_t* frame, size_t length, bool& physicalMulticast) const;
    uint16_t RingFreeBytes() const;
    void StartRemoteDma(uint8_t rd);
    void StartTransmit(uint64_t now);
    void Loopback(const std::vector<uint8_t>& frame);
    void SetIsr(uint8_t bits);
    void NoteIrq(bool before);
    void CountTally(int counter);
    uint8_t NextRingPage(uint8_t page) const;

    IBoard& _board;
    State _s{};
    std::function<void()> _irqChanged;
    uint64_t _ringFullWaits = 0;
};
