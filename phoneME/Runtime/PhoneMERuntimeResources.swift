import Foundation

struct PhoneMERuntimeLayout {
    let homeURL: URL
}

enum PhoneMERuntimeResources {
    private final class LayoutCache: @unchecked Sendable {
        let lock = NSLock()
        var layout: PhoneMERuntimeLayout?
    }

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

    private static func prepareUncached() throws -> PhoneMERuntimeLayout {
        let fileManager = FileManager.default
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
        try createRuntimeDirectories(at: runtimeHome, fileManager: fileManager)
        excludeFromBackup(runtimeHome)
        return PhoneMERuntimeLayout(homeURL: runtimeHome)
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
