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
        case .name: return L10n.string("Name")
        case .date: return L10n.string("Date")
        case .vendor: return L10n.string("Vendor")
        }
    }

    var systemImage: String {
        switch self {
        case .name: return "textformat"
        case .date: return "calendar"
        case .vendor: return "building.2"
        }
    }
}

private struct LibrarySection: Identifiable {
    let title: String
    var games: [Game]

    var id: String { title }
}

#if DEBUG
@MainActor
private final class LibraryDebugAutolaunchGate {
    static let shared = LibraryDebugAutolaunchGate()

    private var hasHandledRequest = false

    func claimRequest() -> Bool {
        guard !hasHandledRequest else { return false }
        hasHandledRequest = true
        return true
    }
}
#endif

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
    @State private var renameGame: Game?
    @State private var renameText = ""
    @State private var deleteGame: Game?
    @State private var isImporting = false
    @State private var rmsImportGame: Game?
    @State private var isImportingRMS = false
    @State private var rmsExportDocument: RMSBackupDocument?
    @State private var isExportingRMS = false
    @State private var rmsExportFileName = L10n.string("RMS Backup")
    @State private var showSettings = false
    @State private var showProfiles = false
    @State private var searchText = ""
    @State private var showRunningOnly = false
    @State private var errorMessage: String?
    @State private var noticeMessage: String?

    private var jarType: UTType {
        UTType(filenameExtension: "jar") ?? .data
    }

    private var sort: LibrarySort {
        LibrarySort(rawValue: sortRawValue) ?? .name
    }

    private var visibleGames: [Game] {
        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines)

        return library.games
            .filter { game in
                guard !showRunningOnly || session.isRunning(game.id) else {
                    return false
                }
                guard !query.isEmpty else { return true }

                return [
                    game.title,
                    game.vendor,
                    game.version,
                    game.mainClass,
                    game.fileName
                ].contains { value in
                    value.localizedCaseInsensitiveContains(query)
                }
            }
            .sorted { lhs, rhs in
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
                return sortDescending
                    ? result == .orderedDescending
                    : result == .orderedAscending
            }
    }

    private var librarySections: [LibrarySection] {
        var sectionOrder: [String] = []
        var groupedGames: [String: [Game]] = [:]

        for game in visibleGames {
            let title = sectionTitle(for: game)
            if groupedGames[title] == nil {
                sectionOrder.append(title)
            }
            groupedGames[title, default: []].append(game)
        }

        return sectionOrder.map { title in
            LibrarySection(title: title, games: groupedGames[title] ?? [])
        }
    }

    var body: some View {
        libraryChromeContent
        .gamePresentation(item: $activeGame, onDismiss: {
            session.hideCurrent()
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
            PhoneMENavigationStack {
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
            PhoneMENavigationStack {
                SettingsView()
                    .toolbar {
                        ToolbarItem(placement: .confirmationAction) {
                            Button("Done") { showSettings = false }
                        }
                    }
            }
        }
        .sheet(isPresented: $showProfiles) {
            PhoneMENavigationStack {
                ProfilesView()
                    .toolbar {
                        ToolbarItem(placement: .confirmationAction) {
                            Button("Done") { showProfiles = false }
                        }
                    }
            }
        }
        .alert("Rename", isPresented: renameAlertBinding) {
            TextField("App name", text: $renameText)
                .foregroundStyle(.primary)
                .tint(.accentColor)
            Button("Cancel", role: .cancel) {}
            Button("OK") {
                if let renameGame {
                    library.rename(renameGame, to: renameText)
                }
            }
        }
        .confirmationDialog(
            uninstallDialogTitle,
            isPresented: deleteDialogBinding,
            titleVisibility: .visible
        ) {
            Button("Uninstall App & Delete Data", role: .destructive) {
                guard let game = deleteGame else { return }
                deleteGame = nil
                remove(game, dataPolicy: .deleteData)
            }
            Button("Uninstall App Only", role: .destructive) {
                guard let game = deleteGame else { return }
                deleteGame = nil
                remove(game, dataPolicy: .keepData)
            }
            Button("Cancel", role: .cancel) {
                deleteGame = nil
            }
        } message: {
            Text("Keep data to restore saves and settings when the same JAR is imported again, or delete all RMS, files and app settings permanently.")
        }
        .alert("Error", isPresented: errorAlertBinding) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(errorMessage ?? "")
        }
        .alert("RMS", isPresented: noticeAlertBinding) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(noticeMessage ?? "")
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
            // SwiftUI may restart this task whenever the Library tab reappears.
            // Debug autolaunch must remain a one-shot action for this app process.
            guard environment["PHONEME_AUTOLAUNCH"] == "1",
                  activeGame == nil,
                  LibraryDebugAutolaunchGate.shared.claimRequest() else {
                return
            }

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

    private var libraryChromeContent: some View {
        libraryNavigationContent
            .toolbar {
                libraryToolbar
            }
            .jarFileImporter(
                isPresented: $isImporting,
                contentTypes: [jarType],
                onCompletion: importFiles
            )
            .jarFileImporter(
                isPresented: $isImportingRMS,
                contentTypes: [.phoneMERMSBackup],
                onCompletion: importRMSFiles
            )
            .fileExporter(
                isPresented: $isExportingRMS,
                document: rmsExportDocument,
                contentType: .phoneMERMSBackup,
                defaultFilename: rmsExportFileName
            ) { result in
                rmsExportDocument = nil
                if case .failure(let error) = result {
                    errorMessage = error.localizedDescription
                }
            }
    }

    @ViewBuilder
    private var libraryNavigationContent: some View {
#if os(iOS)
        librarySurface
            .navigationTitle("Library")
#else
        librarySurface
            .navigationTitle("phoneME")
#endif
    }

    @ViewBuilder
    private var librarySurface: some View {
        if library.games.isEmpty {
            PhoneMEEmptyStateView(
                title: "No Apps",
                message: "Import a J2ME JAR file to add it to your library.",
                systemImage: "square.stack.3d.up.slash",
                actionTitle: "Import JAR",
                action: { isImporting = true }
            )
        } else {
            List {
                if librarySections.isEmpty {
                    Section {
                        EmptyView()
                    } header: {
                        Text("No Apps")
                            .textCase(nil)
                    } footer: {
                        Text(emptyLibraryMessage)
                    }
                } else {
                    ForEach(librarySections) { section in
                        Section {
                            ForEach(section.games) { game in
                                Button {
                                    open(game)
                                } label: {
                                    GameRow(
                                        game: game,
                                        iconURL: library.iconURL(for: game),
                                        runningState: session.runningApplications[game.id]?.state,
                                        searchText: searchText
                                    )
                                    .contentShape(Rectangle())
                                }
                                .buttonStyle(.plain)
                                .contextMenu {
                                    contextMenu(for: game)
                                }
                            }
                        } header: {
                            Text(section.title)
                        } footer: {
                            if section.id == librarySections.last?.id {
                                Text(libraryCountTitle)
                            }
                        }
                    }
                }
            }
            .listStyle(.insetGrouped)
            .searchable(
                text: $searchText,
                placement: .navigationBarDrawer(displayMode: .automatic),
                prompt: showRunningOnly ? "Search Running Apps" : "Search Apps"
            )
            .refreshable {
                library.reloadFromStorage()
            }
        }
    }

    private var uninstallDialogTitle: String {
        guard let title = deleteGame?.title else {
            return L10n.string("Uninstall this app?")
        }
        return L10n.format("Uninstall %@?", title)
    }

    private var emptyLibraryMessage: String {
        if showRunningOnly {
            return L10n.string("No J2ME apps are currently running.")
        }
        return searchText.isEmpty
            ? L10n.string("No apps match the current filter.")
            : L10n.format("No apps match “%@”.", searchText)
    }

    private var libraryCountTitle: String {
        let count = library.games.count
        return count == 1
            ? L10n.format("%d App", count)
            : L10n.format("%d Apps", count)
    }

    private var sortAccessibilityValue: String {
        let order = sortDescending
            ? L10n.string("descending")
            : L10n.string("ascending")
        return L10n.format("%@, %@", sort.title, order)
    }

    private func sectionTitle(for game: Game) -> String {
        switch sort {
        case .date:
            return String(Calendar.current.component(.year, from: game.importedAt))
        case .vendor:
            return alphabeticalSectionTitle(
                for: game.vendor.isEmpty ? game.title : game.vendor
            )
        case .name:
            return alphabeticalSectionTitle(for: game.title)
        }
    }

    private func alphabeticalSectionTitle(for value: String) -> String {
        let latinValue = value
            .applyingTransform(.toLatin, reverse: false)?
            .folding(
                options: [.caseInsensitive, .diacriticInsensitive, .widthInsensitive],
                locale: .current
            ) ?? value

        guard let first = latinValue.first else { return "#" }
        let title = String(first).uppercased()
        return title.rangeOfCharacter(from: .letters) == nil ? "#" : title
    }

    @ToolbarContentBuilder
    private var libraryToolbar: some ToolbarContent {
        ToolbarItem(placement: .navigationBarLeading) {
            importToolbarButton
        }

        ToolbarItemGroup(placement: .primaryAction) {
            runningFilterButton
            moreToolbarMenu
        }
    }

    private var runningFilterButton: some View {
        Button {
            withAnimation(.easeOut(duration: 0.2)) {
                showRunningOnly.toggle()
            }
        } label: {
            Image(
                systemName: showRunningOnly
                    ? "line.3.horizontal.decrease.circle.fill"
                    : "line.3.horizontal.decrease.circle"
            )
        }
        .accessibilityLabel("Show Running Apps Only")
        .accessibilityValue(
            showRunningOnly ? L10n.string("On") : L10n.string("Off")
        )
    }

    private var importToolbarButton: some View {
        Button {
            isImporting = true
        } label: {
            Image(systemName: "plus")
        }
        .accessibilityLabel("Import JAR")
    }

    private var moreToolbarMenu: some View {
        Menu {
            Picker("Sort by", selection: $sortRawValue) {
                ForEach(LibrarySort.allCases) { item in
                    Label(item.title, systemImage: item.systemImage)
                        .tag(item.rawValue)
                }
            }

            Picker("Order", selection: $sortDescending) {
                Label("Ascending", systemImage: "arrow.up")
                    .tag(false)
                Label("Descending", systemImage: "arrow.down")
                    .tag(true)
            }

            Divider()

            Button {
                showSettings = true
            } label: {
                Label("Settings", systemImage: "gearshape")
            }
            Button {
                showProfiles = true
            } label: {
                Label("Profiles", systemImage: "slider.horizontal.3")
            }
        } label: {
            Image(systemName: "ellipsis.circle")
        }
        .accessibilityLabel("More options")
        .accessibilityValue(sortAccessibilityValue)
    }

    @ViewBuilder
    private func contextMenu(for game: Game) -> some View {
        if session.isRunning(game.id) {
            Button(role: .destructive) {
                session.terminate(gameID: game.id)
            } label: {
                Label("Stop", systemImage: "stop.circle")
            }
        }

        Button {
            exportRMS(for: game)
        } label: {
            Label("Export RMS", systemImage: "square.and.arrow.up")
        }
        .disabled(!session.runningApplications.isEmpty)

        Button {
            rmsImportGame = game
            isImportingRMS = true
        } label: {
            Label("Import RMS", systemImage: "square.and.arrow.down")
        }
        .disabled(!session.runningApplications.isEmpty)

        Divider()

        Button {
            beginRename(game)
        } label: {
            Label("Rename", systemImage: "pencil")
        }
        Button {
            configuringGame = game
        } label: {
            Label("Settings", systemImage: "gearshape")
        }
        Button(role: .destructive) {
            deleteGame = game
        } label: {
            Label("Uninstall", systemImage: "trash")
        }
    }

    private func open(_ game: Game) {
        if session.isRunning(game.id) || profiles.hasProfile(for: game) {
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

    private func importFiles(_ result: Result<[URL], Error>) {
        do {
            let urls = try result.get()
            guard let url = urls.first else { return }
            let game = try library.importJar(from: url)
            applyDefaultProfile(to: game)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func remove(
        _ game: Game,
        dataPolicy: GameRemovalDataPolicy
    ) {
        let jarURL = library.fileURL(for: game)
        session.terminate(gameID: game.id)
        session.uninstallApplication(
            game,
            jarURL: jarURL,
            removeData: dataPolicy == .deleteData
        ) { _ in
            // Removing an app from the library must never depend on reinstalling
            // its JAR into the Core first. Broken legacy entries, missing JARs
            // and invalid manifests still need to remain uninstallable.
            if dataPolicy == .deleteData {
                profiles.removeProfile(for: game)
            }
            library.remove(game, dataPolicy: dataPolicy)
        }
    }

    private func exportRMS(for game: Game) {
        do {
            let jarURL = try library.prepareJarForLaunch(game)
            session.exportRMS(for: game, jarURL: jarURL) { result in
                switch result {
                case .success(let data):
                    rmsExportDocument = RMSBackupDocument(data: data)
                    rmsExportFileName = rmsBackupFileName(for: game)
                    isExportingRMS = true
                case .failure(let error):
                    errorMessage = error.localizedDescription
                }
            }
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func importRMSFiles(_ result: Result<[URL], Error>) {
        guard let game = rmsImportGame else { return }
        rmsImportGame = nil

        do {
            let urls = try result.get()
            guard let sourceURL = urls.first else { return }
            let jarURL = try library.prepareJarForLaunch(game)
            session.importRMS(
                from: sourceURL,
                for: game,
                jarURL: jarURL
            ) { result in
                switch result {
                case .success:
                    noticeMessage = L10n.format(
                        "RMS data was imported for %@.",
                        game.title
                    )
                case .failure(let error):
                    errorMessage = error.localizedDescription
                }
            }
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func rmsBackupFileName(for game: Game) -> String {
        let invalidCharacters = CharacterSet.alphanumerics.inverted
        let components = game.title.components(separatedBy: invalidCharacters)
        let safeTitle = components
            .filter { !$0.isEmpty }
            .joined(separator: "-")
        let baseName = safeTitle.isEmpty ? L10n.string("Game") : safeTitle
        return String(baseName.prefix(80)) + "-RMS"
    }

    private func applyDefaultProfile(to game: Game) {
        guard let defaultID = profileTemplates.defaultTemplateID,
              let template = profileTemplates.templates.first(where: { $0.id == defaultID }) else {
            return
        }
        profiles.save(template.profile, for: game)
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

    private var noticeAlertBinding: Binding<Bool> {
        Binding(
            get: { noticeMessage != nil },
            set: { if !$0 { noticeMessage = nil } }
        )
    }
}

private struct GameRow: View {
    let game: Game
    let iconURL: URL?
    let runningState: RunningJ2MEApplication.State?
    let searchText: String

    var body: some View {
        HStack(spacing: 12) {
            GameIconView(iconURL: iconURL)
                .frame(width: 32, height: 32)

            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 4) {
                    Text(highlighted(game.title))
                        .font(.headline)
                        .lineLimit(1)

                    statusIndicator
                }

                Text(highlighted(identifierTitle))
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }

            Spacer(minLength: 8)

            if !game.version.isEmpty {
                Text(game.version)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        }
        .padding(.vertical, 2)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(game.title)
        .accessibilityValue(accessibilityDetails)
        .accessibilityHint("Opens the app")
    }

    private var identifierTitle: String {
        if !game.vendor.isEmpty {
            return game.vendor
        }
        return game.fileName
    }

    @ViewBuilder
    private var statusIndicator: some View {
        switch runningState {
        case .background, .foreground:
            Image(systemName: "play.circle.fill")
                .font(.subheadline)
                .foregroundStyle(.green)
                .accessibilityHidden(true)
        case .paused:
            Image(systemName: "pause.circle.fill")
                .font(.subheadline)
                .foregroundStyle(.orange)
                .accessibilityHidden(true)
        case .starting:
            ProgressView()
                .controlSize(.small)
                .accessibilityLabel("Starting")
        case .failed:
            Image(systemName: "exclamationmark.triangle.fill")
                .font(.subheadline)
                .foregroundStyle(.red)
                .accessibilityHidden(true)
        case nil:
            EmptyView()
        }
    }

    private func highlighted(_ value: String) -> AttributedString {
        var attributed = AttributedString(value)
        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !query.isEmpty,
              let range = attributed.range(
                  of: query,
                  options: [.caseInsensitive, .diacriticInsensitive]
              ) else {
            return attributed
        }
        attributed[range].foregroundColor = .accentColor
        return attributed
    }

    private var accessibilityDetails: String {
        var details = [game.vendor, game.version]
            .filter { !$0.isEmpty }

        switch runningState {
        case .background:
            details.append(L10n.string("Running in background"))
        case .paused:
            details.append(L10n.string("Paused in background"))
        case .foreground:
            details.append(L10n.string("Currently open"))
        case .starting:
            details.append(L10n.string("Starting"))
        case .failed:
            details.append(L10n.string("Stopped with an error"))
        case nil:
            break
        }

        return details.joined(separator: ", ")
    }
}

private struct GameIconView: View {
    let iconURL: URL?

    var body: some View {
        Group {
            if let image = platformImage {
                image
                    .resizable()
                    .interpolation(.none)
                    .scaledToFit()
            } else {
                Image(systemName: "gamecontroller.fill")
                    .font(.system(size: 15, weight: .medium))
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background(Color.phoneMEIconBackground)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
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

private extension UTType {
    static let phoneMERMSBackup = UTType(
        exportedAs: "dev.phoneme.rms-backup",
        conformingTo: .data
    )
}

private struct RMSBackupDocument: FileDocument {
    static var readableContentTypes: [UTType] {
        [.phoneMERMSBackup]
    }

    let data: Data

    init(data: Data) {
        self.data = data
    }

    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else {
            throw PhoneMERMSBackupError.invalidBackup
        }
        self.data = data
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
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
