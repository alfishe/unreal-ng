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
            // Best guess until the next layout measures the truth (see report(pictureSize:)).
            measuredChrome.height += statusBarVisible ? Self.statusBarHeight : -Self.statusBarHeight
            measuredChrome.height = max(0, measuredChrome.height)
            reapply()
        }
    }

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

    func toggleFullScreen() {
        guard let window, !isTransitioning else { return }
        // Belt and braces: a window restored from an old state may lack the behaviour.
        window.collectionBehavior.insert(.fullScreenPrimary)
        window.toggleFullScreen(nil)
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

    // The transition is deliberately AppKit's DEFAULT snapshot zoom: this app does not
    // return the window from customWindowsToEnter/ExitFullScreen, so there is no second
    // animator to desync from Core Animation. What the artefacts came from instead was
    // our own frame pump presenting asynchronously while AppKit reframed the window,
    // and the aspect constraint being dropped too late. Both are fixed by arming the
    // freeze one callback EARLIER, in `will`, rather than in `did`.

    func windowWillEnterFullScreen(_ notification: Notification) {
        beginTransition()
        // Must be cleared BEFORE AppKit starts reframing, not after: an aspect
        // constraint applied to the intermediate frames is the ratio distortion.
        clearAspectConstraint()
        forwardee?.windowWillEnterFullScreen?(notification)
    }

    func windowWillExitFullScreen(_ notification: Notification) {
        beginTransition()
        forwardee?.windowWillExitFullScreen?(notification)
    }

    private func beginTransition() {
        guard !isTransitioning else { return }
        if GeometryLog.enabled { NSLog("[WindowManager] transition begin") }
        isTransitioning = true
        controller?.releaseAllKeys()
        pictureView?.freezeForTransition()

        // Safety net: a silent frame pump is fatal (a permanently frozen picture), so
        // never let a missed did-callback strand it. Tokenised so a late timer from a
        // previous transition cannot cut short the one currently running.
        transitionToken &+= 1
        let token = transitionToken
        DispatchQueue.main.asyncAfter(deadline: .now() + 3.0) { [weak self] in
            guard let self, self.transitionToken == token else { return }
            if GeometryLog.enabled { NSLog("[WindowManager] transition watchdog fired") }
            self.endTransition()
        }
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

    @objc private func handleEnterFullScreen() {
        set(isFullScreen: true)
        // Integer snapping and the aspect lock must not fight AppKit here.
        clearAspectConstraint()
        endTransition()
    }

    @objc private func handleExitFullScreen() {
        set(isFullScreen: false)
        // AppKit restores the pre-fullscreen frame, which may no longer match the
        // current framebuffer - re-snap (this also restores the aspect constraint).
        reapply()
        endTransition()
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
        window.toolbarStyle = .unified
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
