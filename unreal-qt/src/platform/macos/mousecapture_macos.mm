#include "platform/macos/mousecapture_macos.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace
{
struct NativeCapture
{
    id monitor = nil;  // Retained NSEvent local monitor (no ARC in this target)
    MouseCaptureMacOS::MotionFn onMotion;
};
}  // namespace

void* MouseCaptureMacOS::Begin(double centreX, double centreY, MotionFn onMotion)
{
    NativeCapture* capture = new NativeCapture();
    capture->onMotion = std::move(onMotion);

    // Global display coordinates: top-left origin of the main display, in points -
    // the same space as Qt's global coordinates on macOS
    CGWarpMouseCursorPosition(CGPointMake(centreX, centreY));

    // Freeze the cursor: the mouse keeps producing events with deltas, the
    // cursor itself no longer moves (no warping per event, no permission needed)
    CGAssociateMouseAndMouseCursorPosition(false);

    const NSEventMask mask = NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged |
                             NSEventMaskOtherMouseDragged;
    capture->monitor = [[NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                              handler:^NSEvent*(NSEvent* event) {
                                                                  if (capture->onMotion)
                                                                      capture->onMotion(event.deltaX, event.deltaY);
                                                                  return event;
                                                              }] retain];
    return capture;
}

void MouseCaptureMacOS::End(void* handle)
{
    NativeCapture* capture = static_cast<NativeCapture*>(handle);
    if (!capture)
        return;

    if (capture->monitor)
    {
        [NSEvent removeMonitor:capture->monitor];
        [capture->monitor release];
        capture->monitor = nil;
    }
    capture->onMotion = nullptr;

    CGAssociateMouseAndMouseCursorPosition(true);
    delete capture;
}
