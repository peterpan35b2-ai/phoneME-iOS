import SwiftUI
#if canImport(UIKit)
import UIKit
#elseif canImport(AppKit)
import AppKit
#endif

struct EmulatorView: View {
    @Environment(\.dismiss) private var dismiss
    @EnvironmentObject private var library: GameLibrary
    @EnvironmentObject private var profiles: GameProfileStore
    @EnvironmentObject private var session: EmulatorSession

    @AppStorage("enableActionBar") private var enableActionBar = true
    @AppStorage("enableStatusBar") private var enableStatusBar = true
    @AppStorage("keepScreenOn") private var keepScreenOn = false
    @AppStorage("playNativeChromeDefaultApplied") private var playNativeChromeDefaultApplied = false

    let game: Game
    private let closeAction: (() -> Void)?

    @State private var runtimeProfile: GameProfile
    @State private var persistedProfile: GameProfile
    @State private var showKeypad: Bool
    @State private var keyboardAdjustmentMode: KeyboardAdjustmentMode = .none
    @State private var keyboardObscuresDisplay = false
    @State private var activeVirtualKeyCount = 0
    @State private var keyboardHideTask: Task<Void, Never>?
    @State private var keyboardChangeSnapshot: GameProfile?
    @State private var showSaveKeyboardChanges = false
    @State private var showKeyboardLayoutPicker = false
    @State private var showHiddenKeysEditor = false
    @State private var hiddenKeyDraft: Set<String> = []
    @State private var hiddenKeyChangesApplied = false
    @State private var showExitConfirmation = false
    @State private var showError = false
    @State private var errorMessage = ""
    @State private var hasStarted = false
    @State private var isClosing = false

    init(
        game: Game,
        profile: GameProfile,
        closeAction: (() -> Void)? = nil
    ) {
        let profile = profile.normalized()
        self.game = game
        self.closeAction = closeAction
        _runtimeProfile = State(initialValue: profile)
        _persistedProfile = State(initialValue: profile)
        _showKeypad = State(initialValue: profile.showVirtualKeyboard)
    }

    private var navigationTitle: String {
        guard session.isPresentingNativeLCDUI,
              session.lcdUI.screenKind == .list,
              let title = session.lcdUI.screen?.title.trimmingCharacters(
                  in: .whitespacesAndNewlines
              ),
              !title.isEmpty else {
            return game.title
        }
        return title
    }

    private var presentsFullscreenSurface: Bool {
        runtimeProfile.forceFullscreen
    }

    var body: some View {
        NavigationStack {
            GeometryReader { geometry in
                ZStack {
                    Color.playSurfaceBackground
                        .ignoresSafeArea()

                    if session.isPresentingNativeLCDUI {
                        NativeLCDUIScreenView(
                            imageStore: session.lcdUIImageStore,
                            state: session.lcdUI,
                            profile: runtimeProfile,
                            showsListTitleInContent: !enableActionBar
                                || runtimeProfile.forceFullscreen
                        )
                            .environmentObject(session)
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                    } else {
                        VStack(spacing: 0) {
                            GeometryReader { canvasGeometry in
                                let displayRect = FrameLayout.renderedFrameRect(
                                    frame: nil,
                                    availableSize: canvasGeometry.size,
                                    profile: runtimeProfile
                                )

                                ZStack {
                                    FrameSurface(
                                        frameStore: session.frameStore,
                                        profile: runtimeProfile
                                    )
                                        .environmentObject(session)
                                        .frame(maxWidth: .infinity, maxHeight: .infinity)

                                    if showKeypad {
                                        KeypadView(
                                            profile: $runtimeProfile,
                                            editMode: keyboardAdjustmentMode,
                                            layoutRect: keyboardFrame(
                                                in: canvasGeometry.size
                                            ),
                                            displayRect: displayRect,
                                            onKeyActivity: { active in
                                                handleVirtualKeyActivity(
                                                    active,
                                                    obscuresDisplay: keyboardObscuresDisplay
                                                )
                                            },
                                            onObscuresDisplayChange: updateKeyboardOverlap
                                        )
                                        .environmentObject(session)
                                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                                        .transition(.opacity)
                                    }
                                }
                            }

                            if session.lcdUI.isCanvasVisible {
                                LCDUICommandBar(state: session.lcdUI)
                                    .environmentObject(session)
                            }
                        }
                    }

                    if keyboardAdjustmentMode != .none,
                       !session.isPresentingNativeLCDUI {
                        VStack {
                            HStack(spacing: 10) {
                                Label(
                                    keyboardAdjustmentMode == .position
                                        ? "Move virtual keys"
                                        : "Resize virtual keys",
                                    systemImage: keyboardAdjustmentMode == .position
                                        ? "arrow.up.and.down.and.arrow.left.and.right"
                                        : "arrow.up.left.and.arrow.down.right"
                                )
                                .font(.callout.weight(.semibold))

                                Spacer()

                                Button("Done") {
                                    finishKeyboardAdjustment()
                                }
                                .buttonStyle(.borderedProminent)
                            }
                            .padding(10)
                            .background(.regularMaterial, in: Capsule())
                            .padding(.horizontal, 12)
                            .padding(.top, 8)

                            Spacer()
                        }
                    }

                    if runtimeProfile.showFPS,
                       session.state == .running,
                       !session.isPresentingNativeLCDUI {
                        Text(String(format: "%.1f FPS", session.framesPerSecond))
                            .font(.caption.monospacedDigit().weight(.semibold))
                            .padding(.horizontal, 8)
                            .padding(.vertical, 5)
                            .background(.regularMaterial, in: Capsule())
                            .frame(
                                maxWidth: .infinity,
                                maxHeight: .infinity,
                                alignment: .topTrailing
                            )
                            .padding(10)
                            .allowsHitTesting(false)
                    }

                    if session.state == .starting {
                        ProgressView()
                            .controlSize(.large)
                    }
                }
                .coordinateSpace(name: "emulatorSurface")
                .contentShape(Rectangle())
                .simultaneousGesture(backGesture)
            }
            .ignoresSafeArea(
                .container,
                edges: presentsFullscreenSurface ? .all : []
            )
#if os(iOS)
            .navigationTitle("")
            .navigationBarTitleDisplayMode(.inline)
            .toolbarBackground(.visible, for: .navigationBar)
            .toolbar(
                enableActionBar && !presentsFullscreenSurface
                    ? .visible
                    : .hidden,
                for: .navigationBar
            )
#else
            .navigationTitle(navigationTitle)
#endif
            .toolbar {
#if os(iOS)
                ToolbarItem(placement: .navigationBarLeading) {
                    Text(navigationTitle)
                        .font(.headline)
                        .lineLimit(1)
                        .truncationMode(.tail)
                        .accessibilityAddTraits(.isHeader)
                }
#endif

                ToolbarItemGroup(placement: .primaryAction) {
                    Button {
                        toggleKeyboard()
                    } label: {
                        Image(systemName: "keyboard")
                    }
                    .accessibilityLabel("Keyboard (IME)")
                    .disabled(session.isPresentingNativeLCDUI)

                    Button {
                        saveScreenshot()
                    } label: {
                        Image(systemName: "camera")
                    }
                    .accessibilityLabel("Take screenshot")

                    Menu {
                        Button("Exit") { showExitConfirmation = true }
                        Button("Save log") { saveLog() }
                        Button("Lock screen rotate") {}
                        Divider()
                        Button("Keyboard (IME)") { toggleKeyboard() }
                        Button("Take screenshot") { saveScreenshot() }
                        Button("Limit FPS") {}
                        Menu("Virtual keyboard") {
                            Button("Keylayout edit mode") {
                                beginKeyboardAdjustment(.position)
                            }
                            Button("Keylayout resize mode") {
                                beginKeyboardAdjustment(.size)
                            }
                            if keyboardAdjustmentMode != .none {
                                Button("Finish edit mode") {
                                    finishKeyboardAdjustment()
                                }
                            }
                            Button("Switch keylayout") {
                                showKeyboardLayoutPicker = true
                            }
                            .disabled(keyboardAdjustmentMode != .none)
                            Button("Hide buttons") {
                                openHiddenKeysEditor()
                            }
                            .disabled(keyboardAdjustmentMode != .none)
                        }
                    } label: {
                        Image(systemName: "ellipsis")
                    }
                }
            }
        }
#if os(iOS)
        .statusBarHidden(!enableStatusBar || presentsFullscreenSurface)
#endif
        .onAppear {
            if !playNativeChromeDefaultApplied {
                enableActionBar = true
                enableStatusBar = true
                playNativeChromeDefaultApplied = true
            }

            configureScreenAwake(true)
            applyPreferredOrientation(runtimeProfile.orientation)
            scheduleKeyboardAutoHide(obscuresDisplay: keyboardObscuresDisplay)
            guard !hasStarted else { return }
            hasStarted = true
            launch()
        }
        .onDisappear {
            keyboardHideTask?.cancel()
            profiles.save(persistedProfile, for: game)
            configureScreenAwake(false)
            resetPreferredOrientation()
            session.stop()
        }
        .onChange(of: showKeypad) { visible in
            if visible {
                scheduleKeyboardAutoHide(obscuresDisplay: keyboardObscuresDisplay)
            } else {
                keyboardHideTask?.cancel()
                activeVirtualKeyCount = 0
            }
        }
        .onChange(of: session.state) { state in
            switch state {
            case let .failed(message):
                errorMessage = message
                showError = true

            case .stopped where hasStarted && !isClosing:
                close(stopSession: false)

            default:
                break
            }
        }
        .confirmationDialog(
            "Switch keylayout",
            isPresented: $showKeyboardLayoutPicker,
            titleVisibility: .visible
        ) {
            ForEach(
                KeyboardLayoutCatalog.selectableLayouts(
                    hasCustomLayout: runtimeProfile.keyboardLayoutCustomization != nil
                        || runtimeProfile.keyboardCustomBaseType != nil
                )
            ) { layout in
                Button(layoutPickerTitle(layout)) {
                    selectKeyboardLayout(layout)
                }
            }
        }
        .sheet(
            isPresented: $showHiddenKeysEditor,
            onDismiss: hiddenKeysEditorDidDismiss
        ) {
            NavigationStack {
                KeyboardVisibilityEditor(
                    controls: KeyboardLayoutCatalog.controlChoices(for: runtimeProfile),
                    hiddenControlIDs: $hiddenKeyDraft,
                    applyAction: applyHiddenKeyChanges
                )
            }
        }
        .alert("Confirmation required", isPresented: $showSaveKeyboardChanges) {
            Button("Don't save", role: .cancel) {
                keepKeyboardChangesForSession()
            }
            Button("Save") {
                saveKeyboardChanges()
            }
        } message: {
            Text("Save the changed virtual keyboard layout for this game?")
        }
        .alert("Error", isPresented: $showError) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(errorMessage)
        }
        .alert("Confirmation required", isPresented: $showExitConfirmation) {
            Button("Cancel", role: .cancel) {}
            Button("Settings") {
                close()
            }
            Button("OK", role: .destructive) {
                close()
            }
        } message: {
            Text("Force-closing the application may result in data loss or even break it completely!\nProceed?")
        }
    }

    private var backGesture: some Gesture {
        DragGesture(minimumDistance: 24)
            .onEnded { value in
                guard value.startLocation.x < 32,
                      value.translation.width > 100,
                      abs(value.translation.height) < 80 else { return }
                showExitConfirmation = true
            }
    }

    private func controlsMaxWidth(for size: CGSize) -> CGFloat {
        max(size.width - 12, 0)
    }

    private func controlsHeight(for size: CGSize) -> CGFloat {
        size.width > size.height ? min(size.height * 0.50, 280) : min(size.height * 0.42, 320)
    }

    private func keyboardFrame(in size: CGSize) -> CGRect {
        let width = controlsMaxWidth(for: size)
        let height = controlsHeight(for: size)
        return CGRect(
            x: (size.width - width) / 2,
            y: size.height - height - 8,
            width: width,
            height: height
        )
    }

    private func updateKeyboardOverlap(_ obscuresDisplay: Bool) {
        keyboardObscuresDisplay = obscuresDisplay
        if showKeypad, activeVirtualKeyCount == 0 {
            scheduleKeyboardAutoHide(obscuresDisplay: obscuresDisplay)
        }
    }

    private func toggleKeyboard() {
        keyboardHideTask?.cancel()
        if keyboardAdjustmentMode != .none {
            finishKeyboardAdjustment()
            return
        }
        withAnimation(.easeInOut(duration: 0.15)) {
            showKeypad.toggle()
        }
    }

    private func handleVirtualKeyActivity(
        _ active: Bool,
        obscuresDisplay: Bool
    ) {
        if active {
            activeVirtualKeyCount += 1
            keyboardHideTask?.cancel()
        } else {
            activeVirtualKeyCount = max(0, activeVirtualKeyCount - 1)
            if activeVirtualKeyCount == 0 {
                scheduleKeyboardAutoHide(obscuresDisplay: obscuresDisplay)
            }
        }
    }

    private func scheduleKeyboardAutoHide(obscuresDisplay: Bool) {
        keyboardHideTask?.cancel()
        guard showKeypad,
              keyboardAdjustmentMode == .none,
              activeVirtualKeyCount == 0,
              obscuresDisplay,
              runtimeProfile.keyboardHideDelayMilliseconds > 0 else {
            return
        }

        let milliseconds = min(runtimeProfile.keyboardHideDelayMilliseconds, 60_000)
        keyboardHideTask = Task { @MainActor in
            try? await Task.sleep(nanoseconds: UInt64(milliseconds) * 1_000_000)
            guard !Task.isCancelled,
                  activeVirtualKeyCount == 0,
                  keyboardAdjustmentMode == .none else {
                return
            }
            withAnimation(.easeInOut(duration: 0.15)) {
                showKeypad = false
            }
        }
    }

    private func beginKeyboardAdjustment(_ mode: KeyboardAdjustmentMode) {
        keyboardHideTask?.cancel()
        if keyboardAdjustmentMode == .none {
            keyboardChangeSnapshot = runtimeProfile
            prepareKeyboardForEditing()
        }
        runtimeProfile.keyboardTuning = nil
        keyboardAdjustmentMode = mode
        withAnimation(.easeInOut(duration: 0.15)) {
            showKeypad = true
        }
    }

    private func finishKeyboardAdjustment() {
        guard keyboardAdjustmentMode != .none else { return }
        keyboardAdjustmentMode = .none
        showSaveKeyboardChanges = true
    }

    private func selectKeyboardLayout(_ layout: GameProfile.VirtualKeyboardType) {
        keyboardHideTask?.cancel()
        keyboardAdjustmentMode = .none
        runtimeProfile.virtualKeyboardType = layout
        if layout != .custom {
            runtimeProfile.keyboardTuning = nil
        }
        withAnimation(.easeInOut(duration: 0.15)) {
            showKeypad = true
        }
        scheduleKeyboardAutoHide(obscuresDisplay: keyboardObscuresDisplay)
    }

    private func openHiddenKeysEditor() {
        keyboardHideTask?.cancel()
        keyboardAdjustmentMode = .none
        keyboardChangeSnapshot = runtimeProfile
        prepareKeyboardForEditing()
        hiddenKeyDraft = runtimeProfile.effectiveKeyboardLayoutCustomization.hiddenControlIDs
        hiddenKeyChangesApplied = false
        showHiddenKeysEditor = true
        showKeypad = true
    }

    private func applyHiddenKeyChanges() {
        runtimeProfile.updateKeyboardLayoutCustomization { customization in
            customization.hiddenControlIDs = hiddenKeyDraft
        }
        hiddenKeyChangesApplied = true
        showHiddenKeysEditor = false
    }

    private func hiddenKeysEditorDidDismiss() {
        guard hiddenKeyChangesApplied else {
            if let snapshot = keyboardChangeSnapshot {
                runtimeProfile = snapshot
            }
            keyboardChangeSnapshot = nil
            return
        }
        hiddenKeyChangesApplied = false
        DispatchQueue.main.async {
            showSaveKeyboardChanges = true
        }
    }

    private func prepareKeyboardForEditing() {
        guard runtimeProfile.virtualKeyboardType != .custom else { return }
        let baseType = runtimeProfile.resolvedKeyboardBaseType
        runtimeProfile.keyboardLayoutCustomization = nil
        runtimeProfile.keyboardCustomBaseType = baseType
        runtimeProfile.virtualKeyboardType = .custom
    }

    private func saveKeyboardChanges() {
        let baseType = keyboardChangeSnapshot?.resolvedKeyboardBaseType
            ?? runtimeProfile.resolvedKeyboardBaseType
        runtimeProfile.makeKeyboardLayoutCustom(baseType: baseType)
        persistedProfile = runtimeProfile
        profiles.save(persistedProfile, for: game)
        keyboardChangeSnapshot = nil
        scheduleKeyboardAutoHide(obscuresDisplay: keyboardObscuresDisplay)
    }

    private func keepKeyboardChangesForSession() {
        keyboardChangeSnapshot = nil
        keyboardAdjustmentMode = .none
        scheduleKeyboardAutoHide(obscuresDisplay: keyboardObscuresDisplay)
    }

    private func layoutPickerTitle(_ layout: GameProfile.VirtualKeyboardType) -> String {
        let selected = runtimeProfile.virtualKeyboardType == layout
        return selected ? "✓ \(layout.title)" : layout.title
    }

    private func launch() {
        do {
            let jarURL = try library.prepareJarForLaunch(game)
            library.markPlayed(game)
            session.launch(
                game: game,
                jarURL: jarURL,
                profile: runtimeProfile
            )
        } catch {
            errorMessage = error.localizedDescription
            showError = true
        }
    }

    private func close(stopSession: Bool = true) {
        guard !isClosing else { return }
        isClosing = true
        keyboardHideTask?.cancel()
        profiles.save(persistedProfile, for: game)
        if stopSession {
            session.stop()
        }
        if let closeAction {
            closeAction()
        } else {
            dismiss()
        }
    }

    private func saveLog() {
        do {
            try library.saveLog()
        } catch {
            errorMessage = error.localizedDescription
            showError = true
        }
    }

    private func saveScreenshot() {
        guard session.frame != nil else { return }
    }

    private func configureScreenAwake(_ active: Bool) {
#if canImport(UIKit)
        UIApplication.shared.isIdleTimerDisabled = active && keepScreenOn
#endif
    }

    private func applyPreferredOrientation(_ orientation: GameProfile.Orientation) {
#if os(iOS)
        let mask: UIInterfaceOrientationMask
        switch orientation {
        case .defaultValue, .portrait:
            mask = .portrait
        case .auto:
            mask = .allButUpsideDown
        case .landscape:
            mask = .landscape
        }
        requestOrientation(mask)
#endif
    }

    private func resetPreferredOrientation() {
#if os(iOS)
        requestOrientation(.allButUpsideDown)
#endif
    }

#if os(iOS)
    private func requestOrientation(_ mask: UIInterfaceOrientationMask) {
        guard let scene = UIApplication.shared.connectedScenes
            .compactMap({ $0 as? UIWindowScene })
            .first(where: { $0.activationState == .foregroundActive }) else {
            return
        }

        scene.requestGeometryUpdate(
            .iOS(interfaceOrientations: mask)
        ) { _ in }
        scene.windows
            .first(where: \.isKeyWindow)?
            .rootViewController?
            .setNeedsUpdateOfSupportedInterfaceOrientations()
    }
#endif
}

private struct KeyboardVisibilityEditor: View {
    @Environment(\.dismiss) private var dismiss

    let controls: [KeyboardControlDescriptor]
    @Binding var hiddenControlIDs: Set<String>
    let applyAction: () -> Void

    var body: some View {
        List {
            ForEach(controls) { control in
                Toggle(
                    control.accessibilityLabel.capitalized,
                    isOn: visibilityBinding(for: control.id)
                )
            }
        }
        .navigationTitle("Hide buttons")
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
#endif
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("Cancel") {
                    dismiss()
                }
            }
            ToolbarItem(placement: .confirmationAction) {
                Button("OK", action: applyAction)
            }
        }
    }

    private func visibilityBinding(for id: String) -> Binding<Bool> {
        Binding(
            get: { !hiddenControlIDs.contains(id) },
            set: { visible in
                if visible {
                    hiddenControlIDs.remove(id)
                } else {
                    hiddenControlIDs.insert(id)
                }
            }
        )
    }
}

private struct FrameSurface: View {
    @EnvironmentObject private var session: EmulatorSession
    @ObservedObject var frameStore: EmulatorFrameStore

    let profile: GameProfile

    @State private var pointerIsDown = false

    var body: some View {
        GeometryReader { geometry in
            if let frame = frameStore.frame {
                let rect = FrameLayout.renderedFrameRect(
                    frame: frame,
                    availableSize: geometry.size,
                    profile: profile
                )

                ZStack(alignment: .topLeading) {
                    renderedFrame(frame)
                        .frame(
                            width: max(rect.width, 0),
                            height: max(rect.height, 0)
                        )
                        .offset(x: rect.minX, y: rect.minY)
                }
                .frame(
                    width: geometry.size.width,
                    height: geometry.size.height,
                    alignment: .topLeading
                )
                .clipped()
                .contentShape(Rectangle())
                .gesture(pointerGesture(
                    frame: frame,
                    availableSize: geometry.size
                ))
            }
        }
        .accessibilityLabel("J2ME display")
    }

    @ViewBuilder
    private func renderedFrame(_ frame: CGImage) -> some View {
        let image = Image(decorative: frame, scale: 1, orientation: .up)
            .resizable()
            .interpolation(profile.filtering ? .high : .none)

        switch profile.graphicsMode {
        case .software:
            image
        case .openGLES:
            image.drawingGroup(opaque: true, colorMode: .nonLinear)
        case .window:
            image.compositingGroup()
        }
    }

    private func pointerGesture(
        frame: CGImage,
        availableSize: CGSize
    ) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .local)
            .onChanged { value in
                guard profile.touchInput else { return }
                let action: Int32 = pointerIsDown ? 3 : 1
                guard let point = pointerPoint(
                    value.location,
                    frame: frame,
                    availableSize: availableSize,
                    clampOutside: pointerIsDown
                ) else {
                    return
                }

                pointerIsDown = true
                session.sendPointer(x: point.x, y: point.y, action: action)
            }
            .onEnded { value in
                defer { pointerIsDown = false }
                guard profile.touchInput,
                      pointerIsDown,
                      let point = pointerPoint(
                        value.location,
                        frame: frame,
                        availableSize: availableSize,
                        clampOutside: true
                      ) else {
                    return
                }

                session.sendPointer(x: point.x, y: point.y, action: 2)
            }
    }

    private func pointerPoint(
        _ location: CGPoint,
        frame: CGImage,
        availableSize: CGSize,
        clampOutside: Bool
    ) -> (x: Int32, y: Int32)? {
        let rect = FrameLayout.renderedFrameRect(
            frame: frame,
            availableSize: availableSize,
            profile: profile
        )
        guard rect.width > 0, rect.height > 0 else { return nil }
        guard clampOutside || rect.contains(location) else { return nil }

        let clampedX = min(max(location.x, rect.minX), rect.maxX)
        let clampedY = min(max(location.y, rect.minY), rect.maxY)
        let normalizedX = (clampedX - rect.minX) / rect.width
        let normalizedY = (clampedY - rect.minY) / rect.height
        let x = min(max(Int(normalizedX * CGFloat(frame.width)), 0), frame.width - 1)
        let y = min(max(Int(normalizedY * CGFloat(frame.height)), 0), frame.height - 1)
        return (Int32(x), Int32(y))
    }

}

private enum FrameLayout {
    static func renderedFrameRect(
        frame: CGImage?,
        availableSize: CGSize,
        profile: GameProfile
    ) -> CGRect {
        let frameSize = frame.map {
            CGSize(width: $0.width, height: $0.height)
        } ?? CGSize(
            width: max(profile.screenWidth, 1),
            height: max(profile.screenHeight, 1)
        )
        return renderedFrameRect(
            frameSize: frameSize,
            availableSize: availableSize,
            profile: profile
        )
    }

    static func renderedFrameRect(
        frame: CGImage,
        availableSize: CGSize,
        profile: GameProfile
    ) -> CGRect {
        renderedFrameRect(
            frameSize: CGSize(width: frame.width, height: frame.height),
            availableSize: availableSize,
            profile: profile
        )
    }

    private static func renderedFrameRect(
        frameSize: CGSize,
        availableSize: CGSize,
        profile: GameProfile
    ) -> CGRect {
        let renderedSize: CGSize

        switch profile.scaleType {
        case .asIs:
            let scale = CGFloat(profile.scalePercent) / 100
            renderedSize = CGSize(
                width: frameSize.width * scale,
                height: frameSize.height * scale
            )
        case .fit:
            let requestedScale = max(CGFloat(profile.scalePercent) / 100, 0.01)
            if profile.preserveAspectRatio {
                let widthScale = availableSize.width / max(frameSize.width, 1)
                let heightScale = availableSize.height / max(frameSize.height, 1)
                let shouldFitWidth = profile.screenGravity == .top
                    && frameSize.height >= frameSize.width
                    && availableSize.height >= availableSize.width
                let fitScale = shouldFitWidth
                    ? widthScale
                    : min(widthScale, heightScale)
                let scale = fitScale * requestedScale
                renderedSize = CGSize(
                    width: frameSize.width * scale,
                    height: frameSize.height * scale
                )
            } else {
                renderedSize = CGSize(
                    width: availableSize.width * requestedScale,
                    height: availableSize.height * requestedScale
                )
            }
        case .fill:
            if profile.preserveAspectRatio {
                let fillScale = max(
                    availableSize.width / max(frameSize.width, 1),
                    availableSize.height / max(frameSize.height, 1)
                )
                renderedSize = CGSize(
                    width: frameSize.width * fillScale,
                    height: frameSize.height * fillScale
                )
            } else {
                renderedSize = availableSize
            }
        }

        return CGRect(
            origin: alignedOrigin(
                renderedSize: renderedSize,
                availableSize: availableSize,
                gravity: profile.screenGravity
            ),
            size: renderedSize
        )
    }

    private static func alignedOrigin(
        renderedSize: CGSize,
        availableSize: CGSize,
        gravity: GameProfile.ScreenGravity
    ) -> CGPoint {
        switch gravity {
        case .left:
            return CGPoint(
                x: 0,
                y: (availableSize.height - renderedSize.height) / 2
            )
        case .top:
            return CGPoint(
                x: (availableSize.width - renderedSize.width) / 2,
                y: 0
            )
        case .center:
            return CGPoint(
                x: (availableSize.width - renderedSize.width) / 2,
                y: (availableSize.height - renderedSize.height) / 2
            )
        case .right:
            return CGPoint(
                x: availableSize.width - renderedSize.width,
                y: (availableSize.height - renderedSize.height) / 2
            )
        case .bottom:
            return CGPoint(
                x: (availableSize.width - renderedSize.width) / 2,
                y: availableSize.height - renderedSize.height
            )
        }
    }
}

private extension Color {
    static var playSurfaceBackground: Color {
#if canImport(UIKit)
        Color(uiColor: .systemBackground)
#elseif canImport(AppKit)
        Color(nsColor: .windowBackgroundColor)
#else
        Color.white
#endif
    }
}
