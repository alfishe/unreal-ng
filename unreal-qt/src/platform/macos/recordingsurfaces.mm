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
    // The GL objects go in Release (the context current); the surfaces held and the pool need none
    for (Target& t : _targets)
        if (t.surface)
            CFRelease(static_cast<IOSurfaceRef>(t.surface));
    if (_pool)
        CVPixelBufferPoolRelease(static_cast<CVPixelBufferPoolRef>(_pool));
}

bool RecordingSurfaces::Configure(int width, int height)
{
    if (_pool && width == _width && height == _height)
        return true;
    ReleaseTargets();  // of the old size (the context is current: Compose's caller)
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

RecordingSurfaces::Target* RecordingSurfaces::TargetFor(void* surface)
{
    Target* target = &_targets[0];
    for (Target& t : _targets)
    {
        if (t.surface == surface)
        {
            t.used = ++_uses;
            return &t;
        }
        if (!t.surface || (target->surface && t.used < target->used))
            target = &t;
    }
    // A surface not seen: the least recently used target goes (or a free one)
    if (target->surface)
    {
        glDeleteFramebuffers(1, &target->framebuffer);
        glDeleteTextures(1, &target->texture);
        CFRelease(static_cast<IOSurfaceRef>(target->surface));
        *target = Target();
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_RECTANGLE, texture);
    // The texture takes this buffer's surface: no pixels move, the GPU draws into the buffer itself
    const CGLError bound = CGLTexImageIOSurface2D(CGLGetCurrentContext(), GL_TEXTURE_RECTANGLE, GL_RGBA, _width,
                                                  _height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                                                  static_cast<IOSurfaceRef>(surface), 0);
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    GLuint framebuffer = 0;
    bool ok = bound == kCGLNoError;
    if (ok)
    {
        GLint drawBefore = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawBefore);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, texture, 0);
        ok = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawBefore));
    }
    if (!ok)
    {
        if (framebuffer)
            glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
        return nullptr;
    }
    CFRetain(static_cast<IOSurfaceRef>(surface));
    target->surface = surface;
    target->texture = texture;
    target->framebuffer = framebuffer;
    target->used = ++_uses;
    return target;
}

bool RecordingSurfaces::Compose(const float background[3], const std::function<bool(unsigned framebuffer)>& draw,
                                Pending& out)
{
    out = Pending();
    if (!_pool)
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

    // Errors other GL work left behind are not this frame's (the check after the draw reads only its own)
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i)
    {
    }
    const Target* target = TargetFor(surface);
    if (!target)
        return false;
    GLint drawBefore = 0;
    GLint readBefore = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawBefore);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readBefore);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);  // bytes as the picture has them (the CPU way copies them)
    const GLfloat color[4] = {background[0], background[1], background[2], 1.0f};
    glClearBufferfv(GL_COLOR, 0, color);
    // A draw, not glBlitFramebuffer: the same cost here, and the keyboard can be drawn over it the same way
    bool ok = draw(target->framebuffer) && glGetError() == GL_NO_ERROR;
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
        if (out.fence)
            glDeleteSync(static_cast<GLsync>(out.fence));
        out = Pending();
        return false;
    }
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
    pending = Pending();
    return frame;
}

void RecordingSurfaces::Drop(Pending& pending)
{
    if (pending.fence)
        glDeleteSync(static_cast<GLsync>(pending.fence));
    pending = Pending();
}

void RecordingSurfaces::ReleaseTargets()
{
    for (Target& t : _targets)
    {
        if (!t.surface)
            continue;
        glDeleteFramebuffers(1, &t.framebuffer);
        glDeleteTextures(1, &t.texture);
        CFRelease(static_cast<IOSurfaceRef>(t.surface));
        t = Target();
    }
}

void RecordingSurfaces::Release()
{
    ReleaseTargets();
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
