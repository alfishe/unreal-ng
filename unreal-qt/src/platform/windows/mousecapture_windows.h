#pragma once

#include <functional>

/// Windows native pointer capture for the emulated mouse.
///
/// The generic Qt backend measures each mouse move event against the capture
/// centre and warps the cursor back. On Windows that over-counts: WM_MOUSEMOVE
/// messages queued before the warp still carry pre-warp positions, so each stale
/// one is measured against the new centre and the same travel is reported again
/// and again (the guest pointer leaps across the screen). Logical coordinates at
/// a fractional DPI scale (125 %, 150 %) also round every sample.
///
/// This backend uses move events only as a trigger: GetCursorPos (synchronous,
/// physical pixels) gives where the pointer really is now, the offset from the
/// centre is the travel - the system's pointer speed and "Enhance pointer
/// precision" already applied, as on the host desktop - and SetCursorPos puts
/// it back. A queued stale event samples the centre and reports nothing.
/// ClipCursor keeps the pointer over the screen area between samples, so a
/// fast flick never escapes to another window or monitor.
///
/// All functions must be called on the main (GUI) thread. No-ops returning
/// nullptr on other platforms.
namespace MouseCaptureWindows
{
/// Travel in physical pixels, screen axes (+y down)
using MotionFn = std::function<void(int dx, int dy)>;

/// The cursor must already be at the capture centre (the caller warps it there in Qt's
/// logical coordinates; Windows reports the physical position it landed on).
/// @param clipHalfWidth, clipHalfHeight half the size of the screen area, physical pixels
/// @return opaque capture handle, or nullptr if native capture is unavailable
void* Begin(int clipHalfWidth, int clipHalfHeight, MotionFn onMotion);

/// Reports the travel since the last sample and puts the cursor back to the centre.
/// Call on every mouse move event while captured
void Sample(void* handle);

/// Ends a capture started with Begin (nullptr is ignored): the cursor is free again
void End(void* handle);
}  // namespace MouseCaptureWindows
