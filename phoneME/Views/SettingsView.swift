import SwiftUI

enum AppTheme: String, CaseIterable, Identifiable {
    case light
    case dark
    case system

    var id: String { rawValue }

    var title: String {
        switch self {
        case .light: return "Light"
        case .dark: return "Dark"
        case .system: return "System"
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
    @State private var storageErrorMessage: String?

    var body: some View {
        Form {
            Section {
                Picker("Appearance", selection: $appTheme) {
                    ForEach(AppTheme.allCases) { theme in
                        Text(theme.title)
                            .tag(theme.rawValue)
                    }
                }
            } header: {
                Text("Appearance")
            } footer: {
                Text("System follows the appearance selected in iOS Settings.")
            }

            Section("Player") {
                Toggle("Action Bar", isOn: $enableActionBar)
                Toggle("Status Bar", isOn: $enableStatusBar)
                Toggle("Keep Screen Awake", isOn: $keepScreenOn)
            }

#if os(iOS)
            Section {
                Toggle("Run in Background", isOn: backgroundExecutionBinding)
            } header: {
                Text("Background Execution")
            } footer: {
                Text("iOS requires low-accuracy Location Services while this option is enabled.")
            }
#endif

            Section {
                Picker("Data Storage", selection: storageLocationBinding) {
                    ForEach(PhoneMEStorageLocation.allCases) { location in
                        Text(location.title)
                            .tag(location)
                    }
                }
                .disabled(storage.isSwitching)

                if storage.isSwitching {
                    HStack {
                        ProgressView()
                        Text("Moving library and game data…")
                            .foregroundStyle(.secondary)
                    }
                }

            } header: {
                Text("Storage")
            } footer: {
                Text("Includes JAR files, profiles, RMS saves, memory-card files and runtime data. Close all running apps before changing the storage location.")
            }

        }
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

struct ProfilesView: View {
    @EnvironmentObject private var templates: ProfileTemplateStore

    @State private var editingTemplate: ProfileTemplate?
    @State private var renamingTemplate: ProfileTemplate?
    @State private var showAddDialog = false
    @State private var enteredName = ""

    @ViewBuilder
    var body: some View {
        Group {
            if templates.templates.isEmpty {
                PhoneMEEmptyStateView(
                    title: "No Profiles",
                    message: "Create a reusable profile for display, font and input settings.",
                    systemImage: "slider.horizontal.3",
                    actionTitle: "Create Profile",
                    action: beginAddingProfile
                )
            } else {
                List(templates.templates) { template in
                    Button {
                        editingTemplate = template
                    } label: {
                        HStack {
                            Label {
                                VStack(alignment: .leading) {
                                    Text(template.name)
                                        .foregroundStyle(.primary)
                                        .lineLimit(1)

                                    if template.id == templates.defaultTemplateID {
                                        Text("Default")
                                            .font(.caption)
                                            .foregroundStyle(.secondary)
                                    }
                                }
                            } icon: {
                                Image(systemName: "slider.horizontal.3")
                                    .foregroundStyle(.secondary)
                            }

                            Spacer()

                            if template.id == templates.defaultTemplateID {
                                Image(systemName: "checkmark")
                                    .foregroundStyle(Color.accentColor)
                                    .accessibilityLabel("Default profile")
                            }
                        }
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .contextMenu {
                        Button("Set as Default") {
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
        .alert("New Profile", isPresented: $showAddDialog) {
            TextField("Profile Name", text: $enteredName)
            Button("Cancel", role: .cancel) {}
            Button("Add") {
                if let template = templates.add(name: enteredName) {
                    editingTemplate = template
                }
            }
        }
        .alert("Rename Profile", isPresented: renameDialogBinding) {
            TextField("Profile Name", text: $enteredName)
            Button("Cancel", role: .cancel) {}
            Button("Save") {
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
