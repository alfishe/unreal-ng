#pragma once

#include <QColor>
#include <QImage>
#include <QRect>
#include <QSize>

#include <cstdint>
#include <memory>

/// One picture of the window recording as the screen captured it: the picture alone (`image`, B,G,R,A: the CPU
/// way), the picture already in its place's size as NV12 (`image` Format_Grayscale8, the place's width x 1.5 times
/// its height: the Y rows, then the U,V rows; RecordingFrameLayout::nv12), or the whole frame of the file already
/// composed by the GPU in the encoder's own buffer (`frame`: a CVPixelBufferRef on macOS, the GL window and the
/// hardware encoder; no copy through the CPU)
struct RecordingPicture
{
    QImage image;
    std::shared_ptr<void> frame;
    uint64_t number = 0;  ///< the emulated frame shown (0: not known)

    bool empty() const { return image.isNull() && !frame; }
};

/// The file's frame as the GPU may compose it (setRecordingFrame): its size, where the picture goes in it, the color
/// around. Empty size: the screen gives pictures alone
struct RecordingFrameLayout
{
    QSize size;
    QRect screenRect;
    QColor background;
    int scale = 1;  ///< the picture in scale x scale blocks first (1..4)
    /// The pictures as NV12 at `screenRect`'s size (transfer nv12, ffmpeg): converted by the GPU, read back at 1.5
    /// bytes a pixel instead of 4, nothing left for the CPU to convert. `screenRect` even
    bool nv12 = false;
};
