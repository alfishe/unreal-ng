import AppKit
import Combine
import SwiftUI

// MARK: - WindowManager
//
// Owns everything about the window's geometry: integer scaling, the aspect-ratio
// lock during live resize, the snap-back when the user lets go, the zoom (green
// button) frame and the minimum size. Kept out of the SwiftUI layer because none
// of it is expressible there - SwiftUI has no notion of "content must be an exact
// multiple of N pixels".
//
// It DOES become the window's delegate, because `windowWillUseStandardFrame` has
// no notification equivalent - without it AppKit satisfies `contentAspectRatio`
// by growing a single axis and the green button only makes the window taller.
// SwiftUI's own delegate is kept and every message we do not implement is
// forwarded to it (see responds(to:) / forwardingTarget(for:)), so window
// restoration and close handling keep working.
final class WindowManager: NSObject, ObservableObject, NSWindowDelegate {
    /// Height of the SwiftUI status bar, in points. The status bar lives INSIDE the
    /// window's content rect, so it has to be added on top of the scaled framebuffer
    /// when sizing the window - otherwise it eats into the picture and the integer
    /// mapping is off by 22pt.
    static let statusBarHeight: CGFloat = 22

    /// Scale currently in effect (content height / framebuffer height, rounded).
    ///
    /// Written through `set(scale:)` / `set(isFullScreen:)`, which drop no-op writes.
    /// @Published fires unconditionally, and every fire rebuilds SwiftUI's `.commands`
    /// tree - which closes an open View menu under the cursor. That was BUG 8.
    @Published private(set) var scale: CGFloat = 3
    @Published private(set) var isFullScreen = false

    private func set(scale newValue: CGFloat) {
        guard scale != newValue else { return }
        scale = newValue
    }

    private func set(isFullScreen newValue: Bool) {
        guard isFullScreen != newValue else { return }
        isFullScreen = newValue
    }

    /// Framebuffer the window is being sized against. Changes at runtime when the
    /// core switches video mode.
    private(set) var frameSize = CGSize(width: 352, height: 288)

    var statusBarVisible: Bool = true {
        didSet {
            guard statusBarVisible != oldValue else { return }
            let delta = statusBarVisible ? Self.statusBarHeight : -Self.statusBarHeight
            // While the chrome is suppressed the bar is not on screen, so the change
            // only concerns the geometry we will restore to.
            if chromeHidden {
                if var saved = savedChrome {
                    saved.height = max(0, saved.height + delta)
                    savedChrome = saved
                }
                return
            }
            // Best guess until the next layout measures the truth (see report(pictureSize:)).
            measuredChrome.height = max(0, measuredChrome.height + delta)
            reapply()
        }
    }

    /// True while the window wears no toolbar and no status bar because a fullscreen
    /// transition is in flight or in effect. Published because the status bar is a
    /// SwiftUI view and has to disappear from the tree; it changes twice per
    /// transition, so it costs two `.commands` rebuilds, not a stream of them.
    @Published private(set) var chromeHidden = false

    /// Chrome measurement to restore once the bars come back.
    private var savedChrome: CGSize?

    /// Window frame from before the teleport, restored when leaving fullscreen.
    private var savedFrame: NSRect?

    /// Screen rect the exit zoom is shrinking the picture into.
    private var exitPictureRect: NSRect?
    /// Duration AppKit gave us for the exit, replayed when the zoom actually starts.
    private var exitZoomDuration: TimeInterval = 0.4
    private var exitZoomStarted = false

    private weak var window: NSWindow?
    /// SwiftUI's own window delegate. Held strongly: the delegate slot on NSWindow is
    /// weak, so taking it over without retaining it can deallocate SwiftUI's object.
    private var forwardee: NSWindowDelegate?
    private var toolbarController: EmulatorToolbarController?

    /// Everything inside the window's content rect that is NOT the emulated picture:
    /// the status bar, the unified toolbar strip AppKit insets the content by, and any
    /// safe-area padding SwiftUI adds. It is MEASURED from the live view tree rather
    /// than assumed - assuming "22pt for the status bar" was the whole of BUG 5: the
    /// real inset was 74pt (50pt toolbar + 24pt status bar), so the picture area came
    /// out shorter than framebufferHeight * scale and the renderer pillarboxed it,
    /// which is what the black side bars were.
    private var measuredChrome = CGSize(width: 0, height: WindowManager.statusBarHeight)

    /// True from `windowWillEnter/ExitFullScreen` until the matching `did` callback.
    ///
    /// Deliberately NOT @Published: it must not rebuild the commands tree (BUG 8), and
    /// it is read from the frame tick. While set, the frame pump is silent and nobody
    /// writes drawableSize - see the fullscreen-transition notes in
    /// docs/inprogress/2026-08-09-fullscreen-transition. Async presents issued during
    /// the transition land outside the transactions that carry the geometry, which is
    /// exactly the "stale snapshots" artefact.
    private(set) var isTransitioning = false

    /// The Metal view, so the transition can freeze and settle it.
    weak var pictureView: EmulatorMetalView?

    private weak var controller: EmulatorController?

    private var doubleClickMonitor: Any?
    private var transitionToken: UInt = 0

    private var statsSubscription: AnyCancellable?

    override init() {
        super.init()
        if PublishStats.enabled {
            statsSubscription = objectWillChange.sink { PublishStats.note("windowManager") }
        }
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    // MARK: Attachment

    func attach(_ window: NSWindow) {
        guard self.window !== window else { return }
        self.window = window

        NotificationCenter.default.removeObserver(self)
        let center = NotificationCenter.default
        center.addObserver(self, selector: #selector(handleEndLiveResize),
                           name: NSWindow.didEndLiveResizeNotification, object: window)
        center.addObserver(self, selector: #selector(handleEnterFullScreen),
                           name: NSWindow.didEnterFullScreenNotification, object: window)
        center.addObserver(self, selector: #selector(handleExitFullScreen),
                           name: NSWindow.didExitFullScreenNotification, object: window)

        // Without .fullScreenPrimary the window simply cannot enter fullscreen and
        // toggleFullScreen(_:) is silently dropped.
        window.collectionBehavior.insert(.fullScreenPrimary)

        // Become the delegate, forwarding everything we do not handle to SwiftUI's.
        if window.delegate !== self {
            forwardee = window.delegate
            window.delegate = self
        }

        set(isFullScreen: window.styleMask.contains(.fullScreen))

        // Adopt whatever size the window was restored at rather than yanking it.
        set(scale: detectedScale() ?? scale)
        reapply()
    }

    /// Installs the AppKit toolbar. SwiftUI's `.toolbar` never materialised on this
    /// window, so the toolbar is built directly on the NSWindow instead.
    func installToolbar(controller: EmulatorController) {
        self.controller = controller
        installTitlebarDoubleClick()
        guard let window, toolbarController == nil else { return }
        let owner = EmulatorToolbarController(controller: controller, windowManager: self)
        toolbarController = owner
        owner.install(on: window)
    }

    /// The core changed video mode - re-derive every constraint from the new size.
    func update(frameSize newSize: CGSize) {
        guard newSize.width > 0, newSize.height > 0, newSize != frameSize else { return }
        frameSize = newSize
        reapply()
    }

    // MARK: Geometry

    /// Content rect that makes the emulated picture exactly `scale`x, chrome included.
    func contentSize(for scale: CGFloat) -> NSSize {
        NSSize(width: (frameSize.width * scale).rounded() + measuredChrome.width,
               height: (frameSize.height * scale).rounded() + measuredChrome.height)
    }

    /// Called by the Metal view whenever its bounds change. Closes the loop between
    /// "what we sized the window to" and "what the picture actually got", so a single
    /// correction pass makes the picture area exactly framebuffer * scale no matter
    /// what AppKit or SwiftUI decided to inset.
    func report(pictureSize: CGSize) {
        guard let window,
              !window.styleMask.contains(.fullScreen),
              !window.inLiveResize,
              // Mid-transition the bars are gone but the window has not been reframed
              // yet; measuring there would record a chrome of zero as the truth.
              !isTransitioning, !chromeHidden,
              pictureSize.width > 0, pictureSize.height > 0
        else { return }

        let content = window.contentRect(forFrameRect: window.frame).size
        let chrome = CGSize(width: (content.width - pictureSize.width).rounded(),
                            height: (content.height - pictureSize.height).rounded())
        guard chrome.width >= 0, chrome.height >= 0 else { return }
        guard abs(chrome.width - measuredChrome.width) > 0.5
                || abs(chrome.height - measuredChrome.height) > 0.5 else { return }

        if GeometryLog.enabled {
            NSLog("[WindowManager] measured chrome %.0fx%.0f (was %.0fx%.0f) - resizing",
                  chrome.width, chrome.height, measuredChrome.width, measuredChrome.height)
        }

        measuredChrome = chrome
        reapply()
    }

    func apply(scale newScale: CGFloat, animate: Bool = true) {
        let clamped = max(1, newScale.rounded())
        set(scale: clamped)
        applyConstraints()

        guard let window, !window.styleMask.contains(.fullScreen) else { return }

        let content = contentSize(for: clamped)
        var frame = window.frameRect(forContentRect: NSRect(origin: .zero, size: content))
        // Keep the top-left corner put, the way macOS apps grow downward.
        frame.origin = NSPoint(x: window.frame.origin.x, y: window.frame.maxY - frame.height)
        window.setFrame(frame, display: true, animate: animate)

        if GeometryLog.enabled {
            NSLog("[WindowManager] sizing with fb=%.0fx%.0f scale=%.0f statusBar=%@ content=%.0fx%.0f",
                  frameSize.width, frameSize.height, clamped,
                  statusBarVisible ? "on" : "off", content.width, content.height)
        }
    }

    private func applyConstraints() {
        guard let window, !window.styleMask.contains(.fullScreen) else { return }
        window.contentMinSize = contentSize(for: 1)
        // The aspect ratio is only constant for a fixed status-bar height, so it has
        // to be recomputed for the scale in effect. Live resize therefore keeps the
        // shape approximately; the snap in handleEndLiveResize makes it exact.
        window.contentAspectRatio = contentSize(for: max(scale, 1))
    }

    /// Drops the aspect-ratio lock. Fullscreen has to be free of it: AppKit sizes the
    /// window to the whole screen and an aspect constraint fights that.
    private func clearAspectConstraint() {
        // Documented way to drop an aspect ratio: set a resize increment instead.
        window?.contentResizeIncrements = NSSize(width: 1, height: 1)
    }

    private func reapply() {
        applyConstraints()
        apply(scale: scale, animate: false)
    }

    /// Scale implied by the window's current content rect, or nil if there is no window.
    private func detectedScale() -> CGFloat? {
        guard let window else { return nil }
        let content = window.contentRect(forFrameRect: window.frame)
        let picture = max(1, content.height - measuredChrome.height)
        let byWidth = max(1, content.width - measuredChrome.width) / frameSize.width
        let byHeight = picture / frameSize.height
        return max(1, ((byWidth + byHeight) / 2).rounded())
    }

    /// Largest integer scale whose content rect fits inside `contentLimit`.
    private func largestScale(fitting contentLimit: NSSize) -> CGFloat {
        let picture = CGSize(width: max(1, contentLimit.width - measuredChrome.width),
                             height: max(1, contentLimit.height - measuredChrome.height))
        let byWidth = floor(picture.width / frameSize.width)
        let byHeight = floor(picture.height / frameSize.height)
        return max(1, min(byWidth, byHeight))
    }

    // MARK: Full screen

    /// Screen rect the emulated picture occupies right now.
    private func pictureScreenRect() -> NSRect? {
        guard let window, let view = pictureView else { return nil }
        return window.convertToScreen(view.convert(view.bounds, to: nil))
    }

    /// Strips the toolbar and the status bar and shrinks the window by exactly the
    /// height they occupied, so the picture keeps the screen rect it already had.
    ///
    /// Both halves are required. Hiding the bars alone lets the picture grow into the
    /// freed space, and since only the height changes the aspect ratio breaks - that
    /// is the distortion seen at the very start of the zoom. Reframing alone would
    /// zoom a window that still carries its bars. Done here, in `will`, the snapshot
    /// AppKit is about to take already shows a bare device frame at the target ratio.
    private func hideChromeForFullScreen(reframe: Bool) {
        guard let window, !chromeHidden else { return }

        let picture = pictureScreenRect()
        savedChrome = measuredChrome
        savedFrame = window.frame          // the real windowed frame, bars included

        chromeHidden = true
        // Hidden, NOT detached. Detaching it was a workaround from before
        // willUseFullScreenPresentationOptions declared .autoHideToolbar, and putting
        // an NSToolbar back on a window costs 762ms (measured) - which is the window
        // sitting there bare after the exit zoom before the bars reappear.
        window.toolbar?.isVisible = false
        measuredChrome = .zero

        // Shrinking the window by exactly the height the bars occupied keeps the
        // picture on the same screen rect. Without it the picture grows into the freed
        // space - and since only the height changes, the aspect breaks.
        if reframe, let picture {
            window.setFrame(window.frameRect(forContentRect: picture),
                            display: false, animate: false)
        }

        if GeometryLog.enabled {
            NSLog("[WindowManager] chrome hidden (was %.0fx%.0f) reframe=%@",
                  savedChrome?.width ?? -1, savedChrome?.height ?? -1,
                  reframe ? "yes" : "no")
        }
    }

    /// Puts the bars back. Called from the `did` callback, so the zoom is over and the
    /// window is already at its normal frame - `reapply` then re-establishes the
    /// integer multiple with the chrome included.
    private func restoreChromeAfterFullScreen() {
        guard let window, chromeHidden else { return }
        let t0 = CFAbsoluteTimeGetCurrent()
        chromeHidden = false
        let t1 = CFAbsoluteTimeGetCurrent()
        window.toolbar?.isVisible = true
        if let savedChrome {
            measuredChrome = savedChrome
            self.savedChrome = nil
        }
        if GeometryLog.enabled {
            NSLog("[WindowManager] chrome restored: swiftui=%.0fms toolbar=%.0fms",
                  (t1 - t0) * 1000, (CFAbsoluteTimeGetCurrent() - t1) * 1000)
        }
    }

    func toggleFullScreen() {
        guard let window, !isTransitioning else { return }
        // Belt and braces: a window restored from an old state may lack the behaviour.
        window.collectionBehavior.insert(.fullScreenPrimary)

        guard !window.styleMask.contains(.fullScreen) else {
            window.toggleFullScreen(nil)
            return
        }

        // Chrome goes BEFORE the transition is asked for, not inside it. Dropping the
        // status bar is a SwiftUI change, and SwiftUI relayouts on ITS own cycle, one
        // or two frames later - landing in the middle of the zoom. Every such relayout
        // moves the layer our transform is relative to, which is the picture jumping
        // to a different place mid-animation. Doing it here, with the reframe that
        // keeps the picture rect fixed, lets the layout settle while nothing moves.
        clearAspectConstraint()

        // ARM THE WAIT FIRST. SwiftUI re-evaluates the body synchronously inside the
        // `chromeHidden = true` assignment, so subscribing afterwards misses the only
        // notification there will ever be - the wait then always timed out and the
        // transition started with the bars still drawn.
        whenChromeSettled { window.toggleFullScreen(nil) }
        hideChromeForFullScreen(reframe: true)
    }

    private var chromeSettledHandler: (() -> Void)?
    private var chromeWaitStarted: CFAbsoluteTime = 0

    /// Runs `body` once SwiftUI has ACTED on `chromeHidden` and the bar-less window has
    /// been composited.
    ///
    /// Polling geometry for this does not work: every quantity worth comparing already
    /// held its final value before SwiftUI re-evaluated anything, so the wait passed
    /// immediately, toggleFullScreen went out in the same runloop turn, and macOS
    /// captured the space with the bars still on it. That is why the chrome looked
    /// like it came off after the transition rather than before it. ContentView calls
    /// chromeDidApply() from .onChange, which only fires once the body has actually
    /// been re-evaluated - a real event, not a guess about one.
    private func whenChromeSettled(_ body: @escaping () -> Void) {
        chromeSettledHandler = body

        // A view that is off-screen or otherwise never updated must not wedge the
        // transition forever.
        chromeWaitStarted = CFAbsoluteTimeGetCurrent()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self] in
            guard let self, let pending = self.chromeSettledHandler else { return }
            if GeometryLog.enabled { NSLog("[WindowManager] chrome settle timed out") }
            self.chromeSettledHandler = nil
            pending()
        }
    }

    /// Called by ContentView when SwiftUI has re-evaluated with the new chrome state.
    func chromeDidApply() {
        guard let pending = chromeSettledHandler else { return }
        chromeSettledHandler = nil

        // SwiftUI has re-laid out; force the result onto the screen, then hand over on
        // the next turn so the bar-less window is what gets captured.
        window?.contentView?.layoutSubtreeIfNeeded()
        window?.displayIfNeeded()

        if GeometryLog.enabled {
            NSLog("[WindowManager] chrome applied by SwiftUI after %.0fms, picture %.0f",
                  (CFAbsoluteTimeGetCurrent() - chromeWaitStarted) * 1000,
                  pictureView?.bounds.height ?? -1)
        }
        DispatchQueue.main.async(execute: pending)
    }

    /// macOS's "double-click the title bar to" preference defaults to Zoom, and this
    /// app wants Zoom on the green button (integer maximise) but FULLSCREEN on a
    /// title-bar double-click. Those two cannot both come out of `zoom:`, so the
    /// double-click is intercepted before AppKit turns it into a zoom.
    private func installTitlebarDoubleClick() {
        guard doubleClickMonitor == nil else { return }

        doubleClickMonitor = NSEvent.addLocalMonitorForEvents(matching: .leftMouseDown) {
            [weak self] event in
            guard let self,
                  let window = self.window,
                  event.window === window,
                  event.clickCount == 2,
                  !window.styleMask.contains(.fullScreen)
            else { return event }

            // Titlebar/toolbar strip = everything above the content layout rect.
            guard event.locationInWindow.y > window.contentLayoutRect.maxY else { return event }

            // Leave the traffic lights and any toolbar control alone.
            if let hit = window.contentView?.superview?.hitTest(event.locationInWindow),
               hit is NSButton || hit.enclosingMenuItem != nil {
                return event
            }

            if let controller = self.controller {
                EmulatorActions.toggleFullScreen(controller, windowManager: self)
            } else {
                self.toggleFullScreen()
            }
            return nil
        }
    }

    // MARK: NSWindowDelegate

    /// Green button / View > Zoom. AppKit's default standard frame is the whole
    /// visible frame; with `contentAspectRatio` set it then satisfies the constraint
    /// by growing one axis only, which is why zoom used to change the height alone.
    /// Returning an explicitly computed frame - the largest integer multiple of the
    /// framebuffer that fits, status bar added on top, centred - fixes that.
    func windowWillUseStandardFrame(_ window: NSWindow, defaultFrame: NSRect) -> NSRect {
        guard !window.styleMask.contains(.fullScreen) else { return defaultFrame }

        let limit = window.contentRect(forFrameRect: defaultFrame).size
        let target = largestScale(fitting: limit)

        // Keep the constraint in step with the frame we are about to hand back,
        // otherwise AppKit re-constrains it and we are back to a one-axis zoom.
        set(scale: target)
        applyConstraints()

        let content = contentSize(for: target)
        var frame = window.frameRect(forContentRect: NSRect(origin: .zero, size: content))

        let visible = window.screen?.visibleFrame ?? defaultFrame
        frame.origin = NSPoint(x: (visible.midX - frame.width / 2).rounded(),
                               y: (visible.midY - frame.height / 2).rounded())
        return frame
    }

    // MARK: The teleport transition
    //
    // Returning the window from customWindowsTo{Enter,Exit}FullScreen takes AppKit's
    // snapshot zoom out of the picture entirely - that is the ONLY way to be rid of
    // it. Its snapshot is taken before windowWillEnterFullScreen, so no amount of
    // hiding chrome from a delegate callback can keep the bars out of it; that is what
    // the last two attempts ran into.
    //
    // In exchange we owe AppKit the animation. It is deliberately NOT a window
    // animation: NSWindow's animator is an app-side timer with its own curve while
    // Core Animation interpolates on the render server, and two animators only ever
    // agree at the endpoints. So the WINDOW TELEPORTS - one setFrame, no animation -
    // and the only thing that moves is a transform on the Metal layer. One animator,
    // no desync, and every intermediate frame is real live content rather than a
    // stale bitmap.

    /// Keep the menu bar and any titlebar strip out of the way unless the pointer goes
    /// looking for them - the emulated picture owns the screen in fullscreen.
    func window(_ window: NSWindow,
                willUseFullScreenPresentationOptions proposedOptions: NSApplication.PresentationOptions
                = []) -> NSApplication.PresentationOptions {
        proposedOptions.union([.fullScreen, .autoHideToolbar, .autoHideMenuBar, .autoHideDock])
    }

    func customWindowsToEnterFullScreen(for window: NSWindow) -> [NSWindow]? { [window] }
    func customWindowsToExitFullScreen(for window: NSWindow) -> [NSWindow]? { [window] }

    func window(_ window: NSWindow,
                startCustomAnimationToEnterFullScreenWithDuration duration: TimeInterval) {
        beginTransition()

        guard let view = pictureView, let sourceView = viewScreenRect() else {
            window.setFrame(window.screen?.frame ?? window.frame, display: true)
            finishZoom(reason: "enter-nopicture")
            return
        }

        // Where the picture is now, before anything moves.
        let source = pictureRect(in: sourceView)

        // Everything between here and NSEnableScreenUpdates lands on screen at once,
        // so the intermediate geometry is never composited. NOTE: display:false and no
        // CATransaction flush - flushing here froze the display for hundreds of ms.
        NSDisableScreenUpdates()

        // Normally a no-op: toggleFullScreen() already did this and let the layout
        // settle. It still runs for transitions we did not start ourselves, such as
        // AppKit's own Ctrl-Cmd-F.
        clearAspectConstraint()
        hideChromeForFullScreen(reframe: true)

        window.setFrame(window.screen?.frame ?? window.frame, display: false, animate: false)
        window.contentView?.layoutSubtreeIfNeeded()

        // Fill the freshly reallocated drawable before it can be composited empty.
        view.renderForTransition()

        let targetView = viewScreenRect() ?? sourceView
        zoom(view: view, from: source, viewRect: targetView, duration: duration, reverse: false)

        NSEnableScreenUpdates()

        armFinish(after: duration, reason: "enter")
    }

    func window(_ window: NSWindow,
                startCustomAnimationToExitFullScreenWithDuration duration: TimeInterval) {
        beginTransition()

        guard let view = pictureView, let viewRect = viewScreenRect() else {
            window.setFrame(savedFrame ?? window.frame, display: true)
            finishZoom(reason: "exit-nopicture")
            return
        }

        // Backwards the window CANNOT teleport first: it would already be small, and a
        // layer scaled up beyond the window's own surface simply has nowhere to draw.
        // So the window stays fullscreen for the whole animation, the layer shrinks
        // into the rect the picture is about to occupy, and the teleport happens at
        // the very end - under disabled screen updates, so the swap is not seen.
        // NOTHING IS ANIMATED HERE. Measured: for the whole of this callback and for
        // ~500ms after it the window is not on the active space -
        //
        //     after exit finalise: onActiveSpace=no
        //     didExitFullScreen:   onActiveSpace=yes   (+500ms)
        //
        // so a zoom started here plays to nobody; what the user actually watches is
        // the system's space switch, and whether any of our animation shows through
        // is a race. That was the "sometimes it disappears" report.
        //
        // windowDidExitFullScreen is the moment the window is back on the active space
        // AND still fullscreen-sized, which is exactly the frame the zoom should start
        // from. The animation is started from there instead; see handleExitFullScreen.
        exitPictureRect = destinationPictureRect(for: window)
        exitZoomDuration = duration
        exitZoomStarted = false
        _ = view
        _ = viewRect
    }

    /// Screen rect of the Metal view.
    private func viewScreenRect() -> NSRect? {
        guard let window, let view = pictureView else { return nil }
        return window.convertToScreen(view.convert(view.bounds, to: nil))
    }

    /// Where the emulated picture sits inside a given view rect: aspect-fit, centred -
    /// the same letterbox the renderer's viewport computes, in screen coordinates.
    private func pictureRect(in viewRect: NSRect) -> NSRect {
        guard frameSize.width > 0, frameSize.height > 0 else { return viewRect }
        let aspect = frameSize.width / frameSize.height
        var size = CGSize(width: viewRect.width, height: viewRect.width / aspect)
        if size.height > viewRect.height {
            size = CGSize(width: viewRect.height * aspect, height: viewRect.height)
        }
        return NSRect(x: viewRect.midX - size.width / 2,
                      y: viewRect.midY - size.height / 2,
                      width: size.width,
                      height: size.height)
    }

    /// Screen rect the picture will occupy once the window is back to `savedFrame`
    /// with its bars restored. Derived rather than measured because the window is
    /// still fullscreen when the exit animation has to be set up.
    private func destinationPictureRect(for window: NSWindow) -> NSRect {
        let frame = savedFrame ?? window.frame
        let content = window.contentRect(forFrameRect: frame)
        let chrome = savedChrome ?? measuredChrome
        let size = CGSize(width: max(1, content.width - chrome.width),
                          height: max(1, content.height - chrome.height))

        // Where the picture will really be, i.e. inside the window's own frame - the
        // same thing the Qt POC animates to. Centring this on the screen was a fix for
        // a misdiagnosis; the sliding came from the anchor-point error in zoom().
        let bottom = statusBarVisible ? Self.statusBarHeight : 0
        return NSRect(x: (content.midX - size.width / 2).rounded(),
                      y: (content.minY + bottom).rounded(),
                      width: size.width,
                      height: size.height)
    }

    /// Window frame that puts the emulated picture exactly at `picture`.
    private func restoreFrame(for window: NSWindow, pictureAt picture: NSRect) -> NSRect {
        let chrome = savedChrome ?? measuredChrome
        // The status bar is the bottom part of the chrome; the rest is the toolbar
        // strip above the picture.
        let bottom = statusBarVisible ? Self.statusBarHeight : 0
        let content = NSRect(x: picture.minX - chrome.width / 2,
                             y: picture.minY - bottom,
                             width: picture.width + chrome.width,
                             height: picture.height + chrome.height)
        return window.frameRect(forContentRect: content)
    }

    /// Animates the layer between "picture appears at `rect`" and "picture sits where
    /// it really is". Forward runs rect -> identity, backward identity -> rect.
    ///
    /// The transform is built around the PICTURE, not the view: in fullscreen the view
    /// is the whole screen and the picture is pillarboxed inside it, so scaling by the
    /// view's height would size the emulated image wrong at t=0 and pop.
    private func zoom(view: NSView, from rect: NSRect, viewRect: NSRect,
                      duration: TimeInterval, reverse: Bool) {
        guard let layer = view.layer else { return }

        // Everything below is in the LAYER's own coordinates, not screen ones.
        let target = NSRect(x: rect.minX - viewRect.minX, y: rect.minY - viewRect.minY,
                            width: rect.width, height: rect.height)
        let bounds = NSRect(origin: .zero, size: viewRect.size)
        let picture = pictureRect(in: bounds)
        guard picture.height > 0, target.height > 0 else { return }

        // A view's backing layer has anchorPoint (0,0), so the transform scales about
        // the layer's CORNER, not its centre. Offsetting by the difference of centres -
        // which is what a centre anchor would want - therefore pulls the shrinking
        // picture towards that corner: the "sliding away down-left" artefact. Map
        // corner to corner instead, exactly as the Qt POC does.
        let scale = target.height / picture.height
        let offset = CATransform3DConcat(
            CATransform3DMakeScale(scale, scale, 1),
            CATransform3DMakeTranslation(target.minX - scale * picture.minX,
                                         target.minY - scale * picture.minY, 0))

        let animation = CABasicAnimation(keyPath: "transform")
        animation.fromValue = NSValue(caTransform3D: reverse ? CATransform3DIdentity : offset)
        animation.toValue = NSValue(caTransform3D: reverse ? offset : CATransform3DIdentity)
        animation.duration = duration
        animation.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        animation.fillMode = .forwards
        animation.isRemovedOnCompletion = false

        // The model value is the endpoint, so nothing snaps back if the animation is
        // dropped early.
        layer.transform = reverse ? offset : CATransform3DIdentity
        layer.add(animation, forKey: "un.zoom")

        if GeometryLog.enabled {
            NSLog("[WindowManager] zoom %@ target=%@ picture=%@ anchor=%.1f,%.1f scale=%.3f dur=%.0fms",
                  reverse ? "out" : "in", NSStringFromRect(target),
                  NSStringFromRect(picture), layer.anchorPoint.x, layer.anchorPoint.y,
                  scale, duration * 1000)
        }
    }

    /// When the running zoom is due to be over.
    private var zoomDeadline: CFAbsoluteTime = 0

    private func armFinish(after duration: TimeInterval, reason: String) {
        transitionToken &+= 1
        let token = transitionToken
        zoomDeadline = CFAbsoluteTimeGetCurrent() + duration
        DispatchQueue.main.asyncAfter(deadline: .now() + duration) { [weak self] in
            guard let self, self.transitionToken == token else { return }
            self.finishZoom(reason: reason + "-timer")
        }
    }

    private func finishZoom(reason: String) {
        guard isTransitioning, let window else { return }

        // AppKit does not honour the duration it handed us - it fires its did-callback
        // hundreds of ms early. Finishing there would tear down a zoom that is still
        // running: the animation visibly stops half way and the window snaps to its
        // end state. So an early notification does not finalise, it only makes sure
        // finalisation happens at the deadline.
        let remaining = zoomDeadline - CFAbsoluteTimeGetCurrent()
        if remaining > 0.005 {
            if GeometryLog.enabled {
                NSLog("[WindowManager] %@ arrived %.0fms early - deferring", reason, remaining * 1000)
            }
            transitionToken &+= 1
            let token = transitionToken
            DispatchQueue.main.asyncAfter(deadline: .now() + remaining) { [weak self] in
                guard let self, self.transitionToken == token else { return }
                self.finishZoom(reason: reason + "-deferred")
            }
            return
        }

        if GeometryLog.enabled { NSLog("[WindowManager] finish zoom (%@)", reason) }

        // A CATransaction, NOT NSDisableScreenUpdates: measured at 524ms of dead time
        // here, which is the performance problem its deprecation note warns about.
        // Nothing in this block reallocates a drawable, so suppressing implicit
        // animations is enough to make the swap atomic.
        let tf0 = CFAbsoluteTimeGetCurrent()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if GeometryLog.enabled {
            NSLog("[WindowManager] transaction begin took %.0fms", (CFAbsoluteTimeGetCurrent() - tf0) * 1000)
        }

        // Which way we are going comes from the will-callbacks and NOTHING else.
        // Asking the style mask does not work: with a custom transition AppKit only
        // sets .fullScreen at its did-callback, which lands AFTER this runs - so on
        // the way IN the window still looks windowed here, the exit branch ran by
        // mistake, and the bars came back mid-flight while AppKit went on to finish
        // the transition around them.
        let leaving = isExiting
        // Where the zoom just put the picture - computed BEFORE restoring the chrome,
        // which clears the saved measurement this depends on.
        let restoreFrame = leaving
            ? restoreFrame(for: window, pictureAt: exitPictureRect ?? destinationPictureRect(for: window))
            : nil

        if leaving {
            restoreChromeAfterFullScreen()
            if let restoreFrame {
                window.setFrame(restoreFrame, display: false, animate: false)
            }
            savedFrame = nil
            exitPictureRect = nil
            window.contentView?.layoutSubtreeIfNeeded()
        }

        pictureView?.layer?.removeAnimation(forKey: "un.zoom")
        pictureView?.layer?.transform = CATransform3DIdentity

        endTransition()
        CATransaction.commit()

        logWindowState(leaving ? "after exit finalise" : "after enter finalise")

        // Only now, with nothing left to animate, is it safe to publish. Same reason
        // as above: the style mask is not yet authoritative at this point.
        set(isFullScreen: !leaving)

        guard leaving else { return }
        reapply()

        // AppKit finishes an exit by restoring the frame IT recorded when the
        // transition began - which is the shrunk, chrome-less one, since the chrome
        // comes off before we ask to go fullscreen. That lands after everything here,
        // so the window ends up offset by the height of the bars. Re-assert the real
        // pre-fullscreen frame once AppKit is done having its say.
        if let restoreFrame {
            DispatchQueue.main.async { [weak self] in
                guard let self, let window = self.window,
                      !window.styleMask.contains(.fullScreen), !self.isTransitioning,
                      window.frame != restoreFrame
                else { return }
                if GeometryLog.enabled {
                    NSLog("[WindowManager] re-asserting frame %@ (AppKit left it at %@)",
                          NSStringFromRect(restoreFrame), NSStringFromRect(window.frame))
                }
                window.setFrame(restoreFrame, display: true, animate: false)
            }
        }
    }

    private var isExiting = false

    func windowWillEnterFullScreen(_ notification: Notification) {
        isExiting = false
        forwardee?.windowWillEnterFullScreen?(notification)
    }

    func windowWillExitFullScreen(_ notification: Notification) {
        isExiting = true
        forwardee?.windowWillExitFullScreen?(notification)
    }

    private func beginTransition() {
        guard !isTransitioning else { return }
        if GeometryLog.enabled { NSLog("[WindowManager] transition begin") }
        isTransitioning = true
        controller?.releaseAllKeys()
        pictureView?.freezeForTransition()

        // Safety net: a silent frame pump is fatal (a permanently frozen picture), so
        // never let a lost callback strand it. armFinish() re-stamps the token with
        // the real duration, which retires this one.
        armFinish(after: 3.0, reason: "watchdog")
    }

    private func endTransition() {
        guard isTransitioning else { return }
        if GeometryLog.enabled { NSLog("[WindowManager] transition end") }
        isTransitioning = false
        // Settle geometry and the frame that goes with it in ONE transaction, using a
        // transaction-tied present, so the compositor never shows a surface that does
        // not match where the layer now is.
        pictureView?.settleAfterTransition()
        controller?.requestFocus()
    }

    // Everything else the window asks for belongs to SwiftUI.

    override func responds(to aSelector: Selector!) -> Bool {
        if super.responds(to: aSelector) { return true }
        return forwardee?.responds(to: aSelector) ?? false
    }

    override func forwardingTarget(for aSelector: Selector!) -> Any? {
        if let forwardee, forwardee.responds(to: aSelector) { return forwardee }
        return super.forwardingTarget(for: aSelector)
    }

    // MARK: Notifications

    @objc private func handleEndLiveResize() {
        guard let window, !window.styleMask.contains(.fullScreen) else { return }
        guard let snapped = detectedScale() else { return }
        apply(scale: snapped, animate: false)
    }

    // NOTE: neither of these publishes anything. `isFullScreen` is @Published, and a
    // publish rebuilds the SwiftUI tree AND re-runs the toolbar's Combine sink, both
    // of which relayout the view the zoom transform is anchored to. Setting it here
    // is what produced the run of jumps mid-animation; it is set in finishZoom
    // instead, when nothing is moving any more.

    private func logWindowState(_ tag: String) {
        guard GeometryLog.enabled, let window else { return }
        NSLog("[WindowManager] %@: fullScreenStyle=%@ frame=%@ onActiveSpace=%@ visible=%@",
              tag,
              window.styleMask.contains(.fullScreen) ? "yes" : "no",
              NSStringFromRect(window.frame),
              window.isOnActiveSpace ? "yes" : "no",
              window.isVisible ? "yes" : "no")
    }

    @objc private func handleEnterFullScreen() {
        logWindowState("didEnterFullScreen")
        // Integer snapping and the aspect lock must not fight the fullscreen frame.
        clearAspectConstraint()
        // Whichever comes first wins; finishZoom is idempotent.
        finishZoom(reason: "didEnter")
    }

    @objc private func handleExitFullScreen() {
        logWindowState("didExitFullScreen")

        // The exit zoom starts HERE, not in startCustomAnimationToExitFullScreen: this
        // is the first moment the window is on the active space, and it is still
        // fullscreen-sized, so the shrink is both visible and starts from the right
        // rect. The window teleports to its restored frame when the zoom finishes.
        if isTransitioning, isExiting, !exitZoomStarted,
           let view = pictureView, let viewRect = viewScreenRect(),
           let destination = exitPictureRect {
            exitZoomStarted = true
            zoom(view: view, from: destination, viewRect: viewRect,
                 duration: exitZoomDuration, reverse: true)
            armFinish(after: exitZoomDuration, reason: "exit")
            return
        }

        finishZoom(reason: "didExit")
    }
}

// MARK: - EmulatorToolbarController
//
// SwiftUI's `.toolbar` modifier produced no toolbar on this window (see BUG 3 in
// the POC notes), so the toolbar is an ordinary NSToolbar driven from here. The
// buttons call the exact same EmulatorActions the menu bar uses, so the
// releaseAllKeys()/requestFocus() discipline is identical.
final class EmulatorToolbarController: NSObject, NSToolbarDelegate, NSToolbarItemValidation {
    private weak var controller: EmulatorController?
    private weak var windowManager: WindowManager?
    private var cancellables = Set<AnyCancellable>()

    private var runItem: NSToolbarItem?
    private var pauseItem: NSToolbarItem?
    private var fullScreenItem: NSToolbarItem?

    private static let openID = NSToolbarItem.Identifier("un.open")
    private static let runID = NSToolbarItem.Identifier("un.run")
    private static let pauseID = NSToolbarItem.Identifier("un.pause")
    private static let resetID = NSToolbarItem.Identifier("un.reset")
    private static let fullScreenID = NSToolbarItem.Identifier("un.fullscreen")

    init(controller: EmulatorController, windowManager: WindowManager) {
        self.controller = controller
        self.windowManager = windowManager
        super.init()

        controller.$isRunning
            .combineLatest(controller.$isPaused)
            .receive(on: RunLoop.main)
            .sink { [weak self] running, paused in
                self?.updateRunItem(isRunning: running)
                self?.updatePauseItem(isRunning: running, isPaused: paused)
            }
            .store(in: &cancellables)

        windowManager.$isFullScreen
            .receive(on: RunLoop.main)
            .sink { [weak self] full in self?.updateFullScreenItem(isFullScreen: full) }
            .store(in: &cancellables)
    }

    func install(on window: NSWindow) {
        let toolbar = NSToolbar(identifier: "un.main")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false

        window.styleMask.insert(.titled)
        // Must agree with the scene's .windowToolbarStyle(.unifiedCompact): declaring
        // .unified here made the chrome 74pt at launch and 50pt after a fullscreen
        // round trip, and that 24pt difference was a visible jerk at the end of every
        // exit as the window resized to match the new measurement.
        window.toolbarStyle = .unifiedCompact
        window.toolbar = toolbar
        toolbar.isVisible = true
    }

    // MARK: Items

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [Self.openID, .space,
         Self.runID, Self.pauseID, Self.resetID,
         .flexibleSpace, Self.fullScreenID]
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        toolbarDefaultItemIdentifiers(toolbar)
    }

    func toolbar(_ toolbar: NSToolbar,
                 itemForItemIdentifier identifier: NSToolbarItem.Identifier,
                 willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        switch identifier {
        case Self.openID:
            return makeItem(identifier, label: "Open", symbol: "folder",
                            tip: "Open a snapshot, tape or disk image",
                            action: #selector(openFile))
        case Self.runID:
            let running = controller?.isRunning ?? false
            let item = makeItem(identifier,
                                label: running ? "Stop" : "Start",
                                symbol: running ? "stop.fill" : "play.fill",
                                tip: running ? "Stop the machine" : "Start the machine",
                                action: #selector(toggleRunning))
            runItem = item
            return item
        case Self.pauseID:
            let running = controller?.isRunning ?? false
            let paused = controller?.isPaused ?? false
            let item = makeItem(identifier,
                                label: paused ? "Resume" : "Pause",
                                symbol: paused ? "play.fill" : "pause.fill",
                                tip: paused ? "Resume the machine" : "Pause the machine",
                                action: #selector(togglePause))
            item.isEnabled = running
            pauseItem = item
            return item
        case Self.resetID:
            return makeItem(identifier, label: "Reset", symbol: "arrow.clockwise",
                            tip: "Reset the machine",
                            action: #selector(resetMachine))
        case Self.fullScreenID:
            let full = windowManager?.isFullScreen ?? false
            let item = makeItem(identifier,
                                label: full ? "Exit Full Screen" : "Full Screen",
                                symbol: full ? "arrow.down.right.and.arrow.up.left"
                                             : "arrow.up.left.and.arrow.down.right",
                                tip: "Toggle full screen",
                                action: #selector(toggleFullScreen))
            fullScreenItem = item
            return item
        default:
            return nil
        }
    }

    private func makeItem(_ identifier: NSToolbarItem.Identifier,
                          label: String,
                          symbol: String,
                          tip: String,
                          action: Selector) -> NSToolbarItem {
        let item = NSToolbarItem(itemIdentifier: identifier)
        item.label = label
        item.paletteLabel = label
        item.toolTip = tip
        item.image = NSImage(systemSymbolName: symbol, accessibilityDescription: label)
        item.isBordered = true
        item.target = self
        item.action = action
        return item
    }

    private func updateRunItem(isRunning: Bool) {
        guard let runItem else { return }
        runItem.label = isRunning ? "Stop" : "Start"
        runItem.paletteLabel = runItem.label
        runItem.toolTip = isRunning ? "Stop the machine" : "Start the machine"
        runItem.image = NSImage(systemSymbolName: isRunning ? "stop.fill" : "play.fill",
                                accessibilityDescription: runItem.label)
    }

    private func updatePauseItem(isRunning: Bool, isPaused: Bool) {
        guard let pauseItem else { return }
        pauseItem.label = isPaused ? "Resume" : "Pause"
        pauseItem.paletteLabel = pauseItem.label
        pauseItem.toolTip = isPaused ? "Resume the machine" : "Pause the machine"
        pauseItem.image = NSImage(systemSymbolName: isPaused ? "play.fill" : "pause.fill",
                                  accessibilityDescription: pauseItem.label)
        pauseItem.isEnabled = isRunning
    }

    private func updateFullScreenItem(isFullScreen: Bool) {
        guard let fullScreenItem else { return }
        fullScreenItem.label = isFullScreen ? "Exit Full Screen" : "Full Screen"
        fullScreenItem.paletteLabel = fullScreenItem.label
        fullScreenItem.image = NSImage(
            systemSymbolName: isFullScreen ? "arrow.down.right.and.arrow.up.left"
                                           : "arrow.up.left.and.arrow.down.right",
            accessibilityDescription: fullScreenItem.label)
    }

    /// AppKit re-validates toolbar items on every run-loop pass and would otherwise
    /// override whatever `isEnabled` we set by hand.
    func validateToolbarItem(_ item: NSToolbarItem) -> Bool {
        switch item.itemIdentifier {
        case Self.pauseID:
            return controller?.isRunning ?? false
        default:
            return true
        }
    }

    // MARK: Actions

    @objc private func openFile() {
        guard let controller else { return }
        EmulatorActions.openFile(controller)
    }

    @objc private func toggleRunning() {
        guard let controller else { return }
        EmulatorActions.toggleRunning(controller)
    }

    @objc private func togglePause() {
        guard let controller else { return }
        EmulatorActions.togglePause(controller)
    }

    @objc private func resetMachine() {
        guard let controller else { return }
        EmulatorActions.reset(controller)
    }

    @objc private func toggleFullScreen() {
        guard let controller else { return }
        EmulatorActions.toggleFullScreen(controller, windowManager: windowManager)
    }
}

// MARK: - WindowAccessor
//
// Zero-size NSView whose only job is handing the enclosing NSWindow to the
// WindowManager once SwiftUI has actually put the hierarchy on screen.
struct WindowAccessor: NSViewRepresentable {
    let onWindow: (NSWindow) -> Void

    func makeNSView(context: Context) -> NSView {
        let view = NSView(frame: .zero)
        DispatchQueue.main.async { if let window = view.window { onWindow(window) } }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        DispatchQueue.main.async { if let window = nsView.window { onWindow(window) } }
    }
}

// MARK: - StatusBarView

struct StatusBarView: View {
    @ObservedObject var controller: EmulatorController
    @ObservedObject var windowManager: WindowManager
    /// Observed HERE and nowhere else - see EmulatorController.Telemetry (BUG 8).
    @ObservedObject var telemetry: EmulatorController.Telemetry

    var body: some View {
        HStack(spacing: 12) {
            label(String(format: "%.0f fps", telemetry.fps))
                .frame(width: 56, alignment: .leading)
                .monospacedDigit()

            divider
            label(controller.stateText)

            divider
            ActivityIndicator(title: "TAPE", color: .yellow,
                              active: telemetry.activity.contains(.tape),
                              supported: EmulatorController.supportedActivity.contains(.tape))
            ActivityIndicator(title: "DISK", color: .green,
                              active: telemetry.activity.contains(.disk),
                              supported: EmulatorController.supportedActivity.contains(.disk))
            ActivityIndicator(title: "SND", color: .cyan,
                              active: telemetry.activity.contains(.sound),
                              supported: EmulatorController.supportedActivity.contains(.sound))

            divider
            label(controller.loadedFileName ?? "No media")
                .lineLimit(1)
                .truncationMode(.middle)

            Spacer(minLength: 8)

            label("\(Int(windowManager.frameSize.width))x\(Int(windowManager.frameSize.height)) @\(Int(windowManager.scale))x")
                .monospacedDigit()
        }
        .padding(.horizontal, 10)
        .frame(height: WindowManager.statusBarHeight)
        .background(.bar)
        .overlay(alignment: .top) { Divider() }
    }

    private var divider: some View {
        Text("|").font(.caption2).foregroundStyle(.quaternary)
    }

    private func label(_ text: String) -> some View {
        Text(text).font(.caption).foregroundStyle(.secondary)
    }
}

/// A small coloured LED plus its caption, mirroring the Qt POC's StatusIndicator:
/// bright while the subsystem is doing something, dimmed the rest of the time.
/// A subsystem the core cannot report at all stays permanently dark.
private struct ActivityIndicator: View {
    let title: String
    let color: Color
    let active: Bool
    let supported: Bool

    var body: some View {
        HStack(spacing: 3) {
            Circle()
                .fill(lit ? color : Color.secondary.opacity(0.25))
                .frame(width: 7, height: 7)
                .overlay(Circle().strokeBorder(.black.opacity(0.25), lineWidth: 0.5))
                .shadow(color: lit ? color.opacity(0.9) : .clear, radius: lit ? 3 : 0)
                .animation(.easeOut(duration: 0.12), value: lit)

            Text(title)
                .font(.caption2)
                .foregroundStyle(lit ? .primary : .tertiary)
        }
        .help(supported ? "\(title) activity"
                        : "\(title) activity is not reported by the emulator core")
    }

    private var lit: Bool { supported && active }
}
