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
    @AppStorage(AppLanguage.preferenceKey) private var appLanguage =
        AppLanguage.defaultLanguage.rawValue

    @StateObject private var storage: PhoneMEStorageController
    @StateObject private var library: GameLibrary
    @StateObject private var profiles: GameProfileStore
    @StateObject private var profileTemplates: ProfileTemplateStore
    @StateObject private var session: EmulatorSession
    @StateObject private var backgroundExecution: BackgroundExecutionController

    init() {
        let storage = PhoneMEStorageController()
        PhoneMERuntimeResources.configure(storageRootURL: storage.rootURL)
        _storage = StateObject(wrappedValue: storage)
        _library = StateObject(wrappedValue: GameLibrary(storage: storage))
        _profiles = StateObject(wrappedValue: GameProfileStore(storage: storage))
        _profileTemplates = StateObject(
            wrappedValue: ProfileTemplateStore(storage: storage)
        )
        _session = StateObject(wrappedValue: EmulatorSession())
        _backgroundExecution = StateObject(
            wrappedValue: BackgroundExecutionController()
        )
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(storage)
                .environmentObject(library)
                .environmentObject(profiles)
                .environmentObject(profileTemplates)
                .environmentObject(session)
                .environmentObject(backgroundExecution)
                .environment(
                    \.locale,
                    AppLanguage(rawValue: appLanguage)?.locale
                        ?? AppLanguage.defaultLanguage.locale
                )
                .onAppear {
                    backgroundExecution.setRunningApplicationCount(
                        session.runningApplications.count
                    )
                    updateLifecycle(for: scenePhase)
#if DEBUG
                    debugLaunchIfRequested()
#endif
                }
                .onChange(of: session.runningApplications.count) { count in
                    backgroundExecution.setRunningApplicationCount(count)
                }
                .onChange(of: scenePhase) { newPhase in
                    updateLifecycle(for: newPhase)
                }
        }

#if os(macOS)
        Settings {
            SettingsView()
                .environmentObject(storage)
                .environmentObject(library)
                .environmentObject(profiles)
                .environmentObject(profileTemplates)
                .environmentObject(session)
                .environmentObject(backgroundExecution)
                .environment(
                    \.locale,
                    AppLanguage(rawValue: appLanguage)?.locale
                        ?? AppLanguage.defaultLanguage.locale
                )
                .frame(width: 420)
        }
#endif
    }

#if DEBUG
    private func debugLaunchIfRequested() {
        guard let path = ProcessInfo.processInfo.environment["PHONEME_DEBUG_JAR"],
              !path.isEmpty else {
            return
        }

        let sourceURL = URL(fileURLWithPath: path)
        let environment = ProcessInfo.processInfo.environment
        let reopenCount = Int(environment["PHONEME_DEBUG_REOPEN_COUNT"] ?? "")
            ?? (environment["PHONEME_DEBUG_REOPEN"] == "1" ? 1 : 0)
        Task { @MainActor in
            let reportURL = FileManager.default.temporaryDirectory
                .appendingPathComponent("phoneme-debug-report.txt")
            do {
                let game = try library.importJar(from: sourceURL)
                let jarURL = try library.prepareJarForLaunch(game)
                session.launch(
                    game: game,
                    jarURL: jarURL,
                    artworkURL: library.iconURL(for: game),
                    profile: .default
                )

                for _ in 0..<max(reopenCount, 0) {
                    for _ in 0..<400 where session.state != .running {
                        if case .failed = session.state { break }
                        try? await Task.sleep(nanoseconds: 50_000_000)
                    }
                    guard session.state == .running else { break }

                    session.hideCurrent()
                    try? await Task.sleep(nanoseconds: 500_000_000)
                    session.launch(
                        game: game,
                        jarURL: jarURL,
                        artworkURL: library.iconURL(for: game),
                        profile: .default
                    )
                }

                try? await Task.sleep(nanoseconds: 8_000_000_000)
                let appState = session.runningApplications[game.id]
                    .map { String(describing: $0.state) } ?? "missing"
                let report = [
                    "source=\(sourceURL.lastPathComponent)",
                    "mainClass=\(game.mainClass)",
                    "reopenCount=\(reopenCount)",
                    "state=\(String(describing: session.state))",
                    "application=\(appState)",
                    "frame=\(session.frame != nil)",
                    "fps=\(session.fpsStore.value)",
                    "presentation=\(String(describing: session.presentationMode))",
                    "nativeLCDUI=\(session.isPresentingNativeLCDUI)"
                ].joined(separator: "\n")
                try report.write(to: reportURL, atomically: true, encoding: .utf8)
                print("[PhoneMEDebug] \(report.replacingOccurrences(of: "\n", with: " "))")
            } catch {
                let report = "source=\(sourceURL.lastPathComponent)\nsetupError=\(String(describing: error))\nlocalized=\(error.localizedDescription)"
                try? report.write(to: reportURL, atomically: true, encoding: .utf8)
                print("[PhoneMEDebug] \(report.replacingOccurrences(of: "\n", with: " "))")
            }
        }
    }
#endif

    private func updateLifecycle(for phase: ScenePhase) {
        switch phase {
        case .active:
            backgroundExecution.setApplicationPhase(.active)
            session.resume()
        case .inactive:
            // Arm coarse Location while iOS is still completing the transition
            // out of the foreground. This is intentionally stopped again as
            // soon as the scene becomes active, so transient overlays cost only
            // a very short low-accuracy location session.
            backgroundExecution.setApplicationPhase(.inactive)
        case .background:
            backgroundExecution.setApplicationPhase(.background)
            session.suspend()
        @unknown default:
            break
        }
    }
}
