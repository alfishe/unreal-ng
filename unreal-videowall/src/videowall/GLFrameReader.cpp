#define GL_SILENCE_DEPRECATION
#include "GLFrameReader.h"

#include <QByteArray>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <cstring>

GLFrameReader::GLFrameReader() = default;

GLFrameReader::~GLFrameReader() = default;  // the owner calls Reset() with its context current

void GLFrameReader::Queue(QOpenGLFunctions* gl, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    if (_support < 0)
    {
        // Pixel-pack buffers: desktop GL 2.1, OpenGL ES 3. A software renderer reads into memory either way; the
        // buffer is one more copy there (Qt's opengl32sw llvmpipe on Windows: 4.3 -> 5.0 ms at 2048x1536)
        const QOpenGLContext* context = QOpenGLContext::currentContext();
        const QPair<int, int> version = context ? context->format().version() : qMakePair(0, 0);
        const bool es = context && context->isOpenGLES();
        const QByteArray renderer = QByteArray(reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER))).toLower();
        const bool software = renderer.contains("llvmpipe") || renderer.contains("softpipe") ||
                              renderer.contains("swiftshader") || renderer.contains("gdi generic");
        _support = (es ? version.first >= 3 : version >= qMakePair(2, 1)) && !software ? 1 : 0;
    }
    if (_support == 0 || (!_buffer.isCreated() && !_buffer.create()))
    {
        _support = 0;
        ReadSync(gl, w, h);
        return;
    }

    // The copy runs on the GPU after the frame's commands; nothing waits here. A frame queued before the last one
    // was taken is simply replaced: the recorder wants the newest
    const QSize size(w, h);
    _buffer.bind();
    if (_bufferSize != size)
    {
        _buffer.setUsagePattern(QOpenGLBuffer::StreamRead);
        _buffer.allocate(w * h * 4);
        _bufferSize = size;
    }
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    QOpenGLBuffer::release(QOpenGLBuffer::PixelPackBuffer);  // later glReadPixels into memory (grabFramebuffer) work
    _pendingSize = size;
    _pending = true;
}

const QImage& GLFrameReader::Take()
{
    if (!_pending)
        return _image;
    _pending = false;
    _buffer.bind();
    const void* data = _buffer.map(QOpenGLBuffer::ReadOnly);
    if (data)
        Flip(static_cast<const uint8_t*>(data), _pendingSize.width(), _pendingSize.height());
    _buffer.unmap();
    QOpenGLBuffer::release(QOpenGLBuffer::PixelPackBuffer);
    return _image;
}

void GLFrameReader::Reset()
{
    if (_buffer.isCreated())
        _buffer.destroy();
    _bufferSize = QSize();
    _pending = false;
    _image = QImage();
    _readBack.clear();
    _readBack.shrink_to_fit();
}

void GLFrameReader::ReadSync(QOpenGLFunctions* gl, int w, int h)
{
    _readBack.resize(static_cast<size_t>(w) * 4 * static_cast<size_t>(h));
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, _readBack.data());
    Flip(_readBack.data(), w, h);
}

/// GL rows run bottom-up, the image's top-down
void GLFrameReader::Flip(const uint8_t* bottomUp, int w, int h)
{
    if (_image.width() != w || _image.height() != h)
        _image = QImage(w, h, QImage::Format_RGBA8888);
    const size_t rowBytes = static_cast<size_t>(w) * 4;
    for (int y = 0; y < h; y++)
        std::memcpy(_image.scanLine(y), bottomUp + static_cast<size_t>(h - 1 - y) * rowBytes, rowBytes);
}
