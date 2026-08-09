import AppKit
import Combine

/// Observable façade the SwiftUI layer talks to. Owns the ObjC++ bridge, translates
/// AppKit key events into ZX matrix events and republishes core notifications.
final class EmulatorController: NSObject, ObservableObject, UNEmulatorBridgeDelegate {
    @Published private(set) var isRunning = false
    @Published private(set) var isPaused = false
    @Published private(set) var frameSize = CGSize(width: 352, height: 288)
    @Published private(set) var statusText = "Idle"
    @Published private(set) var loadedFileName: String?
    @Published private(set) var recentURLs: [URL] = []

    /// Fast-moving readouts (fps, activity LEDs) live in their own observable object.
    ///
    /// They MUST NOT be @Published on the controller: SwiftUI rebuilds the whole
    /// `.commands` tree whenever an object the App scope observes publishes, and an
    /// NSMenu that is rebuilt while open is torn down and closes under the cursor.
    /// That was BUG 8. Only StatusBarView observes this.
    let telemetry = Telemetry()

    /// Subsystems whose activity the core can actually report. Anything missing here
    /// is drawn permanently dark rather than faked.
    static let supportedActivity: ActivityFlags = [.tape, .disk, .sound]

    struct ActivityFlags: OptionSet {
        let rawValue: UInt
        static let tape = ActivityFlags(rawValue: UInt(UNActivityFlags.tape.rawValue))
        static let disk = ActivityFlags(rawValue: UInt(UNActivityFlags.disk.rawValue))
        static let sound = ActivityFlags(rawValue: UInt(UNActivityFlags.sound.rawValue))
    }

    final class Telemetry: ObservableObject {
        @Published fileprivate(set) var fps: Double = 0
        @Published fileprivate(set) var activity: ActivityFlags = []
    }

    /// Bumped on every emulated frame; the Metal view redraws when it changes.
    let frameTick = PassthroughSubject<Void, Never>()
    /// Asks the Metal view to take back key focus (after a toolbar/menu interaction).
    let focusRequest = PassthroughSubject<Void, Never>()

    /// Run state as shown in the status bar.
    var stateText: String {
        guard isRunning else { return "Stopped" }
        return isPaused ? "Paused" : "Running"
    }

    let bridge = UNEmulatorBridge()

    /// Modifier flags whose ZX equivalents are currently held.
    private var heldModifiers: NSEvent.ModifierFlags = []

    /// FPS accounting. Measured here, in Swift - the bridge stays free of per-frame work.
    private var framesInWindow = 0
    private var fpsWindowStart = CFAbsoluteTimeGetCurrent()

    private var statsSubscription: AnyCancellable?

    override init() {
        super.init()
        if PublishStats.enabled {
            statsSubscription = objectWillChange.sink { PublishStats.note("controller") }
        }
        AppDelegate.activeController = self
        bridge.delegate = self
        recentURLs = NSDocumentController.shared.recentDocumentURLs
    }

    deinit {
        bridge.delegate = nil
        bridge.stop()
    }

    // MARK: - Transport

    @discardableResult
    func start() -> Bool {
        guard !bridge.isRunning else {
            if bridge.isPaused { bridge.resume() }
            return true
        }

        let ok = bridge.start()
        statusText = ok ? "Running" : "Failed to start emulator"
        return ok
    }

    func stop() {
        releaseAllKeys()
        bridge.stop()
        statusText = "Stopped"
        isPaused = false
        telemetry.fps = 0
        framesInWindow = 0
    }

    func toggleRunning() {
        if bridge.isRunning { stop() } else { start() }
    }

    func togglePause() {
        guard bridge.isRunning else { return }
        if bridge.isPaused {
            bridge.resume()
        } else {
            // A paused machine cannot process key-ups; drop what is held first.
            releaseAllKeys()
            bridge.pause()
        }
        isPaused = bridge.isPaused
        statusText = isPaused ? "Paused" : "Running"
        if isPaused { telemetry.fps = 0 }
    }

    func reset() {
        if bridge.isRunning {
            bridge.reset()
            statusText = "Reset"
        } else {
            start()
        }
    }

    // MARK: - Media

    @discardableResult
    func load(url: URL) -> Bool {
        releaseAllKeys()

        let ok = bridge.loadFile(url.path)
        if ok {
            loadedFileName = url.lastPathComponent
            statusText = "Loaded \(url.lastPathComponent)"
            NSDocumentController.shared.noteNewRecentDocumentURL(url)
            recentURLs = NSDocumentController.shared.recentDocumentURLs
        } else {
            statusText = "Failed to load \(url.lastPathComponent)"
        }
        return ok
    }

    /// The bridge has no eject entry point, so "eject" is the closest equivalent we
    /// can build on top of what exists: forget the media and reset the machine, which
    /// drops whatever the loaded image had installed in RAM.
    func ejectMedia() {
        guard loadedFileName != nil else { return }
        releaseAllKeys()
        if bridge.isRunning { bridge.reset() }
        loadedFileName = nil
        statusText = "Media ejected"
    }

    func clearRecentDocuments() {
        NSDocumentController.shared.clearRecentDocuments(nil)
        recentURLs = []
    }

    func requestFocus() {
        focusRequest.send()
    }

    static var supportedExtensions: [String] { UNEmulatorBridge.supportedFileExtensions() }

    static func isSupported(url: URL) -> Bool {
        supportedExtensions.contains(url.pathExtension.lowercased())
    }

    // MARK: - Keyboard

    func handleKeyDown(_ event: NSEvent) {
        guard !event.isARepeat, let key = ZXKeyboardMap.zxKey(forVirtualKeyCode: event.keyCode) else { return }
        bridge.pressKey(key)
    }

    func handleKeyUp(_ event: NSEvent) {
        guard let key = ZXKeyboardMap.zxKey(forVirtualKeyCode: event.keyCode) else { return }
        bridge.releaseKey(key)
    }

    /// macOS reports modifiers as state, not as press/release - diff against what we hold.
    func handleFlagsChanged(_ event: NSEvent) {
        let flags = event.modifierFlags

        for (flag, key) in ZXKeyboardMap.modifiers {
            let nowDown = flags.contains(flag)
            let wasDown = heldModifiers.contains(flag)
            guard nowDown != wasDown else { continue }

            if nowDown {
                heldModifiers.insert(flag)
                bridge.pressKey(key)
            } else {
                heldModifiers.remove(flag)
                bridge.releaseKey(key)
            }
        }
    }

    /// Drop every held key. Call on focus loss and before any modal or fullscreen
    /// transition - a modifier whose key-up gets swallowed stays latched in the ZX
    /// matrix and the machine stops responding to the keyboard.
    func releaseAllKeys() {
        heldModifiers = []
        bridge.releaseAllKeys()
    }

    // MARK: - UNEmulatorBridgeDelegate (always called on the main thread)

    func emulatorBridgeDidRenderFrame() {
        frameTick.send()

        // One cheap flag read per frame; only publishes on an edge.
        let flags = ActivityFlags(rawValue: UInt(bridge.activityFlags.rawValue))
        if flags != telemetry.activity { telemetry.activity = flags }

        // Cheap counter; @Published is only written once a second so SwiftUI does not
        // re-render the chrome at 50Hz.
        framesInWindow += 1
        let now = CFAbsoluteTimeGetCurrent()
        let elapsed = now - fpsWindowStart
        if elapsed >= 1.0 {
            telemetry.fps = Double(framesInWindow) / elapsed
            framesInWindow = 0
            fpsWindowStart = now
        }
    }

    func emulatorBridgeDidChangeResolution(_ width: Int32, height: Int32) {
        frameSize = CGSize(width: Int(width), height: Int(height))
    }

    func emulatorBridgeDidChangeState() {
        isRunning = bridge.isRunning
        isPaused = bridge.isPaused
        if !isRunning {
            telemetry.fps = 0
            telemetry.activity = []
        }
    }
}
