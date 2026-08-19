#include "FullscreenHelper.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CATransaction.h>
#include <QDebug>
#include <QDateTime>

#define FS_LOG(msg) qDebug().nospace() << QDateTime::currentMSecsSinceEpoch() << " [FS] " << msg

// =============================================================================
// FullscreenWindowDelegate - NSWindowDelegate for custom fullscreen animations
// Implements the AppKit API to replace system animations while keeping native
// fullscreen (Space, menu bar on hover, etc.)
// =============================================================================

@interface FullscreenWindowDelegate : NSObject <NSWindowDelegate>
{
    FullscreenHelper::Delegate* _delegate;
    NSRect _originalFrame;
    NSRect _originalContentRect;
    NSRect _originalScreenFrame;  // Screen widget frame before fullscreen
    std::function<void()> _hideQtUI;
    std::function<void()> _showQtUI;
    std::function<void(int, int, int, int, double)> _screenZoomIn;
    std::function<void(int, int, int, int, double)> _screenZoomOut;
    __weak id<NSWindowDelegate> _qtDelegate;  // Qt's original QNSWindowDelegate
    BOOL _zoomFinished;          // idempotency guard
    NSRect _pendingTeleport;     // frame to jump to when the zoom ends (exit)
}
@property (nonatomic, weak) id<NSWindowDelegate> qtDelegate;
@property (nonatomic, assign) FullscreenHelper::Delegate* delegate;
@property (nonatomic, assign) NSRect originalFrame;
@property (nonatomic, assign) NSRect originalScreenFrame;
@property (nonatomic, copy) void (^hideQtUIBlock)(void);
@property (nonatomic, copy) void (^showQtUIBlock)(void);

- (instancetype)initWithDelegate:(FullscreenHelper::Delegate*)delegate;
- (void)setHideQtUI:(std::function<void()>)func;
- (void)setShowQtUI:(std::function<void()>)func;
- (void)setScreenZoomIn:(std::function<void(int, int, int, int, double)>)func;
- (void)setScreenZoomOut:(std::function<void(int, int, int, int, double)>)func;
@end

@implementation FullscreenWindowDelegate

- (instancetype)initWithDelegate:(FullscreenHelper::Delegate*)delegate
{
    self = [super init];
    if (self) {
        _delegate = delegate;
        _originalFrame = NSZeroRect;
        _originalContentRect = NSZeroRect;
    }
    return self;
}

- (void)setHideQtUI:(std::function<void()>)func
{
    _hideQtUI = func;
}

- (void)setShowQtUI:(std::function<void()>)func
{
    _showQtUI = func;
}

- (void)setScreenZoomIn:(std::function<void(int, int, int, int, double)>)func
{
    _screenZoomIn = func;
}

- (void)setScreenZoomOut:(std::function<void(int, int, int, int, double)>)func
{
    _screenZoomOut = func;
}

#pragma mark - Forwarding to Qt's delegate
// We REPLACE Qt's QNSWindowDelegate on the NSWindow, so every delegate method
// we don't override ourselves must be forwarded to it — otherwise Qt never
// hears about window lifecycle events. Symptom of not forwarding: closing the
// window via the red button bypasses Qt entirely (no closeEvent, app doesn't
// quit on last window close).

- (BOOL)respondsToSelector:(SEL)aSelector
{
    return [super respondsToSelector:aSelector]
        || [_qtDelegate respondsToSelector:aSelector];
}

- (id)forwardingTargetForSelector:(SEL)aSelector
{
    if ([_qtDelegate respondsToSelector:aSelector])
        return _qtDelegate;
    return [super forwardingTargetForSelector:aSelector];
}

#pragma mark - Custom Fullscreen Animation (live Metal, no snapshots)
// Returning the window from customWindowsToEnter/ExitFullScreenForWindow tells
// AppKit NOT to snapshot-zoom it. We animate the REAL window frame instead:
// every animator tick delivers a genuine resize -> Qt relayout -> synchronous
// presentsWithTransaction Metal render glued to that tick's CATransaction.
// The content is therefore LIVE at full rate during the whole zoom.

- (NSArray<NSWindow*>*)customWindowsToEnterFullScreenForWindow:(NSWindow*)window
{
    FS_LOG("customWindowsToEnterFullScreenForWindow");

    // Save frames while the styleMask is still the windowed one
    _originalFrame = [window frame];
    _originalContentRect = [window contentRectForFrameRect:_originalFrame];

    return @[window];
}

// The window TELEPORTS to its destination; the visible zoom is a single CA
// transform animation on the Metal layer. Two animators (NSWindow's app-side
// timer and CA's render-server bezier) can never track each other mid-motion,
// which is what produced every drift artefact.
- (void)window:(NSWindow*)window startCustomAnimationToEnterFullScreenWithDuration:(NSTimeInterval)duration
{
    FS_LOG("startCustomAnimationToEnterFullScreen duration=" << duration);

    NSScreen* screen = [window screen] ?: [NSScreen mainScreen];
    NSRect screenFrame = [screen frame];
    NSTimeInterval animDuration = duration > 0 ? duration : 0.4;

    NSRect oldContent = _originalContentRect;
    _zoomFinished = NO;
    _pendingTeleport = NSZeroRect;   // enter teleports right now, not at the end

    // Teleport + animation start in ONE commit: no frame may show the layer
    // full-size at identity before the animation is attached.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    [window setFrame:screenFrame display:NO animate:NO];
    NSRect newContent = [window contentRectForFrameRect:screenFrame];

    // Where the old content sits inside the new content area (flipped coords)
    CGFloat fromX = oldContent.origin.x - newContent.origin.x;
    CGFloat fromYTop = (newContent.origin.y + newContent.size.height)
                     - (oldContent.origin.y + oldContent.size.height);

    if (_delegate) {
        _delegate->zoomStarted(animDuration,
                               (int)newContent.size.width, (int)newContent.size.height,
                               (int)fromX, (int)fromYTop,
                               (int)oldContent.size.width, (int)oldContent.size.height,
                               false);
    }
    [CATransaction commit];

    FullscreenWindowDelegate* __weak weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((animDuration + 0.03) * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [weakSelf finishZoomForWindow:window reason:"timer"];
    });
}

// Idempotent: runs on whichever trigger comes first — our timer or AppKit's
// own did-enter/did-exit. AppKit does NOT honour our duration (traced: it
// finished 576ms before our timer), so waiting only for the timer left the
// window un-teleported with the zoom transform still attached.
- (void)finishZoomForWindow:(NSWindow*)window reason:(const char*)reason
{
    if (_zoomFinished)
        return;
    _zoomFinished = YES;

    FS_LOG("zoom finish (" << reason << ")");

    // Teleport (exit only) and layer settle must reach the screen together.
    //
    // The CATransaction alone is what provides that. NSDisableScreenUpdates used to
    // wrap it as well, but it is deprecated precisely for the performance problem it
    // caused here: measured in the native POC, the identical block spent 524ms inside
    // it doing nothing, which is the pause between the end of the zoom and the bars
    // coming back. Nothing in this block reallocates a drawable, so suppressing
    // implicit animations is enough.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (!NSEqualRects(_pendingTeleport, NSZeroRect))
        [window setFrame:_pendingTeleport display:NO animate:NO];
    if (_delegate)
        _delegate->zoomFinished();
    [CATransaction commit];

    _pendingTeleport = NSZeroRect;
}

- (NSArray<NSWindow*>*)customWindowsToExitFullScreenForWindow:(NSWindow*)window
{
    FS_LOG("customWindowsToExitFullScreenForWindow");
    return @[window];
}

- (void)window:(NSWindow*)window startCustomAnimationToExitFullScreenWithDuration:(NSTimeInterval)duration
{
    FS_LOG("startCustomAnimationToExitFullScreen duration=" << duration);

    // The window stays fullscreen for the whole zoom and teleports at the end.
    NSRect targetFrame = NSEqualRects(_originalFrame, NSZeroRect)
        ? [window frame] : _originalFrame;
    NSRect targetContent = NSEqualRects(_originalContentRect, NSZeroRect)
        ? targetFrame : _originalContentRect;
    NSTimeInterval animDuration = duration > 0 ? duration : 0.3;

    NSRect curContent = [window contentRectForFrameRect:[window frame]];
    CGFloat toX = targetContent.origin.x - curContent.origin.x;
    CGFloat toYTop = (curContent.origin.y + curContent.size.height)
                   - (targetContent.origin.y + targetContent.size.height);

    _zoomFinished = NO;
    _pendingTeleport = targetFrame;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (_delegate) {
        _delegate->zoomStarted(animDuration,
                               (int)curContent.size.width, (int)curContent.size.height,
                               (int)toX, (int)toYTop,
                               (int)targetContent.size.width, (int)targetContent.size.height,
                               true);
    }
    [CATransaction commit];

    FullscreenWindowDelegate* __weak weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((animDuration + 0.03) * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [weakSelf finishZoomForWindow:window reason:"timer"];
    });
}


#pragma mark - Standard Fullscreen Notifications

- (void)windowWillEnterFullScreen:(NSNotification*)notification
{
    FS_LOG("willEnterFullScreen");
    // Don't hide UI here - wait for startCustomAnimation to avoid visible relayout
    if (_delegate)
        _delegate->willEnterFullscreen();
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification
{
    // BEFORE the delegate callback: AppKit is about to reframe the window and
    // the zoom must already be settled by then
    [self finishZoomForWindow:[notification object] reason:"didEnterFullScreen"];

    FS_LOG("didEnterFullScreen");
    if (_delegate)
        _delegate->didEnterFullscreen();
}

- (void)windowWillExitFullScreen:(NSNotification*)notification
{
    FS_LOG("willExitFullScreen");
    if (_delegate)
        _delegate->willExitFullscreen();
}

- (void)windowDidExitFullScreen:(NSNotification*)notification
{
    [self finishZoomForWindow:[notification object] reason:"didExitFullScreen"];

    FS_LOG("didExitFullScreen");
    // Qt UI already shown in the exit-animation completion handler
    if (_delegate)
        _delegate->didExitFullscreen();
}

@end

// =============================================================================
// Storage
// =============================================================================

static NSMapTable<NSValue*, FullscreenWindowDelegate*>* g_delegates = nil;

// =============================================================================
// FullscreenHelper implementation
// =============================================================================

namespace FullscreenHelper {

void install(QWindow* window, Delegate* delegate)
{
    if (!window || !delegate)
        return;

    if (!g_delegates)
        g_delegates = [NSMapTable strongToStrongObjectsMapTable];

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];
    NSValue* key = [NSValue valueWithPointer:(__bridge void*)nsWindow];

    // Remove existing
    uninstall(window);

    // Create and set our delegate, keeping a reference to Qt's original
    // delegate — all methods we don't override are forwarded to it
    FullscreenWindowDelegate* fsDelegate = [[FullscreenWindowDelegate alloc] initWithDelegate:delegate];
    fsDelegate.qtDelegate = [nsWindow delegate];
    [nsWindow setDelegate:fsDelegate];
    [g_delegates setObject:fsDelegate forKey:key];

    FS_LOG("installed custom fullscreen delegate (forwarding to Qt delegate)");
}

void uninstall(QWindow* window)
{
    if (!window || !g_delegates)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];
    NSValue* key = [NSValue valueWithPointer:(__bridge void*)nsWindow];

    FullscreenWindowDelegate* fsDelegate = [g_delegates objectForKey:key];
    if (fsDelegate) {
        if ([nsWindow delegate] == fsDelegate)
            [nsWindow setDelegate:fsDelegate.qtDelegate];  // Restore Qt's delegate
        [g_delegates removeObjectForKey:key];
    }
}

void setCallbacks(QWindow* window,
                  std::function<void()> hideQtUI,
                  std::function<void()> showQtUI,
                  std::function<void(int, int, int, int, double)> screenZoomIn,
                  std::function<void(int, int, int, int, double)> screenZoomOut)
{
    if (!window || !g_delegates)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];
    NSValue* key = [NSValue valueWithPointer:(__bridge void*)nsWindow];

    FullscreenWindowDelegate* fsDelegate = [g_delegates objectForKey:key];
    if (fsDelegate) {
        [fsDelegate setHideQtUI:hideQtUI];
        [fsDelegate setShowQtUI:showQtUI];
        [fsDelegate setScreenZoomIn:screenZoomIn];
        [fsDelegate setScreenZoomOut:screenZoomOut];
    }
}

void enterFullscreen(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    if (!([nsWindow styleMask] & NSWindowStyleMaskFullScreen)) {
        FS_LOG("calling toggleFullScreen");
        [nsWindow toggleFullScreen:nil];
    }
}

void exitFullscreen(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    if ([nsWindow styleMask] & NSWindowStyleMaskFullScreen) {
        FS_LOG("calling toggleFullScreen to exit");
        [nsWindow toggleFullScreen:nil];
    }
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

    [NSAnimationContext beginGrouping];
    [[NSAnimationContext currentContext] setDuration:0];

    nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    [nsWindow setTitlebarAppearsTransparent:YES];
    [nsWindow setTitleVisibility:NSWindowTitleHidden];
    [[nsWindow standardWindowButton:NSWindowCloseButton] setHidden:YES];
    [[nsWindow standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
    [[nsWindow standardWindowButton:NSWindowZoomButton] setHidden:YES];
    [nsWindow setBackgroundColor:[NSColor blackColor]];

    [NSAnimationContext endGrouping];
}

void showTitleBar(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    [NSAnimationContext beginGrouping];
    [[NSAnimationContext currentContext] setDuration:0];

    nsWindow.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
    [nsWindow setTitlebarAppearsTransparent:NO];
    [nsWindow setTitleVisibility:NSWindowTitleVisible];
    [[nsWindow standardWindowButton:NSWindowCloseButton] setHidden:NO];
    [[nsWindow standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
    [[nsWindow standardWindowButton:NSWindowZoomButton] setHidden:NO];
    [nsWindow setBackgroundColor:nil];

    [NSAnimationContext endGrouping];
}

QSize fullscreenSize(QWindow* window)
{
    if (!window)
        return QSize();

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];
    NSScreen* screen = [nsWindow screen];

    if (!screen)
        screen = [NSScreen mainScreen];

    NSRect frame = [screen frame];
    return QSize(static_cast<int>(frame.size.width),
                 static_cast<int>(frame.size.height));
}

void flushGraphics()
{
    [CATransaction flush];
}

void hideWindowChrome(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    // ONE non-animated batch. Deliberately does NOT touch the styleMask
    // (leaks through the Space transition and breaks the responder chain).
    [NSAnimationContext beginGrouping];
    [[NSAnimationContext currentContext] setDuration:0];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    [[nsWindow standardWindowButton:NSWindowCloseButton] setHidden:YES];
    [[nsWindow standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
    [[nsWindow standardWindowButton:NSWindowZoomButton] setHidden:YES];
    [nsWindow setTitleVisibility:NSWindowTitleHidden];
    [nsWindow setTitlebarAppearsTransparent:YES];
    [nsWindow setBackgroundColor:[NSColor blackColor]];

    [CATransaction commit];
    [NSAnimationContext endGrouping];

    FS_LOG("hideWindowChrome done");
}

void restoreWindowChrome(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];

    [NSAnimationContext beginGrouping];
    [[NSAnimationContext currentContext] setDuration:0];
    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    [[nsWindow standardWindowButton:NSWindowCloseButton] setHidden:NO];
    [[nsWindow standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
    [[nsWindow standardWindowButton:NSWindowZoomButton] setHidden:NO];
    [[nsWindow standardWindowButton:NSWindowCloseButton] setAlphaValue:1];
    [[nsWindow standardWindowButton:NSWindowMiniaturizeButton] setAlphaValue:1];
    [[nsWindow standardWindowButton:NSWindowZoomButton] setAlphaValue:1];
    [nsWindow setTitleVisibility:NSWindowTitleVisible];
    [nsWindow setTitlebarAppearsTransparent:NO];
    [nsWindow setBackgroundColor:nil];

    [CATransaction commit];
    [NSAnimationContext endGrouping];

    FS_LOG("restoreWindowChrome done");
}

void ensureKeyboardFocus(QWindow* window)
{
    if (!window)
        return;

    NSView* view = (__bridge NSView*)(void*)window->winId();
    NSWindow* nsWindow = [view window];
    if (!nsWindow)
        return;

    NSResponder* current = [nsWindow firstResponder];
    FS_LOG("ensureKeyboardFocus: firstResponder=" << [NSStringFromClass([current class]) UTF8String]
           << (current == view ? " (qt view, ok)" : " -> restoring to qt view"));

    if (current != view) {
        [nsWindow makeFirstResponder:view];
    }
}

} // namespace FullscreenHelper
