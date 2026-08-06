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
    std::function<void()> _hideQtUI;
    std::function<void()> _showQtUI;
}
@property (nonatomic, assign) FullscreenHelper::Delegate* delegate;
@property (nonatomic, assign) NSRect originalFrame;
@property (nonatomic, copy) void (^hideQtUIBlock)(void);
@property (nonatomic, copy) void (^showQtUIBlock)(void);

- (instancetype)initWithDelegate:(FullscreenHelper::Delegate*)delegate;
- (void)setHideQtUI:(std::function<void()>)func;
- (void)setShowQtUI:(std::function<void()>)func;
@end

@implementation FullscreenWindowDelegate

- (instancetype)initWithDelegate:(FullscreenHelper::Delegate*)delegate
{
    self = [super init];
    if (self) {
        _delegate = delegate;
        _originalFrame = NSZeroRect;
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

#pragma mark - Custom Fullscreen Animation (Enter)

- (NSArray<NSWindow*>*)customWindowsToEnterFullScreenForWindow:(NSWindow*)window
{
    FS_LOG("customWindowsToEnterFullScreenForWindow");

    // Save original frame FIRST
    _originalFrame = [window frame];

    NSScreen* screen = [window screen] ?: [NSScreen mainScreen];
    NSRect screenFrame = [screen frame];

    // Immediately hide Qt UI and title bar to prevent visible relayout during Space transition
    if (_hideQtUI) _hideQtUI();

    [[window standardWindowButton:NSWindowCloseButton] setHidden:YES];
    [[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
    [[window standardWindowButton:NSWindowZoomButton] setHidden:YES];
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
    [window setTitlebarAppearsTransparent:YES];
    [window setTitleVisibility:NSWindowTitleHidden];
    [window setBackgroundColor:[NSColor blackColor]];

    // Set frame to fullscreen immediately to prevent visible resize during Space transition
    [window setFrame:screenFrame display:YES animate:NO];

    FS_LOG("set frame to fullscreen immediately");

    // Return the window itself - tells AppKit we'll handle animation
    return @[window];
}

- (void)window:(NSWindow*)window startCustomAnimationToEnterFullScreenWithDuration:(NSTimeInterval)duration
{
    FS_LOG("startCustomAnimationToEnterFullScreen duration=" << duration);

    // Frame already set in customWindowsToEnterFullScreenForWindow
    // No animation needed - just signal completion immediately
    FS_LOG("enter animation complete (instant)");
}

#pragma mark - Custom Fullscreen Animation (Exit)

- (NSArray<NSWindow*>*)customWindowsToExitFullScreenForWindow:(NSWindow*)window
{
    FS_LOG("customWindowsToExitFullScreenForWindow");
    return @[window];
}

- (void)window:(NSWindow*)window startCustomAnimationToExitFullScreenWithDuration:(NSTimeInterval)duration
{
    FS_LOG("startCustomAnimationToExitFullScreen duration=" << duration);

    NSRect targetFrame = NSEqualRects(_originalFrame, NSZeroRect) ? [window frame] : _originalFrame;

    // Instant: restore title bar and buttons
    window.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
    [window setTitlebarAppearsTransparent:NO];
    [window setTitleVisibility:NSWindowTitleVisible];
    [window setBackgroundColor:nil];
    [[window standardWindowButton:NSWindowCloseButton] setHidden:NO];
    [[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:NO];
    [[window standardWindowButton:NSWindowZoomButton] setHidden:NO];
    [[window standardWindowButton:NSWindowCloseButton] setAlphaValue:1];
    [[window standardWindowButton:NSWindowMiniaturizeButton] setAlphaValue:1];
    [[window standardWindowButton:NSWindowZoomButton] setAlphaValue:1];

    // Fast shrink animation
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
        context.duration = 0.15;
        context.allowsImplicitAnimation = YES;

        [[window animator] setFrame:targetFrame display:YES];

    } completionHandler:^{
        FS_LOG("exit animation complete");
        // Don't show Qt UI here - defer to didExitFullScreen to avoid glitch during Space transition
        // Reset original frame for next cycle
        _originalFrame = NSZeroRect;
    }];
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
    FS_LOG("didExitFullScreen");
    // Qt UI already shown in animation completion handler
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

    // Create and set our delegate
    FullscreenWindowDelegate* fsDelegate = [[FullscreenWindowDelegate alloc] initWithDelegate:delegate];
    [nsWindow setDelegate:fsDelegate];
    [g_delegates setObject:fsDelegate forKey:key];

    FS_LOG("installed custom fullscreen delegate");
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
            [nsWindow setDelegate:nil];
        [g_delegates removeObjectForKey:key];
    }
}

void setCallbacks(QWindow* window, std::function<void()> hideQtUI, std::function<void()> showQtUI)
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

} // namespace FullscreenHelper
