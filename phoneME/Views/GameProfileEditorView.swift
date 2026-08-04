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
        .phoneMEScrollContentBackgroundHidden()
        .background(Color.phoneMEAppBackground)
        .frame(maxWidth: PhoneMEVisualMetrics.contentMaxWidth)
        .frame(maxWidth: .infinity)
        .navigationTitle(title)
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
#endif
        .toolbar {
            ToolbarItemGroup(placement: .primaryAction) {
                if game != nil {
                    Button {
                        persistProfile()
                        startAction?()
                    } label: {
                        Label("Start", systemImage: "play.fill")
                    }
                }

                Menu {
                    Button {
                        profile = .default
                    } label: {
                        Label("Reset settings", systemImage: "arrow.counterclockwise")
                    }
                    Button {
                        profile.keyLayout = .nokiaSE
                        profile.resetCustomKeyMappings()
                        profile.virtualKeyboardType = .arrowsNumbers
                        profile.resetKeyboardLayoutCustomization()
                    } label: {
                        Label("Reset key layout", systemImage: "keyboard")
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
                .accessibilityLabel("More")
            }
        }
        .onDisappear {
            persistProfile()
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

            Text("Canvas output supports up to 60 FPS. Enter 0 to use the 60 FPS default.")
                .font(.footnote)
                .foregroundStyle(.secondary)
        } header: {
            PhoneMESectionTitle(
                title: "Display",
                subtitle: "Canvas size, scaling and rendering"
            )
        }
        .listRowBackground(Color.phoneMECardBackground)
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
            PhoneMESectionTitle(
                title: "Fonts",
                subtitle: "Native LCDUI and Canvas text sizes"
            )
        }
        .listRowBackground(Color.phoneMECardBackground)
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
            PhoneMESectionTitle(
                title: "Input",
                subtitle: "Touch, key mapping and virtual controls"
            )
        }
        .listRowBackground(Color.phoneMECardBackground)
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
    let title: String
    @Binding var selection: Value

    init(_ title: String, selection: Binding<Value>) {
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
    let placeholder: String
    var width: CGFloat? = nil

    var body: some View {
        TextField(placeholder, value: $value, format: .number)
            .foregroundStyle(.primary)
            .tint(.accentColor)
            .multilineTextAlignment(.center)
            .textFieldStyle(.plain)
            .padding(.horizontal, 10)
            .frame(maxWidth: width ?? .infinity, minHeight: 36)
            .background(Color.phoneMEControlBackground)
            .clipShape(
                RoundedRectangle(
                    cornerRadius: PhoneMEVisualMetrics.controlCornerRadius,
                    style: .continuous
                )
            )
            .overlay {
                RoundedRectangle(
                    cornerRadius: PhoneMEVisualMetrics.controlCornerRadius,
                    style: .continuous
                )
                .stroke(Color.phoneMEHairline, lineWidth: 0.5)
            }
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
                PhoneMESectionTitle(title: "Layout")
            }
            .listRowBackground(Color.phoneMECardBackground)

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
                PhoneMESectionTitle(
                    title: "MIDP key codes",
                    subtitle: profile.keyLayout == .custom
                        ? "Enter the Java ME key code for each control."
                        : "Switch to Custom to edit these values."
                )
            }
            .listRowBackground(Color.phoneMECardBackground)

            if profile.keyLayout == .custom {
                Section {
                    Button("Reset custom mappings", role: .destructive) {
                        profile.resetCustomKeyMappings()
                    }
                }
                .listRowBackground(Color.phoneMECardBackground)
            }
        }
        .phoneMEScrollContentBackgroundHidden()
        .background(Color.phoneMEAppBackground)
        .frame(maxWidth: PhoneMEVisualMetrics.contentMaxWidth)
        .frame(maxWidth: .infinity)
        .navigationTitle("Key mappings")
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
