import Foundation

struct PhoneMERuntimeLayout {
    let homeURL: URL
}

private struct PhoneMERMSBackup: Codable {
    let formatVersion: Int
    let exportedAt: Date
    let midletName: String
    let vendor: String
    let mainClass: String
    let stores: [String: Data]
}

enum PhoneMERMSBackupError: LocalizedError {
    case noData
    case invalidBackup
    case incompatibleGame
    case runtimeBusy
    case backupTooLarge

    var errorDescription: String? {
        switch self {
        case .noData:
            return L10n.string("This game does not have any RMS data yet.")
        case .invalidBackup:
            return L10n.string(
                "The selected file is not a valid phoneME RMS backup."
            )
        case .incompatibleGame:
            return L10n.string("This RMS backup belongs to a different game.")
        case .runtimeBusy:
            return L10n.string(
                "Stop all running games before importing or exporting RMS data."
            )
        case .backupTooLarge:
            return L10n.string("The RMS backup is too large.")
        }
    }
}

enum PhoneMERuntimeResources {
    private final class LayoutCache: @unchecked Sendable {
        let lock = NSLock()
        var layout: PhoneMERuntimeLayout?
        var storageRootURL: URL?
    }

    private static let cache = LayoutCache()

    static func configure(storageRootURL: URL) {
        cache.lock.lock()
        defer { cache.lock.unlock() }
        let normalized = storageRootURL.standardizedFileURL
        guard cache.storageRootURL?.standardizedFileURL != normalized else {
            return
        }
        cache.storageRootURL = normalized
        cache.layout = nil
    }

    static func prepare() throws -> PhoneMERuntimeLayout {
        cache.lock.lock()
        if let layout = cache.layout {
            cache.lock.unlock()
            return layout
        }
        cache.lock.unlock()

        let preparedLayout = try prepareUncached()

        cache.lock.lock()
        defer { cache.lock.unlock() }
        if let layout = cache.layout {
            return layout
        }
        cache.layout = preparedLayout
        return preparedLayout
    }

    static func prepare(for gameID: UUID) throws -> PhoneMERuntimeLayout {
        let sharedLayout = try prepare()
        let gameHome = gameRuntimeHome(
            for: gameID,
            sharedRuntimeHome: sharedLayout.homeURL
        )
        let fileManager = FileManager.default
        try fileManager.createDirectory(
            at: gameHome,
            withIntermediateDirectories: true
        )
        try createRuntimeDirectories(at: gameHome, fileManager: fileManager)
        excludeFromBackup(gameHome)
        return PhoneMERuntimeLayout(homeURL: gameHome)
    }

    static func removeStorage(for gameID: UUID) {
        guard let sharedLayout = try? prepare() else { return }
        let storageURL = gameRuntimeHome(
            for: gameID,
            sharedRuntimeHome: sharedLayout.homeURL
        )
        try? FileManager.default.removeItem(at: storageURL)
    }

    static func exportRMSBackup(
        suiteID: Int32,
        jarURL: URL
    ) throws -> Data {
        let fileManager = FileManager.default
        let rmsURL = try rmsDirectoryURL(for: suiteID)
        guard fileManager.fileExists(atPath: rmsURL.path) else {
            throw PhoneMERMSBackupError.noData
        }
        let urls = try fileManager.contentsOfDirectory(
            at: rmsURL,
            includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey],
            options: [.skipsHiddenFiles]
        )

        var stores: [String: Data] = [:]
        var totalSize = 0
        for url in urls where url.pathExtension.lowercased() == "rms" {
            let values = try url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey])
            guard values.isRegularFile == true else { continue }
            let fileSize = values.fileSize ?? 0
            guard fileSize >= 0,
                  totalSize <= maximumRMSBackupBytes - fileSize else {
                throw PhoneMERMSBackupError.backupTooLarge
            }
            totalSize += fileSize
            stores[url.lastPathComponent] = try Data(
                contentsOf: url,
                options: [.mappedIfSafe]
            )
        }

        guard !stores.isEmpty else {
            throw PhoneMERMSBackupError.noData
        }

        let metadata = try? JarMetadataReader.read(from: jarURL)
        let backup = PhoneMERMSBackup(
            formatVersion: rmsBackupFormatVersion,
            exportedAt: Date(),
            midletName: metadata?.title ?? "",
            vendor: metadata?.vendor ?? "",
            mainClass: metadata?.mainClass ?? "",
            stores: stores
        )
        let encoder = PropertyListEncoder()
        encoder.outputFormat = .binary
        let encoded = try encoder.encode(backup)
        guard encoded.count <= maximumRMSBackupBytes else {
            throw PhoneMERMSBackupError.backupTooLarge
        }
        return encoded
    }

    static func importRMSBackup(
        from sourceURL: URL,
        suiteID: Int32,
        jarURL: URL
    ) throws {
        let hasSecurityScope = sourceURL.startAccessingSecurityScopedResource()
        defer {
            if hasSecurityScope {
                sourceURL.stopAccessingSecurityScopedResource()
            }
        }

        let fileManager = FileManager.default
        let sourceValues = try sourceURL.resourceValues(forKeys: [.fileSizeKey])
        if let fileSize = sourceValues.fileSize,
           fileSize > maximumRMSBackupBytes {
            throw PhoneMERMSBackupError.backupTooLarge
        }

        let data = try Data(contentsOf: sourceURL, options: [.mappedIfSafe])
        guard data.count <= maximumRMSBackupBytes,
              let backup = try? PropertyListDecoder().decode(
                  PhoneMERMSBackup.self,
                  from: data
              ),
              backup.formatVersion == rmsBackupFormatVersion,
              !backup.stores.isEmpty,
              backup.stores.count <= maximumRMSStoreCount else {
            throw PhoneMERMSBackupError.invalidBackup
        }

        try validateBackup(backup, for: jarURL)
        try validateStoreFiles(backup.stores)

        let rmsURL = try rmsDirectoryURL(for: suiteID)
        let parentURL = rmsURL.deletingLastPathComponent()
        try fileManager.createDirectory(
            at: parentURL,
            withIntermediateDirectories: true
        )

        let transactionID = UUID().uuidString
        let stagedURL = parentURL.appendingPathComponent(
            ".rms-import-\(suiteID)-\(transactionID)",
            isDirectory: true
        )
        let previousURL = parentURL.appendingPathComponent(
            ".rms-previous-\(suiteID)-\(transactionID)",
            isDirectory: true
        )
        try fileManager.createDirectory(
            at: stagedURL,
            withIntermediateDirectories: true
        )

        do {
            for (name, storeData) in backup.stores {
                try storeData.write(
                    to: stagedURL.appendingPathComponent(name),
                    options: .atomic
                )
            }

            let hadPreviousData = fileManager.fileExists(atPath: rmsURL.path)
            if hadPreviousData {
                try fileManager.moveItem(at: rmsURL, to: previousURL)
            }

            do {
                try fileManager.moveItem(at: stagedURL, to: rmsURL)
                if hadPreviousData {
                    try? fileManager.removeItem(at: previousURL)
                }
            } catch {
                if fileManager.fileExists(atPath: rmsURL.path) {
                    try? fileManager.removeItem(at: rmsURL)
                }
                if hadPreviousData,
                   fileManager.fileExists(atPath: previousURL.path) {
                    try? fileManager.moveItem(at: previousURL, to: rmsURL)
                }
                throw error
            }
        } catch {
            try? fileManager.removeItem(at: stagedURL)
            // Keep the previous directory if rollback itself failed. Losing a
            // stale transaction folder is preferable to deleting the user's
            // last recoverable copy of the RMS data.
            throw error
        }
    }

    private static let rmsBackupFormatVersion = 1
    private static let maximumRMSStoreCount = 4_096
    private static let maximumRMSBackupBytes = 128 * 1024 * 1024

    private static func rmsDirectoryURL(for suiteID: Int32) throws -> URL {
        guard suiteID > 0 else {
            throw PhoneMERMSBackupError.invalidBackup
        }
        return try prepare().homeURL
            .appendingPathComponent("rms", isDirectory: true)
            .appendingPathComponent(String(suiteID), isDirectory: true)
    }

    private static func validateBackup(
        _ backup: PhoneMERMSBackup,
        for jarURL: URL
    ) throws {
        guard let metadata = try? JarMetadataReader.read(from: jarURL) else {
            return
        }

        let backupVendor = normalizedIdentity(backup.vendor)
        let currentVendor = normalizedIdentity(metadata.vendor ?? "")
        if !backupVendor.isEmpty,
           !currentVendor.isEmpty,
           backupVendor != currentVendor {
            throw PhoneMERMSBackupError.incompatibleGame
        }

        let backupMainClass = normalizedIdentity(backup.mainClass)
        let currentMainClass = normalizedIdentity(metadata.mainClass ?? "")
        if !backupMainClass.isEmpty,
           !currentMainClass.isEmpty,
           backupMainClass != currentMainClass {
            throw PhoneMERMSBackupError.incompatibleGame
        }

        let backupName = normalizedIdentity(backup.midletName)
        let currentName = normalizedIdentity(metadata.title ?? "")
        if backupVendor.isEmpty,
           currentVendor.isEmpty,
           !backupName.isEmpty,
           !currentName.isEmpty,
           backupName != currentName {
            throw PhoneMERMSBackupError.incompatibleGame
        }
    }

    private static func validateStoreFiles(_ stores: [String: Data]) throws {
        guard stores.count <= maximumRMSStoreCount else {
            throw PhoneMERMSBackupError.invalidBackup
        }
        var totalSize = 0
        for (name, data) in stores {
            guard name == URL(fileURLWithPath: name).lastPathComponent,
                  !name.contains("/"),
                  !name.contains("\\"),
                  name.lowercased().hasSuffix(".rms"),
                  data.count >= 4,
                  data.prefix(4) == Data([0x50, 0x4D, 0x52, 0x53]),
                  totalSize <= maximumRMSBackupBytes - data.count else {
                throw PhoneMERMSBackupError.invalidBackup
            }
            totalSize += data.count
        }
    }

    private static func normalizedIdentity(_ value: String) -> String {
        value.trimmingCharacters(in: .whitespacesAndNewlines)
            .folding(
                options: [.caseInsensitive, .diacriticInsensitive],
                locale: Locale(identifier: "en_US_POSIX")
            )
    }

    private static func prepareUncached() throws -> PhoneMERuntimeLayout {
        let fileManager = FileManager.default
        cache.lock.lock()
        let configuredRoot = cache.storageRootURL
        cache.lock.unlock()

        let storageRoot: URL
        if let configuredRoot {
            storageRoot = configuredRoot
        } else {
            let applicationSupport = try fileManager.url(
                for: .applicationSupportDirectory,
                in: .userDomainMask,
                appropriateFor: nil,
                create: true
            )
            storageRoot = applicationSupport.appendingPathComponent(
                "phoneME",
                isDirectory: true
            )
        }
        let runtimeHome = storageRoot.appendingPathComponent(
            "runtime",
            isDirectory: true
        )

        try fileManager.createDirectory(
            at: runtimeHome,
            withIntermediateDirectories: true
        )
        try createRuntimeDirectories(at: runtimeHome, fileManager: fileManager)
        let rmsRootURL = runtimeHome.appendingPathComponent("rms", isDirectory: true)
        try fileManager.createDirectory(
            at: rmsRootURL,
            withIntermediateDirectories: true
        )
        recoverRMSImportTransactions(at: rmsRootURL, fileManager: fileManager)
        excludeFromBackup(runtimeHome)
        return PhoneMERuntimeLayout(homeURL: runtimeHome)
    }

    private static func recoverRMSImportTransactions(
        at rmsRootURL: URL,
        fileManager: FileManager
    ) {
        guard let urls = try? fileManager.contentsOfDirectory(
            at: rmsRootURL,
            includingPropertiesForKeys: [.isDirectoryKey, .contentModificationDateKey],
            options: []
        ) else {
            return
        }

        var previousURLsBySuite: [Int32: [URL]] = [:]
        for url in urls {
            guard (try? url.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true else {
                continue
            }
            let name = url.lastPathComponent
            if name.hasPrefix(".rms-import-") {
                try? fileManager.removeItem(at: url)
                continue
            }
            if let suiteID = rmsTransactionSuiteID(
                from: name,
                prefix: ".rms-previous-"
            ) {
                previousURLsBySuite[suiteID, default: []].append(url)
            }
        }

        for (suiteID, previousURLs) in previousURLsBySuite {
            let targetURL = rmsRootURL.appendingPathComponent(
                String(suiteID),
                isDirectory: true
            )
            if fileManager.fileExists(atPath: targetURL.path) {
                for url in previousURLs {
                    try? fileManager.removeItem(at: url)
                }
                continue
            }

            let newestFirst = previousURLs.sorted { lhs, rhs in
                let lhsDate = try? lhs.resourceValues(
                    forKeys: [.contentModificationDateKey]
                ).contentModificationDate
                let rhsDate = try? rhs.resourceValues(
                    forKeys: [.contentModificationDateKey]
                ).contentModificationDate
                return (lhsDate ?? .distantPast) > (rhsDate ?? .distantPast)
            }
            guard let recoverableURL = newestFirst.first,
                  (try? fileManager.moveItem(
                      at: recoverableURL,
                      to: targetURL
                  )) != nil else {
                continue
            }
            for url in newestFirst.dropFirst() {
                try? fileManager.removeItem(at: url)
            }
        }
    }

    private static func rmsTransactionSuiteID(
        from name: String,
        prefix: String
    ) -> Int32? {
        guard name.hasPrefix(prefix) else { return nil }
        let remainder = name.dropFirst(prefix.count)
        guard let separator = remainder.firstIndex(of: "-"),
              let suiteID = Int32(remainder[..<separator]),
              suiteID > 0 else {
            return nil
        }
        return suiteID
    }

    private static func createRuntimeDirectories(
        at runtimeHome: URL,
        fileManager: FileManager
    ) throws {
        for name in ["appdb", "lib", "memory_card", "games"] {
            try fileManager.createDirectory(
                at: runtimeHome.appendingPathComponent(name, isDirectory: true),
                withIntermediateDirectories: true
            )
        }
    }

    private static func gameRuntimeHome(
        for gameID: UUID,
        sharedRuntimeHome: URL
    ) -> URL {
        sharedRuntimeHome
            .appendingPathComponent("games", isDirectory: true)
            .appendingPathComponent(
                gameID.uuidString.lowercased(),
                isDirectory: true
            )
    }

    private static func excludeFromBackup(_ url: URL) {
        var values = URLResourceValues()
        values.isExcludedFromBackup = true
        var mutableURL = url
        try? mutableURL.setResourceValues(values)
    }
}
