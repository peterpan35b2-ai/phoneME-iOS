import SwiftUI

struct ContentView: View {
    @AppStorage("appTheme") private var appTheme = AppTheme.system.rawValue

    var body: some View {
        PhoneMENavigationStack {
            LibraryView()
        }
        .preferredColorScheme(AppTheme(rawValue: appTheme)?.colorScheme)
#if os(macOS)
        .frame(minWidth: 720, minHeight: 560)
#endif
    }
}
