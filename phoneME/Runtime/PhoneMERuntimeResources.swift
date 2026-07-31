import Foundation

struct PhoneMERuntimeLayout {
    let homeURL: URL
    let classesURL: URL
}

enum PhoneMERuntimeResources {
    private final class LayoutCache: @unchecked Sendable {
        let lock = NSLock()
        var layout: PhoneMERuntimeLayout?
    }

    private static let runtimeDirectoryName = "PhoneMERuntime"
    private static let cache = LayoutCache()

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

        // Seed only immutable bundled defaults. Never copy the old shared
        // runtime/appdb here: it can contain RMS/accounts from unrelated games.
        if let bundledRuntime = Bundle.main.url(
            forResource: runtimeDirectoryName,
            withExtension: nil
        ) {
            try mergeInitialAppDatabase(
                from: bundledRuntime.appendingPathComponent(
                    "appdb",
                    isDirectory: true
                ),
                to: gameHome.appendingPathComponent("appdb", isDirectory: true),
                fileManager: fileManager
            )
        }

        var resourceValues = URLResourceValues()
        resourceValues.isExcludedFromBackup = true
        var mutableGameHome = gameHome
        try? mutableGameHome.setResourceValues(resourceValues)

        return PhoneMERuntimeLayout(
            homeURL: gameHome,
            classesURL: sharedLayout.classesURL
        )
    }

    static func removeStorage(for gameID: UUID) {
        guard let sharedLayout = try? prepare() else { return }
        let storageURL = gameRuntimeHome(
            for: gameID,
            sharedRuntimeHome: sharedLayout.homeURL
        )
        try? FileManager.default.removeItem(at: storageURL)
    }

    private static func prepareUncached() throws -> PhoneMERuntimeLayout {
        let fileManager = FileManager.default
        guard let bundledRuntime = Bundle.main.url(
            forResource: runtimeDirectoryName,
            withExtension: nil
        ) else {
            throw PhoneMECoreError.runtimeResourcesMissing
        }

        let applicationSupport = try fileManager.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: true
        )
        let runtimeHome = applicationSupport
            .appendingPathComponent("phoneME", isDirectory: true)
            .appendingPathComponent("runtime", isDirectory: true)

        try fileManager.createDirectory(
            at: runtimeHome,
            withIntermediateDirectories: true
        )
        try refreshReadOnlyRuntimeFiles(
            from: bundledRuntime,
            to: runtimeHome,
            fileManager: fileManager
        )
        try mergeInitialAppDatabase(
            from: bundledRuntime.appendingPathComponent("appdb", isDirectory: true),
            to: runtimeHome.appendingPathComponent("appdb", isDirectory: true),
            fileManager: fileManager
        )
        try createRuntimeDirectories(at: runtimeHome, fileManager: fileManager)

        var resourceValues = URLResourceValues()
        resourceValues.isExcludedFromBackup = true
        var mutableRuntimeHome = runtimeHome
        try? mutableRuntimeHome.setResourceValues(resourceValues)

        return PhoneMERuntimeLayout(
            homeURL: runtimeHome,
            classesURL: runtimeHome.appendingPathComponent("classes.zip")
        )
    }

    private static func refreshReadOnlyRuntimeFiles(
        from bundledRuntime: URL,
        to runtimeHome: URL,
        fileManager: FileManager
    ) throws {
        let sourceClasses = bundledRuntime.appendingPathComponent("classes.zip")
        let destinationClasses = runtimeHome.appendingPathComponent("classes.zip")
        let versionMarker = runtimeHome.appendingPathComponent(".classes-version")
        let fingerprint = try runtimeFingerprint(for: sourceClasses)
        let installedFingerprint = try? String(
            contentsOf: versionMarker,
            encoding: .utf8
        )

        if !fileManager.fileExists(atPath: destinationClasses.path)
            || installedFingerprint != fingerprint {
            try replaceItem(
                at: destinationClasses,
                with: sourceClasses,
                fileManager: fileManager
            )
            try fingerprint.write(
                to: versionMarker,
                atomically: true,
                encoding: .utf8
            )
        }
    }

    private static func createRuntimeDirectories(
        at runtimeHome: URL,
        fileManager: FileManager
    ) throws {
        for name in ["appdb", "lib", "memory_card"] {
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

    private static func mergeInitialAppDatabase(
        from sourceDirectory: URL,
        to destinationDirectory: URL,
        fileManager: FileManager
    ) throws {
        try fileManager.createDirectory(
            at: destinationDirectory,
            withIntermediateDirectories: true
        )

        guard fileManager.fileExists(atPath: sourceDirectory.path) else {
            return
        }

        let bundledFiles = try fileManager.contentsOfDirectory(
            at: sourceDirectory,
            includingPropertiesForKeys: [.isRegularFileKey],
            options: [.skipsHiddenFiles]
        )

        for sourceURL in bundledFiles {
            let destinationURL = destinationDirectory
                .appendingPathComponent(sourceURL.lastPathComponent)
            guard !fileManager.fileExists(atPath: destinationURL.path) else {
                continue
            }
            try fileManager.copyItem(at: sourceURL, to: destinationURL)
        }
    }

    private static func runtimeFingerprint(for classesURL: URL) throws -> String {
        let values = try classesURL.resourceValues(
            forKeys: [.fileSizeKey, .contentModificationDateKey]
        )
        let bundleVersion = Bundle.main.object(
            forInfoDictionaryKey: "CFBundleVersion"
        ) as? String ?? "0"
        let size = values.fileSize ?? 0
        let modified = values.contentModificationDate?.timeIntervalSince1970 ?? 0
        return "\(bundleVersion)|\(size)|\(modified)"
    }

    private static func replaceItem(
        at destinationURL: URL,
        with sourceURL: URL,
        fileManager: FileManager
    ) throws {
        if fileManager.fileExists(atPath: destinationURL.path) {
            try fileManager.removeItem(at: destinationURL)
        }
        try fileManager.copyItem(at: sourceURL, to: destinationURL)
    }
}
