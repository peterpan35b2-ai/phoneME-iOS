import Foundation

struct ProfileTemplate: Identifiable, Codable, Equatable {
    let id: UUID
    var name: String
    var profile: GameProfile

    init(id: UUID = UUID(), name: String, profile: GameProfile = .default) {
        self.id = id
        self.name = name
        self.profile = profile
    }
}

@MainActor
final class ProfileTemplateStore: ObservableObject {
    @Published private(set) var templates: [ProfileTemplate] = []
    @Published private(set) var defaultTemplateID: UUID?

    private struct StoredData: Codable {
        var templates: [ProfileTemplate]
        var defaultTemplateID: UUID?
    }

    private let fileManager: FileManager
    private let storage: PhoneMEStorageController
    private var metadataURL: URL {
        storage.rootURL.appendingPathComponent(
            "templates.json",
            isDirectory: false
        )
    }
    private let keyboardLayoutMigrationKey = "phoneME.profileTemplates.keyboardLayoutV2"
    private let keyboardPaletteMigrationKey = "phoneME.profileTemplates.keyboardPaletteV3"
    private let displayGravityMigrationKey = "phoneME.profileTemplates.displayGravityV5"
    private let parallelRedrawMigrationKey = "phoneME.profileTemplates.parallelRedrawV6"
    private let nativeInputDefaultsMigrationKey = "phoneME.profileTemplates.nativeInputDefaultsV7"
    private let frameRate60MigrationKey = "phoneME.profileTemplates.frameRate60V8"
    private let fpsOverrideOffMigrationKey = "phoneME.profileTemplates.fpsOverrideOffV9"

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

    @discardableResult
    func add(name: String) -> ProfileTemplate? {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty,
              !templates.contains(where: { $0.name.caseInsensitiveCompare(trimmed) == .orderedSame }) else {
            return nil
        }

        let template = ProfileTemplate(name: trimmed)
        templates.append(template)
        sort()
        persist()
        return template
    }

    func save(_ profile: GameProfile, for template: ProfileTemplate) {
        guard let index = templates.firstIndex(where: { $0.id == template.id }) else { return }
        templates[index].profile = profile.normalized()
        persist()
    }

    func rename(_ template: ProfileTemplate, to name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty,
              !templates.contains(where: {
                  $0.id != template.id && $0.name.caseInsensitiveCompare(trimmed) == .orderedSame
              }),
              let index = templates.firstIndex(where: { $0.id == template.id }) else {
            return
        }
        templates[index].name = trimmed
        sort()
        persist()
    }

    func remove(_ template: ProfileTemplate) {
        templates.removeAll { $0.id == template.id }
        if defaultTemplateID == template.id {
            defaultTemplateID = nil
        }
        persist()
    }

    func setDefault(_ template: ProfileTemplate) {
        defaultTemplateID = template.id
        persist()
    }

    func currentTemplate(_ template: ProfileTemplate) -> ProfileTemplate? {
        templates.first { $0.id == template.id }
    }

    private func sort() {
        templates.sort { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    private func load() {
        guard let data = try? Data(contentsOf: metadataURL),
              let stored = try? JSONDecoder().decode(StoredData.self, from: data) else {
            templates = []
            defaultTemplateID = nil
            UserDefaults.standard.set(true, forKey: keyboardLayoutMigrationKey)
            UserDefaults.standard.set(true, forKey: keyboardPaletteMigrationKey)
            UserDefaults.standard.set(true, forKey: displayGravityMigrationKey)
            UserDefaults.standard.set(true, forKey: parallelRedrawMigrationKey)
            UserDefaults.standard.set(true, forKey: nativeInputDefaultsMigrationKey)
            UserDefaults.standard.set(true, forKey: frameRate60MigrationKey)
            UserDefaults.standard.set(true, forKey: fpsOverrideOffMigrationKey)
            return
        }
        let hasLegacyFrameLimit = stored.templates.contains {
            $0.profile.framePacingMode == .cap
        }
        templates = stored.templates.map { template in
            var normalized = template
            normalized.profile = template.profile.normalized()
            return normalized
        }
        defaultTemplateID = stored.defaultTemplateID
        if hasLegacyFrameLimit {
            persist()
        }

        let defaults = UserDefaults.standard
        if !defaults.bool(forKey: keyboardLayoutMigrationKey) {
            var changed = false
            for index in templates.indices where templates[index].profile.virtualKeyboardType == .phone {
                templates[index].profile.virtualKeyboardType = .custom
                changed = true
            }
            defaults.set(true, forKey: keyboardLayoutMigrationKey)
            if changed {
                persist()
            }
        }

        if !defaults.bool(forKey: keyboardPaletteMigrationKey) {
            var changed = false
            for index in templates.indices {
                if templates[index].profile.migrateLegacyKeyboardPaletteIfNeeded() {
                    changed = true
                }
            }
            defaults.set(true, forKey: keyboardPaletteMigrationKey)
            if changed {
                persist()
            }
        }

        if !defaults.bool(forKey: displayGravityMigrationKey) {
            var changed = false
            for index in templates.indices {
                guard templates[index].profile.screenGravity == .center,
                      templates[index].profile.scaleType == .fit,
                      templates[index].profile.scalePercent == 100,
                      templates[index].profile.screenWidth == 240,
                      templates[index].profile.screenHeight == 320 else {
                    continue
                }
                templates[index].profile.screenGravity = .top
                changed = true
            }
            defaults.set(true, forKey: displayGravityMigrationKey)
            if changed {
                persist()
            }
        }

        if !defaults.bool(forKey: parallelRedrawMigrationKey) {
            var changed = false
            for index in templates.indices where !templates[index].profile.parallelScreenRedrawing {
                templates[index].profile.parallelScreenRedrawing = true
                changed = true
            }
            defaults.set(true, forKey: parallelRedrawMigrationKey)
            if changed {
                persist()
            }
        }

        if !defaults.bool(forKey: nativeInputDefaultsMigrationKey) {
            var changed = false
            for index in templates.indices {
                if templates[index].profile.migrateNativeInputDefaultsIfNeeded() {
                    changed = true
                }
            }
            defaults.set(true, forKey: nativeInputDefaultsMigrationKey)
            if changed {
                persist()
            }
        }

        // V8 temporarily promoted every stored 30 FPS template to 60 FPS.
        // Keep existing choices as-is and never repeat that migration now that
        // newly created profiles default to 30 FPS again.
        if !defaults.bool(forKey: frameRate60MigrationKey) {
            defaults.set(true, forKey: frameRate60MigrationKey)
        }

        // Earlier builds stored the former 30 FPS default as an explicit game
        // loop override. Turn only that inherited default off once; custom FPS
        // values remain untouched.
        if !defaults.bool(forKey: fpsOverrideOffMigrationKey) {
            var changed = false
            for index in templates.indices {
                guard templates[index].profile.framePacingMode == .overrideGameLoop,
                      templates[index].profile.frameRateLimit == GameProfile.defaultFrameRate else {
                    continue
                }
                templates[index].profile.framePacingMode = .native
                changed = true
            }
            defaults.set(true, forKey: fpsOverrideOffMigrationKey)
            if changed {
                persist()
            }
        }
        sort()
    }

    private func persist() {
        let stored = StoredData(
            templates: templates,
            defaultTemplateID: defaultTemplateID
        )
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        guard let data = try? encoder.encode(stored) else { return }
        try? data.write(to: metadataURL, options: .atomic)
    }
}
