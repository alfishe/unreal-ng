#pragma once

/// @file nextdma.h
/// @brief The Next's DMA (device/dma.vhd, research-fpga-vhdl.md section 13): a Z80-DMA lookalike with the ZX Next's
/// differences, behind port #6B (zxnDMA: counter starts at 0, LOAD / CONTINUE reset it) and #0B (Z80-DMA compatible:
/// counter starts at #FFFF). WR0-WR6 are parsed by a byte sequencer, the read sequence returns the status and the
/// counters selected by the read mask. A transfer moves a byte from port A or B (memory or I/O, address
/// incrementing, decrementing or fixed) to the other; continuous mode moves the block while holding the CPU, burst mode
/// gives one byte per prescaler period and lets the CPU run between the bytes.
///
/// The host steps it between instructions: Run() moves up to `maxBytes` and says how many CPU clocks the transfer
/// held the bus. The prescaler timer is in 28 MHz clocks (period = prescaler * 32 clocks = 875 kHz steps), so the
/// burst rate does not depend on the CPU speed.

#include <cstdint>
#include <functional>

class NextDma
{
public:
    struct Bus
    {
        std::function<uint8_t(uint16_t)> readMemory;
        std::function<void(uint16_t, uint8_t)> writeMemory;
        std::function<uint8_t(uint16_t)> readIo;
        std::function<void(uint16_t, uint8_t)> writeIo;
        /// CPU clocks the transfer takes before the next access (the read, then the write cycles of the port timing bytes)
        std::function<void(unsigned)> advance;
        /// At the start of a read or write cycle or a T without a strobe: the waits the video logic gives the address on the bus
        /// (`io`: no MREQ, a port cycle or an idle T)
        std::function<void(uint16_t, bool)> contend;
        /// zxnDDMA physical mode: a byte of the SRAM, the MMU bypassed (22-bit address; the board's SRAM lines wrap it further:
        /// NextMemory::PhysicalAddressMask)
        std::function<uint8_t(uint32_t)> readPhysical;
        std::function<void(uint32_t, uint8_t)> writePhysical;
        /// zxnDDMA Layer 2 X,Y destination: the 16K bank (NR #12, or the shadow NR #13 when `hidden`) and the resolution (NR #70 bits 5:4)
        std::function<void(bool hidden, uint8_t& bank, uint8_t& resolution)> layer2;
        /// zxnext.vhd im2_dma_delay at the current clock: asked after each byte that is neither the last of its block nor followed by a
        /// prescaler wait (dma.vhd WRITE_4); true gives the bus back until the host sees the delay end
        std::function<bool()> delay;
        /// The 28 MHz clock now (the prescaler timer starts at a byte's first read clock, after the bus grant); not set: Run's `now28`
        std::function<uint64_t()> now28;
    };

    /// 28 MHz clocks of one CPU clock (8, 4, 2, 1): the DMA's states take CPU clocks, its prescaler timer counts 28 MHz ones
    void SetClockTicks(unsigned ticks) { _clockTicks = ticks ? ticks : 1; }

    /// The core has the zxnDDMA extension (the blitter commands #D7-#FB on port #6B). Off: those bytes fall through as on core 3.02.03
    void SetExtended(bool on) { _extended = on; }
    bool Extended() const { return _extended; }
    void SetBus(Bus bus) { _bus = std::move(bus); }
    /// Told at the end of every block, before an auto restart reloads the addresses (the block's history)
    void SetBlockListener(std::function<void(const NextDma&)> listener) { _onBlock = std::move(listener); }
    /// Reset (hard or soft; dma.vhd reset block): the sequencer, timing bytes, prescaler, mode, CE/WAIT, auto-restart, read mask,
    /// status and the counter go back; the programmed addresses, block length, direction and port types stay
    void Reset();

    /// A byte to port #6B (zxnDMA) or #0B (`z80Compatible`)
    void Write(uint8_t value, bool z80Compatible);
    /// A byte from the read sequence
    uint8_t Read();
    /// The same through port #6B / #0B: every access of either port sets the zxn / Z80 mode latch (zxnext.vhd dma_mode)
    uint8_t ReadAs(bool z80Compatible)
    {
        _z80Compat = z80Compatible;
        return Read();
    }

    /// True while the DMA wants to run (enabled, not finished)
    bool Active() const { return _transferring; }
    /// The bus is held except in a BURST-mode prescaler wait (dma.vhd: only mode 10 releases it; continuous mode with a prescaler
    /// keeps the CPU stopped for the whole block)
    bool HoldsBus() const { return _transferring && !(_waiting && _mode == 2); }
    /// Between the CPU's BUSAK and the release of BUSRQ: a byte boundary of a held bus goes on without asking for the bus again
    bool BusHeld() const { return _busHeld; }
    /// 28 MHz clock the current prescaler wait ends at (0: not waiting): continuous mode reads the next byte then, burst mode asks for the
    /// bus again, the last byte's wait ends the block
    uint64_t WaitEnd() const { return _waiting ? _nextByteAt : 0; }

    /// Move up to `maxBytes` bytes at 28 MHz clock `now`; the count of bytes moved (0 while waiting for the prescaler)
    unsigned Run(unsigned maxBytes, uint64_t now28);

    /// region <State, for tests and reports>
    uint16_t Source() const { return static_cast<uint16_t>(_src); }
    uint16_t Destination() const { return static_cast<uint16_t>(_dst); }
    uint32_t Source24() const { return _src; }
    uint32_t Destination24() const { return _dst; }
    bool Physical() const { return _phys; }
    bool TwoD() const { return _2d; }
    uint16_t Counter() const { return _counter; }
    uint16_t BlockLength() const { return _blockLength; }
    uint8_t Mode() const { return _mode; }
    bool AutoRestart() const { return _autoRestart; }
    bool EndOfBlock() const { return _endOfBlock; }
    uint8_t Prescaler() const { return _prescaler; }
    bool Waiting() const { return _waiting; }
    /// endregion

    /// region <Const view for the next_dma report: nothing here steps the sequencer or the read sequence (design-automation-coverage.md R3)>
    struct PortView
    {
        uint16_t address = 0;
        bool io = false;
        uint8_t step = 1;    ///< 0 decrement, 1 increment, 2 / 3 fixed (WR1 / WR2 bits 5:4)
        uint8_t timing = 1;  ///< the timing byte, bits 1:0 (4, 3, 2, 4 clocks)
    };
    PortView PortA() const { return {_portAAddress, _portAIsIo, _portAMode, _portATiming}; }
    PortView PortB() const { return {_portBAddress, _portBIsIo, _portBMode, _portBTiming}; }
    bool AToB() const { return _aToB; }
    bool Z80Compatible() const { return _z80Compat; }  ///< the last access came through port #0B
    bool CeWait() const { return _ceWait; }
    uint8_t ReadMask() const { return _readMask; }
    uint8_t ReadSequenceIndex() const { return _readSeq; }  ///< which of the 7 values the next read returns
    /// The zxnDDMA state (core 3.03.00, zxnddma.gde 2.5): what #D7-#F7 programmed and where a 2D block is
    struct DdmaView
    {
        uint8_t extA = 0, extB = 0;  ///< #D7 / #DB: address bits 21:16 of port A / B (bits 7:6 dropped)
        bool physical = false, twoD = false, transparent = false, fill = false, layer2Xy = false, hidden = false;  ///< #DF flags
        uint16_t layer2X = 0;
        uint8_t layer2Y = 0;
        uint16_t rowLength = 0, rows = 0, strideA = 0, strideB = 0;  ///< #E3 / #E7 / #EB / #EF
        uint8_t transparentValue = 0, fillValue = 0;                  ///< #F3 / #F7
        uint16_t row = 0, column = 0;  ///< the 2D position of the next byte
    };
    DdmaView Ddma() const
    {
        return {_extA, _extB, _phys, _2d, _transparent, _fill, _l2xy, _hidden, _l2x, _l2y, _rowLen, _rows, _strideA, _strideB,
                _transValue, _fillValue, _row, _col};
    }
    /// The byte a read returns while the read sequence is at the status (what Read() computes first, without stepping anything)
    uint8_t StatusByte() const
    {
        return static_cast<uint8_t>((_endOfBlock ? 0x00 : 0x20) | 0x1A | ((_transferring && _atLeastOne) ? 1 : 0));
    }
    /// endregion

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    enum class Seq : uint8_t
    {
        Idle,
        R0b0, R0b1, R0b2, R0b3,
        R1b0, R1b1,
        R2b0, R2b1,
        R3b0, R3b1,
        R4b0, R4b1,
        R6Mask,
        ExtA, ExtB,                    ///< zxnDDMA: #D7 / #DB, address bits 23:16
        DdmaMode, DdmaX0, DdmaX1, DdmaY,  ///< #DF: the flags, then (Layer 2 X,Y) X low, X high, Y
        RowLen0, RowLen1, Rows0, Rows1,   ///< #E3 / #E7
        StrideA0, StrideA1, StrideB0, StrideB1,  ///< #EB / #EF
        Transparent, FillValue,           ///< #F3 / #F7
        Stuck  ///< dma.vhd R4_BYTE_2: WR4 with only bit 4 set waits for an interrupt control byte the case statement does not handle
    };
    // The bus hand-over in CPU clocks, from the RTL in GHDL (POC 036: a 256-byte block to port #FE holds the CPU 1536 + 5 T): the
    // first read starts 1 T after BUSAK, the CPU's next cycle 4 T after the last write ends (FINISH_DMA, IDLE drops BUSRQ, the T80
    // samples it). A burst-mode prescaler wait releases from WAITING_CYCLES, one state earlier (dma.vhd, not measured)
    static constexpr unsigned kGrantT = 1;
    static constexpr unsigned kFinishReleaseT = 4;
    static constexpr unsigned kBurstReleaseT = 3;
    // WRITE_4 -> START_DMA under im2_dma_delay: START_DMA drops BUSRQ, one state before FINISH_DMA -> IDLE would (dma.vhd, not measured)
    static constexpr unsigned kDelayReleaseT = 3;
    // An auto restart on a held bus: WRITE_4 -> FINISH_DMA -> WAITING_ACK -> READ_1, two states more than WRITE_4 -> READ_1 (dma.vhd)
    static constexpr unsigned kRestartT = 2;
    /// The end of a prescaler wait for a byte that started at `byteStart` (dma.vhd WAITING_CYCLES)
    uint64_t PrescalerWaitEnd(uint64_t byteStart, bool last) const;
    static unsigned Cycles(uint8_t timing);
    void ReleaseBus(unsigned clocks);
    /// Clocks the DMA holds the bus without a strobe: the source address is on it (dma.vhd WAITING_ACK, WRITE_4), and the video
    /// logic decodes contention from the address alone (zxnext.vhd mem_contend), so each T waits as a cycle without MREQ
    void HoldBus(unsigned clocks);
    void Command(uint8_t value);
    void Load();
    /// The working addresses from the programmed ones (the 24-bit zxnDDMA form, the Layer 2 X,Y destination), the 2D position at the start
    void InitAddresses();
    uint32_t Step(uint32_t address, uint8_t mode) const;
    void ClearDdma();
    void FinishBlock();
    void AdvanceReadSequence(int after);

    Bus _bus;
    std::function<void(const NextDma&)> _onBlock;
    // WR0-WR2, WR4. Power-on values: dma.vhd gives the registers its reset leaves alone (direction, addresses, block length, port
    // types and address modes) no initial value; Xilinx synthesis (XST for the issue 2's Spartan-6, Vivado for the issue 4 / 5's Artix-7)
    // makes such a flip-flop's INIT 0, which the configuration loads (GSR). So: B -> A, both ports memory, decrement, all addresses 0.
    // The timing bytes have an initial value ("01") and are reset anyway
    bool _aToB = false;
    uint16_t _portAAddress = 0, _portBAddress = 0, _blockLength = 0;
    bool _portAIsIo = false, _portBIsIo = false;
    uint8_t _portAMode = 0, _portBMode = 0;  // 0 decrement, 1 increment, 2/3 fixed
    uint8_t _portATiming = 1, _portBTiming = 1, _prescaler = 0;
    uint8_t _mode = 1;  // 0 byte, 1 continuous, 2 burst
    bool _autoRestart = false, _ceWait = false;
    uint8_t _readMask = 0x7F;
    // running state
    bool _transferring = false, _waiting = false, _z80Compat = false, _finishPending = false;
    bool _atLeastOne = false, _endOfBlock = false;
    bool _busHeld = false;  // between the CPU's BUSAK and the release of BUSRQ
    uint32_t _src = 0, _dst = 0;  // 24 bits: {bits 23:16, address}
    uint16_t _counter = 0;
    // zxnDDMA (core 3.03.00)
    bool _extended = false;
    unsigned _clockTicks = 8;
    uint8_t _extA = 0, _extB = 0;
    bool _phys = false, _2d = false, _transparent = false, _fill = false, _l2xy = false, _hidden = false;
    uint16_t _l2x = 0;
    uint8_t _l2y = 0;
    uint16_t _rowLen = 0, _rows = 0, _strideA = 0, _strideB = 0;
    uint8_t _transValue = 0, _fillValue = 0;
    uint16_t _row = 0, _col = 0;
    uint32_t _rowSrc = 0, _rowDst = 0;
    uint64_t _nextByteAt = 0;
    Seq _seq = Seq::Idle;
    uint8_t _readSeq = 0;
    uint8_t _regTemp = 0;
};
