#pragma once

/// @file pictureview.h
/// @brief Which part of a frame a window shows: the whole frame, the working picture ("screen": the paper of a
/// Spectrum, the whole frame of a mode without a border), or the content (the part the layers draw into, inside
/// the working picture). The window setting `view` of every automation surface (WindowDisplay), one rule for every
/// front end: unreal-qt and the Steam Deck client ask here for the rectangle to cut out and the shape to draw it in,
/// and fit that shape into the window by height, aspect kept, bars where the window is wider.
///
/// The choice is made per frame from the geometry latched with the frame (PictureGeometry), so a mode switch or a
/// program that moves its clip window changes the cut at once, and the cut always lies inside the frame.

#include <algorithm>
#include <cstdint>

#include "emulator/video/displaygeometry.h"
#include "emulator/video/pictureviewmode.h"
#include "emulator/video/screen.h"

namespace PictureView
{

namespace detail
{
inline bool Inside(const PictureRect& r, uint16_t frameWidth, uint16_t frameHeight)
{
    return r.width > 0 && r.height > 0 && static_cast<uint32_t>(r.x) + r.width <= frameWidth &&
           static_cast<uint32_t>(r.y) + r.height <= frameHeight;
}
}  // namespace detail

/// The rectangle of the frame `mode` shows: always non-empty and inside the frame (an empty or stray window falls back
/// to the next wider one, the whole frame last)
inline PictureRect Source(const PictureGeometry& geometry, Mode mode)
{
    const PictureRect whole{0, 0, geometry.width, geometry.height};
    if (mode == Mode::Content && detail::Inside(geometry.contentWindow, geometry.width, geometry.height))
        return geometry.contentWindow;
    if (mode != Mode::Full && detail::Inside(geometry.screenWindow, geometry.width, geometry.height))
        return geometry.screenWindow;
    return whole;
}

/// The shape the rectangle has on the CRT (width : height), as DisplayGeometry::WindowAspect: the whole frame is the
/// reference 352:288 whatever its stored pixels are. 0 for an empty frame
inline double Aspect(const PictureGeometry& geometry, const PictureRect& source)
{
    return DisplayGeometry::WindowAspect(geometry.width, geometry.height, source.width, source.height);
}

/// What a front end draws of the frame it holds: the rectangle and its shape. `geometry` came with the pixels, but a
/// consumer may hold pixels of another size (a mode switch between the copy and the draw, a source that gives no
/// geometry): a geometry that does not match the `width` x `height` of the pixels is ignored and the whole frame is cut
struct Cut
{
    PictureRect source;
    double aspect = 0.0;  ///< 0: no frame
};

inline Cut CutOf(const PictureGeometry& geometry, uint16_t width, uint16_t height, Mode mode)
{
    PictureGeometry g;
    if (geometry.width == width && geometry.height == height)
        g = geometry;
    g.width = width;
    g.height = height;
    Cut cut;
    cut.source = Source(g, mode);
    cut.aspect = Aspect(g, cut.source);
    return cut;
}

/// The picture fitted into a window of `windowWidth` x `windowHeight`: the whole height, centred, aspect kept
/// (narrower than the window leaves bars at the sides; a picture wider than the window is fitted by width instead,
/// so it never leaves the window)
struct Placement
{
    uint32_t x = 0, y = 0, width = 0, height = 0;
};

inline Placement Fit(double aspect, uint32_t windowWidth, uint32_t windowHeight)
{
    Placement p;
    if (aspect <= 0.0 || windowWidth == 0 || windowHeight == 0)
        return p;
    p.height = windowHeight;
    p.width = static_cast<uint32_t>(windowHeight * aspect + 0.5);
    if (p.width > windowWidth)
    {
        p.width = windowWidth;
        p.height = std::min(windowHeight, static_cast<uint32_t>(windowWidth / aspect + 0.5));
    }
    p.x = (windowWidth - p.width) / 2;
    p.y = (windowHeight - p.height) / 2;
    return p;
}

}  // namespace PictureView
