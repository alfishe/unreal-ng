#pragma once

#include <cstdint>
#include <cstring>

/// What a framebuffer looks like on a screen, in one place.
///
/// A framebuffer stores the machine's picture in its own pixels, and those are not square: the TS-Conf and
/// Sprinter store 14 MHz dots (720 / 736 wide) one line per TV line, the ATM and Profi hi-res modes 640 / 608 wide
/// on the same 288 lines. Every CRT picture is about the 352x288 frame of the ZX family (11:9), so a consumer
/// that wants square pixels repeats each stored line. Whoever shows or records the picture asks here: the
/// window of unreal-qt, the video wall, a recording file. None of them derives the shape from the raw size.
namespace DisplayGeometry
{
/// The visible frame of the ZX family every picture is judged against
constexpr uint16_t kReferenceWidth = 352;
constexpr uint16_t kReferenceHeight = 288;

struct Size
{
    uint16_t width = 0;      ///< square-pixel picture width
    uint16_t height = 0;     ///< square-pixel picture height: the stored lines times lineRepeat
    uint8_t lineRepeat = 1;  ///< how many times each stored line is shown (1 or 2)

    float Aspect() const { return height != 0 ? static_cast<float>(width) / static_cast<float>(height) : 0.0f; }
};

/// Square-pixel picture of a stored picture of `width` x `height` pixels (the framebuffer after the viewport's crop).
/// Each stored line is shown once or twice, whichever puts the aspect closer to the reference 352:288:
/// 352x288 and the Next's 640x512 stay as they are, 720x288 / 736x288 / 640x288 / 608x288 double their lines
inline Size Compute(uint16_t width, uint16_t height)
{
    Size size;
    if (width == 0 || height == 0)
        return size;

    // aspect(1) = width / height, aspect(2) = half of it; pick the one nearer to 352:288 (compared as ratios)
    const double reference = static_cast<double>(kReferenceWidth) / kReferenceHeight;
    const double single = static_cast<double>(width) / height;
    const double doubled = single / 2.0;
    const double distanceSingle = single > reference ? single / reference : reference / single;
    const double distanceDoubled = doubled > reference ? doubled / reference : reference / doubled;

    size.lineRepeat = distanceDoubled < distanceSingle ? 2 : 1;
    size.width = width;
    size.height = static_cast<uint16_t>(height * size.lineRepeat);
    return size;
}

/// The frame a CRT shows the picture in: the reference 352:288 shape, the smallest of it that holds the stored
/// picture without squeezing a pixel (720x288 -> 720x589). unreal-qt's screens always draw in this shape, so a
/// window sized for it has no side bars whatever the machine's framebuffer is
inline Size DisplayFrame(uint16_t width, uint16_t height)
{
    Size size;
    if (width == 0 || height == 0)
        return size;

    // The side that is short of the reference ratio grows to it (integer arithmetic, rounded up)
    const uint32_t w = width, h = height;
    if (w * kReferenceHeight < h * kReferenceWidth)
    {
        size.width = static_cast<uint16_t>((h * kReferenceWidth + kReferenceHeight - 1) / kReferenceHeight);
        size.height = height;
    }
    else
    {
        size.width = width;
        size.height = static_cast<uint16_t>((w * kReferenceHeight + kReferenceWidth - 1) / kReferenceWidth);
    }
    size.lineRepeat = 1;
    return size;
}

/// Width : height of a rectangle of the frame (its working picture: a Spectrum's paper, an ATM's 640x200) as it looks
/// on the CRT. The whole frame is shown in the reference 352:288 shape, so the rectangle is that shape cut down by its
/// share of the frame: the 256x192 paper of a 352x288 frame is 4:3, the whole 720x288 TS-Conf frame 352:288, the ATM's
/// 640x200 of its 640x288 frame 1.76. A consumer with a fixed plate (a video wall tile) fits this shape into it,
/// bars left over, instead of stretching the picture to the plate. 0 when there is no rectangle
inline double WindowAspect(uint16_t frameWidth, uint16_t frameHeight, uint16_t windowWidth, uint16_t windowHeight)
{
    if (frameWidth == 0 || frameHeight == 0 || windowWidth == 0 || windowHeight == 0)
        return 0.0;
    return (static_cast<double>(windowWidth) * frameHeight * kReferenceWidth) /
           (static_cast<double>(frameWidth) * windowHeight * kReferenceHeight);
}

/// The picture a file or a snapshot of the screen holds: `width` x `height` stored pixels as the CRT shows them.
/// `aspect` is the shape the picture has on the CRT (WindowAspect for a working window; 0 = the reference 352:288, a
/// whole frame). When showing each stored line once or twice puts that shape within 5%, the pixels are kept exact
/// (352x288, a TS-Conf's 720x288 doubled to 720x576, a Spectrum's 256x192 paper); otherwise the picture grows to the
/// shape - the narrower side is stretched - so that a file never shows a hi-res mode 10% too narrow. Even sizes:
/// video codecs want them. Every video mode of every machine goes through here, nobody keeps a table of its own
inline Size Picture(uint16_t width, uint16_t height, double aspect = 0.0)
{
    Size size;
    if (width == 0 || height == 0)
        return size;
    if (aspect <= 0.0)
        aspect = static_cast<double>(kReferenceWidth) / kReferenceHeight;

    auto even = [](uint32_t value) { return static_cast<uint16_t>((value + 1) & ~1u); };

    // Exact: one or two copies of each stored line, the one nearer the wanted shape
    const double single = static_cast<double>(width) / height;
    const double doubled = single / 2.0;
    const bool useDouble = (doubled > aspect ? doubled / aspect : aspect / doubled) < (single > aspect ? single / aspect : aspect / single);
    const double shape = useDouble ? doubled : single;
    if ((shape > aspect ? shape / aspect : aspect / shape) <= 1.05)
    {
        size.lineRepeat = useDouble ? 2 : 1;
        size.width = even(width);
        size.height = even(static_cast<uint32_t>(height) * size.lineRepeat);
        return size;
    }

    // Otherwise the picture grows to the wanted shape
    size.lineRepeat = 1;
    if (single < aspect)
    {
        size.height = even(height);
        size.width = even(static_cast<uint32_t>(height * aspect + 0.999));
    }
    else
    {
        size.width = even(width);
        size.height = even(static_cast<uint32_t>(width / aspect + 0.999));
    }
    return size;
}

/// `src` (srcW x srcH RGBA) stretched by the nearest pixel into dstW x dstH (any sizes; the same size is a copy; whole
/// line repeats come out as exact copies of each line)
inline void StretchNearest(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* dst, uint32_t dstW, uint32_t dstH)
{
    if (src == nullptr || dst == nullptr || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0)
        return;
    for (uint32_t y = 0; y < dstH; y++)
    {
        const uint8_t* srcRow = src + static_cast<size_t>(static_cast<uint64_t>(y) * srcH / dstH) * srcW * 4;
        uint8_t* dstRow = dst + static_cast<size_t>(y) * dstW * 4;
        for (uint32_t x = 0; x < dstW; x++)
            std::memcpy(dstRow + static_cast<size_t>(x) * 4, srcRow + static_cast<size_t>(static_cast<uint64_t>(x) * srcW / dstW) * 4, 4);
    }
}
}  // namespace DisplayGeometry
