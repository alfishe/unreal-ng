#pragma once

#include <QColor>
#include <QImage>
#include <QRect>
#include <QSize>

#include <cstdint>
#include <memory>

/// One picture of the window recording as the screen captured it: the picture alone (`image`, B,G,R,A: the CPU
/// way), the picture already in its place's size as planes (`image` Format_Grayscale8, the place's width wide;
/// RecordingFrameLayout::planes: NV12 1.5 times its height, the Y rows then the U,V rows; YUV444 3 times, the Y, U
/// and V rows), or the whole frame of the file already
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
    /// The pictures as planes at `screenRect`'s size (transfer nv12 / yuv444, ffmpeg): converted by the GPU (BT.709,
    /// limited range), read back at 1.5 (NV12, `screenRect` even) or 3 bytes a pixel instead of 4, nothing left for
    /// the CPU or ffmpeg to convert or scale
    enum class Planes
    {
        None,
        Nv12,
        Yuv444
    };
    Planes planes = Planes::None;
};
