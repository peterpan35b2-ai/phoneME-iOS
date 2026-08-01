import CoreGraphics
import Darwin
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
final class EmulatorFPSStore: ObservableObject {
    @Published private(set) var value: Double = 0

    fileprivate func update(_ value: Double) {
        guard self.value != value else { return }
        self.value = value
    }

    fileprivate func reset() {
        update(0)
    }
}

@MainActor
final class LCDUIImageSlot: ObservableObject {
    @Published private(set) var image: CGImage?

    fileprivate init(image: CGImage? = nil) {
        self.image = image
    }

    fileprivate func update(_ image: CGImage?) {
        if let current = self.image, let image, current === image {
            return
        }
        if self.image == nil, image == nil {
            return
        }
        self.image = image
    }
}

@MainActor
final class LCDUIImageStore: ObservableObject {
    private(set) var images: [Int32: CGImage] = [:]
    private var slots: [Int32: LCDUIImageSlot] = [:]

    subscript(componentID: Int32) -> CGImage? {
        images[componentID]
    }

    func slot(for componentID: Int32) -> LCDUIImageSlot {
        if let slot = slots[componentID] {
            return slot
        }
        let slot = LCDUIImageSlot(image: images[componentID])
        slots[componentID] = slot
        return slot
    }

    fileprivate func merge(_ updates: [Int32: CGImage]) {
        for (componentID, image) in updates {
            if let current = images[componentID], current === image {
                continue
            }
            images[componentID] = image
            slots[componentID]?.update(image)
        }
    }

    fileprivate func replace(with nextImages: [Int32: CGImage]) {
        for componentID in images.keys where nextImages[componentID] == nil {
            slots[componentID]?.update(nil)
        }
        for (componentID, image) in nextImages {
            if let current = images[componentID], current === image {
                continue
            }
            slots[componentID]?.update(image)
        }
        images = nextImages
    }

    fileprivate func reset() {
        images.removeAll(keepingCapacity: true)
        for slot in slots.values {
            slot.update(nil)
        }
        slots.removeAll(keepingCapacity: true)
    }
}

enum EmulatorPresentationMode: Equatable, Sendable {
    case framebuffer
    case nativeLCDUI
}

struct RunningJ2MEApplication: Identifiable, Equatable {
    enum State: Equatable {
        case starting
        case foreground
        case background
        case paused
        case failed
    }

    let game: Game
    var state: State

    var id: UUID { game.id }
}

struct J2MERuntimeResourceUsage: Equatable, Sendable {
    let cpuPercent: Double
    let residentMemoryBytes: UInt64
}

private final class J2MEProcessResourceSampler: @unchecked Sendable {
    private let lock = NSLock()
    private var previousCPUTime: Double?
    private var previousUptime: TimeInterval?

    func reset() {
        lock.lock()
        previousCPUTime = nil
        previousUptime = nil
        lock.unlock()
    }

    func sample() -> J2MERuntimeResourceUsage? {
        var processUsage = rusage()
        guard getrusage(RUSAGE_SELF, &processUsage) == 0 else {
            return nil
        }

        let userTime = Double(processUsage.ru_utime.tv_sec)
            + Double(processUsage.ru_utime.tv_usec) / 1_000_000
        let systemTime = Double(processUsage.ru_stime.tv_sec)
            + Double(processUsage.ru_stime.tv_usec) / 1_000_000
        let currentCPUTime = userTime + systemTime
        let currentUptime = ProcessInfo.processInfo.systemUptime

        lock.lock()
        let oldCPUTime = previousCPUTime
        let oldUptime = previousUptime
        previousCPUTime = currentCPUTime
        previousUptime = currentUptime
        lock.unlock()

        let cpuPercent: Double
        if let oldCPUTime, let oldUptime {
            let elapsed = currentUptime - oldUptime
            let consumed = currentCPUTime - oldCPUTime
            let maximum = Double(ProcessInfo.processInfo.activeProcessorCount) * 100
            cpuPercent = elapsed > 0
                ? min(max(consumed / elapsed * 100, 0), maximum)
                : 0
        } else {
            cpuPercent = 0
        }

        return J2MERuntimeResourceUsage(
            cpuPercent: cpuPercent,
            residentMemoryBytes: Self.residentMemoryBytes()
        )
    }

    private static func residentMemoryBytes() -> UInt64 {
        var info = mach_task_basic_info_data_t()
        var count = mach_msg_type_number_t(
            MemoryLayout.size(ofValue: info) / MemoryLayout<natural_t>.size
        )
        let result = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(
                to: integer_t.self,
                capacity: Int(count)
            ) { reboundPointer in
                task_info(
                    mach_task_self_,
                    task_flavor_t(MACH_TASK_BASIC_INFO),
                    reboundPointer,
                    &count
                )
            }
        }
        guard result == KERN_SUCCESS else { return 0 }
        return UInt64(info.resident_size)
    }
}

@MainActor
final class EmulatorSession: ObservableObject {
    @Published private(set) var state: EmulatorState = .idle
    @Published private(set) var presentationMode: EmulatorPresentationMode = .framebuffer
    @Published private(set) var lcdUI: LCDUIState = .empty
    @Published private(set) var currentGame: Game?
    @Published private(set) var runningApplications: [UUID: RunningJ2MEApplication] = [:]
    @Published private(set) var backgroundRuntimeUsage: J2MERuntimeResourceUsage?
    @Published private(set) var backgroundApplicationMemoryUsage: [UUID: UInt64] = [:]

    let frameStore = EmulatorFrameStore()
    let fpsStore = EmulatorFPSStore()
    let lcdUIImageStore = LCDUIImageStore()
    var frame: CGImage? { frameStore.frame }
    var lcdUIImages: [Int32: CGImage] { lcdUIImageStore.images }

    var isPresentingNativeLCDUI: Bool {
        presentationMode == .nativeLCDUI && lcdUI.hasNativeScreen
    }

    private let engine: EmbeddedPhoneMEEngine
    private let resourceMonitorQueue = DispatchQueue(
        label: "dev.phoneme.emulator.resource-monitor",
        qos: .utility
    )
    private let resourceSampler = J2MEProcessResourceSampler()
    private var resourceMonitorTimer: DispatchSourceTimer?
    private var resourceMeasurementInFlight = false
    private var resourceMonitorTick: UInt = 0
    private var isApplicationInBackground = false

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
            self.lcdUIImageStore.merge(images)
        }
        resolvedEngine.onStateChange = { [weak self] state in
            guard let self else { return }
            self.state = state
            switch state {
            case .stopped:
                self.resetSessionResources(clearCurrentGame: true)
            case .failed:
                if let currentGame,
                   var application = self.runningApplications[currentGame.id] {
                    application.state = .failed
                    self.runningApplications[currentGame.id] = application
                }
                self.resetSessionResources(clearCurrentGame: false)
            default:
                break
            }
            self.refreshResourceMonitoringState()
        }
        resolvedEngine.onFPSChange = { [weak self] value in
            self?.fpsStore.update(value)
        }
        resolvedEngine.onApplicationStateChange = { [weak self] gameID, state in
            guard let self else { return }
            switch state {
            case .destroyed:
                self.runningApplications.removeValue(forKey: gameID)
                if self.currentGame?.id == gameID {
                    self.resetSessionResources(clearCurrentGame: true)
                }

            case .error:
                if var application = self.runningApplications[gameID] {
                    application.state = .failed
                    self.runningApplications[gameID] = application
                }

            case .paused:
                if var application = self.runningApplications[gameID] {
                    application.state = .paused
                    self.runningApplications[gameID] = application
                }

            case .active, .none:
                break
            }
            self.refreshResourceMonitoringState()
        }
        resolvedEngine.onForegroundApplicationChange = { [weak self] gameID in
            guard let self else { return }
            for id in Array(self.runningApplications.keys) {
                guard var application = self.runningApplications[id] else {
                    continue
                }
                if id == gameID {
                    application.state = .foreground
                } else {
                    switch application.state {
                    case .starting, .foreground, .background:
                        application.state = .background
                    case .paused, .failed:
                        break
                    }
                }
                self.runningApplications[id] = application
            }
            self.refreshResourceMonitoringState()
        }
    }

    func prepareApplications(
        _ games: [Game],
        fileURL: (Game) -> URL
    ) {
        let applications = Dictionary(
            uniqueKeysWithValues: games.map { game in
                (game.id, fileURL(game))
            }
        )
        engine.prepareApplications(applications)
    }

    func launch(
        game: Game,
        jarURL: URL,
        artworkURL: URL?,
        profile: GameProfile
    ) {
        let profile = profile.normalized()
        currentGame = game
        runningApplications[game.id] = RunningJ2MEApplication(
            game: game,
            state: .starting
        )
        refreshResourceMonitoringState()
        frameStore.reset()
        fpsStore.reset()
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

        engine.launchApplication(
            gameID: game.id,
            jarURL: jarURL,
            mainClass: mainClass,
            mediaTitle: game.title,
            mediaArtist: game.vendor,
            mediaArtworkPath: artworkURL?.path,
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

    func hideCurrent() {
        guard let currentGame,
              let applicationState = runningApplications[currentGame.id]?.state,
              applicationState != .background else {
            return
        }

        if applicationState == .failed {
            self.currentGame = nil
            state = .idle
            fpsStore.reset()
            return
        }

        // Detach only the visible Displayable. The MIDlet isolate, network
        // sockets and native audio players stay alive so another J2ME app can
        // become foreground without destroying this one.
        engine.hideCurrentApplication()
        if var application = runningApplications[currentGame.id] {
            application.state = .background
            runningApplications[currentGame.id] = application
        }
        refreshResourceMonitoringState()
        self.currentGame = nil
        state = .idle
        fpsStore.reset()
    }

    func terminateCurrent() {
        guard let currentGame else { return }
        terminate(gameID: currentGame.id)
    }

    func terminate(gameID: UUID) {
        fpsStore.reset()
        engine.terminateApplication(gameID: gameID)
        runningApplications.removeValue(forKey: gameID)
        refreshResourceMonitoringState()
        if currentGame?.id == gameID {
            resetSessionResources(clearCurrentGame: true)
            state = .stopped
        }
    }

    func stop() {
        terminateCurrent()
    }

    func isRunning(_ gameID: UUID) -> Bool {
        runningApplications[gameID] != nil
    }

    func isRunningInBackground(_ gameID: UUID) -> Bool {
        runningApplications[gameID]?.state == .background
            || runningApplications[gameID]?.state == .paused
    }

    func setApplicationInBackground(_ isInBackground: Bool) {
        guard isApplicationInBackground != isInBackground else {
            refreshResourceMonitoringState()
            return
        }

        isApplicationInBackground = isInBackground
        refreshResourceMonitoringState()
    }

    func suspend() {
        engine.enterBackground()
    }

    func resume() {
        engine.enterForeground()
    }

    private func resetSessionResources(clearCurrentGame: Bool) {
        frameStore.reset()
        lcdUIImageStore.reset()
        lcdUI = .empty
        presentationMode = .framebuffer
        fpsStore.reset()
        if clearCurrentGame {
            currentGame = nil
        }
    }

    private var hiddenApplicationIDs: Set<UUID> {
        Set(
            runningApplications.compactMap { gameID, application in
                application.state == .background || application.state == .paused
                    ? gameID : nil
            }
        )
    }

    private var hasHiddenApplications: Bool {
        !hiddenApplicationIDs.isEmpty
    }

    private func refreshResourceMonitoringState() {
        if hasHiddenApplications && !isApplicationInBackground {
            startResourceMonitoring()
        } else {
            stopResourceMonitoring()
        }
    }

    private func startResourceMonitoring() {
        guard resourceMonitorTimer == nil else { return }

        resourceSampler.reset()
        // Sample process CPU frequently enough for the library indicator, but
        // query isolate heap usage only every fourth tick. A runtime-info
        // request enters the AMS isolate and becomes noticeable with several
        // busy background games.
        resourceMonitorTick = 3
        let sampler = resourceSampler
        let timer = DispatchSource.makeTimerSource(queue: resourceMonitorQueue)
        timer.schedule(
            deadline: .now(),
            repeating: .milliseconds(1_500),
            leeway: .milliseconds(250)
        )
        timer.setEventHandler { [weak self] in
            guard let sample = sampler.sample() else { return }
            DispatchQueue.main.async { [weak self] in
                guard
                    let self,
                    self.hasHiddenApplications,
                    !self.isApplicationInBackground
                else {
                    return
                }
                if self.backgroundRuntimeUsage != sample {
                    self.backgroundRuntimeUsage = sample
                }
                self.resourceMonitorTick &+= 1
                if self.resourceMonitorTick.isMultiple(of: 4) {
                    self.measureHiddenApplicationMemory()
                }
            }
        }
        resourceMonitorTimer = timer
        timer.resume()
    }

    private func measureHiddenApplicationMemory() {
        guard !resourceMeasurementInFlight else { return }
        let gameIDs = hiddenApplicationIDs
        guard !gameIDs.isEmpty else { return }

        resourceMeasurementInFlight = true
        engine.measureApplicationMemoryUsage(gameIDs: gameIDs) { [weak self] usage in
            guard let self else { return }
            self.resourceMeasurementInFlight = false
            guard !self.isApplicationInBackground else { return }
            let currentHiddenIDs = self.hiddenApplicationIDs
            let filteredUsage = usage.filter {
                currentHiddenIDs.contains($0.key)
            }
            if self.backgroundApplicationMemoryUsage != filteredUsage {
                self.backgroundApplicationMemoryUsage = filteredUsage
            }
        }
    }

    deinit {
        resourceMonitorTimer?.cancel()
    }

    private func stopResourceMonitoring() {
        resourceMonitorTimer?.cancel()
        resourceMonitorTimer = nil
        resourceMonitorTick = 0
        resourceSampler.reset()
        if backgroundRuntimeUsage != nil {
            backgroundRuntimeUsage = nil
        }
        if !backgroundApplicationMemoryUsage.isEmpty {
            backgroundApplicationMemoryUsage = [:]
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
