#pragma once

#ifdef __APPLE__

#include <cstddef>
#include <cstdint>
#include <memory>

/// The window recording's frames with no copy through the CPU (macOS, the GL window, the hardware encoder): the GPU
/// composes the whole frame of the file (the background, the picture drawn for the recording in its place) straight
/// into a pixel buffer of the encoder's kind (B,G,R,A, IOSurface-backed), which goes to AVAssetWriter as it is. The
/// CPU way read the picture back, copied it into an image, then into the encoder's buffer: 4 passes over 8 MB a frame
/// at 1080p (45 % CPU), each turning the GPU's tiled layout over on the CPU first
///
/// Every call but the pixel lock wants the window's GL context current
class RecordingSurfaces
{
public:
    static constexpr int kSlots = 3;  ///< frames in flight: composed, waiting for the GPU, taken a refresh later

    /// A frame composed, the GPU maybe still at it
    struct Pending
    {
        int slot = -1;
        void* fence = nullptr;              ///< GLsync
        std::shared_ptr<void> pixelBuffer;  ///< CVPixelBufferRef (released by the last owner)
    };

    RecordingSurfaces();
    ~RecordingSurfaces();
    RecordingSurfaces(const RecordingSurfaces&) = delete;
    RecordingSurfaces& operator=(const RecordingSurfaces&) = delete;

    /// The frames' size (a pool of buffers of it). False: no pool
    bool Configure(int width, int height);
    int Width() const { return _width; }
    int Height() const { return _height; }

    /// Clears a new frame to `background` (0..1), then blits `from` (x, y, width, height; GL rows, from the bottom) of
    /// the framebuffer `source` into `to` (x, y, width, height; rows from the top), turned over; a fence behind it,
    /// flushed. `slot` must not be pending
    bool Compose(int slot, unsigned source, const int from[4], const int to[4], bool linear, const float background[3],
                 Pending& out);
    /// The frame once the GPU drew it (a second at most), else null; the pending is done either way
    std::shared_ptr<void> Finish(Pending& pending);
    /// A frame not wanted
    void Drop(Pending& pending);
    /// The GL objects and the pool
    void Release();

    /// The frame's pixels for the CPU (B,G,R,A, top row first), any thread: the keyboard drawn over it
    static bool LockPixels(void* pixelBuffer, uint8_t*& data, size_t& stride);
    static void UnlockPixels(void* pixelBuffer);

private:
    void Unbind(int slot);

    void* _pool = nullptr;  ///< CVPixelBufferPoolRef
    int _width = 0;
    int _height = 0;
    unsigned _textures[kSlots] = {};
    unsigned _framebuffers[kSlots] = {};
};

#endif  // __APPLE__
