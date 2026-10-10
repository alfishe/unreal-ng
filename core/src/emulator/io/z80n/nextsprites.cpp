#include "stdafx.h"

#include "nextsprites.h"

#include <cstring>

#include "nextvideoregs.h"

void NextSprites::Reset()
{
    std::memset(_attr, 0, sizeof _attr);
    std::memset(_pattern, 0, sizeof _pattern);
    _slot = _byte = _patternHalf = _mirror = 0;
    _patternOffset = 0;
    _collision = _tooMany = false;
}

uint8_t NextSprites::ReadStatus()
{
    const uint8_t value = static_cast<uint8_t>((_tooMany ? 2 : 0) | (_collision ? 1 : 0));
    _tooMany = _collision = false;
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

void NextSprites::DrawSprite(const Effective& s, unsigned y, const int clip[4], bool overBorder, Pixel* out, bool* occupied,
                             uint8_t transparentIndex)
{
    if (!s.visible)
        return;
    const int height = 16 << s.yScale, width = 16 << s.xScale;
    int dy = static_cast<int>(y) - s.y;
    if (dy < 0)
        dy += 512;
    if (dy >= height)
        return;
    if (static_cast<int>(y) < clip[2] || static_cast<int>(y) > clip[3])
        return;
    if (!overBorder && y >= 224)
        return;
    const int row = dy >> s.yScale;
    const int rowIndex = s.yMirror ? 15 - row : row;
    const bool columnMirror = s.xMirror ^ s.rotate;
    for (int col = 0; col < width; col++)
    {
        const int x = (s.x + col) & 0x1FF;
        if (x >= static_cast<int>(kGridWidth) || x < clip[0] || x > clip[1])
            continue;
        const int patternCol = col >> s.xScale;
        int px = columnMirror ? 15 - patternCol : patternCol;
        int py = rowIndex;
        if (s.rotate)
            std::swap(px, py);
        uint8_t index;
        if (!s.fourBit)
        {
            const uint8_t raw = _pattern[(((s.pattern >> 1) << 8) | (py << 4) | px) & (kPatternBytes - 1)];
            if (raw == transparentIndex)
                continue;
            index = static_cast<uint8_t>((((raw >> 4) + s.palette) << 4) | (raw & 0x0F));
        }
        else
        {
            const uint8_t raw = _pattern[(((s.pattern >> 1) << 8) | ((s.pattern & 1) << 7) | (py << 3) | (px >> 1)) & (kPatternBytes - 1)];
            const uint8_t nibble = (px & 1) ? (raw & 0x0F) : (raw >> 4);
            if (nibble == (transparentIndex & 0x0F))
                continue;
            index = static_cast<uint8_t>((s.palette << 4) | nibble);
        }
        if (occupied[x])
            _collision = true;
        occupied[x] = true;
        out[x].index = index;
        out[x].opaque = true;
    }
}

void NextSprites::DrawLine(unsigned y, uint8_t control, const NextVideoRegs& regs, Pixel* out, uint8_t transparentIndex)
{
    if (!(control & 0x01))
        return;
    const bool overBorder = (control & 0x02) != 0;
    const bool zeroOnTop = (control & 0x40) != 0;
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
    unsigned cycles = 0;
    for (unsigned i = 0; i < kSprites; i++)
    {
        cycles++;
        const Effective& e = effective[i];
        if (!e.visible)
            continue;
        int dy = static_cast<int>(y) - e.y;
        if (dy < 0)
            dy += 512;
        if (dy < (16 << e.yScale))
            cycles += 16u << e.xScale;
    }
    if (cycles > kLineBudget)
        _tooMany = true;

    bool occupied[kGridWidth] = {};
    if (zeroOnTop)
        for (int i = kSprites - 1; i >= 0; i--)
            DrawSprite(effective[i], y, clip, overBorder, out, occupied, transparentIndex);
    else
        for (unsigned i = 0; i < kSprites; i++)
            DrawSprite(effective[i], y, clip, overBorder, out, occupied, transparentIndex);
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
