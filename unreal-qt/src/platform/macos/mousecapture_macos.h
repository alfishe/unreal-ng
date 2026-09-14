#pragma once

#include <functional>

/// macOS native pointer capture (relative mode) for the Kempston Mouse.
///
/// Qt has no relative mouse mode, and its QCursor::setPos posts a synthetic HID
/// event (CGEventPost), which needs Accessibility permission - silently lost on
/// every rebuild of an ad-hoc signed binary. This backend needs no permission:
/// the cursor is warped once to the capture centre, dissociated from the mouse
/// (it stays put), and raw travel is read from NSEvent deltaX/deltaY, which
/// already carry the user's OS pointer ballistics.
///
/// All functions must be called on the main (GUI) thread. Implemented in
/// mousecapture_macos.mm; no-ops returning nullptr on other platforms.
namespace MouseCaptureMacOS
{
/// Travel in logical points, screen axes (+y down)
using MotionFn = std::function<void(double dx, double dy)>;

/// @param centreX, centreY global screen position in logical points (Qt global coordinates)
/// @return opaque capture handle, or nullptr if native capture is unavailable
void* Begin(double centreX, double centreY, MotionFn onMotion);

/// Ends a capture started with Begin (nullptr is ignored)
void End(void* handle);
}  // namespace MouseCaptureMacOS
