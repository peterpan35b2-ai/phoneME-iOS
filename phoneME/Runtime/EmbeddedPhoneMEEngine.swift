import CoreGraphics
import Foundation

enum EmulatorState: Equatable {
    case idle
    case starting
    case running
    case stopped
    case failed(String)

    var title: String {
        switch self {
        case .idle: return "Idle"
        case .starting: return "Starting"
        case .running: return "Running"
        case .stopped: return "Stopped"
        case .failed: return "Error"
        }
    }
}

@MainActor
final class EmbeddedPhoneMEEngine: NSObject {
    var onFrame: ((CGImage) -> Void)?
    var onLCDUIEvents: (([PhoneMECAPI.LCDUIEvent]) -> Void)?
    var onLCDUIImages: (([Int32: CGImage]) -> Void)?
    var onStateChange: ((EmulatorState) -> Void)?
    var onFPSChange: ((Double) -> Void)?

    private struct VisibleScreen: Equatable, Sendable {
        let id: Int32
        let componentType: Int32

        var usesNativeLCDUI: Bool {
            componentType != LCDUIState.ComponentType.canvas.rawValue
        }
    }

    private final class ContinuousInputBuffer: @unchecked Sendable {
        struct TextUpdate: Sendable {
            let text: String
            let caretPosition: Int
        }

        struct PointerUpdate: Sendable {
            let x: Int32
            let y: Int32
        }

        struct Snapshot: Sendable {
            let textUpdates: [Int32: TextUpdate]
            let gaugeUpdates: [Int32: Int]
            let dateUpdates: [Int32: Date]
            let scrollPosition: Int?
            let pointerMove: PointerUpdate?
        }

        private let lock = NSLock()
        private var textUpdates: [Int32: TextUpdate] = [:]
        private var gaugeUpdates: [Int32: Int] = [:]
        private var dateUpdates: [Int32: Date] = [:]
        private var scrollPosition: Int?
        private var pointerMove: PointerUpdate?
        private var flushScheduled = false

        func submitText(
            componentID: Int32,
            text: String,
            caretPosition: Int
        ) -> Bool {
            lock.lock()
            textUpdates[componentID] = TextUpdate(
                text: text,
                caretPosition: caretPosition
            )
            let shouldSchedule = markFlushScheduledLocked()
            lock.unlock()
            return shouldSchedule
        }

        func submitGauge(componentID: Int32, value: Int) -> Bool {
            lock.lock()
            gaugeUpdates[componentID] = value
            let shouldSchedule = markFlushScheduledLocked()
            lock.unlock()
            return shouldSchedule
        }

        func submitDate(componentID: Int32, date: Date) -> Bool {
            lock.lock()
            dateUpdates[componentID] = date
            let shouldSchedule = markFlushScheduledLocked()
            lock.unlock()
            return shouldSchedule
        }

        func submitScrollPosition(_ position: Int) -> Bool {
            lock.lock()
            scrollPosition = position
            let shouldSchedule = markFlushScheduledLocked()
            lock.unlock()
            return shouldSchedule
        }

        func submitPointerMove(x: Int32, y: Int32) -> Bool {
            lock.lock()
            pointerMove = PointerUpdate(x: x, y: y)
            let shouldSchedule = markFlushScheduledLocked()
            lock.unlock()
            return shouldSchedule
        }

        func takeSnapshot() -> Snapshot {
            lock.lock()
            let snapshot = Snapshot(
                textUpdates: textUpdates,
                gaugeUpdates: gaugeUpdates,
                dateUpdates: dateUpdates,
                scrollPosition: scrollPosition,
                pointerMove: pointerMove
            )
            textUpdates.removeAll(keepingCapacity: true)
            gaugeUpdates.removeAll(keepingCapacity: true)
            dateUpdates.removeAll(keepingCapacity: true)
            scrollPosition = nil
            pointerMove = nil
            flushScheduled = false
            lock.unlock()
            return snapshot
        }

        func reset() {
            lock.lock()
            textUpdates.removeAll(keepingCapacity: true)
            gaugeUpdates.removeAll(keepingCapacity: true)
            dateUpdates.removeAll(keepingCapacity: true)
            scrollPosition = nil
            pointerMove = nil
            flushScheduled = false
            lock.unlock()
        }

        private func markFlushScheduledLocked() -> Bool {
            guard !flushScheduled else { return false }
            flushScheduled = true
            return true
        }
    }

    private final class PollContext: @unchecked Sendable {
        var visibleScreen: VisibleScreen?
        var nextFrameDeadline: UInt64 = 0
        var didReportRuntimeExit = false

        let frameIntervalNanoseconds: UInt64

        init(framesPerSecond: Int) {
            frameIntervalNanoseconds = 1_000_000_000
                / UInt64(max(framesPerSecond, 1))
        }
    }

    private final class RenderRequestBuffer: @unchecked Sendable {
        struct Snapshot: Sendable {
            let imageComponentIDs: Set<Int32>
            let visibleScreen: VisibleScreen?
            let wantsFrame: Bool
            let previousFrameGeneration: UInt64
        }

        private let lock = NSLock()
        private var pendingImageComponentIDs = Set<Int32>()
        private var pendingVisibleScreen: VisibleScreen?
        private var pendingFrame = false
        private var hasPendingRequest = false
        private var workerRunning = false
        private var frameGeneration: UInt64 = 0

        func submit(
            imageComponentIDs: Set<Int32>,
            visibleScreen: VisibleScreen?,
            wantsFrame: Bool
        ) -> Bool {
            lock.lock()
            pendingImageComponentIDs.formUnion(imageComponentIDs)
            pendingVisibleScreen = visibleScreen
            pendingFrame = pendingFrame || wantsFrame
            hasPendingRequest = true
            let shouldStartWorker = !workerRunning
            workerRunning = true
            lock.unlock()
            return shouldStartWorker
        }

        func takeSnapshot() -> Snapshot {
            lock.lock()
            let snapshot = Snapshot(
                imageComponentIDs: pendingImageComponentIDs,
                visibleScreen: pendingVisibleScreen,
                wantsFrame: pendingFrame,
                previousFrameGeneration: frameGeneration
            )
            pendingImageComponentIDs.removeAll(keepingCapacity: true)
            pendingFrame = false
            hasPendingRequest = false
            lock.unlock()
            return snapshot
        }

        func updateFrameGeneration(_ generation: UInt64) {
            lock.lock()
            frameGeneration = generation
            lock.unlock()
        }

        func completeIteration() -> Bool {
            lock.lock()
            let shouldContinue = hasPendingRequest
            if !shouldContinue {
                workerRunning = false
            }
            lock.unlock()
            return shouldContinue
        }
    }

    private let runtimeQueue = DispatchQueue(
        label: "com.phoneme.runtime.lifecycle",
        qos: .userInitiated
    )
    private let inputQueue = DispatchQueue(
        label: "com.phoneme.runtime.input",
        qos: .userInteractive
    )
    private let pollQueue = DispatchQueue(
        label: "com.phoneme.runtime.lcdui-poll",
        qos: .userInteractive
    )
    private let renderQueue = DispatchQueue(
        label: "com.phoneme.runtime.render",
        qos: .userInitiated
    )
    private var api: PhoneMECAPI?
    private var runtime: PhoneMECAPI.RuntimeHandle?
    private var pollTimer: DispatchSourceTimer?
    private var lastFrameGeneration: UInt64 = 0
    private var launchIdentifier = UUID()
    private let continuousInputBuffer = ContinuousInputBuffer()
    private var renderRequestBuffer = RenderRequestBuffer()
    private var framePollingFramesPerSecond = 60
    private var fpsFrameCount = 0
    private var fpsMeasurementStart = Date.timeIntervalSinceReferenceDate

    override init() {
        super.init()
        // Prepare the immutable runtime payload once. All access to the C ABI
        // is serialized through this same queue; the VM itself owns its worker
        // pthread, so another Swift-side worker layer is unnecessary.
        runtimeQueue.async {
            _ = try? PhoneMERuntimeResources.prepare()
        }
    }

    func start(
        gameID: UUID,
        jarURL: URL,
        mainClass: String,
        screenWidth: Int,
        screenHeight: Int,
        frameRateLimit: Int,
        immediateProcessing: Bool,
        parallelScreenRedrawing: Bool,
        keyUp: Int32,
        keyDown: Int32,
        keyLeft: Int32,
        keyRight: Int32,
        keyFire: Int32,
        keySoftLeft: Int32,
        keySoftRight: Int32
    ) {
        let previousAPI = api
        let previousRuntime = runtime
        // Request cooperative VM shutdown immediately instead of placing the
        // request behind already queued frame/input work. Destruction remains
        // serialized on runtimeQueue so captured handles cannot be freed while
        // an older operation is still using them.
        if let previousAPI, let previousRuntime {
            previousAPI.stop(previousRuntime)
        }
        clearCurrentRuntime()

        let requestedFPS = frameRateLimit > 0
            ? min(max(frameRateLimit, 1), 240)
            : (immediateProcessing ? 120 : 60)
        // LCDUI events are polled independently at 120 Hz. This value only
        // controls framebuffer copies, preserving the user's FPS profile
        // without slowing native Form/List/Alert interaction.
        framePollingFramesPerSecond = min(max(requestedFPS, 1), 120)
        fpsMeasurementStart = Date.timeIntervalSinceReferenceDate
        setState(.starting)

        // Frame reads always run outside the main thread now. Keep accepting
        // this profile setting for compatibility with existing saved profiles.
        _ = parallelScreenRedrawing

        let currentLaunchIdentifier = UUID()
        let cleanupQueue = runtimeQueue
        let inputQueue = inputQueue
        let pollQueue = pollQueue
        let renderQueue = renderQueue
        launchIdentifier = currentLaunchIdentifier

        runtimeQueue.async { [weak self] in
            if let previousAPI, let previousRuntime {
                // No host callback may retain the old runtime while it is
                // being freed. These barriers only wait for already-submitted
                // work; new work is rejected because clearCurrentRuntime()
                // removed the public handle first.
                inputQueue.sync { }
                pollQueue.sync { }
                renderQueue.sync { }
                previousAPI.destroyRuntime(previousRuntime)
            }

            do {
                let loadedAPI = try PhoneMECAPI.load(gameID: gameID)
                guard let createdRuntime = loadedAPI.createRuntime() else {
                    throw PhoneMECoreError.runtimeCreationFailed
                }

                let keymapResult = loadedAPI.configureKeymap(
                    createdRuntime,
                    up: keyUp,
                    down: keyDown,
                    left: keyLeft,
                    right: keyRight,
                    fire: keyFire,
                    softLeft: keySoftLeft,
                    softRight: keySoftRight
                )
                guard keymapResult == 0 else {
                    loadedAPI.destroyRuntime(createdRuntime)
                    throw PhoneMECoreError.launchFailed(keymapResult)
                }

                let result = loadedAPI.startJar(
                    createdRuntime,
                    jarURL: jarURL,
                    mainClass: mainClass,
                    screenWidth: screenWidth,
                    screenHeight: screenHeight
                )
                guard result == 0 else {
                    loadedAPI.destroyRuntime(createdRuntime)
                    throw PhoneMECoreError.launchFailed(result)
                }

                DispatchQueue.main.async {
                    guard
                        let self,
                        self.launchIdentifier == currentLaunchIdentifier
                    else {
                        cleanupQueue.async {
                            loadedAPI.stop(createdRuntime)
                            loadedAPI.destroyRuntime(createdRuntime)
                        }
                        return
                    }

                    self.api = loadedAPI
                    self.runtime = createdRuntime
                    self.setState(.running)
                    self.startPolling(
                        api: loadedAPI,
                        runtime: createdRuntime,
                        launchIdentifier: currentLaunchIdentifier
                    )
                }
            } catch {
                DispatchQueue.main.async { [weak self] in
                    guard
                        let self,
                        self.launchIdentifier == currentLaunchIdentifier
                    else {
                        return
                    }
                    self.clearCurrentRuntime()
                    self.setState(.failed(error.localizedDescription))
                }
            }
        }
    }

    func stop() {
        let currentAPI = api
        let currentRuntime = runtime
        launchIdentifier = UUID()
        // Set the global stop flag now. Waiting to enqueue this behind pending
        // polls/input can leave the VM alive long enough for the next launch to
        // appear stuck in its loading state.
        if let currentAPI, let currentRuntime {
            currentAPI.stop(currentRuntime)
        }
        clearCurrentRuntime()
        setState(.stopped)

        guard let currentAPI, let currentRuntime else {
            return
        }

        let inputQueue = inputQueue
        let pollQueue = pollQueue
        let renderQueue = renderQueue
        runtimeQueue.async {
            inputQueue.sync { }
            pollQueue.sync { }
            renderQueue.sync { }
            currentAPI.destroyRuntime(currentRuntime)
        }
    }

    func sendKey(_ key: J2MEKey, pressed: Bool) {
        guard let api, let runtime else { return }
        inputQueue.async {
            api.sendKey(runtime, key: key, pressed: pressed)
        }
    }

    func sendPointer(x: Int32, y: Int32, action: Int32) {
        guard let api, let runtime else { return }

        // Drag motion is a latest-value signal. Queueing every intermediate
        // coordinate can delay the eventual pointer-up and all LCDUI actions
        // behind it. Preserve down/up ordering, but coalesce action 3 moves.
        if action == 3 {
            scheduleContinuousInputFlush(
                if: continuousInputBuffer.submitPointerMove(x: x, y: y),
                api: api,
                runtime: runtime
            )
        } else {
            inputQueue.async {
                api.sendPointer(runtime, x: x, y: y, action: action)
            }
        }
    }

    func selectLCDUICommand(_ id: Int32) {
        guard let api, let runtime else { return }
        inputQueue.async {
            api.selectLCDUICommand(runtime, id: id)
        }
    }

    func focusLCDUIItem(_ componentID: Int32) {
        guard let api, let runtime else { return }
        inputQueue.async {
            api.focusLCDUIItem(runtime, componentID: componentID)
        }
    }

    func activateLCDUIItem(_ componentID: Int32) {
        guard let api, let runtime else { return }
        inputQueue.async {
            api.activateLCDUIItem(runtime, componentID: componentID)
        }
    }

    func setLCDUIText(
        componentID: Int32,
        text: String,
        caretPosition: Int
    ) {
        guard let api, let runtime else { return }
        scheduleContinuousInputFlush(
            if: continuousInputBuffer.submitText(
                componentID: componentID,
                text: text,
                caretPosition: caretPosition
            ),
            api: api,
            runtime: runtime
        )
    }

    func setLCDUIChoice(
        componentID: Int32,
        index: Int,
        selected: Bool
    ) {
        guard let api, let runtime else { return }
        inputQueue.async {
            api.setLCDUIChoice(
                runtime,
                componentID: componentID,
                index: index,
                selected: selected
            )
        }
    }

    func setLCDUIGauge(componentID: Int32, value: Int) {
        guard let api, let runtime else { return }
        scheduleContinuousInputFlush(
            if: continuousInputBuffer.submitGauge(
                componentID: componentID,
                value: value
            ),
            api: api,
            runtime: runtime
        )
    }

    func setLCDUIDate(componentID: Int32, date: Date) {
        guard let api, let runtime else { return }
        scheduleContinuousInputFlush(
            if: continuousInputBuffer.submitDate(
                componentID: componentID,
                date: date
            ),
            api: api,
            runtime: runtime
        )
    }

    func setLCDUIScrollPosition(_ position: Int) {
        guard let api, let runtime else { return }
        scheduleContinuousInputFlush(
            if: continuousInputBuffer.submitScrollPosition(position),
            api: api,
            runtime: runtime
        )
    }

    private func scheduleContinuousInputFlush(
        if shouldSchedule: Bool,
        api: PhoneMECAPI,
        runtime: PhoneMECAPI.RuntimeHandle
    ) {
        guard shouldSchedule else { return }
        let buffer = continuousInputBuffer
        inputQueue.async {
            let snapshot = buffer.takeSnapshot()

            if let pointerMove = snapshot.pointerMove {
                api.sendPointer(
                    runtime,
                    x: pointerMove.x,
                    y: pointerMove.y,
                    action: 3
                )
            }
            for (componentID, update) in snapshot.textUpdates {
                api.setLCDUIText(
                    runtime,
                    componentID: componentID,
                    text: update.text,
                    caretPosition: update.caretPosition
                )
            }
            for (componentID, value) in snapshot.gaugeUpdates {
                api.setLCDUIGauge(
                    runtime,
                    componentID: componentID,
                    value: value
                )
            }
            for (componentID, date) in snapshot.dateUpdates {
                api.setLCDUIDate(
                    runtime,
                    componentID: componentID,
                    date: date
                )
            }
            if let position = snapshot.scrollPosition {
                api.setLCDUIScrollPosition(runtime, position: position)
            }
        }
    }

    private func startPolling(
        api: PhoneMECAPI,
        runtime: PhoneMECAPI.RuntimeHandle,
        launchIdentifier: UUID
    ) {
        pollTimer?.cancel()

        let context = PollContext(
            framesPerSecond: framePollingFramesPerSecond
        )
        let renderBuffer = renderRequestBuffer
        let renderQueue = renderQueue
        let timer = DispatchSource.makeTimerSource(queue: pollQueue)

        // Native LCDUI is an event surface, not a framebuffer. Poll it at a
        // stable 120 Hz on a background queue so touches and UIKit tracking do
        // not pause the bridge. Frame copies retain the profile's own cadence.
        timer.schedule(
            deadline: .now(),
            repeating: .nanoseconds(8_333_333),
            leeway: .nanoseconds(500_000)
        )
        timer.setEventHandler { [weak self] in
            guard !context.didReportRuntimeExit else { return }

            guard api.isRunning(runtime) else {
                context.didReportRuntimeExit = true
                let exitCode = api.lastExitCode(runtime)
                DispatchQueue.main.async {
                    guard
                        let self,
                        self.launchIdentifier == launchIdentifier,
                        self.runtime == runtime
                    else {
                        return
                    }
                    self.finishRuntime(
                        api: api,
                        runtime: runtime,
                        exitCode: exitCode
                    )
                }
                return
            }

            let lcdUIEvents = api.drainLCDUIEvents(
                runtime,
                maximumCount: 512
            )
            var resolvedVisibleScreen = context.visibleScreen
            var imageComponentIDs = Set<Int32>()

            for event in lcdUIEvents {
                switch event.kind {
                case 1:
                    resolvedVisibleScreen = nil

                case 4:
                    resolvedVisibleScreen = VisibleScreen(
                        id: event.componentID,
                        componentType: event.componentType
                    )

                case 5, 6:
                    if resolvedVisibleScreen?.id == event.componentID {
                        resolvedVisibleScreen = nil
                    }

                default:
                    break
                }

                if event.arguments.3 == -1004 {
                    imageComponentIDs.insert(event.componentID)
                } else if event.kind == 12, event.arguments.3 < 0 {
                    imageComponentIDs.insert(event.arguments.3)
                }
            }

            context.visibleScreen = resolvedVisibleScreen

            if !lcdUIEvents.isEmpty {
                DispatchQueue.main.async {
                    guard
                        let self,
                        self.launchIdentifier == launchIdentifier,
                        self.runtime == runtime
                    else {
                        return
                    }
                    self.onLCDUIEvents?(lcdUIEvents)
                }
            }

            let now = DispatchTime.now().uptimeNanoseconds
            let wantsFrame = resolvedVisibleScreen?.usesNativeLCDUI != true
                && now >= context.nextFrameDeadline
            if wantsFrame {
                context.nextFrameDeadline = now
                    &+ context.frameIntervalNanoseconds
            }

            guard !imageComponentIDs.isEmpty || wantsFrame else {
                return
            }

            let shouldStartRenderWorker = renderBuffer.submit(
                imageComponentIDs: imageComponentIDs,
                visibleScreen: resolvedVisibleScreen,
                wantsFrame: wantsFrame
            )
            guard shouldStartRenderWorker else { return }

            renderQueue.async { [weak self] in
                repeat {
                    let snapshot = renderBuffer.takeSnapshot()
                    var images: [Int32: CGImage] = [:]

                    for componentID in snapshot.imageComponentIDs {
                        if let value = api.copyLCDUIImage(
                            runtime,
                            componentID: componentID
                        ) {
                            images[componentID] = value.image
                        }
                    }

                    let frame = snapshot.wantsFrame
                        && snapshot.visibleScreen?.usesNativeLCDUI != true
                        ? api.copyFrame(
                            runtime,
                            after: snapshot.previousFrameGeneration
                        )
                        : nil
                    if let frame {
                        renderBuffer.updateFrameGeneration(frame.generation)
                    }

                    if !images.isEmpty || frame != nil {
                        DispatchQueue.main.async {
                            guard
                                let self,
                                self.launchIdentifier == launchIdentifier,
                                self.runtime == runtime
                            else {
                                return
                            }
                            if !images.isEmpty {
                                self.onLCDUIImages?(images)
                            }
                            if let frame {
                                self.deliver(frame)
                            }
                        }
                    }
                } while renderBuffer.completeIteration()
            }
        }

        pollTimer = timer
        timer.resume()
    }

    private func deliver(_ frame: (image: CGImage, generation: UInt64)) {
        lastFrameGeneration = frame.generation
        onFrame?(frame.image)

        fpsFrameCount += 1
        let now = Date.timeIntervalSinceReferenceDate
        let elapsed = now - fpsMeasurementStart
        if elapsed >= 0.5 {
            onFPSChange?(Double(fpsFrameCount) / elapsed)
            fpsFrameCount = 0
            fpsMeasurementStart = now
        }
    }

    private func finishRuntime(
        api: PhoneMECAPI,
        runtime: PhoneMECAPI.RuntimeHandle,
        exitCode: Int32
    ) {
        guard self.runtime == runtime else {
            return
        }

        clearCurrentRuntime()
        let inputQueue = inputQueue
        let pollQueue = pollQueue
        let renderQueue = renderQueue
        runtimeQueue.async {
            inputQueue.sync { }
            pollQueue.sync { }
            renderQueue.sync { }
            api.destroyRuntime(runtime)
        }

        if exitCode < 0 {
            setState(.failed(
                PhoneMECoreError.launchFailed(exitCode).localizedDescription
            ))
        } else {
            setState(.stopped)
        }
    }

    private func clearCurrentRuntime() {
        pollTimer?.cancel()
        pollTimer = nil
        lastFrameGeneration = 0
        continuousInputBuffer.reset()
        // Give the next launch a fresh coalescing state. Any old render worker
        // retains the previous buffer and is rejected by launchIdentifier.
        renderRequestBuffer = RenderRequestBuffer()
        fpsFrameCount = 0
        fpsMeasurementStart = Date.timeIntervalSinceReferenceDate
        onFPSChange?(0)
        runtime = nil
        api = nil
    }

    private func setState(_ state: EmulatorState) {
        onStateChange?(state)
    }
}
