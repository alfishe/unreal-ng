// LD_PRELOAD shim for the CPU-cost runs: Mesa's no-op Gallium driver (GALLIUM_NOOP=1) crashes when an EGL context
// is destroyed (null function pointer inside libgallium). The benchmark never needs a context to really go away,
// so eglDestroyContext becomes a no-op. Used only together with GALLIUM_NOOP=1.
#include <EGL/egl.h>
EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
    (void)dpy;
    (void)ctx;
    return EGL_TRUE;
}
