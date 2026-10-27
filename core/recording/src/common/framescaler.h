#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/// @brief Sharp output scaler for recordings: nearest-neighbor, FIT with the aspect kept, black bars - the algorithm of
/// the UI window, which also samples with Nearest.
///
/// The picture is made as large as the output allows (a 16:9 frame gets the full height of a 4:3 picture and bars left
/// and right) and centered. Every output pixel takes the nearest source pixel: nothing is blurred; at a factor that is
/// not whole, source pixels are k and k+1 output pixels wide, as in the window. An exact whole multiple (1080p in
/// 4K) is the fast path: every source pixel is the same k x k block. A picture larger than the output is sampled the
/// same way.
///
/// Built for a 3840x2160 frame at 50 Hz (33 MB per frame): the output buffer is reused, the bars are written once
/// (only the picture rectangle is rewritten per frame), a source row is gathered / expanded once and the output rows
/// that map to it are a memcpy of it.
namespace FrameScaler
{
/// Where the picture lands in the output
struct Layout
{
    uint32_t scale = 0;    ///< Whole factor when the picture is an exact multiple of the source (0 = sampled nearest)
    uint32_t width = 0;    ///< Picture width in the output
    uint32_t height = 0;   ///< Picture height in the output
    uint32_t offsetX = 0;  ///< Left bar width
    uint32_t offsetY = 0;  ///< Top bar height
};

/// Layout of a srcW x srcH picture in a dstW x dstH output (all zero when any size is zero)
Layout ComputeLayout(uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH);

/// Scales into a buffer the caller owns - the encoder's own pixel buffer (zero-copy, see FrameTarget): rows `dstStride`
/// BYTES apart, every pixel of the dstW x dstH output written (picture and black bars; the buffer may hold anything).
/// `swapRedBlue` writes B,G,R,A from an R,G,B,A source - done once per SOURCE pixel, before the pixels are repeated,
/// so the swap costs a few hundred thousand pixels, not the 8.3 million of a 4K frame.
/// False for a size of zero or an output row shorter than dstW pixels
bool ScaleInto(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* dst, size_t dstStride, uint32_t dstW,
               uint32_t dstH, bool swapRedBlue);

/// The same picture into an NV12 target (the hardware encoders' input: Y plane, then interleaved U,V at half
/// resolution, BT.601 limited range - the arithmetic of the NVENC converter). The color conversion runs once per
/// SOURCE pixel and the integer blocks are repeated in the converted planes, so a 4K frame costs a few hundred
/// thousand conversions, not 8.3 million. Chroma of a 2x2 output block is the average of the pixels under it (a
/// block inside one source pixel is exactly that pixel's chroma; a block on a source edge or on the edge of the
/// bars averages, as the old full-frame conversion did). Black bars: Y 16, U = V = 128.
/// dstW and dstH must be even. A picture larger than the output (no integer factor) goes through ScaleInto and
/// PackedToNv12. False for a size of zero, an odd output size or a stride shorter than the row
bool ScaleIntoNv12(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* yDst, size_t yStride, uint8_t* uvDst,
                   size_t uvStride, uint32_t dstW, uint32_t dstH);

/// Plain conversion of a packed R,G,B,A picture (rows `srcStride` bytes apart) to NV12, w x h even. The reference
/// of ScaleIntoNv12 and its fallback
void PackedToNv12(const uint8_t* rgba, size_t srcStride, uint32_t w, uint32_t h, uint8_t* yDst, size_t yStride,
                  uint8_t* uvDst, size_t uvStride);

/// One reusable scaler: holds the output buffer and remembers where the bars are
class Scaler
{
public:
    /// Scales a 32-bit-per-pixel picture (rows tightly packed, any channel order) into the output buffer.
    /// Returns the output (dstW * dstH * 4 bytes), valid until the next call or Reset; nullptr on bad arguments
    const uint8_t* Scale(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH);

    size_t OutputSize() const
    {
        return _output.size();
    }

    /// Forget the buffer (and its bars)
    void Reset();

private:
    std::vector<uint8_t> _output;
    uint32_t _dstW = 0;
    uint32_t _dstH = 0;
    Layout _barsFor;  ///< The layout the bars in _output were drawn for
};
}  // namespace FrameScaler
