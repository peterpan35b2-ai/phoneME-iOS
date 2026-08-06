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
    private let fpsOverrideOffMigrationKey = "phoneME.gameProfiles.fpsOverrideOffV9"

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
        profiles[game.id] ?? .default
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
            UserDefaults.standard.set(true, forKey: fpsOverrideOffMigrationKey)
            return
        }

        let decoder = JSONDecoder()
        guard let stored = try? decoder.decode([String: GameProfile].self, from: data) else {
            profiles = [:]
            return
        }

        let hasLegacyFrameLimit = stored.values.contains {
            $0.framePacingMode == .cap
        }
        profiles = Dictionary(uniqueKeysWithValues: stored.compactMap { key, value in
            guard let id = UUID(uuidString: key) else { return nil }
            return (id, value.normalized())
        })
        if hasLegacyFrameLimit {
            // A short-lived build stored the FPS control as `.cap`, which
            // throttled every framebuffer publication and slowed progress
            // screens. Persist the corrected render-loop override immediately.
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

        // Earlier builds stored the former 30 FPS default as an explicit game
        // loop override. Turn only that inherited default off once; custom FPS
        // values remain untouched.
        if !defaults.bool(forKey: fpsOverrideOffMigrationKey) {
            var changed = false
            for id in profiles.keys {
                guard profiles[id]?.framePacingMode == .overrideGameLoop,
                      profiles[id]?.frameRateLimit == GameProfile.defaultFrameRate else {
                    continue
                }
                profiles[id]?.framePacingMode = .native
                changed = true
            }
            defaults.set(true, forKey: fpsOverrideOffMigrationKey)
            if changed {
                persist()
            }
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
