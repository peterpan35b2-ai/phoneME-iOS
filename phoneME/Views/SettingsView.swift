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
    @AppStorage("appTheme") private var appTheme = AppTheme.system.rawValue
    @AppStorage("blackBackground") private var blackBackground = false
    @AppStorage("enableActionBar") private var enableActionBar = true
    @AppStorage("enableStatusBar") private var enableStatusBar = false
    @AppStorage("keepScreenOn") private var keepScreenOn = false
    @AppStorage("rawScreenshot") private var rawScreenshot = false
    @AppStorage("enableVibration") private var enableVibration = true
    @AppStorage("detectMascotCapsule") private var detectMascotCapsule = false

    var body: some View {
        Form {
            Picker(selection: $appTheme) {
                ForEach(AppTheme.allCases) { theme in
                    Text(theme.title).tag(theme.rawValue)
                }
            } label: {
                SettingsLabel(title: "Theme", systemImage: "paintpalette", tint: .red)
            }

            Toggle(isOn: $blackBackground) {
                SettingsLabel(title: "Black background", subtitle: "For the dark theme", systemImage: "circle.lefthalf.filled", tint: .black)
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

            Toggle(isOn: $rawScreenshot) {
                SettingsLabel(title: "Raw screenshot", subtitle: "Disable scaling and filtering for screenshots", systemImage: "camera.fill", tint: .orange)
            }

            Toggle(isOn: $enableVibration) {
                SettingsLabel(title: "Enable vibration", systemImage: "iphone.radiowaves.left.and.right", tint: .purple)
            }

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

            Section("Experimental/temporary options") {
                Toggle(isOn: $detectMascotCapsule) {
                    SettingsLabel(
                        title: "Detect Mascot Capsule 3D",
                        subtitle: "Show message when using",
                        systemImage: "message.fill",
                        tint: .indigo
                    )
                }
            }
        }
        .navigationTitle("Settings")
    }
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
            TextField("", text: $enteredName)
            Button("Cancel", role: .cancel) {}
            Button("OK") {
                if let template = templates.add(name: enteredName) {
                    editingTemplate = template
                }
            }
        }
        .alert("Enter new name", isPresented: renameDialogBinding) {
            TextField("", text: $enteredName)
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
