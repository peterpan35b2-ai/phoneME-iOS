import SwiftUI

@main
struct PhoneMEApp: App {
    @StateObject private var library = GameLibrary()
    @StateObject private var profiles = GameProfileStore()
    @StateObject private var profileTemplates = ProfileTemplateStore()
    @StateObject private var session = EmulatorSession()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(library)
                .environmentObject(profiles)
                .environmentObject(profileTemplates)
                .environmentObject(session)
        }

#if os(macOS)
        Settings {
            SettingsView()
                .environmentObject(profileTemplates)
                .frame(width: 420)
        }
#endif
    }
}
