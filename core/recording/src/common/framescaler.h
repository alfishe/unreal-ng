#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/// @brief Sharp output scaler for recordings: nearest-neighbor, aspect kept, black bars (the algorithm of the UI
/// window, which also samples with Nearest).
///
/// A picture that grows is multiplied by the largest INTEGER factor that fits the output, so every source pixel
/// becomes the same k x k block - no uneven pixel widths, no blur - and is centered in the output; the rest is black.
/// A picture that is larger than the output (a videowall window above the file size) is sampled nearest, aspect kept.
///
/// Built for a 3840x2160 frame at 50 Hz (33 MB per frame): the output buffer is reused, the bars are written once
/// (only the picture rectangle is rewritten per frame), a source row is expanded once and the other k-1 rows of its
/// block are a memcpy of it.
namespace FrameScaler
{
/// Where the picture lands in the output
struct Layout
{
    uint32_t scale = 0;    ///< Integer factor (0 = the picture is sampled nearest, not multiplied)
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
