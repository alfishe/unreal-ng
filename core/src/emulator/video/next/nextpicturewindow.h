#pragma once

/// @file nextpicturewindow.h
/// @brief Where the ZX Next picture is: the working window of its mode and the content window of its layers, in
/// frame pixels. Pure arithmetic on register values (ScreenNext feeds it the machine's), so the rules are tested
/// without a machine.
///
/// The frame is the HDMI picture of zxula_timing.vhd, 360 x 288 grid pixels doubled (720 x 576): the 320 x 256 grid (32 grid
/// pixels of border around the 256 x 192 paper) with the outer border around it (Origin). What the window of a mode is:
///   - Layer 2 in 320 x 256 or 640 x 256 (NR #70 bits 5:4 = 1 or 2) covers the whole grid, border included: the mode
///     has no border and the whole frame is the picture;
///   - every other mode is a Spectrum picture: the 256 x 192 paper inside its border.
/// The content window is smaller still when the program clips its layers: the bounding box of the clip windows of
/// the layers that are on (the ULA and LoRes cut to the paper, Layer 2, the tilemap, the sprites that have a sprite
/// to show), never larger than the frame. Nothing on = no content window.

#include <algorithm>
#include <cstdint>

namespace NextPictureWindow
{

inline constexpr int kGridWidth = 320;
inline constexpr int kGridHeight = 256;
inline constexpr int kPaperLeft = 32;  ///< grid pixels of border left of the paper
inline constexpr int kPaperTop = 32;
inline constexpr int kPaperWidth = 256;
inline constexpr int kPaperHeight = 192;
inline constexpr int kScale = 2;       ///< frame pixels per grid pixel, both axes
inline constexpr int kFrameWidth = 360;   ///< the HDMI picture in grid pixels (zxula_timing.vhd c_hdmi_xmin..xmax)
inline constexpr int kFrameHeight = 288;  ///< its lines (c_hdmi_ymin..ymax); the 60 Hz picture is 240 of them

/// Where the grid sits in the frame and which of its lines the frame shows, by the frame family (zxula_timing.vhd, the HDMI window
/// against the grid's first pixel and line): 48K and 128K 16 pixels left (24 right), 16 lines above (16 below); Pentagon 20 left
/// (20 right), 24 above (8 below). The 60 Hz picture is 360 x 240 from grid line 8 to 247: its lines stay where the 50 Hz ones are
/// (the paper at the same height), the frame's lines above and below it are black
struct FrameOrigin
{
    int left = 16;       ///< grid pixels of outer border left of the grid
    int top = 16;        ///< frame lines above the grid's line 0
    int firstLine = 0;   ///< the first grid line the frame shows
    int lastLine = kGridHeight - 1;
    bool blackOutside = false;  ///< 60 Hz: the frame's lines outside [top + firstLine, top + lastLine] are black, not border
};

/// `timingClass`: EmulatorState::ula_timing_class (1 48K, 4 Pentagon, else 128K); `sixtyHz`: the 60 Hz frame (not on the Pentagon)
inline FrameOrigin Origin(unsigned timingClass, bool sixtyHz)
{
    FrameOrigin origin;
    if (timingClass == 4)
    {
        origin.left = 20;
        origin.top = 24;
    }
    else if (sixtyHz)
    {
        origin.firstLine = 8;
        origin.lastLine = kGridHeight - 9;
        origin.blackOutside = true;
    }
    return origin;
}

/// What the layers' registers say (NextVideoRegs::Clip(window, coordinate), NR #15 / #68 / #6B / #70)
struct Inputs
{
    uint8_t clip[4][4] = {};   ///< windows: 0 Layer 2, 1 sprites, 2 ULA / LoRes, 3 tilemap; x1, x2, y1, y2
    uint8_t nr15 = 0;          ///< sprite and layers system: bit 0 sprites on, bit 1 over border, bit 5 clip over border
    uint8_t nr68 = 0;          ///< ULA control: bit 7 ULA off
    uint8_t nr6B = 0;          ///< tilemap control: bit 7 on
    uint8_t nr70 = 0;          ///< Layer 2 control: bits 5:4 resolution
    bool layer2Enabled = false;
    bool spritesVisible = false;  ///< the sprite engine holds a sprite that shows
    FrameOrigin origin;           ///< where the grid is in the frame
};

/// A rectangle of the grid, both ends included; empty when x1 < x0 or y1 < y0
struct GridBox
{
    int x0 = kGridWidth, y0 = kGridHeight, x1 = -1, y1 = -1;

    bool Empty() const { return x1 < x0 || y1 < y0; }
    void Add(int ax0, int ay0, int ax1, int ay1)
    {
        ax0 = std::max(ax0, 0);
        ay0 = std::max(ay0, 0);
        ax1 = std::min(ax1, kGridWidth - 1);
        ay1 = std::min(ay1, kGridHeight - 1);
        if (ax1 < ax0 || ay1 < ay0)
            return;
        x0 = std::min(x0, ax0);
        y0 = std::min(y0, ay0);
        x1 = std::max(x1, ax1);
        y1 = std::max(y1, ay1);
    }
};

/// A rectangle in frame pixels
struct FrameBox
{
    uint16_t x = 0, y = 0, width = 0, height = 0;

    bool Empty() const { return width == 0 || height == 0; }
};

/// The grid box in frame pixels, cut to the grid lines the frame shows
inline FrameBox ToFrame(const GridBox& box, const FrameOrigin& origin)
{
    const int y0 = std::max(box.y0, origin.firstLine), y1 = std::min(box.y1, origin.lastLine);
    if (box.Empty() || y1 < y0)
        return FrameBox{};
    return FrameBox{static_cast<uint16_t>((origin.left + box.x0) * kScale), static_cast<uint16_t>((origin.top + y0) * kScale),
                    static_cast<uint16_t>((box.x1 - box.x0 + 1) * kScale), static_cast<uint16_t>((y1 - y0 + 1) * kScale)};
}

inline bool Layer2CoversGrid(const Inputs& in)
{
    return in.layer2Enabled && ((in.nr70 >> 4) & 3) != 0;
}

/// The working window of the mode: the whole frame without a border, else the paper
inline FrameBox Screen(const Inputs& in)
{
    GridBox box;
    if (Layer2CoversGrid(in))
        box.Add(0, 0, kGridWidth - 1, kGridHeight - 1);
    else
        box.Add(kPaperLeft, kPaperTop, kPaperLeft + kPaperWidth - 1, kPaperTop + kPaperHeight - 1);
    return ToFrame(box, in.origin);
}

/// The bounding box of the layers that are on, cut by their clip windows; empty when none is
inline FrameBox Content(const Inputs& in)
{
    GridBox box;

    // The ULA (its LoRes replacement has the same window): the paper, cut by NR #1A. The border is not content
    if (!(in.nr68 & 0x80))
        box.Add(kPaperLeft + in.clip[2][0], kPaperTop + in.clip[2][2],
                kPaperLeft + std::min<int>(in.clip[2][1], kPaperWidth - 1),
                kPaperTop + std::min<int>(in.clip[2][3], kPaperHeight - 1));

    // Layer 2: 256 x 192 in paper pixels; the wide modes in the grid, X in units of two pixels
    if (in.layer2Enabled)
    {
        if (((in.nr70 >> 4) & 3) == 0)
            box.Add(kPaperLeft + in.clip[0][0], kPaperTop + in.clip[0][2],
                    kPaperLeft + std::min<int>(in.clip[0][1], kPaperWidth - 1),
                    kPaperTop + std::min<int>(in.clip[0][3], kPaperHeight - 1));
        else
            box.Add(in.clip[0][0] * 2, in.clip[0][2], in.clip[0][1] * 2 + 1, in.clip[0][3]);
    }

    // The tilemap: the grid, X in units of two pixels
    if (in.nr6B & 0x80)
        box.Add(in.clip[3][0] * 2, in.clip[3][2], in.clip[3][1] * 2 + 1, in.clip[3][3]);

    // Sprites, only when one shows. The window is in paper coordinates (32 added, as NextSprites::Output does) unless
    // the sprites go over the border, where it is the grid's when NR #15 bit 5 says so, else the whole grid
    if ((in.nr15 & 0x01) && in.spritesVisible)
    {
        if (in.nr15 & 0x02)
        {
            if (in.nr15 & 0x20)
                box.Add(in.clip[1][0] * 2, in.clip[1][2], in.clip[1][1] * 2 + 1, in.clip[1][3]);
            else
                box.Add(0, 0, kGridWidth - 1, kGridHeight - 1);
        }
        else
        {
            auto shifted = [](unsigned v) { return static_cast<int>(((((v >> 5) & 7) + 1) << 5) | (v & 0x1F)); };
            box.Add(shifted(in.clip[1][0]), shifted(in.clip[1][2]), shifted(in.clip[1][1]),
                    std::min(shifted(in.clip[1][3]), 223));
        }
    }
    return ToFrame(box, in.origin);
}

}  // namespace NextPictureWindow
