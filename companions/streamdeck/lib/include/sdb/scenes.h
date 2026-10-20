// Ready-made scenes for the horizontal display (Plus XL 1200x100, Plus 800x100; they scale to any width, height ~100).
// Pure functions: params -> Rgb. `frame` drives animation (call with frame++ at 10-20 fps).
#pragma once
#include <string>
#include <vector>

#include "sdb/draw.h"
#include "sdb/icons.h"

namespace sdb::scenes
{

struct Size
{
    int w = 1200, h = 100;
};

// L-R balance meter: value -100 (full left) .. +100 (full right).
[[nodiscard]] Rgb Balance(Size, int value, int frame = 0);

// ZX Spectrum style tape loader: flashing border stripes, spinning cassette, "PROGRAM: NAME", block progress bar.
struct Loading
{
    std::string name = "UNREAL";
    bool bytes = false;   // "BYTES:" header block instead of "PROGRAM:"
    bool pilot = false;   // pilot tone (red/cyan regular stripes) vs data (yellow/blue irregular)
    int progress = 0;     // 0..100
    bool disk = false;    // disk icon instead of tape
    std::string iconSvg;  // optional SVG icon file (e.g. unreal-ng resources/icons/tape.svg) instead of the drawn one
};
[[nodiscard]] Rgb LoadingScene(Size, const Loading&, int frame = 0);

enum class Status
{
    Ok,
    Error,
    Warn,
    Paused,
    Resumed,
    Muted,
    Unmuted,
    Info,
    Saved,
    Reset
};
[[nodiscard]] bool StatusByName(const std::string& n, Status& out);
[[nodiscard]] Rgb StatusScene(Size, Status, const std::string& title = {}, const std::string& subtitle = {},
                              int frame = 0, const std::string& iconSvg = {});

struct Adapter
{
    std::string name = "-";
    int level = 0;
    bool muted = false;
};
[[nodiscard]] Rgb VolumeTile(int w, int h, const Adapter&, bool selected, int index,
                             int frame = 0);  // one dial's 200x100 tile
[[nodiscard]] Rgb Volume(Size, const std::vector<Adapter>&, int selected, int tileW = 200, int frame = 0);

// Time-travel debugging timeline.
struct Ttd
{
    int pos = 0, total = 1000;  // frame cursor, recorded length
    std::vector<int> checkpoints, breakpoints, hits;
    bool reverse = false;     // replaying backwards
    bool live = false;        // cursor at the live edge
    long long absStart = -1;  // >= 0: header shows absolute emulator frames start..end and the position
    double fps = 0;           // > 0: also show time (position / length), e.g. 48.83 for Pentagon
    std::string badge;        // overrides the LIVE/REVERSE/REPLAY badge text
};
[[nodiscard]] Rgb TtdScene(Size, const Ttd&, int frame = 0);

// TTD off, machine running (or paused): green, no frame counter.
[[nodiscard]] Rgb TtdLive(Size, bool paused, int frame = 0);
// TTD recording: red, indeterminate progress (no jumping bar), "FRAME from TO to" + recorded time.
[[nodiscard]] Rgb TtdRecording(Size, long long fromFrame, long long toFrame, double fps, int frame = 0);
[[nodiscard]] std::string ClockText(double seconds);  // m:ss

// Debugger views.
struct Reg
{
    std::string name;
    int value = 0;
    int prev = -1;
};  // prev >= 0 => highlight a change
[[nodiscard]] Rgb Registers(Size, const std::vector<Reg>&, int flags = -1, int tileW = 200,
                            int frame = 0);  // one tile per dial
struct AsmLine
{
    int addr = 0;
    std::string text;
};
[[nodiscard]] Rgb Disasm(Size, const std::vector<AsmLine>&, int pcIndex, const std::vector<int>& breakpointAddrs,
                         int tstates = -1, int frame = 0);
[[nodiscard]] Rgb Memory(Size, int addr, const std::vector<int>& bytes, const std::vector<int>& highlight = {},
                         int frame = 0);  // 4 rows x 20 bytes
struct Frame
{
    int addr = 0;
    std::string name;
};
[[nodiscard]] Rgb CallStack(Size, const std::vector<Frame>&, int current, int frame = 0);

[[nodiscard]] Rgb Configs(Size, const std::vector<std::string>& names, int selected,
                          int frame = 0);                                 // machine config carousel
[[nodiscard]] Rgb Marquee(Size, const std::string& text, int frame = 0);  // rainbow scrolling text
[[nodiscard]] Rgb Splash(Size, int frame = 0);

}  // namespace sdb::scenes
