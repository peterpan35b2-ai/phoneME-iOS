import SwiftUI

struct ContentView: View {
    @AppStorage("appTheme") private var appTheme = AppTheme.system.rawValue

    var body: some View {
        TabView {
            PhoneMENavigationStack {
                LibraryView()
            }
            .tabItem {
                Label("Library", systemImage: "square.stack.3d.up")
            }

            PhoneMENavigationStack {
                WorkspacesView()
            }
            .tabItem {
                Label("Workspaces", systemImage: "rectangle.3.group")
            }
        }
        .preferredColorScheme(AppTheme(rawValue: appTheme)?.colorScheme)
#if os(macOS)
        .frame(minWidth: 720, minHeight: 560)
#endif
    }
}
