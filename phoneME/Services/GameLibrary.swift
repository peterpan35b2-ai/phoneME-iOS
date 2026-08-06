import Foundation

enum GameRemovalDataPolicy {
    case keepData
    case deleteData
}

private struct RetainedGameData: Codable {
    let id: UUID
    let title: String
    let vendor: String
    let version: String
    let mainClass: String
    let deletedAt: Date

    init(game: Game) {
        id = game.id
        title = game.title
        vendor = game.vendor
        version = game.version
        mainClass = game.mainClass
        deletedAt = Date()
    }
}

@MainActor
final class GameLibrary: ObservableObject {
    @Published private(set) var games: [Game] = []

    private let fileManager: FileManager
    private let storage: PhoneMEStorageController
    private var retainedData: [RetainedGameData] = []

    private var rootURL: URL { storage.rootURL }
    private var gamesURL: URL {
        rootURL.appendingPathComponent("Games", isDirectory: true)
    }
    private var iconsURL: URL {
        rootURL.appendingPathComponent("Icons", isDirectory: true)
    }
    private var metadataURL: URL {
        rootURL.appendingPathComponent("library.json", isDirectory: false)
    }
    private var retainedDataURL: URL {
        rootURL.appendingPathComponent("retained-data.json", isDirectory: false)
    }

    init(
        storage: PhoneMEStorageController,
        fileManager: FileManager = .default
    ) {
        self.storage = storage
        self.fileManager = fileManager
        reloadFromStorage()
    }

    func reloadFromStorage() {
        do {
            try prepareDirectories()
            try loadRetainedData()
            try load()
        } catch {
            games = []
            retainedData = []
        }
    }

    func fileURL(for game: Game) -> URL {
        gamesURL.appendingPathComponent(game.fileName, isDirectory: false)
    }

    func iconURL(for game: Game) -> URL? {
        guard let iconFileName = game.iconFileName else { return nil }
        let url = iconsURL.appendingPathComponent(iconFileName, isDirectory: false)
        return fileManager.fileExists(atPath: url.path) ? url : nil
    }

    /// Returns the original imported JAR unchanged. Class-file parsing,
    /// StackMap handling and verification belong exclusively to the C++ core.
    func prepareJarForLaunch(_ game: Game) throws -> URL {
        let sourceURL = fileURL(for: game)
        guard fileManager.fileExists(atPath: sourceURL.path) else {
            throw LibraryError.missingGameFile
        }
        return sourceURL
    }

    @discardableResult
    func importJar(from sourceURL: URL) throws -> Game {
        let hasSecurityScope = sourceURL.startAccessingSecurityScopedResource()
        defer {
            if hasSecurityScope {
                sourceURL.stopAccessingSecurityScopedResource()
            }
        }

        guard sourceURL.pathExtension.lowercased() == "jar" else {
            throw LibraryError.unsupportedFile
        }

        let safeName = sourceURL
            .deletingPathExtension()
            .lastPathComponent
            .replacingOccurrences(of: "/", with: "-")
        let metadata = try JarMetadataReader.read(from: sourceURL)
        let importedTitle = uniqueImportedTitle(for: metadata.title ?? safeName)
        let retainedIndex = retainedData.firstIndex {
            matchesRetainedData($0, metadata: metadata, fallbackTitle: safeName)
        }
        let id = retainedIndex.map { retainedData[$0].id } ?? UUID()
        let storedName = "\(id.uuidString)-\(safeName).jar"
        let destinationURL = gamesURL.appendingPathComponent(storedName)
        let iconFileName = try storeIcon(from: metadata, gameID: id)

        do {
            try fileManager.copyItem(at: sourceURL, to: destinationURL)
        } catch {
            if let iconFileName {
                try? fileManager.removeItem(at: iconsURL.appendingPathComponent(iconFileName))
            }
            throw error
        }

        let game = Game(
            id: id,
            title: importedTitle,
            vendor: metadata.vendor ?? "",
            version: metadata.version ?? "",
            mainClass: metadata.mainClass ?? "",
            fileName: storedName,
            iconFileName: iconFileName
        )
        games.append(game)
        sortGames()
        let previousRetainedData = retainedData
        if let retainedIndex {
            retainedData.remove(at: retainedIndex)
        }

        do {
            try save()
            try saveRetainedData()
        } catch {
            games.removeAll { $0.id == game.id }
            retainedData = previousRetainedData
            try? fileManager.removeItem(at: destinationURL)
            removeIcon(for: game)
            throw error
        }

        return game
    }

    func reinstall(_ game: Game, from sourceURL: URL) throws {
        let hasSecurityScope = sourceURL.startAccessingSecurityScopedResource()
        defer {
            if hasSecurityScope {
                sourceURL.stopAccessingSecurityScopedResource()
            }
        }

        guard sourceURL.pathExtension.lowercased() == "jar" else {
            throw LibraryError.unsupportedFile
        }

        let metadata = try JarMetadataReader.read(from: sourceURL)
        let destinationURL = fileURL(for: game)
        let replacementURL = destinationURL.deletingLastPathComponent()
            .appendingPathComponent("replacement-\(UUID().uuidString).jar")
        try fileManager.copyItem(at: sourceURL, to: replacementURL)

        do {
            _ = try fileManager.replaceItemAt(destinationURL, withItemAt: replacementURL)
        } catch {
            try? fileManager.removeItem(at: replacementURL)
            throw error
        }

        guard let index = games.firstIndex(where: { $0.id == game.id }) else { return }
        let previousIcon = games[index].iconFileName
        let replacementIcon = try storeIcon(from: metadata, gameID: game.id)
        games[index].vendor = metadata.vendor ?? ""
        games[index].version = metadata.version ?? ""
        games[index].mainClass = metadata.mainClass ?? ""
        games[index].iconFileName = replacementIcon
        if let previousIcon, previousIcon != replacementIcon {
            try? fileManager.removeItem(at: iconsURL.appendingPathComponent(previousIcon))
        }
        try save()
    }

    func saveLog() throws {
        let lines = [
            "phoneME",
            "Games: \(games.count)",
            "Date: \(ISO8601DateFormatter().string(from: Date()))"
        ]
        let logURL = rootURL.appendingPathComponent("log.txt")
        try lines.joined(separator: "\n").write(to: logURL, atomically: true, encoding: .utf8)
    }

    func removeGames(at offsets: IndexSet) {
        let removedGames = offsets.compactMap { index in
            games.indices.contains(index) ? games[index] : nil
        }

        games.remove(atOffsets: offsets)
        for game in removedGames {
            let jarURL = fileURL(for: game)
            try? fileManager.removeItem(at: jarURL)
            removeIcon(for: game)
            retainedData.removeAll { $0.id == game.id }
            PhoneMERuntimeResources.removeStorage(for: game.id)
        }
        try? save()
        try? saveRetainedData()
    }

    func remove(
        _ game: Game,
        dataPolicy: GameRemovalDataPolicy
    ) {
        guard let index = games.firstIndex(where: { $0.id == game.id }) else {
            return
        }

        games.remove(at: index)
        let jarURL = fileURL(for: game)
        try? fileManager.removeItem(at: jarURL)
        removeIcon(for: game)

        retainedData.removeAll { $0.id == game.id }
        if dataPolicy == .keepData {
            retainedData.append(RetainedGameData(game: game))
        } else {
            PhoneMERuntimeResources.removeStorage(for: game.id)
        }

        try? save()
        try? saveRetainedData()
    }

    func rename(_ game: Game, to title: String) {
        let trimmed = title.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty,
              let index = games.firstIndex(where: { $0.id == game.id }) else { return }
        games[index].title = trimmed
        try? save()
    }

    func markPlayed(_ game: Game) {
        guard let index = games.firstIndex(where: { $0.id == game.id }) else {
            return
        }

        games[index].lastPlayedAt = Date()
        games[index].playCount += 1
        sortGames()
        try? save()
    }

    private func sortGames() {
        games.sort {
            let lhs = $0.lastPlayedAt ?? $0.importedAt
            let rhs = $1.lastPlayedAt ?? $1.importedAt
            return lhs > rhs
        }
    }

    private func load() throws {
        guard fileManager.fileExists(atPath: metadataURL.path) else {
            games = []
            return
        }

        let data = try Data(contentsOf: metadataURL)
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        games = try decoder.decode([Game].self, from: data).filter {
            fileManager.fileExists(atPath: fileURL(for: $0).path)
        }
        try refreshMetadataIfNeeded()
        sortGames()
    }

    private func refreshMetadataIfNeeded() throws {
        var changed = false

        for index in games.indices {
            let needsMetadata = games[index].vendor.isEmpty
                || games[index].version.isEmpty
                || games[index].mainClass.isEmpty
                || games[index].iconFileName == nil
            guard needsMetadata,
                  let metadata = try? JarMetadataReader.read(from: fileURL(for: games[index])) else {
                continue
            }

            if games[index].vendor.isEmpty, let vendor = metadata.vendor {
                games[index].vendor = vendor
                changed = true
            }
            if games[index].version.isEmpty, let version = metadata.version {
                games[index].version = version
                changed = true
            }
            if games[index].mainClass.isEmpty, let mainClass = metadata.mainClass {
                games[index].mainClass = mainClass
                changed = true
            }
            if games[index].iconFileName == nil,
               let iconFileName = try storeIcon(from: metadata, gameID: games[index].id) {
                games[index].iconFileName = iconFileName
                changed = true
            }
        }

        if changed {
            try save()
        }
    }

    private func storeIcon(from metadata: JarMetadata?, gameID: UUID) throws -> String? {
        guard let data = metadata?.iconData, !data.isEmpty else { return nil }
        let rawExtension = metadata?.iconExtension?.lowercased() ?? "png"
        let allowedExtensions = CharacterSet.alphanumerics
        let fileExtension = rawExtension.unicodeScalars.allSatisfy(allowedExtensions.contains)
            ? rawExtension
            : "png"
        let fileName = "\(gameID.uuidString).\(fileExtension.isEmpty ? "png" : fileExtension)"
        try data.write(to: iconsURL.appendingPathComponent(fileName), options: .atomic)
        return fileName
    }

    private func removeIcon(for game: Game) {
        guard let iconFileName = game.iconFileName else { return }
        try? fileManager.removeItem(at: iconsURL.appendingPathComponent(iconFileName))
    }

    private func prepareDirectories() throws {
        try fileManager.createDirectory(
            at: gamesURL,
            withIntermediateDirectories: true
        )
        try fileManager.createDirectory(
            at: iconsURL,
            withIntermediateDirectories: true
        )
    }

    private func loadRetainedData() throws {
        guard fileManager.fileExists(atPath: retainedDataURL.path) else {
            retainedData = []
            return
        }
        let data = try Data(contentsOf: retainedDataURL)
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        retainedData = try decoder.decode([RetainedGameData].self, from: data)
    }

    private func saveRetainedData() throws {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(retainedData)
        try data.write(to: retainedDataURL, options: .atomic)
    }

    private func matchesRetainedData(
        _ retained: RetainedGameData,
        metadata: JarMetadata?,
        fallbackTitle: String
    ) -> Bool {
        let retainedMainClass = normalizedIdentity(retained.mainClass)
        let currentMainClass = normalizedIdentity(metadata?.mainClass ?? "")
        let retainedVendor = normalizedIdentity(retained.vendor)
        let currentVendor = normalizedIdentity(metadata?.vendor ?? "")

        if !retainedMainClass.isEmpty,
           retainedMainClass == currentMainClass,
           (retainedVendor.isEmpty || currentVendor.isEmpty
               || retainedVendor == currentVendor) {
            return true
        }

        let retainedTitle = normalizedIdentity(retained.title)
        let currentTitle = normalizedIdentity(metadata?.title ?? fallbackTitle)
        return !retainedTitle.isEmpty
            && retainedTitle == currentTitle
            && (retainedVendor.isEmpty || currentVendor.isEmpty
                || retainedVendor == currentVendor)
    }

    /// Assigns a unique display title only to newly imported apps. Existing
    /// library entries are intentionally left untouched, including old
    /// duplicates created before this behavior was introduced.
    private func uniqueImportedTitle(for proposedTitle: String) -> String {
        let trimmedTitle = proposedTitle.trimmingCharacters(in: .whitespacesAndNewlines)
        let baseTitle = trimmedTitle.isEmpty ? proposedTitle : trimmedTitle
        let existingTitles = Set(games.map { normalizedIdentity($0.title) })

        guard existingTitles.contains(normalizedIdentity(baseTitle)) else {
            return baseTitle
        }

        var suffix = 1
        while existingTitles.contains(normalizedIdentity("\(baseTitle) (\(suffix))")) {
            suffix += 1
        }
        return "\(baseTitle) (\(suffix))"
    }

    private func normalizedIdentity(_ value: String) -> String {
        value.trimmingCharacters(in: .whitespacesAndNewlines)
            .folding(
                options: [.caseInsensitive, .diacriticInsensitive],
                locale: Locale(identifier: "en_US_POSIX")
            )
    }

    private func save() throws {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(games)
        try data.write(to: metadataURL, options: .atomic)
    }
}

enum LibraryError: LocalizedError {
    case unsupportedFile
    case missingGameFile

    var errorDescription: String? {
        switch self {
        case .unsupportedFile:
            return L10n.string("Unsupported file.")
        case .missingGameFile:
            return L10n.string("The game JAR is missing from the library.")
        }
    }
}
