#pragma once

/// @file nextcopper.h
/// @brief The Next's copper (device/copper.vhd, research-fpga-vhdl.md section 11): 1024 instructions of 16 bits written
/// through NR #60-#63, executed at 28 MHz - a MOVE (NextREG <- value) takes two clocks, a WAIT is re-evaluated every
/// clock until the beam is at its line (NR #64 shifts the line count) and horizontal position.
///
/// The copper's raster is paper-relative: line 0 is the first paper line, horizontal 0 is 11 pixels (7 MHz) before the
/// first paper pixel (the ULA's counter zero). Time is kept in 28 MHz clocks from line 0 / horizontal 0 of the frame
/// the geometry describes.
///
/// The copper is a bus master of the machine, not a part of the picture: the board runs it up to the beam after every CPU instruction and
/// before every CPU NextREG write (Sync), so its MOVEs reach the registers in machine time, in order with the CPU's writes, whether or
/// not a picture is drawn. The picture follows: the hook before a MOVE's write (SetBeforeWrite) draws the pixels the old state made.

#include <cstdint>
#include <functional>
#include <vector>

class NextCopper
{
public:
    /// The copper and the video count in 28 MHz clocks, the pixel clock (hc) is 7 MHz: four clocks to a pixel
    static constexpr unsigned kClocksPerPixel = 4;

    /// The NextREG write a MOVE performs
    using WriteFn = std::function<void(uint8_t reg, uint8_t value)>;
    /// The beam as copper time (28 MHz clocks)
    using NowFn = std::function<uint64_t()>;

    /// One instruction the copper completed: a WAIT that became true (it leaves) or a MOVE (reg 0 = the NOP), at its list index and clock
    struct Step
    {
        uint16_t pc = 0;
        uint64_t at = 0;  ///< copper clock the instruction completed on
        bool isMove = false;
        uint8_t reg = 0;
        uint8_t value = 0;
    };
    /// A watcher of the copper's steps. It sees an instruction before it takes effect; answering true HOLDS it (a breakpoint stopped the
    /// machine): the copper stays at that instruction and carries it out, without asking again, when it is run on
    using StepFn = std::function<bool(const Step&)>;

    void SetWriter(WriteFn writer) { _write = std::move(writer); }
    void SetNow(NowFn now) { _now = std::move(now); }
    /// Called with the copper time, the register and the value of a MOVE just before it writes (the screen draws the pixels the old state made
    /// first)
    void SetBeforeWrite(std::function<void(uint64_t, uint8_t, uint8_t)> hook) { _beforeWrite = std::move(hook); }
    /// 7 MHz counts per line and lines per frame of the machine timing
    void SetGeometry(unsigned hcPerLine, unsigned linesPerFrame);
    /// Any reset: the mode, the write address, the stored byte, the line offset and the list counter (zxnext.vhd 5011-5020 `reset`); the
    /// instruction RAM is a block RAM that only the configuration load (a hard reset) clears
    void Reset(bool clearCode = true);

    /// region <NextREG>
    void WriteData(uint8_t value);        ///< NR #60: one byte at the write address (auto increment)
    void WriteAddressLow(uint8_t value);  ///< NR #61
    void WriteControl(uint8_t value);     ///< NR #62: mode (7:6), address bits 10:8 (2:0)
    void WriteWord(uint8_t value);        ///< NR #63: two writes make an instruction
    void WriteOffset(uint8_t value) { _offset = value; _idleUntil = 0; }  ///< NR #64
    uint8_t ReadAddressLow() const { return static_cast<uint8_t>(_address & 0xFF); }
    uint8_t ReadControl() const { return static_cast<uint8_t>((_mode << 6) | ((_address >> 8) & 7)); }
    uint8_t ReadOffset() const { return _offset; }
    /// endregion

    /// Run the copper until copper time `target` (clocks from line 0 of the frame; earlier than the cursor = the frame wrapped)
    void RunTo(uint64_t target);
    /// Run up to the beam
    void Sync();
    /// The cheap per-instruction call: does nothing while the copper is stopped
    void Poll()
    {
        if (_mode != 0)
            Sync();
    }
    void SetObserver(StepFn observer) { _observer = std::move(observer); }
    /// The copper clock of the most recent MOVE, and the beam's place at a copper clock: the line counter with NR #64 applied (the cvc
    /// of zxula_timing.vhd, what a WAIT and the line interrupt compare) and the pixel clock in the row (hc)
    uint64_t LastMoveAt() const { return _lastMoveAt; }
    unsigned BeamLine(uint64_t clock) const { return static_cast<unsigned>(((clock / LineClocks()) + _offset) % _lines); }
    unsigned BeamHc(uint64_t clock) const { return static_cast<unsigned>((clock % LineClocks()) / kClocksPerPixel); }
    /// The beam now as a copper clock (false before the screen has bound the clock)
    bool BeamNow(uint64_t& clock) const
    {
        if (!_now)
            return false;
        clock = _now();
        return true;
    }
    /// The copper clock the beam passes line counter `line` (offset applied) at pixel clock `hc`; UINT64_MAX if the row has no such clock
    uint64_t ClockOf(unsigned line, unsigned hc) const
    {
        if (line >= _lines || hc >= _hcPerLine)
            return UINT64_MAX;
        const uint64_t rawLine = (line + _lines - (_offset % _lines)) % _lines;
        return (rawLine * _hcPerLine + hc) * kClocksPerPixel;
    }
    unsigned HcPerLine() const { return _hcPerLine; }
    unsigned Lines() const { return _lines; }

    uint8_t Mode() const { return _mode; }
    uint16_t Pc() const { return _pc; }
    /// The write address NR #60 / #63 store at next (byte granular, 0..2047); reading it steps nothing
    uint16_t WriteAddress() const { return _address; }
    uint16_t Instruction(unsigned index) const { return _code[index & 0x3FF]; }
    uint64_t FrameClocks() const { return _frameClocks; }

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    uint64_t LineClocks() const { return static_cast<uint64_t>(_hcPerLine) * kClocksPerPixel; }
    /// false: the observer held an instruction (the copper stands at it)
    bool RunSegment(uint64_t end);

    uint16_t _code[1024] = {};
    uint16_t _pc = 0;
    uint16_t _address = 0;  ///< write address, byte granular (0..2047)
    uint8_t _stored = 0;
    uint8_t _mode = 0;
    uint8_t _lastMode = 0;
    uint8_t _offset = 0;
    bool _pending = false;
    bool _running = false;
    uint64_t _t = 0;
    unsigned _hcPerLine = 448;
    unsigned _lines = 312;
    uint64_t _frameClocks = 448ull * 312 * 4;
    uint64_t _lastMoveAt = 0;
    /// Nothing can happen before this copper clock: a WAIT that is not true yet and will be at that clock. The fast path of RunTo; 0 = unknown
    /// (any write to the list, the mode or the offset puts it back)
    uint64_t _idleUntil = 0;
    bool _observed = false;  ///< the instruction at the cursor was shown to the observer (and held): it is carried out next, unasked
    WriteFn _write;
    NowFn _now;
    StepFn _observer;
    std::function<void(uint64_t, uint8_t, uint8_t)> _beforeWrite;
};
