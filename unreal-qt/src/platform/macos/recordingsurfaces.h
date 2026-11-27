#pragma once

#ifdef __APPLE__

#include <cstddef>
#include <cstdint>
#include <functional>
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
    /// A frame composed, the GPU maybe still at it
    struct Pending
    {
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

    /// Clears a new frame to `background` (0..1), then `draw` draws into its framebuffer (GL row r: the frame's row r
    /// from the top; false: failed); a fence behind it, flushed
    bool Compose(const float background[3], const std::function<bool(unsigned framebuffer)>& draw, Pending& out);
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
    /// A surface of the pool's with the texture and the framebuffer that draw into it. Kept while the pool hands the
    /// surface out again (it has a few, frame after frame): a texture made anew each frame cost the GUI thread 4 % of
    /// a core at 50 frames/s (the driver wires the new texture's memory at the next submit)
    struct Target
    {
        void* surface = nullptr;  ///< IOSurfaceRef, retained: its address stays this surface's while held
        unsigned texture = 0;
        unsigned framebuffer = 0;
        uint64_t used = 0;
    };
    static constexpr int kTargets = 8;  ///< more than the pool keeps in use; the least recently used goes

    Target* TargetFor(void* surface);
    void ReleaseTargets();

    void* _pool = nullptr;  ///< CVPixelBufferPoolRef
    int _width = 0;
    int _height = 0;
    Target _targets[kTargets] = {};
    uint64_t _uses = 0;
};

#endif  // __APPLE__
