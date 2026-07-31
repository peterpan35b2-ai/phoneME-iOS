import CoreGraphics
import Foundation

@MainActor
final class EmulatorFrameStore: ObservableObject {
    @Published private(set) var frame: CGImage?

    fileprivate func update(_ frame: CGImage) {
        self.frame = frame
    }

    fileprivate func reset() {
        frame = nil
    }
}

@MainActor
final class LCDUIImageStore: ObservableObject {
    @Published private(set) var images: [Int32: CGImage] = [:]

    subscript(componentID: Int32) -> CGImage? {
        images[componentID]
    }

    fileprivate func replace(with images: [Int32: CGImage]) {
        self.images = images
    }

    fileprivate func reset() {
        images.removeAll(keepingCapacity: true)
    }
}

enum EmulatorPresentationMode: Equatable, Sendable {
    case framebuffer
    case nativeLCDUI
}

@MainActor
final class EmulatorSession: ObservableObject {
    @Published private(set) var state: EmulatorState = .idle
    @Published private(set) var presentationMode: EmulatorPresentationMode = .framebuffer
    @Published private(set) var lcdUI: LCDUIState = .empty
    @Published private(set) var currentGame: Game?
    @Published private(set) var framesPerSecond: Double = 0

    let frameStore = EmulatorFrameStore()
    let lcdUIImageStore = LCDUIImageStore()
    var frame: CGImage? { frameStore.frame }
    var lcdUIImages: [Int32: CGImage] { lcdUIImageStore.images }

    var isPresentingNativeLCDUI: Bool {
        presentationMode == .nativeLCDUI && lcdUI.hasNativeScreen
    }

    private let engine: EmbeddedPhoneMEEngine

    init(engine: EmbeddedPhoneMEEngine? = nil) {
        let resolvedEngine = engine ?? EmbeddedPhoneMEEngine()
        self.engine = resolvedEngine

        resolvedEngine.onFrame = { [weak self] frame in
            // Framebuffer generations may continue briefly while phoneME is
            // switching Displayables. Only SCREEN_SHOWN is authoritative;
            // otherwise a stale Canvas refresh can cover a native Form/List.
            self?.frameStore.update(frame)
        }
        resolvedEngine.onLCDUIEvents = { [weak self] events in
            guard let self else { return }
            var nextState = self.lcdUI
            var nextImages = self.lcdUIImageStore.images
            var imagesChanged = false
            var nextPresentationMode = self.presentationMode
            for event in events {
                nextState.apply(event)
                if event.arguments.3 == -1004 {
                    imagesChanged = nextImages.removeValue(
                        forKey: event.componentID
                    ) != nil || imagesChanged
                } else if event.kind == 12, event.arguments.3 < 0 {
                    imagesChanged = nextImages.removeValue(
                        forKey: event.arguments.3
                    ) != nil || imagesChanged
                }
                switch event.kind {
                case 1:
                    imagesChanged = !nextImages.isEmpty || imagesChanged
                    nextImages.removeAll(keepingCapacity: true)
                    nextPresentationMode = .framebuffer

                case 4:
                    nextPresentationMode = event.componentType
                        == LCDUIState.ComponentType.canvas.rawValue
                        ? .framebuffer
                        : .nativeLCDUI

                case 5, 6:
                    if !nextState.hasNativeScreen && !nextState.isCanvasVisible {
                        nextPresentationMode = .framebuffer
                    }
                    let filteredImages = nextImages.filter {
                        nextState.items[$0.key] != nil
                    }
                    imagesChanged = filteredImages.count != nextImages.count
                        || imagesChanged
                    nextImages = filteredImages

                case 11:
                    imagesChanged = nextImages.removeValue(
                        forKey: event.componentID
                    ) != nil || imagesChanged
                    let filteredImages = nextImages.filter {
                        !Self.isChoiceImageKey(
                            $0.key,
                            componentID: event.componentID
                        )
                    }
                    imagesChanged = filteredImages.count != nextImages.count
                        || imagesChanged
                    nextImages = filteredImages

                case 13:
                    let filteredImages = nextImages.filter {
                        !Self.isChoiceImageKey(
                            $0.key,
                            componentID: event.componentID
                        )
                    }
                    imagesChanged = filteredImages.count != nextImages.count
                        || imagesChanged
                    nextImages = filteredImages

                default:
                    break
                }
            }

            if nextState.hasNativeScreen {
                nextPresentationMode = .nativeLCDUI
            } else if nextState.isCanvasVisible {
                nextPresentationMode = .framebuffer
            } else if nextPresentationMode == .nativeLCDUI {
                nextPresentationMode = .framebuffer
            }

            if nextState != self.lcdUI {
                self.lcdUI = nextState
            }
            if imagesChanged {
                self.lcdUIImageStore.replace(with: nextImages)
            }
            if nextPresentationMode != self.presentationMode {
                self.presentationMode = nextPresentationMode
            }
        }
        resolvedEngine.onLCDUIImages = { [weak self] images in
            guard let self, !images.isEmpty else { return }
            var nextImages = self.lcdUIImageStore.images
            for (componentID, image) in images {
                nextImages[componentID] = image
            }
            self.lcdUIImageStore.replace(with: nextImages)
        }
        resolvedEngine.onStateChange = { [weak self] state in
            guard let self else { return }
            self.state = state
            switch state {
            case .stopped:
                self.resetSessionResources(clearCurrentGame: true)
            case .failed:
                self.resetSessionResources(clearCurrentGame: false)
            default:
                break
            }
        }
        resolvedEngine.onFPSChange = { [weak self] value in
            self?.framesPerSecond = value
        }
    }

    func launch(game: Game, jarURL: URL, profile: GameProfile) {
        let profile = profile.normalized()
        currentGame = game
        frameStore.reset()
        framesPerSecond = 0
        presentationMode = .framebuffer
        lcdUI = .empty
        lcdUIImageStore.reset()

        let storedMainClass = game.mainClass.trimmingCharacters(
            in: .whitespacesAndNewlines
        )
        let mainClass = storedMainClass.isEmpty
            ? (try? JarMetadataReader.read(from: jarURL).mainClass)
            : storedMainClass

        guard let mainClass, !mainClass.isEmpty else {
            state = .failed(
                PhoneMECoreError.mainClassMissing.localizedDescription
            )
            return
        }

        engine.start(
            gameID: game.id,
            jarURL: jarURL,
            mainClass: mainClass,
            screenWidth: profile.screenWidth,
            screenHeight: profile.screenHeight,
            frameRateLimit: profile.frameRateLimit,
            immediateProcessing: profile.immediateProcessing,
            parallelScreenRedrawing: profile.parallelScreenRedrawing,
            keyUp: profile.keyCode(for: .up),
            keyDown: profile.keyCode(for: .down),
            keyLeft: profile.keyCode(for: .left),
            keyRight: profile.keyCode(for: .right),
            keyFire: profile.keyCode(for: .fire),
            keySoftLeft: profile.keyCode(for: .softLeft),
            keySoftRight: profile.keyCode(for: .softRight)
        )
    }

    func stop() {
        framesPerSecond = 0
        engine.stop()
    }

    private func resetSessionResources(clearCurrentGame: Bool) {
        frameStore.reset()
        lcdUIImageStore.reset()
        lcdUI = .empty
        presentationMode = .framebuffer
        framesPerSecond = 0
        if clearCurrentGame {
            currentGame = nil
        }
    }

    func send(_ key: J2MEKey, pressed: Bool) {
        engine.sendKey(key, pressed: pressed)
    }

    func sendPointer(x: Int32, y: Int32, action: Int32) {
        engine.sendPointer(x: x, y: y, action: action)
    }

    func selectLCDUICommand(_ id: Int32) {
        engine.selectLCDUICommand(id)
    }

    func focusLCDUIItem(_ componentID: Int32) {
        if lcdUI.focusedItemID != componentID,
           lcdUI.items[componentID] != nil {
            var nextState = lcdUI
            nextState.focusedItemID = componentID
            for id in nextState.items.keys {
                nextState.items[id]?.isFocused = id == componentID
            }
            lcdUI = nextState
        }
        engine.focusLCDUIItem(componentID)
    }

    func activateLCDUIItem(_ componentID: Int32) {
        engine.activateLCDUIItem(componentID)
    }

    func setLCDUIText(
        componentID: Int32,
        text: String,
        caretPosition: Int
    ) {
        engine.setLCDUIText(
            componentID: componentID,
            text: text,
            caretPosition: caretPosition
        )
    }

    func setLCDUIChoice(
        componentID: Int32,
        index: Int,
        selected: Bool
    ) {
        // Reflect selection immediately instead of waiting for the VM event
        // round-trip. The native event remains authoritative and will correct
        // the state if the MIDlet changes it in response.
        if var item = lcdUI.items[componentID],
           let choiceIndex = item.choices.firstIndex(where: {
               $0.index == index
           }) {
            var changed = false
            if item.type != .multipleChoice && selected {
                for candidateIndex in item.choices.indices
                    where item.choices[candidateIndex].isSelected
                        && candidateIndex != choiceIndex {
                    item.choices[candidateIndex].isSelected = false
                    changed = true
                }
            }
            if item.choices[choiceIndex].isSelected != selected {
                item.choices[choiceIndex].isSelected = selected
                changed = true
            }
            if changed {
                var nextState = lcdUI
                nextState.items[componentID] = item
                lcdUI = nextState
            }
        }

        engine.setLCDUIChoice(
            componentID: componentID,
            index: index,
            selected: selected
        )
    }

    func setLCDUIGauge(componentID: Int32, value: Int) {
        engine.setLCDUIGauge(componentID: componentID, value: value)
    }

    private static func isChoiceImageKey(
        _ key: Int32,
        componentID: Int32
    ) -> Bool {
        guard key < 0 else { return false }
        return ((-Int64(key)) >> 10) == Int64(componentID)
    }

    func setLCDUIDate(componentID: Int32, date: Date) {
        engine.setLCDUIDate(componentID: componentID, date: date)
    }

    func setLCDUIScrollPosition(_ position: Int) {
        engine.setLCDUIScrollPosition(position)
    }
}
