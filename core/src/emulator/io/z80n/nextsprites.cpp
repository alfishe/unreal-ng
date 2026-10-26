#include "stdafx.h"

#include "nextsprites.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "nextvideoregs.h"

void NextSprites::Reset()
{
    _epoch++;
    std::memset(_attr, 0, sizeof _attr);
    std::memset(_pattern, 0, sizeof _pattern);
    _slot = _byte = _patternHalf = _mirror = 0;
    _patternOffset = 0;
    ClearFlags();
}

void NextSprites::SoftReset()
{
    _slot = _byte = _patternHalf = _mirror = 0;
    _patternOffset = 0;
    ClearFlags();
}

uint8_t NextSprites::ReadStatus()
{
    const uint8_t value = static_cast<uint8_t>(((_tooMany || !_pendingTooMany.empty()) ? 2 : 0) | ((_collision || !_pendingCollision.empty()) ? 1 : 0));
    ClearFlags();
    return value;
}

void NextSprites::WriteSlotSelect(uint8_t value)
{
    _slot = value & 0x7F;
    _byte = 0;
    _patternHalf = (value >> 7) & 1;
    _patternOffset = static_cast<uint16_t>((((value & 0x3F) << 8) | (_patternHalf << 7)) & (kPatternBytes - 1));
}

void NextSprites::WriteAttribute(uint8_t value)
{
    _epoch++;
    SetAttribute(_slot, _byte, value);
    switch (_byte)
    {
        case 3:
            if (value & 0x40)
            {
                _byte = 4;
                return;
            }
            [[fallthrough]];
        case 4:
            _byte = 0;
            _slot = (_slot + 1) & 0x7F;
            return;
        default:
            _byte++;
    }
}

void NextSprites::WritePattern(uint8_t value)
{
    _epoch++;
    _pattern[_patternOffset & (kPatternBytes - 1)] = value;
    _patternOffset = (_patternOffset + 1) & (kPatternBytes - 1);
}

void NextSprites::WriteMirrorSprite(uint8_t value, bool tied)
{
    _mirror = value;
    if (tied)
    {
        _slot = value & 0x7F;
        _byte = 0;
        _patternHalf = (value >> 7) & 1;
        _patternOffset = static_cast<uint16_t>((((value & 0x3F) << 8) | (_patternHalf << 7)) & (kPatternBytes - 1));
    }
}

void NextSprites::WriteMirrorAttribute(unsigned byte, uint8_t value, bool increment, bool tied)
{
    _epoch++;
    if (byte > 4)
        return;
    SetAttribute(_mirror & 0x7F, byte, value);
    if (!increment)
        return;
    _mirror = static_cast<uint8_t>(((_mirror + 1) & 0x7F) | (_patternHalf << 7));
    if (tied)
    {
        _slot = _mirror & 0x7F;
        _byte = 0;
        _patternOffset = static_cast<uint16_t>((((_mirror & 0x3F) << 8) | (_patternHalf << 7)) & (kPatternBytes - 1));
    }
}

NextSprites::Effective NextSprites::Decode(unsigned s) const
{
    const uint8_t* a = _attr[s];
    const bool ext = (a[3] & 0x40) != 0;
    Effective e;
    e.x = ((a[2] & 1) << 8) | a[0];
    e.y = ((ext ? (a[4] & 1) : 0) << 8) | a[1];
    e.palette = a[2] >> 4;
    e.xMirror = (a[2] & 8) != 0;
    e.yMirror = (a[2] & 4) != 0;
    e.rotate = (a[2] & 2) != 0;
    e.visible = (a[3] & 0x80) != 0;
    e.fourBit = ext && (a[4] & 0x80);
    e.xScale = ext ? (a[4] >> 3) & 3 : 0;
    e.yScale = ext ? (a[4] >> 1) & 3 : 0;
    e.pattern = static_cast<uint8_t>(((a[3] & 0x3F) << 1) | ((ext && e.fourBit) ? ((a[4] >> 6) & 1) : 0));
    return e;
}

void NextSprites::UpdateAnchor(unsigned s, Anchor& anchor) const
{
    const Effective e = Decode(s);
    const bool ext = Extended(s);
    anchor.type1 = ext && (_attr[s][4] & 0x20);
    anchor.h4bit = e.fourBit;
    anchor.visible = e.visible;
    anchor.x = e.x;
    anchor.y = e.y;
    anchor.pattern = e.pattern;
    anchor.palette = e.palette;
    anchor.rotate = anchor.type1 && e.rotate;
    anchor.xMirror = anchor.type1 && e.xMirror;
    anchor.yMirror = anchor.type1 && e.yMirror;
    anchor.xScale = anchor.type1 ? e.xScale : 0;
    anchor.yScale = anchor.type1 ? e.yScale : 0;
}

/// A relative sprite: x / y are signed offsets from the anchor (turned, mirrored and scaled by the anchor's flags when
/// the anchor is of the unified type), its pattern and palette add to the anchor's when its flags say so
void NextSprites::Resolve(unsigned s, const Anchor& anchor, Effective& out) const
{
    const uint8_t* a = _attr[s];
    const int8_t rawX = static_cast<int8_t>(a[0]), rawY = static_cast<int8_t>(a[1]);
    int offX = anchor.rotate ? rawY : rawX;
    int offY = anchor.rotate ? rawX : rawY;
    if (anchor.rotate ^ anchor.xMirror)
        offX = -offX;
    if (anchor.yMirror)
        offY = -offY;
    out.x = (anchor.x + (offX * (1 << anchor.xScale))) & 0x1FF;
    out.y = (anchor.y + (offY * (1 << anchor.yScale))) & 0x1FF;
    const uint8_t own = a[2] >> 4;
    out.palette = (a[2] & 1) ? static_cast<uint8_t>((own + anchor.palette) & 0x0F) : own;
    bool relXm, relYm;
    if (anchor.rotate)
    {
        relXm = ((a[2] >> 2) & 1) ^ ((a[2] >> 1) & 1);
        relYm = ((a[2] >> 3) & 1) ^ ((a[2] >> 1) & 1);
    }
    else
    {
        relXm = (a[2] & 8) != 0;
        relYm = (a[2] & 4) != 0;
    }
    const bool relRot = (a[2] & 2) != 0;
    if (anchor.type1)
    {
        out.xMirror = anchor.xMirror ^ relXm;
        out.yMirror = anchor.yMirror ^ relYm;
        out.rotate = anchor.rotate ^ relRot;
        out.xScale = anchor.xScale;
        out.yScale = anchor.yScale;
    }
    else
    {
        out.xMirror = (a[2] & 8) != 0;
        out.yMirror = (a[2] & 4) != 0;
        out.rotate = relRot;
        out.xScale = (a[4] >> 3) & 3;
        out.yScale = (a[4] >> 1) & 3;
    }
    unsigned pattern = static_cast<unsigned>(((a[3] & 0x3F) << 1) | ((a[4] >> 5) & 1));
    if (a[4] & 1)
        pattern = (pattern + anchor.pattern) & 0x7F;
    out.pattern = static_cast<uint8_t>(pattern);
    out.visible = anchor.visible && (a[3] & 0x80);
    out.fourBit = anchor.h4bit;
}

namespace
{
/// S_PROCESS clocks of a sprite starting at x (9 bits): one per pixel up to `16 << scale`, but it ends when the position reaches 320
/// unless it is in the last `16 << scale` of the 512 (sprites.vhd: (hcount(8:4) and x_wrap) = x_wrap on the incremented count)
unsigned ProcessTicksOf(int x, unsigned xScale)
{
    static const unsigned kWrapMask[4] = {0x1F, 0x1E, 0x1C, 0x18};
    const int width = 16 << xScale;
    int k = 0;
    for (;; k++)
    {
        if (k + 1 == width)
            break;
        const unsigned next = static_cast<unsigned>(x + k + 1) & 0x1FF;
        if (next >= 320 && (((next >> 4) & kWrapMask[xScale]) != kWrapMask[xScale]))
            break;
    }
    return static_cast<unsigned>(k + 1);
}
}  // namespace

/// The pixel of a sprite pattern: `column` / `row` are the pattern's coordinates after scaling, mirror and rotation are applied here.
/// Returns false when it is transparent (NR #4B compares the raw pattern byte, or the nibble of a 4-bit pattern)
bool NextSprites::PatternPixel(const Effective& s, int patternCol, int rowIndex, uint8_t transparentIndex, uint8_t& index) const
{
    const bool columnMirror = s.xMirror ^ s.rotate;
    int px = columnMirror ? 15 - patternCol : patternCol;
    int py = rowIndex;
    if (s.rotate)
        std::swap(px, py);
    if (!s.fourBit)
    {
        const uint8_t raw = _pattern[(((s.pattern >> 1) << 8) | (py << 4) | px) & (kPatternBytes - 1)];
        if (raw == transparentIndex)
            return false;
        index = static_cast<uint8_t>((((raw >> 4) + s.palette) << 4) | (raw & 0x0F));
        return true;
    }
    const uint8_t raw = _pattern[(((s.pattern >> 1) << 8) | ((s.pattern & 1) << 7) | (py << 3) | (px >> 1)) & (kPatternBytes - 1)];
    const uint8_t nibble = (px & 1) ? (raw & 0x0F) : (raw >> 4);
    if (nibble == (transparentIndex & 0x0F))
        return false;
    index = static_cast<uint8_t>((s.palette << 4) | nibble);
    return true;
}

bool NextSprites::AnyVisible() const
{
    for (unsigned i = 0; i < kSprites; i++)
        if (_attr[i][3] & 0x80)
            return true;
    return false;
}

bool NextSprites::Fill(unsigned vcount, uint8_t control, uint8_t transparentIndex, unsigned lineClocks, LineBuffer& out,
                       std::vector<uint16_t>& collisions) const
{
    FillCursor cursor;
    return FillUntil(cursor, ~0u, vcount, control, transparentIndex, lineClocks, out, collisions);
}

bool NextSprites::FillUntil(FillCursor& c, unsigned stopTick, unsigned vcount, uint8_t control, uint8_t transparentIndex,
                            unsigned lineClocks, LineBuffer& out, std::vector<uint16_t>& collisions) const
{
    // sprites.vhd runs whatever NR #15 bit 0 says (the bit only gates the output in zxnext.vhd), so the collision and "too many" flags
    // are set with the sprites disabled too. A line with no visible sprite costs the engine 129 clocks and sets nothing
    if (!c.started)
    {
        c.started = true;
        collisions.clear();
        out.any = false;
        if (stopTick == ~0u && !AnyVisible())
        {
            c.done = true;
            return false;
        }
        out.any = true;
        for (Pixel& p : out.pixel)
            p.opaque = false;
    }
    const bool zeroOnTop = (control & 0x40) != 0;

    // The engine, one 28 MHz clock per step (sprites.vhd states S_START, S_QUALIFY, S_PROCESS): it fills the line buffer in index order,
    // writes every opaque pixel with an x of 0..319 whatever the clip window says (the window and the 224-line limit act at the output),
    // notes a collision (the clock of the write) when it writes onto a pixel already there and, when the line period `lineClocks` is used
    // up before it is idle, is cut off: what it has not reached is not on the line. A sprite is decoded when it qualifies (a relative one
    // against the anchor of the sprites before it)
    while (!c.done)
    {
        if (c.pixel < 0)
        {
            if (c.sprite >= kSprites)
            {
                c.done = true;
                break;
            }
            if (c.tick + 1 > stopTick)
                return false;
            c.tick++;  // S_QUALIFY
            const unsigned i = c.sprite;
            if (Relative(i))
                Resolve(i, c.anchor, c.current);
            else
            {
                c.current = Decode(i);
                UpdateAnchor(i, c.anchor);
            }
            const Effective& e = c.current;
            const int height = 16 << e.yScale;
            int dy = static_cast<int>(vcount) - e.y;
            if (dy < 0)
                dy += 512;
            if (!e.visible || dy >= height)
            {
                c.sprite++;
                continue;
            }
            c.rowIndex = e.yMirror ? 15 - (dy >> e.yScale) : (dy >> e.yScale);
            c.pixel = 0;
        }
        if (c.tick + 1 > stopTick)
            return false;
        if (++c.tick > lineClocks)
        {
            c.overtime = true;
            c.done = true;
            break;
        }
        const Effective& e = c.current;
        const int k = c.pixel;
        const unsigned hc = static_cast<unsigned>(e.x + k) & 0x1FF;
        uint8_t index;
        if (hc < kGridWidth && PatternPixel(e, k >> e.xScale, c.rowIndex, transparentIndex, index))
        {
            Pixel& p = out.pixel[hc];
            if (p.opaque)
                collisions.push_back(static_cast<uint16_t>(c.tick));
            if (!zeroOnTop || !p.opaque)
            {
                p.index = index;
                p.sprite = static_cast<uint8_t>(c.sprite);
            }
            p.opaque = true;
        }
        // spr_cur_x_wrap: a position of 320 or more ends the sprite unless it is in the last 16 << scale of the 512 (it wraps to 0)
        static const unsigned kWrapMask[4] = {0x1F, 0x1E, 0x1C, 0x18};
        const unsigned wrapMask = kWrapMask[e.xScale];
        const int width = 16 << e.xScale;
        const unsigned next = static_cast<unsigned>(e.x + k + 1) & 0x1FF;
        if (k + 1 == width || (next >= kGridWidth && (((next >> 4) & wrapMask) != wrapMask)))
        {
            c.pixel = -1;
            c.sprite++;
        }
        else
            c.pixel++;
    }
    return c.overtime || c.tick >= lineClocks;  // still busy when the next line starts (the last clock of the line is a busy one too)
}

void NextSprites::Output(const LineBuffer& line, unsigned y, uint8_t control, const NextVideoRegs& regs, Pixel* out) const
{
    if (!line.any || !(control & 0x01))
        return;
    const bool overBorder = (control & 0x02) != 0;
    const bool borderClip = (control & 0x20) != 0;
    int clip[4];
    if (overBorder)
    {
        if (!borderClip)
        {
            clip[0] = 0;
            clip[1] = 319;
            clip[2] = 0;
            clip[3] = 255;
        }
        else
        {
            clip[0] = regs.Clip(1, 0) * 2;
            clip[1] = regs.Clip(1, 1) * 2 + 1;
            clip[2] = regs.Clip(1, 2);
            clip[3] = regs.Clip(1, 3);
        }
    }
    else  // the clip window is in paper coordinates: the paper starts 32 into the grid
    {
        auto shifted = [](unsigned v) { return static_cast<int>(((((v >> 5) & 7) + 1) << 5) | (v & 0x1F)); };
        clip[0] = shifted(regs.Clip(1, 0));
        clip[1] = shifted(regs.Clip(1, 1));
        clip[2] = shifted(regs.Clip(1, 2));
        clip[3] = shifted(regs.Clip(1, 3));
    }
    if (static_cast<int>(y) < clip[2] || static_cast<int>(y) > clip[3] || (!overBorder && y >= 224))
        return;
    const int from = std::max(clip[0], 0), to = std::min(clip[1], static_cast<int>(kGridWidth) - 1);
    for (int x = from; x <= to; x++)
        if (line.pixel[x].opaque)
            out[x] = line.pixel[x];
}

void NextSprites::DrawLine(unsigned y, uint8_t control, const NextVideoRegs& regs, Pixel* out, uint8_t transparentIndex, unsigned lineClocks)
{
    LineBuffer line;
    std::vector<uint16_t> collisions;
    if (Fill(y, control, transparentIndex, lineClocks, line, collisions))
        _tooMany = true;
    if (!collisions.empty())
        _collision = true;
    Output(line, y, control, regs, out);
}

void NextSprites::PostFill(int64_t start, const std::vector<uint16_t>& collisions, bool overtime, unsigned lineClocks, int64_t now)
{
    // An event before `now` is before any read still to come: it only needs to be remembered. One at or after it waits with its clock (a
    // read on the clock of a set loses it: sprites.vhd clears the register on that edge)
    const auto post = [&](int64_t at, bool& flag, std::vector<int64_t>& pending) {
        if (at <= _lastRead)
            return;
        if (at < now)
            flag = true;
        else
            pending.insert(std::upper_bound(pending.begin(), pending.end(), at), at);
    };
    for (uint16_t tick : collisions)
        post(start + tick, _collision, _pendingCollision);
    if (overtime)  // taken where the next pass starts: the engine is not idle on that edge
        post(start + static_cast<int64_t>(lineClocks) - 1, _tooMany, _pendingTooMany);
}

uint8_t NextSprites::ReadStatus(int64_t clock)
{
    const auto take = [&](bool& flag, std::vector<int64_t>& pending) {
        bool set = flag;
        flag = false;
        auto end = std::upper_bound(pending.begin(), pending.end(), clock);
        if (end != pending.begin() && *pending.begin() < clock)
            set = true;
        pending.erase(pending.begin(), end);
        return set;
    };
    const bool collision = take(_collision, _pendingCollision);
    const bool tooMany = take(_tooMany, _pendingTooMany);
    _lastRead = clock;
    return static_cast<uint8_t>((tooMany ? 2 : 0) | (collision ? 1 : 0));
}

void NextSprites::ShiftClock(int64_t delta)
{
    for (int64_t& at : _pendingCollision)
        at += delta;
    for (int64_t& at : _pendingTooMany)
        at += delta;
    if (_lastRead != kNever)
        _lastRead += delta;
}

void NextSprites::ClearFlags()
{
    _collision = _tooMany = false;
    _pendingCollision.clear();
    _pendingTooMany.clear();
    _lastRead = kNever;
}

unsigned NextSprites::FillClocks(unsigned y) const
{
    Effective effective[kSprites];
    Anchor anchor;
    for (unsigned i = 0; i < kSprites; i++)
    {
        if (Relative(i))
            Resolve(i, anchor, effective[i]);
        else
        {
            effective[i] = Decode(i);
            UpdateAnchor(i, anchor);
        }
    }
    unsigned last = 0, drawn = 0, processed = 0;
    for (unsigned i = 0; i < kSprites; i++)
    {
        const Effective& e = effective[i];
        if (!e.visible)
            continue;
        int dy = static_cast<int>(y) - e.y;
        if (dy < 0)
            dy += 512;
        if (dy < (16 << e.yScale))
        {
            last = i;
            drawn++;
            processed += ProcessTicksOf(e.x, e.xScale);
        }
    }
    // one clock per sprite up to the last drawn one to qualify it, and the process clocks of the drawn ones
    return drawn ? (last + 1) + processed : kSprites;
}

void NextSprites::Describe(Info (&out)[kSprites]) const
{
    Anchor anchor;
    int anchorIndex = -1;
    for (unsigned i = 0; i < kSprites; i++)
    {
        Effective e;
        Info& info = out[i];
        info = Info{};
        info.extended = Extended(i);
        info.relative = Relative(i);
        if (info.relative)
        {
            Resolve(i, anchor, e);
            info.anchor = anchorIndex;
            info.unified = anchor.type1;
            info.offsetX = static_cast<int8_t>(_attr[i][0]);
            info.offsetY = static_cast<int8_t>(_attr[i][1]);
            info.relativePattern = (_attr[i][4] & 1) != 0;
            info.relativePalette = (_attr[i][2] & 1) != 0;
        }
        else
        {
            e = Decode(i);
            UpdateAnchor(i, anchor);
            anchorIndex = static_cast<int>(i);
            info.unified = anchor.type1;
        }
        info.x = e.x;
        info.y = e.y;
        info.fourBit = e.fourBit;
        info.pattern = e.fourBit ? e.pattern : static_cast<unsigned>(e.pattern >> 1);
        info.palette = e.palette;
        info.xMirror = e.xMirror;
        info.yMirror = e.yMirror;
        info.rotate = e.rotate;
        info.visible = e.visible;
        info.xScale = 1u << e.xScale;
        info.yScale = 1u << e.yScale;
    }
}
