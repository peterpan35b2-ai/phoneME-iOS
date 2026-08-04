import SwiftUI

enum TranslationPreferences {
    static let enabledKey = "translateChineseToVietnamese"
}

enum AppTheme: String, CaseIterable, Identifiable {
    case light
    case dark
    case system

    var id: String { rawValue }

    var title: String {
        switch self {
        case .light: return "Light"
        case .dark: return "Dark"
        case .system: return "Auto, by time of day"
        }
    }

    var colorScheme: ColorScheme? {
        switch self {
        case .light: return .light
        case .dark: return .dark
        case .system: return nil
        }
    }
}

struct SettingsView: View {
    @EnvironmentObject private var storage: PhoneMEStorageController
    @EnvironmentObject private var library: GameLibrary
    @EnvironmentObject private var profiles: GameProfileStore
    @EnvironmentObject private var profileTemplates: ProfileTemplateStore
    @EnvironmentObject private var session: EmulatorSession
    @EnvironmentObject private var backgroundExecution: BackgroundExecutionController

    @AppStorage("appTheme") private var appTheme = AppTheme.system.rawValue
    @AppStorage("enableActionBar") private var enableActionBar = true
    @AppStorage("enableStatusBar") private var enableStatusBar = false
    @AppStorage("keepScreenOn") private var keepScreenOn = false
    @AppStorage(TranslationPreferences.enabledKey)
    private var translateChineseToVietnamese = false
    @State private var storageErrorMessage: String?

    var body: some View {
        Form {
            Section {
                Picker(selection: $appTheme) {
                    ForEach(AppTheme.allCases) { theme in
                        Text(theme.title).tag(theme.rawValue)
                    }
                } label: {
                    SettingsLabel(
                        title: "Appearance",
                        subtitle: "Choose how phoneME looks",
                        systemImage: "paintpalette.fill",
                        tint: .pink
                    )
                }
            } header: {
                PhoneMESectionTitle(
                    title: "Appearance",
                    subtitle: "Uses native system colors in every theme."
                )
            }
            .listRowBackground(Color.phoneMECardBackground)

            Section {
                Toggle(isOn: $enableActionBar) {
                    SettingsLabel(
                        title: "Action bar",
                        subtitle: "Show app controls above the emulator",
                        systemImage: "rectangle.topthird.inset.filled",
                        tint: .cyan
                    )
                }

                Toggle(isOn: $enableStatusBar) {
                    SettingsLabel(
                        title: "Status bar",
                        subtitle: "Keep the iOS status bar visible",
                        systemImage: "iphone.gen3",
                        tint: .purple
                    )
                }

                Toggle(isOn: $keepScreenOn) {
                    SettingsLabel(
                        title: "Keep screen awake",
                        subtitle: "Prevent display sleep while playing",
                        systemImage: "sun.max.fill",
                        tint: .orange
                    )
                }

                Toggle(isOn: $translateChineseToVietnamese) {
                    SettingsLabel(
                        title: "Translate Chinese to Vietnamese",
                        subtitle: "Translate native game text online and cache it locally",
                        systemImage: "character.book.closed.fill",
                        tint: .green
                    )
                }
            } header: {
                PhoneMESectionTitle(title: "Player")
            }
            .listRowBackground(Color.phoneMECardBackground)

#if os(iOS)
            Section {
                Toggle(isOn: backgroundExecutionBinding) {
                    SettingsLabel(
                        title: "Run in background",
                        subtitle: "Keep active MIDlets running after leaving the player",
                        systemImage: "location.fill",
                        tint: .blue
                    )
                }
            } header: {
                PhoneMESectionTitle(title: "Background execution")
            } footer: {
                Text("iOS requires low-accuracy Location Services while this option is active.")
            }
            .listRowBackground(Color.phoneMECardBackground)
#endif

            Section {
                Picker(selection: storageLocationBinding) {
                    ForEach(PhoneMEStorageLocation.allCases) { location in
                        Text(location.title)
                            .tag(location)
                    }
                } label: {
                    SettingsLabel(
                        title: "Data storage",
                        subtitle: storage.activeLocation.subtitle,
                        systemImage: storage.activeLocation == .iCloud
                            ? "icloud.fill"
                            : "internaldrive.fill",
                        tint: .blue
                    )
                }
                .disabled(storage.isSwitching)

                if storage.isSwitching {
                    HStack(spacing: 12) {
                        ProgressView()
                        Text("Moving library and game data…")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                }

                PhoneMELabeledContent {
                    Text(storage.locationDescription)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.trailing)
                        .fixedSize(horizontal: false, vertical: true)
                } label: {
                    SettingsLabel(
                        title: "Data folder",
                        systemImage: "folder.fill",
                        tint: .yellow
                    )
                }
            } header: {
                PhoneMESectionTitle(
                    title: "Storage",
                    subtitle: "Includes JAR files, profiles, RMS saves, memory-card files and runtime data."
                )
            } footer: {
                Text("Stop every running J2ME app before changing storage. Existing data is copied safely to the selected location.")
            }
            .listRowBackground(Color.phoneMECardBackground)

            Section {
                NavigationLink {
                    ProfilesView()
                } label: {
                    SettingsLabel(
                        title: "Profiles",
                        subtitle: "Reusable display and input presets",
                        systemImage: "slider.horizontal.3",
                        tint: .green
                    )
                }
            } header: {
                PhoneMESectionTitle(title: "Library")
            }
            .listRowBackground(Color.phoneMECardBackground)
        }
        .phoneMEScrollContentBackgroundHidden()
        .background(Color.phoneMEAppBackground)
        .frame(maxWidth: PhoneMEVisualMetrics.contentMaxWidth)
        .frame(maxWidth: .infinity)
        .navigationTitle("Settings")
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
#endif
        .alert("Storage", isPresented: storageErrorBinding) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(storageErrorMessage ?? "")
        }
    }

    private var storageLocationBinding: Binding<PhoneMEStorageLocation> {
        Binding(
            get: { storage.activeLocation },
            set: { switchStorage(to: $0) }
        )
    }

    private var storageErrorBinding: Binding<Bool> {
        Binding(
            get: { storageErrorMessage != nil },
            set: { if !$0 { storageErrorMessage = nil } }
        )
    }

    private func switchStorage(to location: PhoneMEStorageLocation) {
        guard location != storage.activeLocation else { return }
        guard session.runningApplications.isEmpty else {
            storageErrorMessage = "Close all running J2ME apps before changing the data location."
            return
        }

        Task { @MainActor in
            do {
                try await storage.switchLocation(to: location)
                PhoneMERuntimeResources.configure(
                    storageRootURL: storage.rootURL
                )
                session.resetRuntimeForStorageChange()
                library.reloadFromStorage()
                profiles.reloadFromStorage()
                profileTemplates.reloadFromStorage()
            } catch {
                storageErrorMessage = error.localizedDescription
            }
        }
    }

#if os(iOS)
    private var backgroundExecutionBinding: Binding<Bool> {
        Binding(
            get: { backgroundExecution.isEnabled },
            set: { backgroundExecution.setEnabled($0) }
        )
    }
#endif
}

private struct SettingsLabel: View {
    let title: String
    var subtitle: String? = nil
    let systemImage: String
    let tint: Color

    var body: some View {
        Label {
            VStack(alignment: .leading, spacing: 3) {
                Text(title)
                    .foregroundStyle(.primary)
                if let subtitle {
                    Text(subtitle)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        } icon: {
            Image(systemName: systemImage)
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(tint)
                .frame(width: 30, height: 30)
                .background(tint.opacity(0.14))
                .clipShape(
                    RoundedRectangle(cornerRadius: 8, style: .continuous)
                )
        }
        .frame(minHeight: 42)
    }
}

struct ProfilesView: View {
    @EnvironmentObject private var templates: ProfileTemplateStore

    @State private var editingTemplate: ProfileTemplate?
    @State private var renamingTemplate: ProfileTemplate?
    @State private var showAddDialog = false
    @State private var enteredName = ""

    var body: some View {
        ZStack {
            Color.phoneMEAppBackground
                .ignoresSafeArea()

            if templates.templates.isEmpty {
                PhoneMEEmptyStateView(
                    title: "No profiles",
                    message: "Create a reusable profile for screen, font and input settings.",
                    systemImage: "slider.horizontal.3",
                    actionTitle: "Create profile",
                    action: beginAddingProfile
                )
            } else {
                List(templates.templates) { template in
                    Button {
                        editingTemplate = template
                    } label: {
                        HStack(spacing: 12) {
                            Image(systemName: "slider.horizontal.3")
                                .font(.subheadline.weight(.semibold))
                                .foregroundStyle(Color.accentColor)
                                .frame(width: 34, height: 34)
                                .background(Color.accentColor.opacity(0.12))
                                .clipShape(
                                    RoundedRectangle(cornerRadius: 9, style: .continuous)
                                )

                            VStack(alignment: .leading, spacing: 3) {
                                Text(template.name)
                                    .font(.body.weight(.medium))
                                    .foregroundStyle(.primary)
                                    .lineLimit(1)

                                if template.id == templates.defaultTemplateID {
                                    Text("Default profile")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }

                            Spacer(minLength: 12)

                            if template.id == templates.defaultTemplateID {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(Color.accentColor)
                                    .accessibilityLabel("Default")
                            }

                            Image(systemName: "chevron.right")
                                .font(.caption.weight(.bold))
                                .foregroundStyle(.tertiary)
                                .accessibilityHidden(true)
                        }
                        .frame(minHeight: PhoneMEVisualMetrics.minimumRowHeight)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .listRowBackground(Color.phoneMECardBackground)
                    .contextMenu {
                        Button("Set as default") {
                            templates.setDefault(template)
                        }
                        Button("Edit") {
                            editingTemplate = template
                        }
                        Button("Rename") {
                            enteredName = template.name
                            renamingTemplate = template
                        }
                        Button("Delete", role: .destructive) {
                            templates.remove(template)
                        }
                    }
                }
                .listStyle(.insetGrouped)
                .phoneMEScrollContentBackgroundHidden()
                .background(Color.phoneMEAppBackground)
                .frame(maxWidth: PhoneMEVisualMetrics.contentMaxWidth)
                .frame(maxWidth: .infinity)
            }
        }
        .navigationTitle("Profiles")
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
#endif
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button(action: beginAddingProfile) {
                    Image(systemName: "plus")
                }
                .accessibilityLabel("Add profile")
            }
        }
        .alert("Enter name", isPresented: $showAddDialog) {
            TextField("Profile name", text: $enteredName)
                .foregroundStyle(.primary)
                .tint(.accentColor)
            Button("Cancel", role: .cancel) {}
            Button("OK") {
                if let template = templates.add(name: enteredName) {
                    editingTemplate = template
                }
            }
        }
        .alert("Enter new name", isPresented: renameDialogBinding) {
            TextField("New profile name", text: $enteredName)
                .foregroundStyle(.primary)
                .tint(.accentColor)
            Button("Cancel", role: .cancel) {}
            Button("OK") {
                if let renamingTemplate {
                    templates.rename(renamingTemplate, to: enteredName)
                }
            }
        }
        .sheet(item: $editingTemplate) { template in
            PhoneMENavigationStack {
                GameProfileEditorView(
                    profileName: template.name,
                    initialProfile: templates.currentTemplate(template)?.profile ?? template.profile
                ) { profile in
                    templates.save(profile, for: template)
                }
            }
        }
    }

    private func beginAddingProfile() {
        enteredName = ""
        showAddDialog = true
    }

    private var renameDialogBinding: Binding<Bool> {
        Binding(
            get: { renamingTemplate != nil },
            set: { if !$0 { renamingTemplate = nil } }
        )
    }
}
