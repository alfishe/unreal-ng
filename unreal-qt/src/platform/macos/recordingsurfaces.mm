#include "recordingsurfaces.h"

#define GL_SILENCE_DEPRECATION

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <OpenGL/CGLIOSurface.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>

RecordingSurfaces::RecordingSurfaces() = default;

RecordingSurfaces::~RecordingSurfaces()
{
    // The GL objects go in Release (the context current); the pool needs none
    if (_pool)
        CVPixelBufferPoolRelease(static_cast<CVPixelBufferPoolRef>(_pool));
}

bool RecordingSurfaces::Configure(int width, int height)
{
    if (_pool && width == _width && height == _height)
        return true;
    if (_pool)
        CVPixelBufferPoolRelease(static_cast<CVPixelBufferPoolRef>(_pool));
    _pool = nullptr;
    _width = 0;
    _height = 0;
    if (width <= 0 || height <= 0)
        return false;
    @autoreleasepool
    {
        // The encoder's own kind of buffer (videotoolbox_encoder.mm): B,G,R,A, IOSurface-backed, so the GPU draws
        // into it and the hardware encoder reads it where it is
        NSDictionary* attributes = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*)kCVPixelBufferWidthKey: @(width),
            (NSString*)kCVPixelBufferHeightKey: @(height),
            (NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        };
        CVPixelBufferPoolRef pool = nullptr;
        if (CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, (__bridge CFDictionaryRef)attributes, &pool) !=
                kCVReturnSuccess ||
            !pool)
            return false;
        _pool = pool;
    }
    _width = width;
    _height = height;
    return true;
}

bool RecordingSurfaces::Compose(int slot, unsigned source, const int from[4], const int to[4], bool linear,
                                const float background[3], Pending& out)
{
    out = Pending();
    if (!_pool || slot < 0 || slot >= kSlots)
        return false;
    CVPixelBufferRef pixelBuffer = nullptr;
    if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, static_cast<CVPixelBufferPoolRef>(_pool),
                                           &pixelBuffer) != kCVReturnSuccess ||
        !pixelBuffer)
    {
        return false;
    }
    std::shared_ptr<void> owned(pixelBuffer, [](void* p) { CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p)); });
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(pixelBuffer);
    if (!surface)
        return false;

    // Errors other GL work left behind are not this frame's (the check after the blit reads only its own)
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i)
    {
    }
    GLint drawBefore = 0;
    GLint readBefore = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawBefore);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readBefore);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);

    if (!_textures[slot])
        glGenTextures(1, &_textures[slot]);
    glBindTexture(GL_TEXTURE_RECTANGLE, _textures[slot]);
    // The texture takes this buffer's surface: no pixels move, the GPU draws into the buffer itself
    const CGLError bound = CGLTexImageIOSurface2D(CGLGetCurrentContext(), GL_TEXTURE_RECTANGLE, GL_RGBA, _width,
                                                  _height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, surface, 0);
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    bool ok = bound == kCGLNoError;
    if (ok)
    {
        if (!_framebuffers[slot])
            glGenFramebuffers(1, &_framebuffers[slot]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _framebuffers[slot]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, _textures[slot], 0);
        ok = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    if (ok)
    {
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);  // bytes as the picture has them (the CPU way copies them)
        const GLfloat color[4] = {background[0], background[1], background[2], 1.0f};
        glClearBufferfv(GL_COLOR, 0, color);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
        // The surface's rows go from the top in memory, GL counts them from the bottom: the picture turned over
        glBlitFramebuffer(from[0], from[1], from[0] + from[2], from[1] + from[3], to[0], to[1] + to[3], to[0] + to[2],
                          to[1], GL_COLOR_BUFFER_BIT, linear ? GL_LINEAR : GL_NEAREST);
        ok = glGetError() == GL_NO_ERROR;
    }
    if (ok)
    {
        GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();  // on its way now: done when taken a refresh later
        ok = fence != nullptr;
        out.fence = fence;
    }
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    if (srgb)
        glEnable(GL_FRAMEBUFFER_SRGB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawBefore));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readBefore));
    if (!ok)
    {
        Unbind(slot);
        out = Pending();
        return false;
    }
    out.slot = slot;
    out.pixelBuffer = std::move(owned);
    return true;
}

std::shared_ptr<void> RecordingSurfaces::Finish(Pending& pending)
{
    std::shared_ptr<void> frame;
    if (pending.fence)
    {
        GLsync fence = static_cast<GLsync>(pending.fence);
        // Started a refresh ago: done, or nearly. A second at most (a GPU that stopped)
        const GLenum waited = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000'000);
        glDeleteSync(fence);
        if (waited == GL_ALREADY_SIGNALED || waited == GL_CONDITION_SATISFIED)
            frame = std::move(pending.pixelBuffer);
    }
    Unbind(pending.slot);
    pending = Pending();
    return frame;
}

void RecordingSurfaces::Drop(Pending& pending)
{
    if (pending.fence)
        glDeleteSync(static_cast<GLsync>(pending.fence));
    Unbind(pending.slot);
    pending = Pending();
}

void RecordingSurfaces::Unbind(int slot)
{
    // The texture lets the buffer's surface go (the pool hands a buffer out again only when no one holds it). A
    // texture made from a surface cannot be given other storage (glTexImage2D: GL_INVALID_OPERATION): it goes, a new
    // one is made at the next Compose
    if (slot < 0 || slot >= kSlots || !_textures[slot])
        return;
    if (_framebuffers[slot])
    {
        GLint drawBefore = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawBefore);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _framebuffers[slot]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, 0, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawBefore));
    }
    glDeleteTextures(1, &_textures[slot]);
    _textures[slot] = 0;
}

void RecordingSurfaces::Release()
{
    for (int slot = 0; slot < kSlots; ++slot)
    {
        if (_framebuffers[slot])
            glDeleteFramebuffers(1, &_framebuffers[slot]);
        if (_textures[slot])
            glDeleteTextures(1, &_textures[slot]);
        _framebuffers[slot] = 0;
        _textures[slot] = 0;
    }
    if (_pool)
        CVPixelBufferPoolRelease(static_cast<CVPixelBufferPoolRef>(_pool));
    _pool = nullptr;
    _width = 0;
    _height = 0;
}

bool RecordingSurfaces::LockPixels(void* pixelBuffer, uint8_t*& data, size_t& stride)
{
    auto* buffer = static_cast<CVPixelBufferRef>(pixelBuffer);
    if (!buffer || CVPixelBufferLockBaseAddress(buffer, 0) != kCVReturnSuccess)
        return false;
    data = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
    stride = CVPixelBufferGetBytesPerRow(buffer);
    if (data)
        return true;
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    return false;
}

void RecordingSurfaces::UnlockPixels(void* pixelBuffer)
{
    if (pixelBuffer)
        CVPixelBufferUnlockBaseAddress(static_cast<CVPixelBufferRef>(pixelBuffer), 0);
}
