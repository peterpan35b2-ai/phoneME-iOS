import Foundation

enum PhoneMEStorageLocation: String, CaseIterable, Identifiable {
    case local
    case iCloud

    var id: String { rawValue }

    var title: String {
        switch self {
        case .local:
            return L10n.string("On My iPhone")
        case .iCloud:
            return "iCloud Drive"
        }
    }

    var systemImage: String {
        switch self {
        case .local: return "iphone"
        case .iCloud: return "icloud"
        }
    }

    var subtitle: String {
        switch self {
        case .local:
            return L10n.string(
                "Keep the library and game data in the Files app on this device."
            )
        case .iCloud:
            return L10n.string(
                "Sync the library, profiles, RMS and game files through iCloud Drive."
            )
        }
    }
}

enum PhoneMEStorageError: LocalizedError {
    case iCloudUnavailable
    case migrationFailed(String)

    var errorDescription: String? {
        switch self {
        case .iCloudUnavailable:
            return L10n.string(
                "iCloud Drive is unavailable. Sign in to iCloud and enable iCloud Drive, then try again."
            )
        case .migrationFailed(let message):
            return L10n.format("Could not move phoneME data: %@", message)
        }
    }
}

@MainActor
final class PhoneMEStorageController: ObservableObject {
    @Published private(set) var selectedLocation: PhoneMEStorageLocation
    @Published private(set) var activeLocation: PhoneMEStorageLocation
    @Published private(set) var rootURL: URL
    @Published private(set) var isSwitching = false
    @Published private(set) var lastErrorMessage: String?

    private static let preferenceKey = "phoneME.storage.location"
    private let fileManager: FileManager
    private let defaults: UserDefaults

    init(
        fileManager: FileManager = .default,
        defaults: UserDefaults = .standard
    ) {
        self.fileManager = fileManager
        self.defaults = defaults

        let requested = PhoneMEStorageLocation(
            rawValue: defaults.string(forKey: Self.preferenceKey) ?? ""
        ) ?? .local
        selectedLocation = requested

        let localRoot = Self.localRootURL(fileManager: fileManager)
        Self.migrateLegacyStorageIfNeeded(
            fileManager: fileManager,
            localRoot: localRoot
        )

        if requested == .iCloud,
           let cloudRoot = Self.resolveICloudRootSynchronously(
               fileManager: fileManager
           ) {
            do {
                try Self.prepareRoot(cloudRoot, fileManager: fileManager)
                if !Self.containsUserData(cloudRoot, fileManager: fileManager) {
                    try Self.coordinatedMerge(
                        from: localRoot,
                        to: cloudRoot,
                        fileManager: fileManager
                    )
                }
                rootURL = cloudRoot
                activeLocation = .iCloud
            } catch {
                rootURL = localRoot
                activeLocation = .local
                lastErrorMessage = PhoneMEStorageError.migrationFailed(
                    error.localizedDescription
                ).localizedDescription
            }
        } else {
            rootURL = localRoot
            activeLocation = .local
        }

        try? Self.prepareRoot(rootURL, fileManager: fileManager)
    }

    var locationDescription: String {
        switch activeLocation {
        case .local:
            return L10n.string("Files › On My iPhone › phoneME › phoneME")
        case .iCloud:
            return L10n.string("Files › iCloud Drive › phoneME › phoneME")
        }
    }

    var isICloudAvailable: Bool {
        fileManager.ubiquityIdentityToken != nil
    }

    func clearLastError() {
        lastErrorMessage = nil
    }

    func switchLocation(to location: PhoneMEStorageLocation) async throws {
        guard location != activeLocation else {
            selectedLocation = location
            defaults.set(location.rawValue, forKey: Self.preferenceKey)
            return
        }

        isSwitching = true
        lastErrorMessage = nil
        let sourceRoot = rootURL
        let fileManager = self.fileManager

        do {
            let destinationRoot = try await withCheckedThrowingContinuation {
                (continuation: CheckedContinuation<URL, Error>) in
                DispatchQueue.global(qos: .utility).async {
                    do {
                        let destination: URL
                        switch location {
                        case .local:
                            destination = Self.localRootURL(
                                fileManager: fileManager
                            )
                        case .iCloud:
                            guard let cloudRoot = fileManager.url(
                                forUbiquityContainerIdentifier: nil
                            )?.appendingPathComponent(
                                "Documents",
                                isDirectory: true
                            ).appendingPathComponent(
                                "phoneME",
                                isDirectory: true
                            ) else {
                                throw PhoneMEStorageError.iCloudUnavailable
                            }
                            destination = cloudRoot
                        }

                        try Self.prepareRoot(
                            destination,
                            fileManager: fileManager
                        )
                        try Self.coordinatedMerge(
                            from: sourceRoot,
                            to: destination,
                            fileManager: fileManager
                        )
                        continuation.resume(returning: destination)
                    } catch {
                        continuation.resume(throwing: error)
                    }
                }
            }

            rootURL = destinationRoot
            activeLocation = location
            selectedLocation = location
            defaults.set(location.rawValue, forKey: Self.preferenceKey)
            isSwitching = false
        } catch {
            isSwitching = false
            let storageError: Error
            if error is PhoneMEStorageError {
                storageError = error
            } else {
                storageError = PhoneMEStorageError.migrationFailed(
                    error.localizedDescription
                )
            }
            lastErrorMessage = storageError.localizedDescription
            throw storageError
        }
    }

    private static func localRootURL(fileManager: FileManager) -> URL {
        let documents = fileManager.urls(
            for: .documentDirectory,
            in: .userDomainMask
        ).first ?? fileManager.temporaryDirectory
        return documents.appendingPathComponent("phoneME", isDirectory: true)
    }

    private static func legacyRootURL(fileManager: FileManager) -> URL {
        let applicationSupport = fileManager.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first ?? fileManager.temporaryDirectory
        return applicationSupport.appendingPathComponent(
            "phoneME",
            isDirectory: true
        )
    }

    private static func resolveICloudRootSynchronously(
        fileManager: FileManager
    ) -> URL? {
        guard fileManager.ubiquityIdentityToken != nil else { return nil }
        return DispatchQueue.global(qos: .utility).sync {
            fileManager.url(forUbiquityContainerIdentifier: nil)?
                .appendingPathComponent("Documents", isDirectory: true)
                .appendingPathComponent("phoneME", isDirectory: true)
        }
    }

    private static func migrateLegacyStorageIfNeeded(
        fileManager: FileManager,
        localRoot: URL
    ) {
        let legacyRoot = legacyRootURL(fileManager: fileManager)
        guard legacyRoot.standardizedFileURL != localRoot.standardizedFileURL,
              fileManager.fileExists(atPath: legacyRoot.path) else {
            try? prepareRoot(localRoot, fileManager: fileManager)
            return
        }

        do {
            try prepareRoot(localRoot, fileManager: fileManager)
            try coordinatedMerge(
                from: legacyRoot,
                to: localRoot,
                fileManager: fileManager
            )
        } catch {
            // Keep the legacy directory untouched as a recoverable copy. The
            // caller still gets a valid local root and can continue using the app.
            try? prepareRoot(localRoot, fileManager: fileManager)
        }
    }

    private static func prepareRoot(
        _ root: URL,
        fileManager: FileManager
    ) throws {
        try fileManager.createDirectory(
            at: root,
            withIntermediateDirectories: true
        )
    }

    private static func containsUserData(
        _ root: URL,
        fileManager: FileManager
    ) -> Bool {
        guard let contents = try? fileManager.contentsOfDirectory(
            at: root,
            includingPropertiesForKeys: nil,
            options: [.skipsHiddenFiles]
        ) else {
            return false
        }
        return !contents.isEmpty
    }

    private static func coordinatedMerge(
        from source: URL,
        to destination: URL,
        fileManager: FileManager
    ) throws {
        guard source.standardizedFileURL != destination.standardizedFileURL,
              fileManager.fileExists(atPath: source.path) else {
            return
        }

        try prepareRoot(destination, fileManager: fileManager)

        let coordinator = NSFileCoordinator(filePresenter: nil)
        var coordinationError: NSError?
        var operationError: Error?
        coordinator.coordinate(
            readingItemAt: source,
            options: .withoutChanges,
            writingItemAt: destination,
            options: .forMerging,
            error: &coordinationError
        ) { coordinatedSource, coordinatedDestination in
            do {
                try mergeDirectory(
                    from: coordinatedSource,
                    to: coordinatedDestination,
                    fileManager: fileManager
                )
            } catch {
                operationError = error
            }
        }

        if let operationError {
            throw operationError
        }
        if let coordinationError {
            throw coordinationError
        }
    }

    private static func mergeDirectory(
        from source: URL,
        to destination: URL,
        fileManager: FileManager
    ) throws {
        try fileManager.createDirectory(
            at: destination,
            withIntermediateDirectories: true
        )

        let contents = try fileManager.contentsOfDirectory(
            at: source,
            includingPropertiesForKeys: [
                .isDirectoryKey,
                .contentModificationDateKey
            ],
            options: [.skipsHiddenFiles]
        )

        for sourceItem in contents {
            let destinationItem = destination.appendingPathComponent(
                sourceItem.lastPathComponent,
                isDirectory: false
            )
            let sourceValues = try sourceItem.resourceValues(
                forKeys: [.isDirectoryKey, .contentModificationDateKey]
            )

            if sourceValues.isDirectory == true {
                try mergeDirectory(
                    from: sourceItem,
                    to: destinationItem,
                    fileManager: fileManager
                )
                continue
            }

            guard fileManager.fileExists(atPath: destinationItem.path) else {
                try fileManager.copyItem(at: sourceItem, to: destinationItem)
                continue
            }

            let destinationValues = try destinationItem.resourceValues(
                forKeys: [.contentModificationDateKey]
            )
            let sourceDate = sourceValues.contentModificationDate ?? .distantPast
            let destinationDate = destinationValues.contentModificationDate
                ?? .distantPast
            guard sourceDate > destinationDate else { continue }

            try fileManager.removeItem(at: destinationItem)
            try fileManager.copyItem(at: sourceItem, to: destinationItem)
        }
    }
}
