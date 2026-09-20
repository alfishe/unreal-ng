import UIKit

@main
final class AppDelegate: UIResponder, UIApplicationDelegate {

    var window: UIWindow?

    func application(
        _ application: UIApplication,
        didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
    ) -> Bool {
        application.isIdleTimerDisabled = true

        let resourceRoot = Bundle.main.bundlePath + "/data"
        let appSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        let writableRoot = appSupport.appendingPathComponent("UnrealNG").path

        try? FileManager.default.createDirectory(atPath: writableRoot, withIntermediateDirectories: true, attributes: nil)

        AudioSessionManager.shared.setupAudioSession()

        let bridge = UNGBridge.shared()
        let ok = bridge.startSystem(withResourceRoot: resourceRoot, writableRoot: writableRoot, webApiPort: 8090, cliPort: 8765)
        if ok {
            _ = bridge.createEmulator(withModel: "PENTAGON", symbolicId: "ios-01")
            _ = bridge.startEmulator()
            AudioSessionManager.shared.startAudioUnit()
        }

        return true
    }

    func applicationWillTerminate(_ application: UIApplication) {
        AudioSessionManager.shared.stopAudioUnit()
        UNGBridge.shared().shutdownSystem()
    }

    // MARK: UISceneSession Lifecycle

    func application(
        _ application: UIApplication,
        configurationForConnecting connectingSceneSession: UISceneSession,
        options: UIScene.ConnectionOptions
    ) -> UISceneConfiguration {
        let config = UISceneConfiguration(name: "Default Configuration", sessionRole: connectingSceneSession.role)
        config.delegateClass = SceneDelegate.self
        return config
    }
}
