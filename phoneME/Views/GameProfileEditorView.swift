import SwiftUI

struct GameProfileEditorView: View {
    @Environment(\.dismiss) private var dismiss
    @EnvironmentObject private var profiles: GameProfileStore

    let title: String
    let game: Game?
    let initialProfile: GameProfile
    let saveAction: ((GameProfile) -> Void)?
    let startAction: (() -> Void)?

    @State private var profile: GameProfile
    @State private var showScreenPresets = false
    @State private var showFontPresets = false
    @State private var showKeyMappings = false

    init(game: Game, initialProfile: GameProfile, startAction: @escaping () -> Void) {
        title = game.title
        self.game = game
        self.initialProfile = initialProfile
        saveAction = nil
        self.startAction = startAction
        _profile = State(initialValue: initialProfile)
    }

    init(
        profileName: String,
        initialProfile: GameProfile,
        saveAction: @escaping (GameProfile) -> Void
    ) {
        title = profileName
        game = nil
        self.initialProfile = initialProfile
        self.saveAction = saveAction
        startAction = nil
        _profile = State(initialValue: initialProfile)
    }

    var body: some View {
        Form {
            displaySection
            fontSection
            inputSection
        }
        .navigationTitle(title)
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
#endif
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("Cancel") {
                    profile = initialProfile
                    dismiss()
                }
            }

            ToolbarItemGroup(placement: .primaryAction) {
                Menu {
                    Button {
                        profile = .default
                    } label: {
                        Label("Reset Settings", systemImage: "arrow.counterclockwise")
                    }
                    Button {
                        profile.keyLayout = .nokiaSE
                        profile.resetCustomKeyMappings()
                        profile.virtualKeyboardType = .arrowsNumbers
                        profile.resetKeyboardLayoutCustomization()
                    } label: {
                        Label("Reset Key Layout", systemImage: "keyboard")
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
                .accessibilityLabel("More options")

                Button {
                    persistProfile()
                    startAction?()
                    dismiss()
                } label: {
                    if game != nil {
                        Label("Start", systemImage: "play.fill")
                    } else {
                        Text("Done")
                    }
                }
            }
        }
        .confirmationDialog(
            "Screen size",
            isPresented: $showScreenPresets,
            titleVisibility: .visible
        ) {
            ForEach(GameProfile.screenPresets, id: \.height) { preset in
                Button("\(preset.width) × \(preset.height)") {
                    profile.screenWidth = preset.width
                    profile.screenHeight = preset.height
                }
            }
        }
        .confirmationDialog(
            "Font size preset",
            isPresented: $showFontPresets,
            titleVisibility: .visible
        ) {
            Button("128 × 128") { setFontPreset(9, 13, 15) }
            Button("128 × 160") { setFontPreset(13, 15, 20) }
            Button("176 × 220") { setFontPreset(15, 18, 22) }
            Button("240 × 320") { setFontPreset(18, 22, 26) }
            Button("360 × 640") { setFontPreset(22, 26, 30) }
        }
        .sheet(isPresented: $showKeyMappings) {
            PhoneMENavigationStack {
                KeyMappingsView(profile: $profile)
            }
        }
    }

    private var displaySection: some View {
        Section {
            HStack(spacing: 10) {
                IntegerTextField(value: $profile.screenWidth, placeholder: "Width")
                Text("×")
                    .foregroundStyle(.secondary)
                IntegerTextField(value: $profile.screenHeight, placeholder: "Height")

                Button {
                    let width = profile.screenWidth
                    profile.screenWidth = profile.screenHeight
                    profile.screenHeight = width
                } label: {
                    Image(systemName: "arrow.left.arrow.right")
                }
                .buttonStyle(.borderless)
                .accessibilityLabel("Swap width and height")
            }

            Button {
                showScreenPresets = true
            } label: {
                Label("Screen size presets", systemImage: "rectangle.on.rectangle")
            }

            Toggle("Keep Canvas aspect ratio", isOn: $profile.preserveAspectRatio)

            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("Canvas scale")
                    Spacer()
                    Text("\(profile.scalePercent)%")
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                Slider(
                    value: Binding(
                        get: { Double(profile.scalePercent) },
                        set: { profile.scalePercent = Int($0.rounded()) }
                    ),
                    in: 10...300,
                    step: 1
                )
            }

            NativeProfilePicker("Screen orientation", selection: $profile.orientation)
            NativeProfilePicker("Canvas gravity", selection: $profile.screenGravity)
            NativeProfilePicker("Canvas scale type", selection: $profile.scaleType)

            Toggle("Image filtering", isOn: $profile.filtering)
            Toggle("Force Canvas fullscreen", isOn: $profile.forceFullscreen)
            Toggle("Show FPS", isOn: $profile.showFPS)

            HStack {
                Text("Frame rate limit")
                Spacer()
                IntegerTextField(
                    value: $profile.frameRateLimit,
                    placeholder: "Auto (60)",
                    width: 108
                )
            }

        } header: {
            Text("Display")
        } footer: {
            Text("Configure Canvas size, scaling and rendering. Frame rate supports up to 60 FPS; enter 0 to use the default.")
        }
    }

    private var fontSection: some View {
        Section {
            HStack(spacing: 8) {
                IntegerTextField(value: $profile.fontSmall, placeholder: "Small")
                IntegerTextField(value: $profile.fontMedium, placeholder: "Medium")
                IntegerTextField(value: $profile.fontLarge, placeholder: "Large")
            }

            Button {
                showFontPresets = true
            } label: {
                Label("Font size presets", systemImage: "textformat.size")
            }

            Toggle(
                "Use Dynamic Type scaling",
                isOn: $profile.fontValuesAreScaledPixels
            )
        } header: {
            Text("Fonts")
        } footer: {
            Text("Font sizes apply to native LCDUI and Canvas text.")
        }
    }

    private var inputSection: some View {
        Section {
            Toggle("Touch input", isOn: $profile.touchInput)
            NativeProfilePicker("J2ME key layout", selection: $profile.keyLayout)

            Button {
                showKeyMappings = true
            } label: {
                Label("Key mappings", systemImage: "keyboard.badge.ellipsis")
            }

            Toggle("Virtual keyboard", isOn: $profile.showVirtualKeyboard)

            NativeProfilePicker(
                "Virtual keyboard layout",
                selection: $profile.virtualKeyboardType
            )
            .disabled(!profile.showVirtualKeyboard)

            NativeProfilePicker("Button shape", selection: $profile.buttonShape)
                .disabled(!profile.showVirtualKeyboard)

            Toggle("Haptic feedback", isOn: $profile.hapticFeedback)
                .disabled(!profile.showVirtualKeyboard)

            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text("Keyboard opacity")
                    Spacer()
                    Text("\(Int((profile.keyboardOpacity * 100).rounded()))%")
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                Slider(value: $profile.keyboardOpacity, in: 0.05...1, step: 0.05)
            }
            .disabled(!profile.showVirtualKeyboard)

            Toggle(
                "Keep off-screen keys fully visible",
                isOn: $profile.forceOpacityForOffscreenKeys
            )
            .disabled(!profile.showVirtualKeyboard)

            HStack {
                Text("Auto-hide delay")
                Spacer()
                IntegerTextField(
                    value: $profile.keyboardHideDelayMilliseconds,
                    placeholder: "0",
                    width: 88
                )
                Text("ms")
                    .foregroundStyle(.secondary)
            }
            .disabled(!profile.showVirtualKeyboard)
        } header: {
            Text("Input")
        } footer: {
            Text("Configure touch input, MIDP key mappings and virtual controls.")
        }
    }

    private func persistProfile() {
        profile.normalize()
        if let game {
            profiles.save(profile, for: game)
        } else {
            saveAction?(profile)
        }
    }

    private func setFontPreset(_ small: Int, _ medium: Int, _ large: Int) {
        profile.fontSmall = small
        profile.fontMedium = medium
        profile.fontLarge = large
    }
}

private struct NativeProfilePicker<Value>: View where
    Value: Hashable & CaseIterable & Identifiable & RawRepresentable,
    Value.AllCases: RandomAccessCollection,
    Value.RawValue == String
{
    let title: LocalizedStringKey
    @Binding var selection: Value

    init(_ title: LocalizedStringKey, selection: Binding<Value>) {
        self.title = title
        _selection = selection
    }

    var body: some View {
        Picker(title, selection: $selection) {
            ForEach(Array(Value.allCases)) { value in
                Text(displayTitle(value)).tag(value)
            }
        }
    }

    private func displayTitle(_ value: Value) -> String {
        switch value {
        case let value as GameProfile.Orientation: return value.title
        case let value as GameProfile.ScreenGravity: return value.title
        case let value as GameProfile.ScaleType: return value.title
        case let value as GameProfile.KeyLayout: return value.title
        case let value as GameProfile.VirtualKeyboardType: return value.title
        case let value as GameProfile.ButtonShape: return value.title
        default: return value.rawValue
        }
    }
}

private struct IntegerTextField: View {
    @Binding var value: Int
    let placeholder: LocalizedStringKey
    var width: CGFloat? = nil

    var body: some View {
        TextField(placeholder, value: $value, format: .number)
            .multilineTextAlignment(.center)
            .textFieldStyle(.roundedBorder)
            .frame(maxWidth: width ?? .infinity)
#if os(iOS)
            .keyboardType(.numberPad)
#endif
    }
}

private struct KeyMappingsView: View {
    @Environment(\.dismiss) private var dismiss
    @Binding var profile: GameProfile

    var body: some View {
        Form {
            Section {
                Picker("Layout", selection: $profile.keyLayout) {
                    ForEach(GameProfile.KeyLayout.allCases) { layout in
                        Text(layout.title).tag(layout)
                    }
                }
            } header: {
                Text("Layout")
            }

            Section {
                ForEach(J2MEKey.configurableKeys) { key in
                    HStack {
                        Text(key.mappingTitle)
                        Spacer()
                        if profile.keyLayout == .custom {
                            TextField(
                                key.mappingTitle,
                                value: customBinding(for: key),
                                format: .number
                            )
                            .foregroundStyle(.primary)
                            .tint(.accentColor)
                            .multilineTextAlignment(.trailing)
                            .textFieldStyle(.roundedBorder)
                            .frame(width: 110)
#if os(iOS)
                            .keyboardType(.numbersAndPunctuation)
#endif
                        } else {
                            Text("\(profile.keyCode(for: key))")
                                .foregroundStyle(.secondary)
                                .monospacedDigit()
                        }
                    }
                }
            } header: {
                Text("MIDP Key Codes")
            } footer: {
                Text(
                    profile.keyLayout == .custom
                        ? L10n.string(
                            "Enter the Java ME key code for each control."
                        )
                        : L10n.string("Choose Custom to edit these values.")
                )
            }

            if profile.keyLayout == .custom {
                Section {
                    Button("Reset custom mappings", role: .destructive) {
                        profile.resetCustomKeyMappings()
                    }
                }
            }
        }
        .navigationTitle("Key Mappings")
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
#endif
        .toolbar {
            ToolbarItem(placement: .confirmationAction) {
                Button("Done") { dismiss() }
            }
        }
    }

    private func customBinding(for key: J2MEKey) -> Binding<Int> {
        Binding(
            get: { Int(profile.keyCode(for: key)) },
            set: { newValue in
                let clamped = Int32(clamping: newValue)
                if clamped != 0 {
                    profile.setCustomKeyCode(clamped, for: key)
                }
            }
        )
    }
}
