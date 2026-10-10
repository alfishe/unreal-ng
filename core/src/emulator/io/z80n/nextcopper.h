#pragma once

/// @file nextcopper.h
/// @brief The Next's copper (device/copper.vhd, research-fpga-vhdl.md section 11): 1024 instructions of 16 bits written
/// through NR #60-#63, executed at 28 MHz - a MOVE (NextREG <- value) takes two clocks, a WAIT is re-evaluated every
/// clock until the beam is at its line (NR #64 shifts the line count) and horizontal position.
///
/// The copper's raster is paper-relative: line 0 is the first paper line, horizontal 0 is 11 pixels (7 MHz) before the
/// first paper pixel (the ULA's counter zero). Time is kept in 28 MHz clocks from line 0 / horizontal 0 of the frame
/// the geometry describes; the screen runs the copper up to the beam as it draws (RunTo), so the video registers a
/// MOVE writes are in place for the lines after it.

#include <cstdint>
#include <functional>

class NextCopper
{
public:
    /// The NextREG write a MOVE performs
    using WriteFn = std::function<void(uint8_t reg, uint8_t value)>;
    /// The beam as copper time (28 MHz clocks)
    using NowFn = std::function<uint64_t()>;

    void SetWriter(WriteFn writer) { _write = std::move(writer); }
    void SetNow(NowFn now) { _now = std::move(now); }
    /// Called with the copper time of a MOVE just before it writes (the screen draws the pixels the old state made first)
    void SetBeforeWrite(std::function<void(uint64_t)> hook) { _beforeWrite = std::move(hook); }
    /// 7 MHz counts per line and lines per frame of the machine timing
    void SetGeometry(unsigned hcPerLine, unsigned linesPerFrame);
    void Reset();

    /// region <NextREG>
    void WriteData(uint8_t value);        ///< NR #60: one byte at the write address (auto increment)
    void WriteAddressLow(uint8_t value);  ///< NR #61
    void WriteControl(uint8_t value);     ///< NR #62: mode (7:6), address bits 10:8 (2:0)
    void WriteWord(uint8_t value);        ///< NR #63: two writes make an instruction
    void WriteOffset(uint8_t value) { _offset = value; }  ///< NR #64
    uint8_t ReadAddressLow() const { return static_cast<uint8_t>(_address & 0xFF); }
    uint8_t ReadControl() const { return static_cast<uint8_t>((_mode << 6) | ((_address >> 8) & 7)); }
    uint8_t ReadOffset() const { return _offset; }
    /// endregion

    /// Run the copper until copper time `target` (clocks from line 0 of the frame; earlier than the cursor = the frame wrapped)
    void RunTo(uint64_t target);
    /// Run up to the beam
    void Sync();

    uint8_t Mode() const { return _mode; }
    uint16_t Pc() const { return _pc; }
    /// The write address NR #60 / #63 store at next (byte granular, 0..2047); reading it steps nothing
    uint16_t WriteAddress() const { return _address; }
    uint16_t Instruction(unsigned index) const { return _code[index & 0x3FF]; }
    uint64_t FrameClocks() const { return _frameClocks; }

private:
    void RunSegment(uint64_t end);

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
    WriteFn _write;
    NowFn _now;
    std::function<void(uint64_t)> _beforeWrite;
};
