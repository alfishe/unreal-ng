import UIKit

final class SceneDelegate: UIResponder, UIWindowSceneDelegate {

    var window: UIWindow?

    func scene(
        _ scene: UIScene,
        willConnectTo session: UISceneSession,
        options connectionOptions: UIScene.ConnectionOptions
    ) {
        guard let windowScene = (scene as? UIWindowScene) else { return }

        let window = UIWindow(windowScene: windowScene)
        window.rootViewController = ScreenViewController()
        self.window = window
        window.makeKeyAndVisible()
    }

    func sceneDidBecomeActive(_ scene: UIScene) {
        UNGBridge.shared().setAudioActive(true)
    }

    func sceneWillResignActive(_ scene: UIScene) {
        // Audio background mode active if playing
    }

    func sceneDidEnterBackground(_ scene: UIScene) {
        // Keeps WebAPI server running
    }

    func sceneWillEnterForeground(_ scene: UIScene) {
        UNGBridge.shared().setAudioActive(true)
    }
}
