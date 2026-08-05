import Foundation

@MainActor
final class GameProfileStore: ObservableObject {
    @Published private(set) var profiles: [UUID: GameProfile] = [:]

    private let fileManager: FileManager
    private let storage: PhoneMEStorageController
    private var metadataURL: URL {
        storage.rootURL.appendingPathComponent(
            "profiles.json",
            isDirectory: false
        )
    }
    private let keyboardLayoutMigrationKey = "phoneME.gameProfiles.keyboardLayoutV2"
    private let keyboardPaletteMigrationKey = "phoneME.gameProfiles.keyboardPaletteV3"
    private let displayScaleMigrationKey = "phoneME.gameProfiles.displayScaleV4"
    private let displayGravityMigrationKey = "phoneME.gameProfiles.displayGravityV5"
    private let parallelRedrawMigrationKey = "phoneME.gameProfiles.parallelRedrawV6"
    private let nativeInputDefaultsMigrationKey = "phoneME.gameProfiles.nativeInputDefaultsV7"
    private let frameRate60MigrationKey = "phoneME.gameProfiles.frameRate60V8"

    init(
        storage: PhoneMEStorageController,
        fileManager: FileManager = .default
    ) {
        self.storage = storage
        self.fileManager = fileManager
        reloadFromStorage()
    }

    func reloadFromStorage() {
        try? fileManager.createDirectory(
            at: storage.rootURL,
            withIntermediateDirectories: true
        )
        load()
    }

    func hasProfile(for game: Game) -> Bool {
        profiles[game.id] != nil
    }

    func profile(for game: Game) -> GameProfile {
        Self.applyingCompatibilityDefaults(
            to: profiles[game.id] ?? .default,
            for: game
        )
    }

    private static func applyingCompatibilityDefaults(
        to storedProfile: GameProfile,
        for game: Game
    ) -> GameProfile {
        var profile = storedProfile
        guard game.mainClass.caseInsensitiveCompare("CCMIDlet") == .orderedSame else {
            return profile
        }

        let normalizedTitle = game.title.folding(
            options: [.caseInsensitive, .diacriticInsensitive],
            locale: Locale(identifier: "en_US_POSIX")
        )
        let normalizedFileName = game.fileName.folding(
            options: [.caseInsensitive, .diacriticInsensitive],
            locale: Locale(identifier: "en_US_POSIX")
        )
        let isTankRaid = normalizedTitle.contains("tank raid")
            || normalizedTitle.contains("xe tang 3d")
            || normalizedFileName.contains("xe tang 3d")
        guard isTankRaid else { return profile }

        // Tank Raid advances its asset loader from a self-paced CCMIDlet loop
        // and flushes the progress screen after each step. A synchronous 30 FPS
        // publication cap therefore inserts about 33 ms between loading steps
        // and makes this family several times slower. Migrate both fresh and
        // previously auto-saved default profiles, while preserving a user-set
        // non-default frame-rate cap.
        if profile.effectiveFramePacingMode == .cap,
           profile.frameRateLimit == GameProfile.defaultFrameRate {
            profile.framePacingMode = .native
        }
        return profile
    }

    func save(_ profile: GameProfile, for game: Game) {
        profiles[game.id] = profile.normalized()
        persist()
    }

    func reset(for game: Game) {
        profiles.removeValue(forKey: game.id)
        persist()
    }

    func removeProfile(for game: Game) {
        profiles.removeValue(forKey: game.id)
        persist()
    }

    private func load() {
        guard let data = try? Data(contentsOf: metadataURL) else {
            profiles = [:]
            UserDefaults.standard.set(true, forKey: keyboardLayoutMigrationKey)
            UserDefaults.standard.set(true, forKey: keyboardPaletteMigrationKey)
            UserDefaults.standard.set(true, forKey: displayScaleMigrationKey)
            UserDefaults.standard.set(true, forKey: displayGravityMigrationKey)
            UserDefaults.standard.set(true, forKey: parallelRedrawMigrationKey)
            UserDefaults.standard.set(true, forKey: nativeInputDefaultsMigrationKey)
            UserDefaults.standard.set(true, forKey: frameRate60MigrationKey)
            return
        }

        let decoder = JSONDecoder()
        guard let stored = try? decoder.decode([String: GameProfile].self, from: data) else {
            profiles = [:]
            return
        }

        let hasUnsafeLegacyFrameOverride = stored.values.contains {
            $0.framePacingMode == .overrideGameLoop
        }
        profiles = Dictionary(uniqueKeysWithValues: stored.compactMap { key, value in
            guard let id = UUID(uuidString: key) else { return nil }
            return (id, value.normalized())
        })
        if hasUnsafeLegacyFrameOverride {
            // Persist the one-way migration immediately. Core also aliases the
            // legacy numeric mode to a safe cap, but rewriting JSON prevents a
            // later editor save from reintroducing accelerated Java sleeps.
            persist()
        }

        // Earlier phoneME-iOS builds incorrectly saved `.phone` while rendering
        // the Numbers & arrows layout for every selection. Preserve the visual
        // behavior users configured before layout types became functional.
        let defaults = UserDefaults.standard
        if !defaults.bool(forKey: keyboardLayoutMigrationKey) {
            var changed = false
            for id in profiles.keys where profiles[id]?.virtualKeyboardType == .phone {
                profiles[id]?.virtualKeyboardType = .custom
                changed = true
            }
            defaults.set(true, forKey: keyboardLayoutMigrationKey)
            if changed {
                persist()
            }
        }

        if !defaults.bool(forKey: keyboardPaletteMigrationKey) {
            var changed = false
            for id in profiles.keys {
                if profiles[id]?.migrateLegacyKeyboardPaletteIfNeeded() == true {
                    changed = true
                }
            }
            defaults.set(true, forKey: keyboardPaletteMigrationKey)
            if changed {
                persist()
            }
        }

        // Older builds treated 100% as the physical J2ME framebuffer size,
        // leaving the display at 240 points wide on modern iPhones. Migrate
        // only untouched legacy scaling while preserving top anchoring and
        // explicit user choices.
        if !defaults.bool(forKey: displayScaleMigrationKey) {
            var changed = false
            for id in profiles.keys {
                guard var profile = profiles[id],
                      profile.scaleType == .asIs,
                      profile.scalePercent == 100,
                      profile.screenWidth == 240,
                      profile.screenHeight == 320,
                      profile.screenGravity == .top,
                      profile.preserveAspectRatio else {
                    continue
                }
                profile.scaleType = .fit
                profiles[id] = profile
                changed = true
            }
            defaults.set(true, forKey: displayScaleMigrationKey)
            if changed {
                persist()
            }
        }

        if !defaults.bool(forKey: displayGravityMigrationKey) {
            var changed = false
            for id in profiles.keys {
                guard var profile = profiles[id],
                      profile.screenGravity == .center,
                      profile.scaleType == .fit,
                      profile.scalePercent == 100,
                      profile.screenWidth == 240,
                      profile.screenHeight == 320 else {
                    continue
                }
                profile.screenGravity = .top
                profiles[id] = profile
                changed = true
            }
            defaults.set(true, forKey: displayGravityMigrationKey)
            if changed {
                persist()
            }
        }

        // Frame conversion used to run synchronously on the main thread by
        // default. Move existing profiles to the render queue so large combat
        // effects cannot stall touch handling and SwiftUI composition.
        if !defaults.bool(forKey: parallelRedrawMigrationKey) {
            var changed = false
            for id in profiles.keys where profiles[id]?.parallelScreenRedrawing == false {
                profiles[id]?.parallelScreenRedrawing = true
                changed = true
            }
            defaults.set(true, forKey: parallelRedrawMigrationKey)
            if changed {
                persist()
            }
        }

        if !defaults.bool(forKey: nativeInputDefaultsMigrationKey) {
            var changed = false
            for id in profiles.keys {
                if profiles[id]?.migrateNativeInputDefaultsIfNeeded() == true {
                    changed = true
                }
            }
            defaults.set(true, forKey: nativeInputDefaultsMigrationKey)
            if changed {
                persist()
            }
        }

        // V8 temporarily promoted every stored 30 FPS profile to 60 FPS.
        // Do not repeat that migration: 30 FPS is the default again, while an
        // existing explicit 60 FPS choice remains untouched.
        if !defaults.bool(forKey: frameRate60MigrationKey) {
            defaults.set(true, forKey: frameRate60MigrationKey)
        }
    }

    private func persist() {
        let stored = Dictionary(uniqueKeysWithValues: profiles.map { ($0.key.uuidString, $0.value) })
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]

        guard let data = try? encoder.encode(stored) else { return }
        try? data.write(to: metadataURL, options: .atomic)
    }
}
