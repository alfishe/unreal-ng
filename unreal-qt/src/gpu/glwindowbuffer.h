#pragma once

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>

namespace gpu
{

/// The window's default framebuffer draws and reads its back buffer again (GL_BACK) where it reads GL_NONE. NVIDIA's EGL
/// (driver 580, Wayland) keeps GL_NONE as the draw and read buffer of a desktop GL context's default framebuffer when
/// the context was first made current without a surface - Qt's EGL context does that when it is created - and is bound
/// to a window afterwards: every draw into the window is dropped and the compositor shows an empty, transparent buffer
/// (the window's background where the picture should be). Mesa, GLX, WGL and CGL start a window with GL_BACK, so the
/// call changes nothing there. Called with the context current on the window; a few state queries, no GPU round trip.
/// OpenGL ES (no glDrawBuffer, its window starts with GL_BACK) and single-buffered windows are left alone
inline void RestoreWindowDrawBuffer(QOpenGLContext* context)
{
    if (!context || context->isOpenGLES() || context->format().swapBehavior() == QSurfaceFormat::SingleBuffer)
        return;
    // Another framebuffer is Qt's (a Wayland window with client-side decorations draws into a content FBO)
    if (context->defaultFramebufferObject() != 0)
        return;
    QOpenGLExtraFunctions* f = context->extraFunctions();
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    f->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    f->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    if (drawFramebuffer != 0 || readFramebuffer != 0)
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLint draw = GL_BACK;
    GLint read = GL_BACK;
    f->glGetIntegerv(GL_DRAW_BUFFER, &draw);
    f->glGetIntegerv(GL_READ_BUFFER, &read);
    if (draw == GL_NONE)
    {
        // Desktop GL only (glDrawBuffers takes no GL_BACK there), resolved the once a context needs it
        using DrawBufferFn = void(QOPENGLF_APIENTRYP)(GLenum);
        if (auto drawBuffer = reinterpret_cast<DrawBufferFn>(context->getProcAddress("glDrawBuffer")))
            drawBuffer(GL_BACK);
    }
    if (read == GL_NONE)
        f->glReadBuffer(GL_BACK);
    if (drawFramebuffer != 0 || readFramebuffer != 0)
    {
        f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer));
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer));
    }
}

}  // namespace gpu
