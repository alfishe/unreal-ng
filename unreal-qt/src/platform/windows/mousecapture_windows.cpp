#include "platform/windows/mousecapture_windows.h"

#include <QtGlobal>

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{
struct NativeCapture
{
    POINT centre{};  // physical pixels, virtual-screen coordinates
    RECT clip{};
    MouseCaptureWindows::MotionFn onMotion;
};
}  // namespace

void* MouseCaptureWindows::Begin(int clipHalfWidth, int clipHalfHeight, MotionFn onMotion)
{
    POINT centre;
    if (!GetCursorPos(&centre))
        return nullptr;

    NativeCapture* capture = new NativeCapture();
    capture->centre = centre;
    capture->onMotion = std::move(onMotion);

    // At least a few pixels each way: the travel of one sample is bounded by the clip
    const LONG halfWidth = clipHalfWidth > 8 ? clipHalfWidth : 8;
    const LONG halfHeight = clipHalfHeight > 8 ? clipHalfHeight : 8;
    capture->clip = RECT{centre.x - halfWidth, centre.y - halfHeight, centre.x + halfWidth, centre.y + halfHeight};
    ClipCursor(&capture->clip);
    return capture;
}

void MouseCaptureWindows::Sample(void* handle)
{
    NativeCapture* capture = static_cast<NativeCapture*>(handle);
    if (!capture)
        return;

    POINT position;
    if (!GetCursorPos(&position))
        return;

    // Windows drops the clip when another window takes the foreground for a moment
    // (a notification, the taskbar); put it back while the capture lasts
    RECT clip;
    if (GetClipCursor(&clip) && !EqualRect(&clip, &capture->clip))
        ClipCursor(&capture->clip);

    const int dx = static_cast<int>(position.x - capture->centre.x);
    const int dy = static_cast<int>(position.y - capture->centre.y);
    if (dx == 0 && dy == 0)
        return;  // the echo of our own warp, or a move queued before it

    SetCursorPos(capture->centre.x, capture->centre.y);
    if (capture->onMotion)
        capture->onMotion(dx, dy);
}

void MouseCaptureWindows::End(void* handle)
{
    NativeCapture* capture = static_cast<NativeCapture*>(handle);
    if (!capture)
        return;

    ClipCursor(nullptr);
    capture->onMotion = nullptr;
    delete capture;
}

#else

void* MouseCaptureWindows::Begin(int, int, MotionFn)
{
    return nullptr;
}

void MouseCaptureWindows::Sample(void*) {}

void MouseCaptureWindows::End(void*) {}

#endif
