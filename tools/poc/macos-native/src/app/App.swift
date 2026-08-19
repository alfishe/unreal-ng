import SwiftUI
import UniformTypeIdentifiers

/// Window/drawable geometry tracing. Off unless `UN_GEOMETRY_LOG=1`, because the
/// interesting lines are emitted on every geometry change and a window drag would
/// otherwise fill the log. Nothing here is per-frame.
enum GeometryLog {
    static let enabled = ProcessInfo.processInfo.environment["UN_GEOMETRY_LOG"] == "1"
}

/// Instrumentation for BUG 8 (View menu closing under the cursor):
/// counts how often each observable in the `.commands` scope publishes.
/// Enable with `UN_PUBLISH_STATS=1`.
enum PublishStats {
    static let enabled = ProcessInfo.processInfo.environment["UN_PUBLISH_STATS"] == "1"
    private static var counts: [String: Int] = [:]
    private static var started = CFAbsoluteTimeGetCurrent()

    static func note(_ who: String) {
        guard enabled else { return }
        counts[who, default: 0] += 1
        let now = CFAbsoluteTimeGetCurrent()
        let elapsed = now - started
        guard elapsed >= 5 else { return }
        let summary = counts.map { "\($0.key)=\(String(format: "%.1f", Double($0.value) / elapsed))/s" }
            .sorted().joined(separator: " ")
        NSLog("[PublishStats] %@", summary)
        counts.removeAll()
        started = now
    }
}

/// A SwiftUI macOS app keeps running with no windows unless it is told otherwise,
/// which is why closing the window left an invisible process behind (BUG 9).
final class AppDelegate: NSObject, NSApplicationDelegate {
    /// Registered by EmulatorController.init. Not wired from a SwiftUI `onAppear`:
    /// the adaptor's instance is created before the scene, and going through the view
    /// left it nil at termination time, so the explicit teardown never ran.
    static weak var activeController: EmulatorController?

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    func applicationWillTerminate(_ notification: Notification) {
        // Explicit teardown rather than relying on deinit ordering. EmulatorController
        // .stop() -> UNEmulatorBridge.stop() does it in the only safe order: drop held
        // keys, unsubscribe the MessageCenter observers, clear the audio callback and
        // stop the audio unit, and only then stop and remove the emulator. Reversing
        // any of that lets a worker-thread callback fire into freed state.
        Self.activeController?.stop()
    }
}

@main
struct UnrealNGApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var controller = EmulatorController()
    @StateObject private var windowManager = WindowManager()

    /// Native framebuffer of the emulated machine, border included. Only used for the
    /// initial window size - everything after boot goes through WindowManager, which
    /// tracks the framebuffer the core actually reports.
    static let nativeWidth: CGFloat = 352
    static let nativeHeight: CGFloat = 288
    /// Default window is an exact integer multiple of the framebuffer, so every
    /// emulated pixel maps to a whole square of screen pixels — no resampling,
    /// no shimmer on 1px-wide Spectrum graphics.
    static let defaultScale: CGFloat = 3

    @AppStorage("showStatusBar") private var showStatusBar = true

    var body: some Scene {
        WindowGroup("unreal-ng (native)") {
            ContentView(controller: controller,
                        windowManager: windowManager,
                        showStatusBar: $showStatusBar)
        }
        .defaultSize(width: Self.nativeWidth * Self.defaultScale,
                     height: Self.nativeHeight * Self.defaultScale + WindowManager.statusBarHeight)
        .windowToolbarStyle(.unifiedCompact)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Open…") { openFile() }
                    .keyboardShortcut("o", modifiers: .command)

                Menu("Open Recent") {
                    ForEach(controller.recentURLs, id: \.self) { url in
                        Button(url.lastPathComponent) {
                            controller.load(url: url)
                            controller.start()
                            controller.requestFocus()
                        }
                    }
                    if !controller.recentURLs.isEmpty {
                        Divider()
                        Button("Clear Menu") { controller.clearRecentDocuments() }
                    }
                }
                .disabled(controller.recentURLs.isEmpty)

                Divider()

                Button("Eject Media") {
                    controller.ejectMedia()
                    controller.requestFocus()
                }
                .keyboardShortcut("e", modifiers: .command)
                .disabled(controller.loadedFileName == nil)
            }

            // NOT `CommandMenu("View")`: that adds a SECOND menu titled View next to
            // the standard one AppKit/SwiftUI already provide. Replacing the .toolbar
            // group puts these items inside the one real View menu, which also keeps
            // AppKit's own "Enter Full Screen" (⌃⌘F) there as the standard alias.
            CommandGroup(replacing: .toolbar) {
                ForEach(1...4, id: \.self) { factor in
                    Toggle(factor == 1 ? "Actual Size (1x)" : "Zoom \(factor)x",
                           isOn: zoomBinding(factor))
                        .keyboardShortcut(KeyEquivalent(Character("\(factor)")), modifiers: .command)
                }

                Divider()

                Toggle("Show Status Bar", isOn: $showStatusBar)
                    .keyboardShortcut("/", modifiers: .command)

                Divider()

                // The user expects plain ⌘F. There is no Find command in this app, so
                // nothing competes for it. AppKit adds its own "Enter Full Screen"
                // (⌃⌘F) to the View menu once the window is .fullScreenPrimary, so the
                // standard alias keeps working without a duplicate item here.
                Button(windowManager.isFullScreen ? "Exit Full Screen" : "Enter Full Screen") {
                    toggleFullScreen()
                }
                .keyboardShortcut("f", modifiers: .command)
            }

            CommandMenu("Machine") {
                Button(controller.isRunning ? "Stop" : "Start") {
                    EmulatorActions.toggleRunning(controller)
                }
                .keyboardShortcut("r", modifiers: .command)

                Button(controller.isPaused ? "Resume" : "Pause") {
                    EmulatorActions.togglePause(controller)
                }
                .keyboardShortcut("p", modifiers: .command)
                .disabled(!controller.isRunning)

                Divider()

                Button("Reset") {
                    EmulatorActions.reset(controller)
                }
                .keyboardShortcut("r", modifiers: [.command, .shift])
            }
        }
    }

    /// Menu items are Toggles so AppKit draws the checkmark next to the scale in effect.
    private func zoomBinding(_ factor: Int) -> Binding<Bool> {
        Binding(get: { Int(windowManager.scale) == factor },
                set: { _ in
                    windowManager.apply(scale: CGFloat(factor))
                    controller.requestFocus()
                })
    }

    private func toggleFullScreen() {
        EmulatorActions.toggleFullScreen(controller, windowManager: windowManager)
    }

    private func openFile() { EmulatorActions.openFile(controller) }
}

/// Actions shared by the menu bar and the window toolbar.
enum EmulatorActions {
    static func openFile(_ controller: EmulatorController) {
        // Any modal steals key focus - drop held keys first so nothing latches.
        controller.releaseAllKeys()

        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.allowedContentTypes = EmulatorController.supportedExtensions.compactMap {
            UTType(filenameExtension: $0)
        }

        if panel.runModal() == .OK, let url = panel.url {
            controller.load(url: url)
            controller.start()
        }
        controller.requestFocus()
    }

    static func toggleRunning(_ controller: EmulatorController) {
        controller.toggleRunning()
        // Without this the toolbar button keeps key focus and the emulator goes deaf.
        controller.requestFocus()
    }

    static func togglePause(_ controller: EmulatorController) {
        controller.togglePause()
        controller.requestFocus()
    }

    static func reset(_ controller: EmulatorController) {
        controller.reset()
        controller.requestFocus()
    }

    /// `NSApp.sendAction(..., to: nil, ...)` walked the responder chain and found no
    /// target, so the old implementation was a no-op. Send it to the window itself.
    static func toggleFullScreen(_ controller: EmulatorController,
                                 windowManager: WindowManager?) {
        // The transition swallows key-ups; a latched modifier makes the keyboard dead.
        controller.releaseAllKeys()

        if let windowManager {
            windowManager.toggleFullScreen()
        } else if let window = NSApp.keyWindow ?? NSApp.mainWindow {
            window.collectionBehavior.insert(.fullScreenPrimary)
            window.toggleFullScreen(nil)
        }

        controller.requestFocus()
    }
}

struct ContentView: View {
    @ObservedObject var controller: EmulatorController
    @ObservedObject var windowManager: WindowManager
    @Binding var showStatusBar: Bool

    @State private var isTargeted = false

    var body: some View {
        let _ = GeometryLog.enabled
            ? NSLog("[ContentView] body: chromeHidden=%@", windowManager.chromeHidden ? "yes" : "no")
            : ()
        VStack(spacing: 0) {
            ZStack {
                Color.black
                MetalScreenView(controller: controller, windowManager: windowManager)

                // Sits inside the video area only, so it can never cover the status bar.
                if !controller.isRunning {
                    Text("Drop a .sna / .z80 / .tap / .tzx / .trd / .scl file here\nor press ⌘R to start")
                        .multilineTextAlignment(.center)
                        .font(.title3)
                        .foregroundStyle(.secondary)
                        .padding(24)
                        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
                        .allowsHitTesting(false)
                }
            }
            .overlay(alignment: .topLeading) {
                if isTargeted {
                    RoundedRectangle(cornerRadius: 4)
                        .strokeBorder(Color.accentColor, lineWidth: 3)
                }
            }

            // `chromeHidden` overrides the preference for the duration of a fullscreen
            // transition and of fullscreen itself, without disturbing the setting.
            if showStatusBar && !windowManager.chromeHidden {
                StatusBarView(controller: controller,
                              windowManager: windowManager,
                              telemetry: controller.telemetry)
            }
        }
        .frame(minWidth: UnrealNGApp.nativeWidth, minHeight: UnrealNGApp.nativeHeight)
        // With the bars gone there is no titlebar to inset for, but SwiftUI keeps
        // reserving its 28pt safe area - and re-deciding that mid-transition shifts
        // the picture by exactly that much. Give the content the whole window while
        // the chrome is suppressed so no such re-decision is pending.
        .ignoresSafeArea(.all, edges: windowManager.chromeHidden ? .all : [])
        .background(WindowAccessor {
            windowManager.attach($0)
            // SwiftUI's `.toolbar` never materialised on this window, so the toolbar
            // is built as a real NSToolbar once the NSWindow exists.
            windowManager.installToolbar(controller: controller)
        })
        .onDrop(of: [.fileURL], isTargeted: $isTargeted) { providers in
            handleDrop(providers)
        }
        .onAppear {
            windowManager.statusBarVisible = showStatusBar
            // Boot the machine straight into BASIC, like a real Spectrum
            controller.start()
        }
        .onChange(of: showStatusBar) { _, visible in
            // Growing/shrinking the content by the bar's height keeps the picture an
            // exact integer multiple instead of stealing 22pt from it.
            windowManager.statusBarVisible = visible
        }
        .onChange(of: controller.frameSize) { _, size in
            windowManager.update(frameSize: size)
        }
        // Fires only after SwiftUI has re-evaluated this body, i.e. the status bar is
        // genuinely out of the tree. The fullscreen transition waits for this before
        // handing over to AppKit - see whenChromeSettled.
        .onChange(of: windowManager.chromeHidden) {
            windowManager.chromeDidApply()
        }
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first else { return false }

        _ = provider.loadObject(ofClass: URL.self) { url, _ in
            guard let url, EmulatorController.isSupported(url: url) else { return }

            DispatchQueue.main.async {
                // Dropping auto-starts the machine (loadFile starts it if needed)
                controller.load(url: url)
                controller.start()
            }
        }
        return true
    }
}
