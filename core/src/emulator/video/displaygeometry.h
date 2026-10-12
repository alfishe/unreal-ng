#pragma once

#include <cstdint>

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
}  // namespace DisplayGeometry
