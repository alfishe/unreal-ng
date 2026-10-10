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
    };

    void SetBus(Bus bus) { _bus = std::move(bus); }
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
    /// 28 MHz clock the current prescaler wait ends at (0: not waiting)
    uint64_t WaitEnd() const { return _waiting ? _nextByteAt : 0; }

    /// Move up to `maxBytes` bytes at 28 MHz clock `now`; the count of bytes moved (0 while waiting for the prescaler)
    unsigned Run(unsigned maxBytes, uint64_t now28);

    /// region <State, for tests and reports>
    uint16_t Source() const { return _src; }
    uint16_t Destination() const { return _dst; }
    uint16_t Counter() const { return _counter; }
    uint16_t BlockLength() const { return _blockLength; }
    uint8_t Mode() const { return _mode; }
    bool AutoRestart() const { return _autoRestart; }
    bool EndOfBlock() const { return _endOfBlock; }
    uint8_t Prescaler() const { return _prescaler; }
    bool Waiting() const { return _waiting; }
    /// endregion

private:
    enum class Seq : uint8_t
    {
        Idle,
        R0b0, R0b1, R0b2, R0b3,
        R1b0, R1b1,
        R2b0, R2b1,
        R3b0, R3b1,
        R4b0, R4b1,
        R6Mask
    };
    static unsigned Cycles(uint8_t timing);
    void Command(uint8_t value);
    void Load();
    void FinishBlock();
    void AdvanceReadSequence(int after);

    Bus _bus;
    // WR0-WR2, WR4
    bool _aToB = true;
    uint16_t _portAAddress = 0, _portBAddress = 0, _blockLength = 0;
    bool _portAIsIo = false, _portBIsIo = false;
    uint8_t _portAMode = 1, _portBMode = 1;  // 0 decrement, 1 increment, 2/3 fixed
    uint8_t _portATiming = 1, _portBTiming = 1, _prescaler = 0;
    uint8_t _mode = 1;  // 0 byte, 1 continuous, 2 burst
    bool _autoRestart = false, _ceWait = false;
    uint8_t _readMask = 0x7F;
    // running state
    bool _transferring = false, _waiting = false, _z80Compat = false, _finishPending = false;
    bool _atLeastOne = false, _endOfBlock = false;
    uint16_t _src = 0, _dst = 0, _counter = 0;
    uint64_t _nextByteAt = 0;
    Seq _seq = Seq::Idle;
    uint8_t _readSeq = 0;
    uint8_t _regTemp = 0;
};
