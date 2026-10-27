#pragma once

// Shared debugger-like workload for the GUI framework benchmark.
//
// One instance simulates what a debugger client receives from the emulator every frame:
//   - a 64K memory image with a burst of writes per frame (hex view, changed bytes highlighted),
//   - a disassembly window of 60 lines that follows a moving PC,
//   - a 500k-event timeline (5 lanes over 10 000 frames) with a zoom that animates every frame,
//   - four 640x512 RGBA images (screen + 3 layers), new content every frame.
//
// The view model (strings, aggregated timeline columns, image pointers) is prepared in Tick(), outside the
// framework's measured section, so every framework draws exactly the same data and only its own cost is measured.

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace bench
{

constexpr int kWindowW = 1600;
constexpr int kWindowH = 1000;

constexpr int kHexRows = 32;            // 32 rows x 16 bytes + ASCII column
constexpr int kHexCols = 16;
constexpr int kDisasmLines = 60;
constexpr int kTimelineLanes = 5;
constexpr int kTimelineFrames = 10000;   // the recording the timeline covers
constexpr int kTimelineEvents = 500000;
constexpr int kTimelineWidth = 1120;    // pixel columns of the timeline plot (fixed layout in every variant)
constexpr int kTimelineTicks = 20;      // ruler labels
constexpr int kTexW = 640;
constexpr int kTexH = 512;
constexpr int kTexCount = 4;
constexpr int kWritesPerFrame = 96;     // memory writes per frame, a third of them inside the hex window

struct DisasmLine
{
    std::string addr;      // "8233"
    std::string bytes;     // "ED A4"
    std::string text;      // "ldix"
    std::string comment;   // "; loop+3"
    bool isPc = false;
    bool isBreakpoint = false;
};

struct TimelineTick
{
    float x = 0;           // pixel column inside the plot
    std::string label;     // frame number
};

class Workload
{
public:
    Workload();

    /// Advance the simulated machine by one frame and rebuild the view model.
    void Tick();

    uint64_t Frame() const { return _frame; }

    // Hex view
    uint16_t HexBase() const { return _hexBase; }
    const uint8_t* Memory() const { return _memory.data(); }
    /// true when the byte at (row, col) was written this frame
    bool HexChanged(int row, int col) const { return _hexChanged[row * kHexCols + col] != 0; }
    /// "00".."FF"
    static const char* Hex2(uint8_t v) { return HexTable()[v].data(); }
    /// "0000".."FFFF" of a row address
    const std::string& HexRowAddr(int row) const { return _hexRowAddr[row]; }
    /// printable ASCII of a row (16 chars)
    const std::string& HexRowAscii(int row) const { return _hexRowAscii[row]; }

    // Disassembly
    const std::array<DisasmLine, kDisasmLines>& Disasm() const { return _disasm; }

    // Timeline: for lane l, column x: normalized density 0..1 (already aggregated from the LOD pyramid)
    float TimelineValue(int lane, int x) const { return _timelineColumns[lane * kTimelineWidth + x]; }
    const std::vector<float>& TimelineColumns() const { return _timelineColumns; }
    float TimelinePlayheadX() const { return _playheadX; }
    const std::array<TimelineTick, kTimelineTicks>& TimelineTicks() const { return _ticks; }
    static const char* LaneName(int lane);
    static uint32_t LaneColour(int lane);   // 0xRRGGBB

    // Images: RGBA8, kTexW x kTexH, new content every frame
    const uint8_t* Image(int index) const;

    /// Freeze the machine (debugger paused): Tick() becomes a no-op for the view model
    void SetPaused(bool paused) { _paused = paused; }

private:
    static const std::array<std::array<char, 3>, 256>& HexTable();
    void BuildTimeline();
    void AggregateTimeline();
    void BuildImages();

    uint64_t _frame = 0;
    bool _paused = false;
    std::mt19937 _rng{12345};

    std::vector<uint8_t> _memory;
    uint16_t _hexBase = 0x8000;
    std::array<uint8_t, kHexRows * kHexCols> _hexChanged{};
    std::array<std::string, kHexRows> _hexRowAddr;
    std::array<std::string, kHexRows> _hexRowAscii;

    uint16_t _pc = 0x8000;
    std::array<DisasmLine, kDisasmLines> _disasm;

    // timeline: per lane, event frame positions (sorted), and a LOD pyramid of counts per 2^k frames
    std::vector<std::vector<uint32_t>> _laneEvents;               // [lane][event] -> frame*256 + subframe
    std::vector<std::vector<std::vector<uint32_t>>> _lod;         // [lane][level][bucket] -> count
    std::vector<float> _timelineColumns;                          // [lane * width + x]
    float _playheadX = 0;
    std::array<TimelineTick, kTimelineTicks> _ticks;

    // 8 precomputed frames per image, cycled; content differs per frame so uploads are real
    static constexpr int kImageFrames = 8;
    std::vector<std::vector<uint8_t>> _images;                    // [image * kImageFrames + n]
};

}  // namespace bench
