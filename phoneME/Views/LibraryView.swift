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
    @State private var renameGame: Game?
    @State private var renameText = ""
    @State private var deleteGame: Game?
    @State private var isImporting = false
    @State private var rmsImportGame: Game?
    @State private var isImportingRMS = false
    @State private var rmsExportDocument: RMSBackupDocument?
    @State private var isExportingRMS = false
    @State private var rmsExportFileName = "RMS Backup"
    @State private var showSettings = false
    @State private var showProfiles = false
    @State private var errorMessage: String?
    @State private var noticeMessage: String?

    private var jarType: UTType {
        UTType(filenameExtension: "jar") ?? .data
    }

    private var sort: LibrarySort {
        LibrarySort(rawValue: sortRawValue) ?? .name
    }

    private var visibleGames: [Game] {
        library.games.sorted { lhs, rhs in
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
            PhoneMENavigationStack { SettingsView() }
        }
        .sheet(isPresented: $showProfiles) {
            PhoneMENavigationStack { ProfilesView() }
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
            "Delete \(deleteGame?.title ?? "this app")?",
            isPresented: deleteDialogBinding,
            titleVisibility: .visible
        ) {
            Button("Delete App & Data", role: .destructive) {
                guard let game = deleteGame else { return }
                deleteGame = nil
                remove(game, dataPolicy: .deleteData)
            }
            Button("Delete App Only", role: .destructive) {
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
            .navigationTitle("")
            .navigationBarTitleDisplayMode(.inline)
#else
        librarySurface
            .navigationTitle("J2ME Loader")
#endif
    }

    private var librarySurface: some View {
        ZStack {
            Color.phoneMEAppBackground
                .ignoresSafeArea()

            VStack(spacing: 0) {
                Group {
                    if library.games.isEmpty {
                        PhoneMEEmptyStateView(
                            title: "No apps yet",
                            message: "Import a J2ME JAR file to add it to your library.",
                            systemImage: "square.stack.3d.up.slash",
                            actionTitle: "Import JAR",
                            action: { isImporting = true }
                        )
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                    } else {
                        ScrollView {
                            LazyVStack(spacing: 10) {
                                libraryListHeader

                                ForEach(visibleGames) { game in
                                    Button {
                                        open(game)
                                    } label: {
                                        GameRow(
                                            game: game,
                                            iconURL: library.iconURL(for: game),
                                            runningState: session.runningApplications[game.id]?.state,
                                            resourceUsage: session.backgroundRuntimeUsage,
                                            applicationMemoryBytes: session.backgroundApplicationMemoryUsage[game.id]
                                        )
                                        .contentShape(Rectangle())
                                    }
                                    .buttonStyle(PhoneMEPressedButtonStyle())
                                    .contextMenu {
                                        contextMenu(for: game)
                                    }
                                }
                            }
                            .frame(maxWidth: PhoneMEVisualMetrics.contentMaxWidth)
                            .padding(.horizontal, PhoneMEVisualMetrics.horizontalInset)
                            .padding(.top, 12)
                            .padding(.bottom, 24)
                            .frame(maxWidth: .infinity)
                        }
                        .phoneMEScrollDismissesKeyboardInteractively()
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }

    private var libraryListHeader: some View {
        HStack(spacing: 12) {
            Text(libraryCountTitle)
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(.secondary)

            Spacer(minLength: 12)

            Menu {
                Picker("Sort by", selection: $sortRawValue) {
                    ForEach(LibrarySort.allCases) { item in
                        Text(item.title)
                            .tag(item.rawValue)
                    }
                }

                Picker("Order", selection: $sortDescending) {
                    Label("Ascending", systemImage: "arrow.up")
                        .tag(false)
                    Label("Descending", systemImage: "arrow.down")
                        .tag(true)
                }
            } label: {
                HStack(spacing: 6) {
                    Text(sort.title)
                    Image(systemName: sortDescending ? "arrow.down" : "arrow.up")
                }
                .font(.subheadline.weight(.semibold))
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            .accessibilityLabel("Sort apps")
            .accessibilityValue(sortAccessibilityValue)
        }
        .padding(.horizontal, 2)
        .padding(.bottom, 2)
    }

    private var libraryCountTitle: String {
        let count = library.games.count
        return "\(count) \(count == 1 ? "App" : "Apps")"
    }

    private var sortAccessibilityValue: String {
        "\(sort.title), \(sortDescending ? "descending" : "ascending")"
    }

    @ToolbarContentBuilder
    private var libraryToolbar: some ToolbarContent {
#if os(iOS)
        ToolbarItem(placement: .navigationBarLeading) {
            importToolbarButton
        }

        ToolbarItemGroup(placement: .primaryAction) {
            moreToolbarMenu
        }
#else
        ToolbarItemGroup(placement: .primaryAction) {
            importToolbarButton
            moreToolbarMenu
        }
#endif
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
            Image(systemName: "ellipsis")
        }
        .accessibilityLabel("More")
    }

    @ViewBuilder
    private func contextMenu(for game: Game) -> some View {
        if session.isRunning(game.id) {
            Button("Stop", role: .destructive) {
                session.terminate(gameID: game.id)
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

        Button("Rename") { beginRename(game) }
        Button("Settings") { configuringGame = game }
        Button("Delete", role: .destructive) { deleteGame = game }
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
        switch dataPolicy {
        case .keepData:
            session.terminate(gameID: game.id)
            library.remove(game, dataPolicy: .keepData)

        case .deleteData:
            do {
                let jarURL = try library.prepareJarForLaunch(game)
                session.terminate(gameID: game.id)
                session.deleteStoredData(
                    for: game,
                    jarURL: jarURL
                ) { result in
                    switch result {
                    case .success:
                        profiles.removeProfile(for: game)
                        library.remove(game, dataPolicy: .deleteData)
                    case .failure(let error):
                        errorMessage = error.localizedDescription
                    }
                }
            } catch {
                errorMessage = error.localizedDescription
            }
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
                    noticeMessage = "RMS data was imported for \(game.title)."
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
        let baseName = safeTitle.isEmpty ? "Game" : safeTitle
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
    let resourceUsage: J2MERuntimeResourceUsage?
    let applicationMemoryBytes: UInt64?

    var body: some View {
        VStack(spacing: 8) {
            HStack(spacing: 14) {
                GameIconView(iconURL: iconURL)
                    .frame(width: 56, height: 56)

                VStack(alignment: .leading, spacing: 6) {
                    HStack(alignment: .firstTextBaseline, spacing: 8) {
                        Text(game.title)
                            .font(.headline)
                            .foregroundStyle(.primary)
                            .lineLimit(1)

                        if !game.version.isEmpty {
                            Text(game.version)
                                .font(.caption2.weight(.semibold))
                                .foregroundStyle(.secondary)
                                .lineLimit(1)
                                .padding(.horizontal, 7)
                                .padding(.vertical, 3)
                                .background(Color.phoneMEControlBackground, in: Capsule())
                        }
                    }

                    Text(game.vendor.isEmpty ? "Unknown vendor" : game.vendor)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
                .frame(maxWidth: .infinity, alignment: .leading)

                Image(systemName: "chevron.right")
                    .font(.caption.weight(.bold))
                    .foregroundStyle(.tertiary)
                    .accessibilityHidden(true)
            }
            .frame(height: 56)

            HStack(spacing: 8) {
                HStack(spacing: 6) {
                    Circle()
                        .fill(statusColor)
                        .frame(width: 7, height: 7)
                    Text(statusTitle ?? "Running")
                        .lineLimit(1)
                        .minimumScaleFactor(0.72)
                }
                .font(.caption.weight(.medium))
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
                .opacity(runningState == nil ? 0 : 1)

                usageBadge(
                    resourceUsage.map {
                        "CPU \(formattedCPU($0.cpuPercent))"
                    } ?? "CPU —",
                    systemImage: "cpu",
                    width: 82
                )
                .opacity(showsResourceUsage ? 1 : 0)

                usageBadge(
                    applicationMemoryBytes.map {
                        "RAM \(formattedMemory($0))"
                    } ?? "RAM…",
                    systemImage: "memorychip",
                    width: 96
                )
                .opacity(showsResourceUsage ? 1 : 0)
            }
            .frame(height: 22)
        }
        .padding(14)
        .frame(
            maxWidth: .infinity,
            minHeight: 112,
            maxHeight: 112,
            alignment: .leading
        )
        .background(Color.phoneMECardBackground)
        .clipShape(
            RoundedRectangle(
                cornerRadius: PhoneMEVisualMetrics.cardCornerRadius,
                style: .continuous
            )
        )
        .overlay {
            RoundedRectangle(
                cornerRadius: PhoneMEVisualMetrics.cardCornerRadius,
                style: .continuous
            )
            .stroke(Color.phoneMEHairline, lineWidth: 0.5)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(game.title)
        .accessibilityValue(accessibilityDetails)
    }

    private func usageBadge(
        _ title: String,
        systemImage: String,
        width: CGFloat
    ) -> some View {
        Label(title, systemImage: systemImage)
            .font(.caption2.monospacedDigit().weight(.medium))
            .lineLimit(1)
            .minimumScaleFactor(0.7)
            .allowsTightening(true)
            .frame(width: width, height: 22)
            .background(Color.phoneMEControlBackground, in: Capsule())
    }

    private var statusTitle: String? {
        switch runningState {
        case .background: return "Running"
        case .paused: return "Paused"
        case .foreground: return "Open"
        case .starting: return "Starting"
        case .failed: return "Stopped"
        case nil: return nil
        }
    }

    private var statusColor: Color {
        switch runningState {
        case .background, .foreground: return .green
        case .paused, .starting: return .orange
        case .failed: return .red
        case nil: return .secondary
        }
    }

    private var accessibilityDetails: String {
        var details = [game.vendor, game.version]
            .filter { !$0.isEmpty }

        switch runningState {
        case .background:
            details.append("Running in background")
        case .paused:
            details.append("Paused in background")
        case .foreground:
            details.append("Currently open")
        case .starting:
            details.append("Starting")
        case .failed:
            details.append("Stopped with an error")
        case nil:
            break
        }

        if showsResourceUsage, let resourceUsage {
            details.append(
                "Shared J2ME runtime CPU \(formattedCPU(resourceUsage.cpuPercent))"
            )
            if let applicationMemoryBytes {
                details.append(
                    "J2ME heap RAM \(formattedMemory(applicationMemoryBytes))"
                )
            } else {
                details.append("J2ME heap RAM is being measured")
            }
        }

        return details.joined(separator: ", ")
    }

    private var showsResourceUsage: Bool {
        guard resourceUsage != nil else { return false }
        return runningState == .background || runningState == .paused
    }

    private func formattedCPU(_ value: Double) -> String {
        value.formatted(
            .number
                .precision(.fractionLength(1))
                .locale(Locale(identifier: "en_US_POSIX"))
        ) + "%"
    }

    private func formattedMemory(_ bytes: UInt64) -> String {
        ByteCountFormatter.string(
            fromByteCount: Int64(min(bytes, UInt64(Int64.max))),
            countStyle: .memory
        )
    }
}

private struct GameIconView: View {
    let iconURL: URL?

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 11, style: .continuous)
                .fill(Color.phoneMEIconBackground)

            if let image = platformImage {
                image
                    .resizable()
                    .interpolation(.none)
                    .scaledToFit()
            } else {
                Image(systemName: "gamecontroller.fill")
                    .font(.system(size: 19, weight: .medium))
                    .foregroundStyle(.secondary)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 11, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 11, style: .continuous)
                .stroke(.primary.opacity(0.08), lineWidth: 0.5)
        }
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
