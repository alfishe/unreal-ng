#include "sprintergamevideo.h"

#include <algorithm>
#include <cstddef>

#include "emulator/video/sprinter/sprintervideoram.h"

namespace
{
constexpr uint32_t kLinePixels = SprinterVideoRenderer::kLinePixels;

inline uint32_t Wrap(int32_t value, uint32_t modulus)
{
    const int32_t m = static_cast<int32_t>(modulus);
    const int32_t r = value % m;
    return static_cast<uint32_t>(r < 0 ? r + m : r);
}

inline uint32_t Lines(const SprinterVideoInputs& in)
{
    return in.lines ? in.lines : 320u;
}

/// Square coordinates of beam pixel (x, y) under grid offset `offset`
inline uint32_t GridA16(const SprinterVideoInputs& in, uint32_t x, uint8_t offset)
{
    return Wrap(static_cast<int32_t>(x) + 2 * static_cast<int32_t>(offset & 0x0F) -
                    static_cast<int32_t>(SprinterVideoRenderer::kBorderLeft) - in.holdX,
                kLinePixels);
}

inline uint32_t GridB8(const SprinterVideoInputs& in, uint32_t y, uint8_t offset)
{
    return Wrap(static_cast<int32_t>(y) + static_cast<int32_t>(offset >> 4) -
                    static_cast<int32_t>(SprinterVideoRenderer::kBorderTop) - in.holdY,
                Lines(in));
}

inline const uint8_t* ModeBytes(const SprinterVideoInputs& in, uint32_t a16, uint32_t b8)
{
    return in.vram + SprinterVideoRam::ModeAddress(static_cast<uint8_t>(a16 >> 4), static_cast<uint8_t>(b8 >> 3), in.modePage);
}

/// The square the beam is in ends here and has bit 2: its Mode3 is the new offset
inline uint8_t AfterSquare(const uint8_t* mode, uint8_t offset)
{
    return (mode[0] & 0x04) ? mode[3] : offset;
}
}  // namespace

/// region <Pixels>

uint32_t SprinterGameVideo::PixelAddress(const uint8_t* mode, uint32_t sub, uint32_t row)
{
    // Corner: column ((Mode0 & 3) << 8) | Mode1, row Mode2; one byte per 2 beam pixels
    const uint32_t column = ((static_cast<uint32_t>(mode[0] & 0x03) << 8) | mode[1]) + (sub >> 1);
    const uint32_t line = (static_cast<uint32_t>(mode[2]) + row) & 0xFFu;
    return line * SprinterVideoRam::kRowBytes + (column & 0x3FFu);
}

uint32_t SprinterGameVideo::Pen(const SprinterVideoInputs& in, const uint8_t* mode, uint32_t sub, uint32_t row)
{
    const uint8_t m0 = mode[0];
    if ((m0 >> 5) == 7)
        return (m0 & 0x0C) == 0x0C ? kPenText : kPenText + static_cast<uint32_t>(in.border & 7) * 9u;  // blank / border
    return (static_cast<uint32_t>(m0 >> 6) << 8) + in.vram[PixelAddress(mode, sub, row)];
}

template <typename Sink>
uint8_t SprinterGameVideo::Run(const SprinterVideoInputs& in, uint8_t offset, uint32_t from, uint32_t to, Sink&& sink)
{
    // Naive v1 (performance guidelines rule 5): the mode bytes once per square segment, the pixel byte per
    // pixel, as the Standard renderer
    const uint32_t frameEnd = Lines(in) * kLinePixels;
    to = std::min(to, frameEnd);
    uint32_t p = from;
    while (p < to)
    {
        const uint32_t y = p / kLinePixels;
        uint32_t x = p - y * kLinePixels;
        const uint32_t lineEnd = std::min(to - y * kLinePixels, kLinePixels);
        while (x < lineEnd)
        {
            const uint32_t a16 = GridA16(in, x, offset);
            const uint32_t b8 = GridB8(in, y, offset);
            const uint32_t sub0 = a16 & 15;
            const uint32_t run = std::min(16 - sub0, lineEnd - x);
            const uint8_t* mode = ModeBytes(in, a16, b8);
            if (y < kVisibleLines && x < kVisibleWidth)
            {
                const uint32_t visibleEnd = std::min(x + run, kVisibleWidth);
                uint32_t sub = sub0;
                for (uint32_t px = x; px < visibleEnd; px++, sub++)
                    sink(px, y, Pen(in, mode, sub, b8 & 7));
            }
            x += run;
            if (sub0 + run == 16)
                offset = AfterSquare(mode, offset);
        }
        p = y * kLinePixels + x;
    }
    return offset;
}

uint8_t SprinterGameVideo::RunRegister(const SprinterVideoInputs& in, uint8_t offset, uint32_t from, uint32_t to)
{
    const uint32_t frameEnd = Lines(in) * kLinePixels;
    to = std::min(to, frameEnd);
    uint32_t p = from;
    while (p < to)
    {
        const uint32_t y = p / kLinePixels;
        uint32_t x = p - y * kLinePixels;
        const uint32_t lineEnd = std::min(to - y * kLinePixels, kLinePixels);
        while (x < lineEnd)
        {
            const uint32_t a16 = GridA16(in, x, offset);
            const uint32_t sub0 = a16 & 15;
            const uint32_t run = std::min(16 - sub0, lineEnd - x);
            x += run;
            if (sub0 + run == 16)
                offset = AfterSquare(ModeBytes(in, a16, GridB8(in, y, offset)), offset);
        }
        p = y * kLinePixels + x;
    }
    return offset;
}

/// endregion </Pixels>

/// region <SprinterVideoRenderer>

void SprinterGameVideo::DrawSpan(const SprinterVideoInputs& in, uint32_t y, uint32_t x0, uint32_t x1, uint32_t* out) const
{
    const uint32_t start = y * kLinePixels + x0;
    const uint8_t offset = RunRegister(in, _state.frameOffset, 0, start);
    Run(in, offset, start, y * kLinePixels + x1, [&](uint32_t x, uint32_t, uint32_t pen) {
        out[x - x0] = in.palette[pen & (SprinterVideoRam::kPens - 1)];
    });
}

void SprinterGameVideo::DrawSpanPlaneB(const SprinterVideoInputs& in, uint32_t y, uint32_t x0, uint32_t x1, uint32_t* out,
                                       uint16_t* planeB) const
{
    DrawSpan(in, y, x0, x1, out);
    std::fill(planeB, planeB + (x1 - x0), uint16_t(0));  // not a ZX picture
}

/// endregion </SprinterVideoRenderer>

/// region <SprinterBeamVideo>

void SprinterGameVideo::Start(uint64_t frame, uint32_t beamT)
{
    // The configuration's registers start cleared (the PLD's power-up value: no offset)
    _state = SprinterGameVideoState{};
    _state.beamT = beamT;
    _state.frame = frame;
}

void SprinterGameVideo::Advance(const SprinterVideoInputs& in, uint64_t frame, uint32_t toT, uint32_t* framebuffer,
                                uint16_t* planeB)
{
    if (frame != _state.frame)
        CloseFrame(in, frame);
    if (toT <= _state.beamT)
        return;
    const uint32_t from = _state.beamT * kFramePixelsPerT;
    const uint32_t to = toT * kFramePixelsPerT;
    if (framebuffer && in.vram && in.palette)
    {
        _state.offset = Run(in, _state.offset, from, to, [&](uint32_t x, uint32_t y, uint32_t pen) {
            const size_t index = static_cast<size_t>(y) * kVisibleWidth + x;
            framebuffer[index] = in.palette[pen & (SprinterVideoRam::kPens - 1)];
            if (planeB)
                planeB[index] = 0;
        });
    }
    else if (in.vram)
        _state.offset = RunRegister(in, _state.offset, from, to);
    _state.beamT = toT;
}

void SprinterGameVideo::CloseFrame(const SprinterVideoInputs& in, uint64_t frame)
{
    if (frame == _state.frame)
        return;  // this frame is open already
    if (in.vram)
        _state.offset = RunRegister(in, _state.offset, _state.beamT * kFramePixelsPerT, Lines(in) * kLinePixels);
    _state.frameOffset = _state.offset;
    _state.beamT = 0;
    _state.frame = frame;
}

void SprinterGameVideo::Redraw(const SprinterVideoInputs& in, uint32_t fromT, uint32_t toT, uint32_t* framebuffer,
                               uint16_t* planeB) const
{
    if (!in.vram || !in.palette || !framebuffer || toT <= fromT)
        return;
    const uint32_t from = fromT * kFramePixelsPerT;
    const uint8_t offset = RunRegister(in, _state.frameOffset, 0, from);
    Run(in, offset, from, toT * kFramePixelsPerT, [&](uint32_t x, uint32_t y, uint32_t pen) {
        const size_t index = static_cast<size_t>(y) * kVisibleWidth + x;
        framebuffer[index] = in.palette[pen & (SprinterVideoRam::kPens - 1)];
        if (planeB)
            planeB[index] = 0;
    });
}

void SprinterGameVideo::FramePens(const SprinterVideoInputs& in, uint16_t* pens) const
{
    if (!in.vram || !pens)
        return;
    Run(in, _state.frameOffset, 0, Lines(in) * kLinePixels, [&](uint32_t x, uint32_t y, uint32_t pen) {
        pens[static_cast<size_t>(y) * kVisibleWidth + x] = static_cast<uint16_t>(pen & (SprinterVideoRam::kPens - 1));
    });
}

/// endregion </SprinterBeamVideo>
