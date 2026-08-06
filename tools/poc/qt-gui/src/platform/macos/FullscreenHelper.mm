#include "FullscreenHelper.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CATransaction.h>

// Raw C++ pointer stored - caller must ensure delegate outlives the observer
@interface FullscreenObserver : NSObject
{
    FullscreenHelper::Delegate* _delegate;
}
- (instancetype)initWithDelegate:(FullscreenHelper::Delegate*)delegate;
- (void)startObserving:(NSWindow*)window;
- (void)stopObserving;
@end

@implementation FullscreenObserver

- (instancetype)initWithDelegate:(FullscreenHelper::Delegate*)delegate
{
    self = [super init];
    if (self) {
        _delegate = delegate;
    }
    return self;
}

- (void)startObserving:(NSWindow*)window
{
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];

    [nc addObserver:self
           selector:@selector(windowWillEnterFullScreen:)
               name:NSWindowWillEnterFullScreenNotification
             object:window];

    [nc addObserver:self
           selector:@selector(windowDidEnterFullScreen:)
               name:NSWindowDidEnterFullScreenNotification
             object:window];

    [nc addObserver:self
           selector:@selector(windowWillExitFullScreen:)
               name:NSWindowWillExitFullScreenNotification
             object:window];

    [nc addObserver:self
           selector:@selector(windowDidExitFullScreen:)
               name:NSWindowDidExitFullScreenNotification
             object:window];
}

- (void)stopObserving
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)windowWillEnterFullScreen:(NSNotification*)notification
{
    NSLog(@"[FullscreenHelper] windowWillEnterFullScreen");
    if (_delegate)
        _delegate->willEnterFullscreen();
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification
{
    NSLog(@"[FullscreenHelper] windowDidEnterFullScreen");
    if (_delegate)
        _delegate->didEnterFullscreen();
}

- (void)windowWillExitFullScreen:(NSNotification*)notification
{
    NSLog(@"[FullscreenHelper] windowWillExitFullScreen");
    if (_delegate)
        _delegate->willExitFullscreen();
}

- (void)windowDidExitFullScreen:(NSNotification*)notification
{
    NSLog(@"[FullscreenHelper] windowDidExitFullScreen");
    if (_delegate)
        _delegate->didExitFullscreen();
}

@end

// Store observers by window
static NSMapTable<NSValue*, FullscreenObserver*>* g_observers = nil;

namespace FullscreenHelper {

void install(QWindow* window, Delegate* delegate)
{
    if (!window || !delegate)
        return;

    if (!g_observers)
        g_observers = [NSMapTable strongToStrongObjectsMapTable];

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];
    NSValue* key = [NSValue valueWithPointer:(__bridge void*)nsWindow];

    // Remove existing
    uninstall(window);

    FullscreenObserver* observer = [[FullscreenObserver alloc] initWithDelegate:delegate];
    [observer startObserving:nsWindow];
    [g_observers setObject:observer forKey:key];
}

void uninstall(QWindow* window)
{
    if (!window || !g_observers)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];
    NSValue* key = [NSValue valueWithPointer:(__bridge void*)nsWindow];

    FullscreenObserver* observer = [g_observers objectForKey:key];
    if (observer) {
        [observer stopObserving];
        [g_observers removeObjectForKey:key];
    }
}

void enterFullscreen(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    if (!([nsWindow styleMask] & NSWindowStyleMaskFullScreen))
        [nsWindow toggleFullScreen:nil];
}

void exitFullscreen(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    if ([nsWindow styleMask] & NSWindowStyleMaskFullScreen)
        [nsWindow toggleFullScreen:nil];
}

bool isFullscreen(QWindow* window)
{
    if (!window)
        return false;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    return ([nsWindow styleMask] & NSWindowStyleMaskFullScreen) != 0;
}

void hideTitleBar(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    // Make content extend under title bar
    nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    [nsWindow setTitlebarAppearsTransparent:YES];
    [nsWindow setTitleVisibility:NSWindowTitleHidden];
    [[nsWindow standardWindowButton:NSWindowCloseButton] setHidden:YES];
    [[nsWindow standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
    [[nsWindow standardWindowButton:NSWindowZoomButton] setHidden:YES];

    // Set background to black
    [nsWindow setBackgroundColor:[NSColor blackColor]];

    [CATransaction flush];
}

void showTitleBar(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    // Restore normal title bar
    nsWindow.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
    [nsWindow setTitlebarAppearsTransparent:NO];
    [nsWindow setTitleVisibility:NSWindowTitleVisible];
    [[nsWindow standardWindowButton:NSWindowCloseButton] setHidden:NO];
    [[nsWindow standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
    [[nsWindow standardWindowButton:NSWindowZoomButton] setHidden:NO];

    // Restore default background
    [nsWindow setBackgroundColor:nil];

    [CATransaction flush];
}

} // namespace FullscreenHelper
