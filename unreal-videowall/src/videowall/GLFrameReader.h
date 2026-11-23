#pragma once

#include <QImage>
#include <QOpenGLBuffer>
#include <QSize>

#include <vector>

class QOpenGLFunctions;

/// Reads the current framebuffer back into a top-down R,G,B,A image without stalling the paint: Queue() starts the
/// copy into a pixel-pack buffer right after the frame is drawn, Take() maps it when the picture is wanted (the
/// recorder's tick, a few ms later, when the GPU has long finished). On a context without pixel-pack buffers
/// (OpenGL ES 2, desktop GL below 2.1) or a software renderer Queue() reads synchronously, as before.
/// Both calls need the drawing context current.
class GLFrameReader
{
public:
    GLFrameReader();
    ~GLFrameReader();

    /// The frame just drawn (w x h pixels of the bound framebuffer, bottom row first in GL)
    void Queue(QOpenGLFunctions* gl, int w, int h);
    /// True when Queue() left a frame that Take() has not converted yet
    bool HasPending() const { return _pending; }
    /// The last queued frame, top row first; null before the first Queue()
    const QImage& Take();
    /// The last taken frame (no GL call)
    const QImage& Image() const { return _image; }
    /// Frees the buffers and the image (the context must be current when buffers exist)
    void Reset();

private:
    void ReadSync(QOpenGLFunctions* gl, int w, int h);
    void Flip(const uint8_t* bottomUp, int w, int h);

    int _support = -1;  ///< -1 not checked yet, 0 synchronous read, 1 pixel-pack buffer
    QOpenGLBuffer _buffer{QOpenGLBuffer::PixelPackBuffer};
    QSize _bufferSize;
    QSize _pendingSize;
    bool _pending = false;
    QImage _image;
    std::vector<uint8_t> _readBack;  ///< the synchronous fallback's memory
};
