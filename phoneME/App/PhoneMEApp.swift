import SwiftUI
#if os(iOS)
import UIKit

@MainActor
final class PhoneMEAppDelegate: NSObject, UIApplicationDelegate {
    static var supportedOrientationMask: UIInterfaceOrientationMask = .allButUpsideDown

    func application(
        _ application: UIApplication,
        supportedInterfaceOrientationsFor window: UIWindow?
    ) -> UIInterfaceOrientationMask {
        Self.supportedOrientationMask
    }
}
#endif

@main
struct PhoneMEApp: App {
#if os(iOS)
    @UIApplicationDelegateAdaptor(PhoneMEAppDelegate.self) private var appDelegate
#endif
    @Environment(\.scenePhase) private var scenePhase

    @StateObject private var library = GameLibrary()
    @StateObject private var profiles = GameProfileStore()
    @StateObject private var profileTemplates = ProfileTemplateStore()
    @StateObject private var session = EmulatorSession()
    @StateObject private var backgroundExecution = BackgroundExecutionController()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(library)
                .environmentObject(profiles)
                .environmentObject(profileTemplates)
                .environmentObject(session)
                .environmentObject(backgroundExecution)
                .onAppear {
                    backgroundExecution.setHasRunningApplications(
                        !session.runningApplications.isEmpty
                    )
                    updateLifecycle(for: scenePhase)
                }
                .onChange(of: session.runningApplications.count) { count in
                    backgroundExecution.setHasRunningApplications(count > 0)
                }
                .onChange(of: scenePhase) { newPhase in
                    updateLifecycle(for: newPhase)
                }
        }

#if os(macOS)
        Settings {
            SettingsView()
                .environmentObject(profileTemplates)
                .frame(width: 420)
        }
#endif
    }

    private func updateLifecycle(for phase: ScenePhase) {
        switch phase {
        case .active:
            backgroundExecution.setShouldKeepAlive(false)
            session.setApplicationInBackground(false)
            session.resume()
        case .background:
            backgroundExecution.setShouldKeepAlive(true)
            session.setApplicationInBackground(true)
            session.suspend()
        case .inactive:
            // Start the optional location keeper before iOS finishes locking
            // or backgrounding the scene. The VM is not suspended here, which
            // avoids racing transient overlays and Displayable transitions.
            backgroundExecution.setShouldKeepAlive(true)
            session.setApplicationInBackground(true)
        @unknown default:
            break
        }
    }
}
