#include "debugger/analyzers/analysis/accesstrace.h"

#include <cstring>
#include <fstream>
#include <initializer_list>
#include <vector>

#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/analyzers/analysis/replaywindow.h"
#include "debugger/analyzers/analysis/z80opinfo.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace analysis
{

namespace
{

constexpr uint32_t kHeaderBytes = 64;

/// The records, buffered and written in blocks
class TraceWriter
{
public:
    explicit TraceWriter(const std::string& path) : _file(path, std::ios::binary | std::ios::trunc) {}

    bool Ok() const { return static_cast<bool>(_file); }

    void U8(uint8_t v) { _buffer.push_back(v); }
    void U16(uint16_t v)
    {
        _buffer.push_back(static_cast<uint8_t>(v));
        _buffer.push_back(static_cast<uint8_t>(v >> 8));
    }
    void U32(uint32_t v)
    {
        U16(static_cast<uint16_t>(v));
        U16(static_cast<uint16_t>(v >> 16));
    }
    void U64(uint64_t v)
    {
        U32(static_cast<uint32_t>(v));
        U32(static_cast<uint32_t>(v >> 32));
    }
    void Bytes(const uint8_t* data, size_t size) { _buffer.insert(_buffer.end(), data, data + size); }

    void FlushIfFull()
    {
        if (_buffer.size() >= (1u << 20))
            Flush();
    }
    void Flush()
    {
        if (!_buffer.empty())
            _file.write(reinterpret_cast<const char*>(_buffer.data()), static_cast<std::streamsize>(_buffer.size()));
        _written += _buffer.size();
        _buffer.clear();
    }
    uint64_t Written() const { return _written + _buffer.size(); }

    /// Rewrite the header at the file's start (the counts are known at the end)
    bool Header(const std::vector<uint8_t>& header)
    {
        Flush();
        _file.seekp(0);
        _file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        _file.flush();
        return static_cast<bool>(_file);
    }

private:
    std::ofstream _file;
    std::vector<uint8_t> _buffer;
    uint64_t _written = 0;
};

std::vector<uint8_t> HeaderBytes(const AccessTraceSummary& s, uint32_t unitsPerT, uint32_t frameT)
{
    std::vector<uint8_t> h;
    auto u16 = [&h](uint16_t v) {
        h.push_back(static_cast<uint8_t>(v));
        h.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto u32 = [&u16](uint32_t v) {
        u16(static_cast<uint16_t>(v));
        u16(static_cast<uint16_t>(v >> 16));
    };
    auto u64 = [&u32](uint64_t v) {
        u32(static_cast<uint32_t>(v));
        u32(static_cast<uint32_t>(v >> 32));
    };
    for (char c : {'U', 'A', 'T', 'R'})
        h.push_back(static_cast<uint8_t>(c));
    u16(1);
    u16(static_cast<uint16_t>(kHeaderBytes));
    u64(s.fromFrame);
    u32(s.fromTInFrame);
    u32(unitsPerT);
    u64(s.toFrame);
    u32(s.toTInFrame);
    u32(frameT);
    u64(s.instructions);
    u64(s.cycles);
    h.push_back(s.truncated ? 1 : 0);
    h.resize(kHeaderBytes, 0);
    return h;
}

bool Before(uint64_t frame, uint32_t t, uint64_t endFrame, uint32_t endT)
{
    return frame < endFrame || (frame == endFrame && t < endT);
}

}  // namespace

bool ReplayAccessTrace(const TraceBundle& bundle, const AccessTraceOptions& options, AccessTraceSummary& out,
                       std::string& error)
{
    out = AccessTraceSummary{};
    if (options.path.empty())
    {
        error = "no file to write the trace to";
        return false;
    }
    // The clone, seeked to the window's start (replaywindow.h: TTD does the restore and the replay)
    ReplayWindow window;
    if (!window.Open(bundle, "analysis-trace", ttd::TTDTimePoint{options.fromFrame, options.fromTInFrame}, error))
        return false;
    Z80* z80 = window.Cpu();
    Memory* memory = window.Mem();
    EmulatorState& state = window.State();

    // The window: [from, end) in TTD units; no end means one frame; past the recording's end means its end
    const ttd::TTDTimePoint from = window.Start();
    const ttd::TTDTimePoint last = window.SessionEnd();
    uint64_t endFrame = options.toFrame;
    uint32_t endT = options.toTInFrame;
    if (endFrame == 0 && endT == 0)
        endFrame = from.frame + 1;
    if (!Before(endFrame, endT, last.frame, last.tInFrame))
    {
        endFrame = last.frame;
        endT = last.tInFrame;
    }
    if (!Before(from.frame, from.tInFrame, endFrame, endT))
    {
        error = "the window holds no moment of the recording (frames " + std::to_string(window.First().frame) +
                ".." + std::to_string(last.frame) + ")";
        return false;
    }
    if (endFrame - from.frame > kAccessTraceMaxFrames)
    {
        error = "the window spans " + std::to_string(endFrame - from.frame) + " frames; a trace takes at most " +
                std::to_string(kAccessTraceMaxFrames);
        return false;
    }

    TraceWriter writer(options.path);
    if (!writer.Ok())
    {
        error = "cannot write " + options.path;
        return false;
    }
    out.fromFrame = from.frame;
    out.fromTInFrame = from.tInFrame;
    out.toFrame = endFrame;
    out.toTInFrame = endT;
    const uint32_t frameT = window.FrameT();
    const uint32_t unitsPerT = state.TtdTInFrame(1);
    writer.Bytes(HeaderBytes(out, unitsPerT, frameT).data(), kHeaderBytes);
    std::vector<uint8_t> image(0x10000);
    for (uint8_t bank = 0; bank < 4; ++bank)
        writer.U16(memory->GetPageForBank(bank));
    for (uint32_t a = 0; a < 0x10000; ++a)
        image[a] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(a));
    writer.Bytes(image.data(), image.size());

    struct Cursor
    {
        bool started = false;  ///< an instruction record was written
        bool done = false;     ///< the window's end reached (or the cap)
        uint32_t startT = 0;   ///< CPU T of the instruction's start
        uint16_t fetchNext = 0;
        uint8_t fetchLeft = 0;
        uint64_t interrupts = 0;
    } cursor;
    cursor.interrupts = z80->interruptsAccepted;

    const auto cycle = [&](uint8_t tag, uint16_t address, uint8_t value) {
        if (cursor.done || !cursor.started)
            return;
        if (out.cycles >= options.maxCycles)
        {
            out.truncated = true;
            cursor.done = true;
            window.Stop();
            return;
        }
        const uint32_t t = static_cast<uint32_t>(z80->t);
        const uint32_t dt = t >= cursor.startT ? t - cursor.startT : t + frameT - cursor.startT;
        writer.U8(tag);
        writer.U16(address);
        writer.U8(value);
        writer.U16(static_cast<uint16_t>(dt > 0xFFFF ? 0xFFFF : dt));
        ++out.cycles;
        switch (tag)
        {
            case 'F':
                ++out.fetches;
                break;
            case 'R':
                ++out.reads;
                break;
            case 'W':
            case 'E':
                ++out.writes;
                break;
            default:
                ++out.ports;
                break;
        }
        writer.FlushIfFull();
    };

    ReplayHooks hooks;
    // An instruction starts: its record (the moment, the registers), and the bytes its fetches will read
    hooks.instruction = [&](uint16_t pc) {
        if (cursor.done)
            return;
        const uint64_t frame = state.frame_counter;
        const uint32_t tInFrame = state.TtdTInFrame(static_cast<uint32_t>(z80->t));
        if (!Before(frame, tInFrame, endFrame, endT))
        {
            cursor.done = true;
            window.Stop();
            return;
        }
        const bool interrupted = z80->interruptsAccepted != cursor.interrupts;
        cursor.interrupts = z80->interruptsAccepted;
        uint8_t bytes[4];
        for (uint16_t i = 0; i < 4; ++i)
            bytes[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(pc + i));
        cursor.fetchNext = pc;
        cursor.fetchLeft = DecodeOp(bytes).length;
        cursor.startT = static_cast<uint32_t>(z80->t);
        cursor.started = true;
        writer.U8('X');
        writer.U32(static_cast<uint32_t>(frame));
        writer.U32(tInFrame);
        for (const uint16_t v : std::initializer_list<uint16_t>{
                 pc, static_cast<uint16_t>(z80->sp), static_cast<uint16_t>(z80->af), static_cast<uint16_t>(z80->bc),
                 static_cast<uint16_t>(z80->de), static_cast<uint16_t>(z80->hl), static_cast<uint16_t>(z80->ix),
                 static_cast<uint16_t>(z80->iy)})
            writer.U16(v);
        writer.U8(static_cast<uint8_t>((interrupted ? 1 : 0) | (z80->halted != 0 ? 2 : 0)));
        ++out.instructions;
    };
    // A bus cycle: a read at the next byte of the instruction is a fetch, the others as the CPU says
    hooks.bus = [&](char type, uint16_t address, uint8_t value) {
        switch (type)
        {
            case 'R':
                if (cursor.fetchLeft != 0 && address == cursor.fetchNext)
                {
                    ++cursor.fetchNext;
                    --cursor.fetchLeft;
                    cycle('F', address, value);
                }
                else
                    cycle('R', address, value);
                break;
            case 'W':
            case 'I':
            case 'O':
                cycle(static_cast<uint8_t>(type), address, value);
                break;
            default:  // 'N': an idle cycle, no access
                break;
        }
    };
    // A tool edit's writes (the fast tape load) under the instruction it replaced
    hooks.editWrite = [&](uint16_t address, uint8_t value) { cycle('E', address, value); };

    // Frame by frame, so a stop lands within a frame of the window's end
    const ReplayWindow::Run run = window.RunTo(endFrame + 1, 1, hooks);
    if (run == ReplayWindow::Run::Stalled && !cursor.done)
    {
        error = "the analysis instance stopped running at frame " + std::to_string(state.frame_counter);
        return false;
    }
    if (!writer.Header(HeaderBytes(out, unitsPerT, frameT)))
    {
        error = "cannot write " + options.path;
        return false;
    }
    out.bytes = writer.Written();
    return true;
}

}  // namespace analysis
