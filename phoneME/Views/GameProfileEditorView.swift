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
        ScrollView {
            VStack(spacing: 10) {
                screenOptions
                fontOptions
                inputDevices
            }
            .padding(8)
        }
        .background(Color.phoneMEConfigBackground)
        .navigationTitle(title)
        .toolbar {
            ToolbarItemGroup(placement: .primaryAction) {
                if game != nil {
                    Button("Start") {
                        persistProfile()
                        startAction?()
                    }
                }

                Menu {
                    if game != nil {
                        Button("Clear data") {}
                    }
                    Button("Reset settings") {
                        profile = .default
                    }
                    Button("Reset keylayout") {
                        profile.keyLayout = .nokiaSE
                        profile.resetCustomKeyMappings()
                        profile.virtualKeyboardType = .arrowsNumbers
                        profile.resetKeyboardLayoutCustomization()
                    }
                    Divider()
                    Button("Load profile") {}
                    Button("Save profile") {}
                } label: {
                    Image(systemName: "ellipsis")
                }
            }
        }
        .onDisappear {
            persistProfile()
        }
        .confirmationDialog("Presets", isPresented: $showScreenPresets, titleVisibility: .visible) {
            ForEach(Array(GameProfile.screenPresets.enumerated()), id: \.offset) { _, preset in
                Button("\(preset.width) x \(preset.height)") {
                    profile.screenWidth = preset.width
                    profile.screenHeight = preset.height
                }
            }
        }
        .confirmationDialog("Presets", isPresented: $showFontPresets, titleVisibility: .visible) {
            Button("128 x 128") { setFontPreset(9, 13, 15) }
            Button("128 x 160") { setFontPreset(13, 15, 20) }
            Button("176 x 220") { setFontPreset(15, 18, 22) }
            Button("240 x 320") { setFontPreset(18, 22, 26) }
            Button("360 x 640") { setFontPreset(22, 26, 30) }
        }
        .sheet(isPresented: $showKeyMappings) {
            NavigationStack {
                KeyMappingsView(profile: $profile)
            }
        }
    }

    private var screenOptions: some View {
        ConfigCard(title: "Display options") {
            HStack(spacing: 10) {
                Button {
                    showScreenPresets = true
                } label: {
                    Image(systemName: "list.bullet")
                        .frame(width: 34, height: 34)
                }
                .buttonStyle(.borderless)

                IntegerTextField(value: $profile.screenWidth, placeholder: "Width")

                Button {
                    let width = profile.screenWidth
                    profile.screenWidth = profile.screenHeight
                    profile.screenHeight = width
                } label: {
                    Image(systemName: "arrow.left.arrow.right")
                        .frame(width: 34, height: 34)
                }
                .buttonStyle(.borderless)

                IntegerTextField(value: $profile.screenHeight, placeholder: "Height")

                Button {
                    showScreenPresets = true
                } label: {
                    Image(systemName: "text.badge.plus")
                        .frame(width: 34, height: 34)
                }
                .buttonStyle(.borderless)
            }

            Toggle("Keep Canvas aspect ratio", isOn: $profile.preserveAspectRatio)

            HStack {
                Text("Canvas scale (%)")
                Slider(value: Binding(
                    get: { Double(profile.scalePercent) },
                    set: { profile.scalePercent = Int($0) }
                ), in: 10...300, step: 1)
                Text("\(profile.scalePercent)")
                    .monospacedDigit()
                    .frame(width: 38, alignment: .trailing)
            }

            ConfigPicker("Screen orientation", selection: $profile.orientation)
            ConfigPicker("Canvas gravity", selection: $profile.screenGravity)
            ConfigPicker("Canvas scale type", selection: $profile.scaleType)

            Toggle("Filter", isOn: $profile.filtering)
            Toggle("Immediate processing mode", isOn: $profile.immediateProcessing)
            ConfigPicker("Graphics mode:", selection: $profile.graphicsMode)
            Toggle("Parallel screen redrawing", isOn: $profile.parallelScreenRedrawing)
            Toggle("Force Canvas fullscreen", isOn: $profile.forceFullscreen)
            Toggle("Show FPS", isOn: $profile.showFPS)

            HStack {
                Text("Limit FPS")
                Spacer()
                IntegerTextField(
                    value: $profile.frameRateLimit,
                    placeholder: "unlimited",
                    width: 110
                )
            }
        }
    }

    private var fontOptions: some View {
        ConfigCard(title: "Font options") {
            HStack(spacing: 8) {
                IntegerTextField(value: $profile.fontSmall, placeholder: "Small")
                Text("-")
                IntegerTextField(value: $profile.fontMedium, placeholder: "Medium")
                Text("-")
                IntegerTextField(value: $profile.fontLarge, placeholder: "Large")
            }

            Button("Presets") {
                showFontPresets = true
            }
            .frame(maxWidth: .infinity)
            .buttonStyle(.bordered)

            Toggle("Values are in Scaled Pixels", isOn: $profile.fontValuesAreScaledPixels)
            Toggle("Anti-Aliasing", isOn: $profile.fontAntialiasing)
        }
    }

    private var inputDevices: some View {
        ConfigCard(title: "Input devices") {
            Toggle("Touch input", isOn: $profile.touchInput)
            ConfigPicker("Layout", selection: $profile.keyLayout)

            Button("Key mappings") {
                showKeyMappings = true
            }
            .frame(maxWidth: .infinity)
            .buttonStyle(.bordered)

            Toggle("Virtual keyboard", isOn: $profile.showVirtualKeyboard)
            ConfigPicker("Virtual keyboard", selection: $profile.virtualKeyboardType)
                .disabled(!profile.showVirtualKeyboard)
            ConfigPicker("Button shape", selection: $profile.buttonShape)
                .disabled(!profile.showVirtualKeyboard)
            Toggle("Haptic feedback", isOn: $profile.hapticFeedback)
                .disabled(!profile.showVirtualKeyboard)

            HStack {
                Text("Opacity")
                Slider(value: $profile.keyboardOpacity, in: 0.05...1)
                Text("\(Int(profile.keyboardOpacity * 100))%")
                    .monospacedDigit()
                    .frame(width: 48, alignment: .trailing)
            }
            .disabled(!profile.showVirtualKeyboard)

            Toggle(
                "Force opacity for off-screen keys",
                isOn: $profile.forceOpacityForOffscreenKeys
            )
            .disabled(!profile.showVirtualKeyboard)

            HStack {
                Text("Hide delay: ")
                Spacer()
                IntegerTextField(
                    value: $profile.keyboardHideDelayMilliseconds,
                    placeholder: "",
                    width: 100
                )
                Text("ms")
            }
            .disabled(!profile.showVirtualKeyboard)

            ColorValueRow(title: "Labels", value: $profile.keyboardForegroundHex)
            ColorValueRow(title: "Buttons", value: $profile.keyboardBackgroundHex)
            ColorValueRow(title: "Labels (P)", value: $profile.keyboardSelectedForegroundHex)
            ColorValueRow(title: "Buttons (P)", value: $profile.keyboardSelectedBackgroundHex)
            ColorValueRow(title: "Outline", value: $profile.keyboardOutlineHex)
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

private struct ConfigCard<Content: View>: View {
    let title: String
    @ViewBuilder let content: Content

    init(title: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        VStack(spacing: 0) {
            Text(title)
                .font(.title3)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)

            VStack(spacing: 12) {
                content
            }
            .padding(10)
        }
        .background(Color.phoneMECardBackground)
        .overlay {
            RoundedRectangle(cornerRadius: 8)
                .stroke(Color.accentColor.opacity(0.65), lineWidth: 1)
        }
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
}

private struct ConfigPicker<Value>: View where Value: Hashable & CaseIterable & Identifiable, Value.AllCases: RandomAccessCollection, Value: RawRepresentable, Value.RawValue == String {
    let title: String
    @Binding var selection: Value

    init(_ title: String, selection: Binding<Value>) {
        self.title = title
        _selection = selection
    }

    var body: some View {
        HStack {
            Text(title)
            Spacer()
            Picker(title, selection: $selection) {
                ForEach(Array(Value.allCases)) { value in
                    Text(displayTitle(value)).tag(value)
                }
            }
            .labelsHidden()
        }
    }

    private func displayTitle(_ value: Value) -> String {
        switch value {
        case let value as GameProfile.Orientation: return value.title
        case let value as GameProfile.ScreenGravity: return value.title
        case let value as GameProfile.ScaleType: return value.title
        case let value as GameProfile.GraphicsMode: return value.title
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
            .multilineTextAlignment(.center)
            .textFieldStyle(.roundedBorder)
            .frame(maxWidth: width ?? .infinity)
#if os(iOS)
            .keyboardType(.numberPad)
#endif
    }
}

private struct ColorValueRow: View {
    let title: String
    @Binding var value: String

    var body: some View {
        HStack {
            Text(title)
            Spacer()
            TextField("000000", text: $value)
                .multilineTextAlignment(.trailing)
                .textFieldStyle(.roundedBorder)
                .monospaced()
                .frame(width: 100)
            RoundedRectangle(cornerRadius: 3)
                .fill(Color(hex: value) ?? .clear)
                .frame(width: 30, height: 30)
        }
    }
}

private struct KeyMappingsView: View {
    @Environment(\.dismiss) private var dismiss
    @Binding var profile: GameProfile

    var body: some View {
        List {
            Picker("Layout", selection: $profile.keyLayout) {
                ForEach(GameProfile.KeyLayout.allCases) { layout in
                    Text(layout.title).tag(layout)
                }
            }

            Section("MIDP key codes") {
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
            }

            if profile.keyLayout == .custom {
                Button("Reset custom mappings") {
                    profile.resetCustomKeyMappings()
                }
            }
        }
        .navigationTitle("Key mappings")
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

private extension Color {
    static var phoneMEConfigBackground: Color {
#if os(macOS)
        Color(nsColor: .windowBackgroundColor)
#else
        Color(uiColor: .systemGroupedBackground)
#endif
    }

    static var phoneMECardBackground: Color {
#if os(macOS)
        Color(nsColor: .controlBackgroundColor)
#else
        Color(uiColor: .secondarySystemGroupedBackground)
#endif
    }

    init?(hex: String) {
        let cleaned = hex.trimmingCharacters(in: CharacterSet.alphanumerics.inverted)
        guard cleaned.count == 6, let value = UInt64(cleaned, radix: 16) else { return nil }
        self.init(
            red: Double((value >> 16) & 0xFF) / 255,
            green: Double((value >> 8) & 0xFF) / 255,
            blue: Double(value & 0xFF) / 255
        )
    }
}
