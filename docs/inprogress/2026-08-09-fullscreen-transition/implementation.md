# Implementation — complete code

Self-contained: everything needed to rebuild the solution from scratch is quoted
here. Files referenced live in `tools/poc/qt-gui/src/`.

## 1. Renderer: the zoom (`platform/macos/MetalScreenWidget.mm`)

### Letterbox math, shared with the render viewport

The transform must map content **onto** content, so it uses the same layout math
the renderer uses for its viewport. Keeping these in sync is essential — if they
diverge, the picture shifts at the animation's endpoints.

```objc
// Letterbox rect of the content inside a box — identical math to render()'s
// viewport, so a transform built from these maps content exactly onto content.
static CGRect zoomContentRect(CGFloat boxW, CGFloat boxH,
                              double targetAspect, double contentAspect)
{
    if (boxW <= 0 || boxH <= 0 || contentAspect <= 0)
        return CGRectMake(0, 0, boxW, boxH);

    CGFloat frameW = boxW, frameH = boxH;
    if (targetAspect > 0) {   // fullscreen layout: fit a frame of that aspect first
        if (boxW / boxH > targetAspect) { frameH = boxH; frameW = boxH * targetAspect; }
        else                            { frameW = boxW; frameH = boxW / targetAspect; }
    }
    CGFloat w, h;
    if (contentAspect > (frameW / frameH)) { w = frameW; h = frameW / contentAspect; }
    else                                   { h = frameH; w = frameH * contentAspect; }
    return CGRectMake((boxW - w) / 2, (boxH - h) / 2, w, h);
}
```

### The transform

Uniform scale only. Separate `sx = fromW/layerW`, `sy = fromH/layerH` squashes
the picture whenever the window aspect differs from the screen aspect — i.e.
always.

```objc
CATransform3D MetalScreenWidget::zoomTransformFor(const QRect& rect) const
{
    const double contentAspect = (m_impl->textureWidth > 0 && m_impl->textureHeight > 0)
        ? (double)m_impl->textureWidth / (double)m_impl->textureHeight : 4.0 / 3.0;

    CGRect lb = m_impl->metalLayer.bounds;
    CGRect cLayer = zoomContentRect(lb.size.width, lb.size.height,
                                    m_impl->targetAspect, contentAspect);
    CGRect cSmall = zoomContentRect(rect.width(), rect.height(), 0.0, contentAspect);

    // UNIFORM scale: content-to-content, so the picture never gets squashed
    CGFloat k = (cLayer.size.width > 0) ? (cSmall.size.width / cLayer.size.width) : 1.0;
    CGFloat tx = rect.x() + cSmall.origin.x - k * cLayer.origin.x;
    CGFloat ty = rect.y() + cSmall.origin.y - k * cLayer.origin.y;

    return CATransform3DConcat(CATransform3DMakeScale(k, k, 1),
                               CATransform3DMakeTranslation(tx, ty, 0));
}
```

`anchorPoint` is `(0,0)` and the layer frame origin is `(0,0)`, so scaling about
the top-left plus a translation is sufficient; `position` never changes.

### prepareZoom — freeze and pre-place

Called before the animation. The static transform is what prevents a one-frame
pop: the oversized layer already appears exactly where the content is now.

```objc
void MetalScreenWidget::prepareZoom(const QSize& layerSize, const QRect& contentBox)
{
    if (!m_metalInitialized || layerSize.isEmpty())
        return;

    @autoreleasepool {
        CGFloat scale = devicePixelRatioF();
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        m_impl->metalLayer.frame = CGRectMake(0, 0, layerSize.width(), layerSize.height());
        m_impl->metalLayer.drawableSize =
            CGSizeMake(layerSize.width() * scale, layerSize.height() * scale);
        m_impl->metalLayer.transform =
            contentBox.isEmpty() ? CATransform3DIdentity : zoomTransformFor(contentBox);
        [CATransaction commit];
    }

    m_zoomActive.store(true, std::memory_order_relaxed);
    render(false);   // one frame at the new layout, plain async present
}
```

### animateZoom — the single animator

```objc
void MetalScreenWidget::animateZoom(const QRect& fromRect, double duration, bool reverse)
{
    if (!m_metalInitialized || fromRect.isEmpty())
        return;

    @autoreleasepool {
        CATransform3D small = zoomTransformFor(fromRect);

        // Model goes to its final value immediately; the animation only drives
        // presentation. One animator (the render server) — nothing to desync.
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        m_impl->metalLayer.transform = reverse ? small : CATransform3DIdentity;
        [CATransaction commit];

        CABasicAnimation* a = [CABasicAnimation animationWithKeyPath:@"transform"];
        a.fromValue = [NSValue valueWithCATransform3D:(reverse ? CATransform3DIdentity : small)];
        a.toValue   = [NSValue valueWithCATransform3D:(reverse ? small : CATransform3DIdentity)];
        a.duration  = duration > 0 ? duration : 0.4;
        a.timingFunction =
            [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        a.removedOnCompletion = NO;   // hold the end state until endZoom()
        a.fillMode = kCAFillModeForwards;
        [m_impl->metalLayer addAnimation:a forKey:@"fsZoom"];
    }
}
```

### endZoom — settle, atomically

The render **must** be inside the same transaction as the geometry change.
Rendering after the commit leaves the layer already small while its contents are
still the old full-screen drawable — `TopLeft` placement shows that as a garbage
crop in the top-left corner until the next frame arrives.

```objc
void MetalScreenWidget::endZoom()
{
    if (!m_metalInitialized)
        return;

    m_zoomActive.store(false, std::memory_order_relaxed);

    @autoreleasepool {
        NSView* view = (__bridge NSView*)(void*)winId();
        NSRect vb = [view bounds];           // Qt's size still lags here
        CGFloat scale = devicePixelRatioF();

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [m_impl->metalLayer removeAnimationForKey:@"fsZoom"];
        m_impl->metalLayer.transform = CATransform3DIdentity;
        if (vb.size.width > 0 && vb.size.height > 0) {
            m_impl->metalLayer.frame = CGRectMake(0, 0, vb.size.width, vb.size.height);
            m_impl->metalLayer.drawableSize =
                CGSizeMake(vb.size.width * scale, vb.size.height * scale);
        }

        render(true);   // transaction-tied present, INSIDE the transaction

        [CATransaction commit];
    }
}
```

### Geometry freeze

Two guards, both required:

```objc
void MetalScreenWidget::updateDrawableSize()
{
    if (!m_impl->metalLayer)
        return;
    if (m_zoomActive.load(std::memory_order_relaxed))   // the zoom owns the layer
        return;
    /* ... normal path: layer.frame + drawableSize + contentsScale ... */
}

void MetalScreenWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_zoomActive.load(std::memory_order_relaxed)) {
        emit resized(event->size());
        return;                       // no drawable churn, no render
    }
    updateDrawableSize();
    render(true);                     // sync, glued to this transaction
    emit resized(event->size());
}
```

### Frame pump — the display link stays SILENT during a transition

This is deliberate and has now been verified twice by removing it. The tree
silences the display link for the whole transition:

```objc
void MetalScreenWidget::displayLinkCallback()
{
    if (!m_animating || !m_metalInitialized)
        return;

    // SILENT during fullscreen transitions: the only renders are the
    // synchronous transaction-tied ones from resizeEvent on the main thread.
    if (m_inTransition)
        return;

    render(false);
}
```

The guard looks like a leftover from the era when the **window frame** was
animated and every present had to be glued to a transaction. The reasoning that
it is now unnecessary — the frame teleports, only the transform moves, so async
presents into a frozen drawable cannot desynchronise against anything — is
plausible and **wrong**.

Removing it was attempted twice, the second time in isolation with no other
change in the tree. Result both times: the zoom becomes visibly worse and slower,
with jerky playback of stale frames. Async presents land outside the
transactions that carry the geometry, so during the zoom the compositor keeps
showing surfaces that no longer match where the layer is.

The trade is real but one-sided:

| | Guard in place (current) | Guard removed |
|---|---|---|
| During the zoom | Smooth; motion comes from the transform | Jerky, stale frames |
| Dead zones before/after the zoom | Picture frozen for a few frames | Live |

The dead-zone freeze is the accepted cost. If someone wants to attack it, the
answer is not async presents — it would have to be additional *transaction-tied*
frames at those specific moments.

### The two present paths

```objc
[encoder endEncoding];
if (syncPresent) {
    m_impl->metalLayer.presentsWithTransaction = YES;
    [cmdBuffer commit];
    [cmdBuffer waitUntilScheduled];
    [drawable present];              // lands in the current CATransaction
} else {
    m_impl->metalLayer.presentsWithTransaction = NO;
    [cmdBuffer presentDrawable:drawable];
    [cmdBuffer commit];              // non-blocking
}
```

## 2. Window side (`platform/macos/FullscreenHelper.mm`)

### Delegate interface

```cpp
class Delegate {
public:
    virtual void willEnterFullscreen()  = 0;
    virtual void didEnterFullscreen()   = 0;
    virtual void willExitFullscreen()   = 0;
    virtual void didExitFullscreen()    = 0;
    // The window has already teleported; animate the layer from/to this rect,
    // expressed in the coordinates of the window DURING the zoom.
    virtual void zoomStarted(double duration, int layerW, int layerH,
                             int fromX, int fromY, int fromW, int fromH,
                             bool reverse) = 0;
    virtual void zoomFinished() = 0;
};
```

### Enter: teleport + start, one commit

```objc
- (NSArray<NSWindow*>*)customWindowsToEnterFullScreenForWindow:(NSWindow*)window
{
    _originalFrame = [window frame];
    _originalContentRect = [window contentRectForFrameRect:_originalFrame];
    return @[window];      // tells AppKit: no snapshot zoom
}

- (void)window:(NSWindow*)window
        startCustomAnimationToEnterFullScreenWithDuration:(NSTimeInterval)duration
{
    NSScreen* screen = [window screen] ?: [NSScreen mainScreen];
    NSRect screenFrame = [screen frame];
    NSTimeInterval animDuration = duration > 0 ? duration : 0.4;
    NSRect oldContent = _originalContentRect;

    _zoomFinished = NO;
    _pendingTeleport = NSZeroRect;     // enter teleports now, not at the end

    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    [window setFrame:screenFrame display:NO animate:NO];        // TELEPORT
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
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 (int64_t)((animDuration + 0.03) * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [weakSelf finishZoomForWindow:window reason:"timer"];
    });
}
```

### Exit: animate now, teleport at the end

```objc
- (void)window:(NSWindow*)window
        startCustomAnimationToExitFullScreenWithDuration:(NSTimeInterval)duration
{
    // Teleport target is the original FRAME (not the content rect: that made the
    // window 28px short and AppKit corrected it afterwards → second jump)
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
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 (int64_t)((animDuration + 0.03) * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [weakSelf finishZoomForWindow:window reason:"timer"];
    });
}
```

### Idempotent finalization

AppKit does **not** honour the duration it handed us — measured finishing 576 ms
early. Whichever trigger arrives first wins, and it must run before the
`didEnter/didExit` delegate callback.

```objc
- (void)finishZoomForWindow:(NSWindow*)window reason:(const char*)reason
{
    if (_zoomFinished)
        return;
    _zoomFinished = YES;

    NSDisableScreenUpdates();          // hold the display: teleport + settle together
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (!NSEqualRects(_pendingTeleport, NSZeroRect))
        [window setFrame:_pendingTeleport display:NO animate:NO];   // display:NO!
    if (_delegate)
        _delegate->zoomFinished();
    [CATransaction commit];
    NSEnableScreenUpdates();

    _pendingTeleport = NSZeroRect;
}

- (void)windowDidEnterFullScreen:(NSNotification*)n
{
    [self finishZoomForWindow:[n object] reason:"didEnterFullScreen"];   // BEFORE
    if (_delegate) _delegate->didEnterFullscreen();
}

- (void)windowDidExitFullScreen:(NSNotification*)n
{
    [self finishZoomForWindow:[n object] reason:"didExitFullScreen"];    // BEFORE
    if (_delegate) _delegate->didExitFullscreen();
}
```

`display:NO` and no `CATransaction flush` are load-bearing: with `display:YES`
or a flush, the same lock freezes the display for 400–690 ms while the window
server tears the Space down.

### Delegate forwarding (unrelated to the zoom, but required)

Replacing Qt's `QNSWindowDelegate` breaks Qt unless everything we do not
override is forwarded — the visible symptom is that closing the window no longer
quits the app.

```objc
- (BOOL)respondsToSelector:(SEL)s
{
    return [super respondsToSelector:s] || [_qtDelegate respondsToSelector:s];
}
- (id)forwardingTargetForSelector:(SEL)s
{
    if ([_qtDelegate respondsToSelector:s]) return _qtDelegate;
    return [super forwardingTargetForSelector:s];
}
```

Capture `_qtDelegate = [nsWindow delegate]` before installing, restore it on
uninstall.

## 3. Window controller (`MainWindow.cpp`)

```cpp
void MainWindow::zoomStarted(double duration, int layerW, int layerH,
                             int fromX, int fromY, int fromW, int fromH, bool reverse)
{
    // Enter: place the oversized layer so the content still appears where it was.
    // Exit: the layer is already correct at identity — only animate.
    if (!reverse)
        m_screen->prepareZoom(QSize(layerW, layerH), QRect(fromX, fromY, fromW, fromH));
    else
        m_screen->prepareZoom(QSize(layerW, layerH), QRect());

    m_screen->animateZoom(QRect(fromX, fromY, fromW, fromH), duration, reverse);
}

void MainWindow::zoomFinished()
{
    m_screen->endZoom();
}
```

Before `toggleFullScreen:`, the bars and chrome must already be gone *and*
committed, or AppKit captures them:

```cpp
m_screen->setFullscreenLayout(screenAspect);
m_screen->setAnimating(true);
applyFullscreenStyle();                              // hide menu/tool/status bars
FullscreenHelper::hideWindowChrome(windowHandle());  // buttons, title, black bg
if (layout()) layout()->activate();                  // synchronous relayout
repaint();
FullscreenHelper::flushGraphics();                   // [CATransaction flush]
QTimer::singleShot(50, this, [this]() {              // one runloop turn
    FullscreenHelper::enterFullscreen(windowHandle());
});
```

Chrome comes back only in `didExitFullscreen` — doing it earlier makes Qt
relayout while AppKit still holds the fullscreen `styleMask`, and the
coordinates jump.

## 4. Unrelated fixes that live in the same code

Two bugs found along the way, both worth keeping:

**Stuck Symbol Shift.** `Qt::Key_Control` is Cmd on macOS and maps to
`ZXKEY_SYM_SHIFT`. The press from the fullscreen shortcut enters the ZX matrix
and its release is lost in the transition, leaving the keyboard "dead" until an
emulator reset. `EmulatorWidget` tracks held keys and releases them all in
`willEnter/willExitFullscreen`.

**Quadruple key delivery.** An application-level event filter sees the same key
event at every node of the delivery chain. Forward only when
`obj == windowHandle()`.

## 5. Remaining hardcoded timeouts

Known debt, with the event that should replace each:

| Location | Current | Event replacement |
|---|---|---|
| `FullscreenHelper`, both directions | `dispatch_after(animDuration + 0.03)` | `CATransaction` completion block around `addAnimation` |
| `MainWindow::didEnterFullscreen` | `QTimer::singleShot(200)` | `windowDidEnterFullScreen` itself |
| `MainWindow::toggleFullscreenMacOS` | `QTimer::singleShot(50)` before `toggleFullScreen:` | verify the synchronous relayout + flush is sufficient |

**Trap:** the first replacement was attempted and failed. The CA completion
fires *before* the teleport reaches the view, so `endZoom()` read a stale
(fullscreen) `NSView.bounds` and the content snapped to the top-left at full
size. It needs the final size passed in explicitly from
`finishZoomForWindow:` — where it is known from the frame just set — instead of
being read back from the view. Both changes must land together.
