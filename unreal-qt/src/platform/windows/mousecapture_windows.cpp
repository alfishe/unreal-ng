#include "platform/windows/mousecapture_windows.h"

#include <QtGlobal>

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "platform/windows/cursortravel.h"

namespace
{
struct NativeCapture
{
    POINT centre{};  // physical pixels, virtual-screen coordinates
    RECT clip{};
    CursorTravel travel;
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

    // Not too small: the travel of one sample is bounded by the clip
    const LONG halfWidth = clipHalfWidth > 64 ? clipHalfWidth : 64;
    const LONG halfHeight = clipHalfHeight > 64 ? clipHalfHeight : 64;
    capture->clip = RECT{centre.x - halfWidth, centre.y - halfHeight, centre.x + halfWidth, centre.y + halfHeight};
    ClipCursor(&capture->clip);
    // Warped back once halfway to the clip edge: room for a fast flick either way.
    // Over RDP never: the client does not follow the server's warp (cursortravel.h)
    const bool remote = GetSystemMetrics(SM_REMOTESESSION) != 0;
    capture->travel.Start(CursorTravel::Point{centre.x, centre.y},
                          CursorTravel::Point{static_cast<int>(halfWidth / 2), static_cast<int>(halfHeight / 2)}, remote);
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

    const CursorTravel::Step step =
        capture->travel.Feed(CursorTravel::Point{position.x, position.y}, static_cast<int64_t>(GetTickCount64()));
    if (step.warp)
        SetCursorPos(capture->centre.x, capture->centre.y);
    if ((step.dx != 0 || step.dy != 0) && capture->onMotion)
        capture->onMotion(step.dx, step.dy);
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
