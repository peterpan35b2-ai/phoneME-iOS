import SwiftUI
import UniformTypeIdentifiers
#if os(macOS)
import AppKit
#else
import UIKit
#endif

enum LibrarySort: String, CaseIterable, Identifiable {
    case name
    case date
    case vendor

    var id: String { rawValue }
    var title: String {
        switch self {
        case .name: return "Name"
        case .date: return "Date"
        case .vendor: return "Vendor"
        }
    }
}

struct LibraryView: View {
    @EnvironmentObject private var library: GameLibrary
    @EnvironmentObject private var profiles: GameProfileStore
    @EnvironmentObject private var profileTemplates: ProfileTemplateStore
    @EnvironmentObject private var session: EmulatorSession

    @AppStorage("librarySort") private var sortRawValue = LibrarySort.name.rawValue
    @AppStorage("librarySortDescending") private var sortDescending = false

    @State private var activeGame: Game?
    @State private var configuringGame: Game?
    @State private var pendingGameToLaunch: Game?
    @State private var installedGame: Game?
    @State private var renameGame: Game?
    @State private var renameText = ""
    @State private var deleteGame: Game?
    @State private var reinstallGame: Game?
    @State private var isImporting = false
    @State private var showSearch = false
    @State private var searchText = ""
    @State private var showSort = false
    @State private var showSettings = false
    @State private var showProfiles = false
    @State private var showAbout = false
    @State private var showHelp = false
    @State private var showLogSaved = false
    @State private var errorMessage: String?

    private var jarType: UTType {
        UTType(filenameExtension: "jar") ?? .data
    }

    private var sort: LibrarySort {
        LibrarySort(rawValue: sortRawValue) ?? .name
    }

    private var visibleGames: [Game] {
        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        let filtered = query.isEmpty ? library.games : library.games.filter {
            $0.title.localizedCaseInsensitiveContains(query)
                || $0.vendor.localizedCaseInsensitiveContains(query)
        }

        return filtered.sorted { lhs, rhs in
            let result: ComparisonResult
            switch sort {
            case .name:
                result = lhs.title.localizedStandardCompare(rhs.title)
            case .date:
                result = lhs.importedAt.compare(rhs.importedAt)
            case .vendor:
                let vendorResult = lhs.vendor.localizedStandardCompare(rhs.vendor)
                result = vendorResult == .orderedSame
                    ? lhs.title.localizedStandardCompare(rhs.title)
                    : vendorResult
            }
            return sortDescending ? result == .orderedDescending : result == .orderedAscending
        }
    }

    var body: some View {
        ZStack(alignment: .bottomTrailing) {
            VStack(spacing: 0) {
                if showSearch {
                    HStack(spacing: 8) {
                        Image(systemName: "magnifyingglass")
                            .foregroundStyle(.secondary)
                        TextField("Search", text: $searchText)
                            .textFieldStyle(.plain)
                        if !searchText.isEmpty {
                            Button {
                                searchText = ""
                            } label: {
                                Image(systemName: "xmark.circle.fill")
                                    .foregroundStyle(.secondary)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                    .padding(.horizontal, 12)
                    .frame(height: 44)
                    .background(Color.phoneMELibraryRow)
                    Divider()
                }

                if library.games.isEmpty {
                    Text("No data")
                        .font(.title3)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
                        .padding(.top, 10)
                } else {
                    List(visibleGames) { game in
                        GameRow(game: game, iconURL: library.iconURL(for: game))
                            .contentShape(Rectangle())
                            .onTapGesture {
                                open(game)
                            }
                            .contextMenu {
                                contextMenu(for: game)
                            }
                    }
                    .listStyle(.plain)
#if os(iOS)
                    .environment(\.defaultMinListRowHeight, 56)
#endif
                }
            }

            Button {
                reinstallGame = nil
                isImporting = true
            } label: {
                Image(systemName: "plus")
                    .font(.title2.weight(.medium))
                    .foregroundStyle(.white)
                    .frame(width: 56, height: 56)
                    .background(Color.red, in: Circle())
                    .shadow(radius: 5, y: 3)
            }
            .buttonStyle(.plain)
            .padding(16)
            .accessibilityLabel("Add")
        }
        .navigationTitle("J2ME Loader")
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
#endif
        .toolbar {
            ToolbarItemGroup(placement: .primaryAction) {
                Button {
                    withAnimation(.easeInOut(duration: 0.15)) {
                        showSearch.toggle()
                        if !showSearch { searchText = "" }
                    }
                } label: {
                    Image(systemName: "magnifyingglass")
                }
                .accessibilityLabel("Search")

                Button {
                    showSort = true
                } label: {
                    Image(systemName: "arrow.up.arrow.down")
                }
                .accessibilityLabel("App sort order")

                Menu {
                    Button("About") { showAbout = true }
                    Button("Settings") { showSettings = true }
                    Button("Profiles") { showProfiles = true }
                    Button("Help") { showHelp = true }
                    Button("Save log") { saveLog() }
                    Button("Exit") { exitApplication() }
                } label: {
                    Image(systemName: "ellipsis")
                }
            }
        }
        .confirmationDialog("App sort order", isPresented: $showSort, titleVisibility: .visible) {
            ForEach(LibrarySort.allCases) { item in
                Button(sortButtonTitle(item)) {
                    selectSort(item)
                }
            }
        }
        .jarFileImporter(
            isPresented: $isImporting,
            contentTypes: [jarType],
            onCompletion: importFiles
        )
        .sheet(item: $installedGame) { game in
            InstallerSuccessView(
                game: game,
                iconURL: library.iconURL(for: game),
                closeAction: {
                    installedGame = nil
                },
                startAction: {
                    installedGame = nil
                    DispatchQueue.main.async {
                        open(game)
                    }
                }
            )
        }
        .gamePresentation(item: $activeGame, onDismiss: {
            session.stop()
        }) { game in
            EmulatorView(
                game: game,
                profile: profiles.profile(for: game),
                closeAction: {
                    activeGame = nil
                }
            )
        }
        .sheet(item: $configuringGame, onDismiss: launchPendingGame) { game in
            NavigationStack {
                GameProfileEditorView(
                    game: game,
                    initialProfile: profiles.profile(for: game)
                ) {
                    pendingGameToLaunch = game
                    configuringGame = nil
                }
            }
        }
        .sheet(isPresented: $showSettings) {
            NavigationStack { SettingsView() }
        }
        .sheet(isPresented: $showProfiles) {
            NavigationStack { ProfilesView() }
        }
        .sheet(isPresented: $showAbout) {
            AboutView()
        }
        .sheet(isPresented: $showHelp) {
            HelpView()
        }
        .alert("Rename", isPresented: renameAlertBinding) {
            TextField("", text: $renameText)
            Button("Cancel", role: .cancel) {}
            Button("OK") {
                if let renameGame {
                    library.rename(renameGame, to: renameText)
                }
            }
        }
        .confirmationDialog(
            "Do you really want to delete this app?",
            isPresented: deleteDialogBinding,
            titleVisibility: .visible
        ) {
            Button("OK", role: .destructive) {
                if let deleteGame {
                    profiles.removeProfile(for: deleteGame)
                    library.remove(deleteGame)
                }
                self.deleteGame = nil
            }
            Button("Cancel", role: .cancel) {
                deleteGame = nil
            }
        }
        .alert("Log has been saved", isPresented: $showLogSaved) {
            Button("OK", role: .cancel) {}
        }
        .alert("Error", isPresented: errorAlertBinding) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(errorMessage ?? "")
        }
        .onOpenURL { url in
            do {
                let game = try library.importJar(from: url)
                configuringGame = game
            } catch {
                errorMessage = error.localizedDescription
            }
        }
#if DEBUG
        .task {
            let environment = ProcessInfo.processInfo.environment
            guard environment["PHONEME_AUTOLAUNCH"] == "1",
                  activeGame == nil else { return }

            let requestedID = environment["PHONEME_AUTOLAUNCH_GAME_ID"]
            let requestedTitle = environment["PHONEME_AUTOLAUNCH_GAME_TITLE"]
            let game = library.games.first { game in
                if let requestedID {
                    return game.id.uuidString.caseInsensitiveCompare(requestedID) == .orderedSame
                }
                if let requestedTitle {
                    return game.title.localizedCaseInsensitiveCompare(requestedTitle) == .orderedSame
                }
                return true
            }
            guard let game else { return }
            print("PHONEME_AUTOLAUNCH id=\(game.id.uuidString) title=\(game.title)")
            activeGame = game
        }
#endif
    }

    @ViewBuilder
    private func contextMenu(for game: Game) -> some View {
        Button("Add shortcut") {}
        Button("Rename") { beginRename(game) }
        Button("Settings") { configuringGame = game }
        Button("Reinstall") {
            reinstallGame = game
            isImporting = true
        }
        Button("Delete", role: .destructive) { deleteGame = game }
    }

    private func open(_ game: Game) {
        if profiles.hasProfile(for: game) {
            activeGame = game
        } else {
            pendingGameToLaunch = nil
            configuringGame = game
        }
    }

    private func launchPendingGame() {
        guard let game = pendingGameToLaunch else { return }
        pendingGameToLaunch = nil
        activeGame = game
    }

    private func beginRename(_ game: Game) {
        renameText = game.title
        renameGame = game
    }

    private func selectSort(_ item: LibrarySort) {
        if item == sort {
            sortDescending.toggle()
        } else {
            sortRawValue = item.rawValue
            sortDescending = false
        }
    }

    private func sortButtonTitle(_ item: LibrarySort) -> String {
        guard item == sort else { return item.title }
        return item.title + (sortDescending ? " ↑" : " ↓")
    }

    private func importFiles(_ result: Result<[URL], Error>) {
        do {
            let urls = try result.get()
            guard let url = urls.first else { return }
            if let game = reinstallGame {
                try library.reinstall(game, from: url)
                reinstallGame = nil
                installedGame = game
                return
            }

            let game = try library.importJar(from: url)
            applyDefaultProfile(to: game)
            installedGame = game
        } catch {
            reinstallGame = nil
            errorMessage = error.localizedDescription
        }
    }

    private func applyDefaultProfile(to game: Game) {
        guard let defaultID = profileTemplates.defaultTemplateID,
              let template = profileTemplates.templates.first(where: { $0.id == defaultID }) else {
            return
        }
        profiles.save(template.profile, for: game)
    }

    private func saveLog() {
        do {
            try library.saveLog()
            showLogSaved = true
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func exitApplication() {
#if os(macOS)
        NSApplication.shared.terminate(nil)
#endif
    }

    private var renameAlertBinding: Binding<Bool> {
        Binding(
            get: { renameGame != nil },
            set: { if !$0 { renameGame = nil } }
        )
    }

    private var deleteDialogBinding: Binding<Bool> {
        Binding(
            get: { deleteGame != nil },
            set: { if !$0 { deleteGame = nil } }
        )
    }

    private var errorAlertBinding: Binding<Bool> {
        Binding(
            get: { errorMessage != nil },
            set: { if !$0 { errorMessage = nil } }
        )
    }
}

private struct GameRow: View {
    let game: Game
    let iconURL: URL?

    var body: some View {
        HStack(spacing: 10) {
            GameIconView(iconURL: iconURL)
                .frame(width: 36, height: 36)

            VStack(alignment: .leading, spacing: 2) {
                Text(game.title)
                    .font(.system(size: 15, weight: .bold))
                    .lineLimit(1)

                HStack(spacing: 8) {
                    Text(game.vendor)
                        .lineLimit(1)
                        .frame(maxWidth: .infinity, alignment: .leading)

                    Text(game.version)
                        .lineLimit(1)
                }
                .font(.system(size: 12))
                .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 5)
    }
}

private struct GameIconView: View {
    let iconURL: URL?

    var body: some View {
        ZStack {
            Rectangle()
                .fill(Color.phoneMEIconBackground)

            if let image = platformImage {
                image
                    .resizable()
                    .interpolation(.none)
                    .scaledToFill()
            } else {
                Image(systemName: "gamecontroller")
                    .font(.system(size: 18))
                    .foregroundStyle(.secondary)
            }
        }
        .clipped()
    }

    private var platformImage: Image? {
        guard let iconURL,
              let data = try? Data(contentsOf: iconURL) else { return nil }
#if os(macOS)
        guard let image = NSImage(data: data) else { return nil }
        return Image(nsImage: image)
#else
        guard let image = UIImage(data: data) else { return nil }
        return Image(uiImage: image)
#endif
    }
}

private struct InstallerSuccessView: View {
    let game: Game
    let iconURL: URL?
    let closeAction: () -> Void
    let startAction: () -> Void

    var body: some View {
        VStack(spacing: 18) {
            Text("MIDlet installer")
                .font(.headline)

            GameIconView(iconURL: iconURL)
                .frame(width: 48, height: 48)

            Text("Application successfully installed!")
                .multilineTextAlignment(.center)

            HStack {
                Button("Close", action: closeAction)
                    .keyboardShortcut(.cancelAction)
                Spacer()
                Button("Start", action: startAction)
                    .buttonStyle(.borderedProminent)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(minWidth: 300)
        .interactiveDismissDisabled()
#if os(iOS)
        .presentationDetents([.height(230)])
        .presentationDragIndicator(.hidden)
#endif
    }
}

private struct AboutView: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 10) {
                    Text("Version: 1.8.2")
                    Link("Email: j2me.loader@mail.ru", destination: URL(string: "mailto:j2me.loader@mail.ru")!)
                    Link("GitHub: github.com/nikita36078/J2ME-Loader", destination: URL(string: "https://github.com/nikita36078/J2ME-Loader")!)
                    Link("4PDA: 4pda.to/forum/index.php?showtopic=824201", destination: URL(string: "https://4pda.to/forum/index.php?showtopic=824201")!)
                    Link("XDA: forum.xda-developers.com/android/apps-games/app-j2me-loader-t3777889", destination: URL(string: "https://forum.xda-developers.com/android/apps-games/app-j2me-loader-t3777889")!)
                    Link("EmuGen Wiki: emulation.gametechwiki.com/index.php/J2ME_Loader", destination: URL(string: "https://emulation.gametechwiki.com/index.php/J2ME_Loader")!)
                    Link("Crowdin(translation): crowdin.com/project/j2me-loader", destination: URL(string: "https://crowdin.com/project/j2me-loader")!)
                    Text("Copyright 2017-2024 Nikita Shakarun")
                        .padding(.top, 8)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(20)
            }
            .navigationTitle("J2ME Loader")
            .safeAreaInset(edge: .bottom) {
                HStack {
                    Button("More") {}
                    Spacer()
                    Button("Licenses") {}
                }
                .padding(.horizontal, 20)
                .padding(.vertical, 12)
                .background(.bar)
            }
        }
    }
}

private struct HelpView: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Text("• Enabling filtering in some cases can greatly reduce performance. Disable this option if game is too slow.\n\n• Image flickering issues can be fixed by enabling the \"Immediate processing mode\" option.")
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                .padding(20)
                .navigationTitle("Help")
        }
    }
}

private struct JarFileImporterModifier: ViewModifier {
    @Binding var isPresented: Bool
    let contentTypes: [UTType]
    let onCompletion: (Result<[URL], Error>) -> Void

    @ViewBuilder
    func body(content: Content) -> some View {
#if os(iOS)
        content.sheet(isPresented: $isPresented) {
            JarDocumentPicker(
                contentTypes: contentTypes,
                onPicked: { urls in
                    isPresented = false
                    onCompletion(.success(urls))
                },
                onCancelled: {
                    isPresented = false
                }
            )
            .ignoresSafeArea()
        }
#else
        content.fileImporter(
            isPresented: $isPresented,
            allowedContentTypes: contentTypes,
            allowsMultipleSelection: false,
            onCompletion: onCompletion
        )
#endif
    }

}

#if os(iOS)
private struct JarDocumentPicker: UIViewControllerRepresentable {
    let contentTypes: [UTType]
    let onPicked: ([URL]) -> Void
    let onCancelled: () -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(onPicked: onPicked, onCancelled: onCancelled)
    }

    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: contentTypes,
            asCopy: true
        )
        picker.delegate = context.coordinator
        picker.allowsMultipleSelection = false
        picker.shouldShowFileExtensions = true
        picker.modalPresentationStyle = .formSheet
        return picker
    }

    func updateUIViewController(
        _ uiViewController: UIDocumentPickerViewController,
        context: Context
    ) {}

    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        private let onPicked: ([URL]) -> Void
        private let onCancelled: () -> Void

        init(
            onPicked: @escaping ([URL]) -> Void,
            onCancelled: @escaping () -> Void
        ) {
            self.onPicked = onPicked
            self.onCancelled = onCancelled
        }

        func documentPicker(
            _ controller: UIDocumentPickerViewController,
            didPickDocumentsAt urls: [URL]
        ) {
            onPicked(urls)
        }

        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
            onCancelled()
        }
    }
}
#endif

private extension View {
    func jarFileImporter(
        isPresented: Binding<Bool>,
        contentTypes: [UTType],
        onCompletion: @escaping (Result<[URL], Error>) -> Void
    ) -> some View {
        modifier(
            JarFileImporterModifier(
                isPresented: isPresented,
                contentTypes: contentTypes,
                onCompletion: onCompletion
            )
        )
    }

    @ViewBuilder
    func gamePresentation<Content: View>(
        item: Binding<Game?>,
        onDismiss: @escaping () -> Void,
        @ViewBuilder content: @escaping (Game) -> Content
    ) -> some View {
#if os(macOS)
        sheet(item: item, onDismiss: onDismiss, content: content)
#else
        fullScreenCover(item: item, onDismiss: onDismiss, content: content)
#endif
    }
}

private extension Color {
    static var phoneMELibraryRow: Color {
#if os(macOS)
        Color(nsColor: .textBackgroundColor)
#else
        Color(uiColor: .systemBackground)
#endif
    }

    static var phoneMEIconBackground: Color {
#if os(macOS)
        Color(nsColor: .controlBackgroundColor)
#else
        Color(uiColor: .secondarySystemBackground)
#endif
    }
}
