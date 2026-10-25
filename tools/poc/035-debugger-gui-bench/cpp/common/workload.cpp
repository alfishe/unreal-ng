#include "workload.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bench
{

namespace
{
const char* kMnemonics[] = {"ld a,(hl)", "ldix",       "djnz loop",  "nextreg #15,a", "out (c),a",  "ld bc,#243B",
                            "push 0123", "mul d,e",    "call 9000",  "ret",           "ld (de),a",  "inc hl",
                            "add a,#10", "jr nz,8230", "ld hl,C000", "ei",            "halt",       "test #3F"};
const char* kComments[] = {"", "", "; loop", "", "; NR layer order", "", "", "; sprite x", "", "; vblank wait"};

const char* kLaneNames[kTimelineLanes] = {"INT", "line INT", "DMA", "reg writes", "copper"};
const uint32_t kLaneColours[kTimelineLanes] = {0xFF8A7A, 0xE8A33D, 0x66D19A, 0x7FB2FF, 0xC3A0FF};
const int kLaneEventCount[kTimelineLanes] = {10000, 40000, 150000, 200000, 100000};  // sum = 500 000
}  // namespace

const char* Workload::LaneName(int lane) { return kLaneNames[lane]; }
uint32_t Workload::LaneColour(int lane) { return kLaneColours[lane]; }

const std::array<std::array<char, 3>, 256>& Workload::HexTable()
{
    static const auto table = [] {
        std::array<std::array<char, 3>, 256> t{};
        for (int i = 0; i < 256; i++)
            std::snprintf(t[i].data(), 3, "%02X", i);
        return t;
    }();
    return table;
}

Workload::Workload() : _memory(65536)
{
    for (auto& b : _memory)
        b = static_cast<uint8_t>(_rng());
    _images.assign(kTexCount, std::vector<uint8_t>(size_t(kTexW) * kTexH * 4));
    BuildTimeline();
    _timelineColumns.assign(size_t(kTimelineLanes) * kTimelineWidth, 0.f);
    Tick();
}

void Workload::BuildTimeline()
{
    _laneEvents.resize(kTimelineLanes);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int lane = 0; lane < kTimelineLanes; lane++)
    {
        auto& ev = _laneEvents[lane];
        ev.reserve(kLaneEventCount[lane]);
        for (int i = 0; i < kLaneEventCount[lane]; i++)
        {
            double f;
            if (lane == 0)
                f = i + 0.01;                                   // one frame interrupt per frame
            else if (lane == 2)
                f = std::floor(u(_rng) * 2000) * 5 + u(_rng) * 0.2;  // DMA: clustered bursts
            else if (lane == 4)
                f = std::fmod(std::floor(u(_rng) * kTimelineFrames) + std::sin(i * 0.001) * 0.5 + 0.5, kTimelineFrames);
            else
                f = u(_rng) * kTimelineFrames;
            ev.push_back(static_cast<uint32_t>(f * 256));
        }
        std::sort(ev.begin(), ev.end());
    }
    // LOD pyramid: level 0 = 1 frame per bucket, level k = 2^k frames
    _lod.resize(kTimelineLanes);
    for (int lane = 0; lane < kTimelineLanes; lane++)
    {
        auto& levels = _lod[lane];
        levels.emplace_back(kTimelineFrames, 0u);
        for (uint32_t e : _laneEvents[lane])
            levels[0][std::min<uint32_t>(e >> 8, kTimelineFrames - 1)]++;
        while (levels.back().size() > 1)
        {
            const auto& prev = levels.back();
            std::vector<uint32_t> next((prev.size() + 1) / 2, 0u);
            for (size_t i = 0; i < prev.size(); i++)
                next[i / 2] += prev[i];
            levels.push_back(std::move(next));
        }
    }
}

void Workload::AggregateTimeline()
{
    // Zoom animates between 40 frames and the whole recording, centered on the playhead
    const double t = double(_frame);
    const double zoom = 0.5 - 0.5 * std::cos(t * 0.02);               // 0..1
    const double span = 40.0 * std::pow(kTimelineFrames / 40.0, zoom);
    const double playhead = std::fmod(t * 3.0, double(kTimelineFrames));
    double start = std::clamp(playhead - span * 0.5, 0.0, kTimelineFrames - span);
    const double perColumn = span / kTimelineWidth;

    for (int lane = 0; lane < kTimelineLanes; lane++)
    {
        const auto& levels = _lod[lane];
        const double expected = double(kLaneEventCount[lane]) / kTimelineFrames;  // events per frame
        for (int x = 0; x < kTimelineWidth; x++)
        {
            const double a = start + x * perColumn;
            const double b = a + perColumn;
            double count = 0;
            if (perColumn < 1.0)
            {
                // sub-frame: count exact events from the sorted list
                const auto& ev = _laneEvents[lane];
                auto lo = std::lower_bound(ev.begin(), ev.end(), uint32_t(a * 256));
                auto hi = std::lower_bound(lo, ev.end(), uint32_t(b * 256));
                count = double(hi - lo);
            }
            else
            {
                int level = std::min<int>(int(std::floor(std::log2(perColumn))), int(levels.size()) - 1);
                const double bucket = double(1u << level);
                const auto& lv = levels[level];
                for (size_t i = size_t(a / bucket); i < std::min(lv.size(), size_t(std::ceil(b / bucket))); i++)
                    count += lv[i];
            }
            const double v = 0.35 * count / (expected * perColumn);  // average density at ~35 % of the lane height
            _timelineColumns[size_t(lane) * kTimelineWidth + x] = float(std::min(1.0, v));
        }
    }
    _playheadX = float((playhead - start) / perColumn);

    // ruler: kTimelineTicks evenly spaced labels with the frame number under each
    for (int i = 0; i < kTimelineTicks; i++)
    {
        const float x = (i + 0.5f) * kTimelineWidth / kTimelineTicks;
        _ticks[i].x = x;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f", start + x * perColumn);
        _ticks[i].label = buf;
    }
}

void Workload::BuildImages()
{
    const int f = int(_frame);
    for (int img = 0; img < kTexCount; img++)
    {
        uint8_t* p = _images[img].data();
        for (int y = 0; y < kTexH; y++)
        {
            for (int x = 0; x < kTexW; x++, p += 4)
            {
                uint8_t r, g, b, a = 255;
                switch (img)
                {
                case 0:  // "composed screen": attribute-like 8x8 blocks with a moving raster bar
                {
                    const int cell = ((x >> 3) + (y >> 3) + (f >> 2)) & 7;
                    r = (cell & 2) ? 200 : 30;
                    g = (cell & 4) ? 200 : 30;
                    b = (cell & 1) ? 200 : 30;
                    if (((y + f * 4) % kTexH) < 6)
                        r = 232, g = 163, b = 61;
                    break;
                }
                case 1:  // "layer 2": scrolling gradient
                    r = uint8_t(x + f * 2);
                    g = uint8_t(y);
                    b = uint8_t((x ^ y) + f);
                    break;
                case 2:  // "sprites": moving squares on transparent
                {
                    const int sx = (x + f * 3) % 160, sy = (y + f * 2) % 128;
                    const bool on = sx < 32 && sy < 32;
                    r = on ? 255 : 0, g = on ? 138 : 0, b = on ? 122 : 0, a = on ? 255 : 0;
                    break;
                }
                default:  // "tilemap": 8x8 tiles with an index pattern
                {
                    const int t = ((x >> 3) * 7 + (y >> 3) * 13 + (f >> 3)) & 15;
                    r = uint8_t(t * 16), g = uint8_t(128 + t * 8), b = uint8_t(64 + ((x & 7) == 0 || (y & 7) == 0 ? 120 : 0));
                    break;
                }
                }
                p[0] = r, p[1] = g, p[2] = b, p[3] = a;
            }
        }
    }
}

const uint8_t* Workload::Image(int index) const { return _images[index].data(); }

void Workload::Tick()
{
    if (_paused)
        return;
    _frame++;

    // memory writes; a third of them land in the hex window
    _hexChanged.fill(0);
    std::uniform_int_distribution<int> any(0, 65535), win(0, kHexRows * kHexCols - 1), val(0, 255);
    for (int i = 0; i < kWritesPerFrame; i++)
    {
        int addr;
        if (i % 3 == 0)
        {
            const int off = win(_rng);
            addr = _hexBase + off;
            _hexChanged[off] = 1;
        }
        else
            addr = any(_rng);
        _memory[addr & 0xFFFF] = uint8_t(val(_rng));
    }
    for (int r = 0; r < kHexRows; r++)
    {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%04X", (_hexBase + r * kHexCols) & 0xFFFF);
        _hexRowAddr[r] = buf;
        std::string ascii(kHexCols, '.');
        for (int c = 0; c < kHexCols; c++)
        {
            const uint8_t b = _memory[(_hexBase + r * kHexCols + c) & 0xFFFF];
            ascii[c] = (b >= 32 && b < 127) ? char(b) : '.';
        }
        _hexRowAscii[r] = std::move(ascii);
    }

    // disassembly follows the PC: the window starts 20 lines before it
    _pc = uint16_t(0x8000 + (_frame * 7) % 0x400);
    uint16_t addr = uint16_t(_pc - 40);
    for (int i = 0; i < kDisasmLines; i++)
    {
        auto& l = _disasm[i];
        const int len = 1 + int((addr * 2654435761u) >> 30);  // 1..4 bytes, deterministic per address
        char buf[32];
        std::snprintf(buf, sizeof buf, "%04X", addr);
        l.addr = buf;
        std::string bytes;
        for (int k = 0; k < len; k++)
        {
            if (k)
                bytes += ' ';
            bytes += Hex2(_memory[(addr + k) & 0xFFFF]);
        }
        l.bytes = std::move(bytes);
        l.text = kMnemonics[(addr * 31u) % (sizeof kMnemonics / sizeof *kMnemonics)];
        l.comment = kComments[(addr * 17u) % (sizeof kComments / sizeof *kComments)];
        l.isPc = (i == 20);
        l.isBreakpoint = (addr % 23) == 0;
        addr = uint16_t(addr + len);
    }

    AggregateTimeline();
    BuildImages();
}

}  // namespace bench
