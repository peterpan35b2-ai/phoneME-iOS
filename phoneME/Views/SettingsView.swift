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
    @EnvironmentObject private var backgroundExecution: BackgroundExecutionController

    @AppStorage("appTheme") private var appTheme = AppTheme.system.rawValue
    @AppStorage("enableActionBar") private var enableActionBar = true
    @AppStorage("enableStatusBar") private var enableStatusBar = false
    @AppStorage("keepScreenOn") private var keepScreenOn = false

    var body: some View {
        Form {
            Picker(selection: $appTheme) {
                ForEach(AppTheme.allCases) { theme in
                    Text(theme.title).tag(theme.rawValue)
                }
            } label: {
                SettingsLabel(title: "Theme", systemImage: "paintpalette", tint: .red)
            }

            Toggle(isOn: $enableActionBar) {
                SettingsLabel(title: "Enable ActionBar", subtitle: "In fullscreen applications", systemImage: "rectangle.topthird.inset.filled", tint: .cyan)
            }

            Toggle(isOn: $enableStatusBar) {
                SettingsLabel(title: "Enable statusbar", subtitle: "In fullscreen applications", systemImage: "rectangle.topthird.inset.filled", tint: .purple)
            }

            Toggle(isOn: $keepScreenOn) {
                SettingsLabel(title: "Keep screen on", systemImage: "sun.max.fill", tint: .yellow)
            }

#if os(iOS)
            Toggle(isOn: backgroundExecutionBinding) {
                SettingsLabel(
                    title: "Run in Background",
                    subtitle: "Uses low-accuracy Location Services while an app is running",
                    systemImage: "location.fill",
                    tint: .blue
                )
            }

            if backgroundExecution.isEnabled {
                LabeledContent {
                    Text(backgroundExecution.status.description)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.trailing)
                } label: {
                    SettingsLabel(
                        title: "Background status",
                        systemImage: backgroundExecution.isKeepingAlive
                            ? "checkmark.circle.fill"
                            : "info.circle.fill",
                        tint: backgroundExecution.isKeepingAlive ? .green : .secondary
                    )
                }

                if backgroundExecution.status.requiresSystemSettings {
                    Button("Open Location Settings") {
                        backgroundExecution.openSystemSettings()
                    }
                }
            }
#endif

            NavigationLink {
                ProfilesView()
            } label: {
                SettingsLabel(title: "Profiles", systemImage: "wrench.adjustable.fill", tint: .orange)
            }

            LabeledContent {
                Text("Application Support/phoneME")
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            } label: {
                SettingsLabel(title: "Working directory", systemImage: "folder.fill", tint: .yellow)
            }

        }
        .navigationTitle("Settings")
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
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                if let subtitle {
                    Text(subtitle)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        } icon: {
            Image(systemName: systemImage)
                .foregroundStyle(tint)
        }
    }
}

struct ProfilesView: View {
    @EnvironmentObject private var templates: ProfileTemplateStore

    @State private var editingTemplate: ProfileTemplate?
    @State private var renamingTemplate: ProfileTemplate?
    @State private var showAddDialog = false
    @State private var enteredName = ""

    var body: some View {
        Group {
            if templates.templates.isEmpty {
                Text("No data")
                    .font(.title3)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
                    .padding(.top, 10)
            } else {
                List(templates.templates) { template in
                    Text(displayName(for: template))
                        .contentShape(Rectangle())
                        .onTapGesture {
                            editingTemplate = template
                        }
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
                .listStyle(.plain)
            }
        }
        .navigationTitle("Profiles")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    enteredName = ""
                    showAddDialog = true
                } label: {
                    Image(systemName: "plus")
                }
                .accessibilityLabel("Add")
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
            NavigationStack {
                GameProfileEditorView(
                    profileName: template.name,
                    initialProfile: templates.currentTemplate(template)?.profile ?? template.profile
                ) { profile in
                    templates.save(profile, for: template)
                }
            }
        }
    }

    private func displayName(for template: ProfileTemplate) -> String {
        template.id == templates.defaultTemplateID
            ? "\(template.name) (default)"
            : template.name
    }

    private var renameDialogBinding: Binding<Bool> {
        Binding(
            get: { renamingTemplate != nil },
            set: { if !$0 { renamingTemplate = nil } }
        )
    }
}
