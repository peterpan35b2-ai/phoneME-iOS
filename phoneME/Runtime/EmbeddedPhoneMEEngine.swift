import CoreGraphics
import Foundation
import OSLog

private let phoneMEMultitaskingLogger = Logger(
    subsystem: "dev.phoneme.emulator",
    category: "J2MEMultitasking"
)

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
    var onApplicationStateChange: ((UUID, PhoneMECAPI.AppState) -> Void)?
    var onForegroundApplicationChange: ((UUID?) -> Void)?

    private final class RuntimeContext: @unchecked Sendable {
        var api: PhoneMECAPI?
        var runtime: PhoneMECAPI.RuntimeHandle?
        var suiteIDs: [UUID: Int32] = [:]
        var appIDs: [UUID: Int32] = [:]
        var gameIDsByAppID: [Int32: UUID] = [:]
        var nextAppID: Int32 = 1

        func nextAvailableAppID() -> Int32? {
            guard gameIDsByAppID.count < 64, nextAppID > 0 else {
                return nil
            }

            // Component IDs are namespaced by appID in Core. Never recycle an
            // appID while this runtime context is alive, otherwise a delayed
            // LCDUI action from a destroyed app could target a component in a
            // later app that inherited the same namespace.
            let candidate = nextAppID
            if candidate == Int32.max {
                nextAppID = 0
            } else {
                nextAppID = candidate + 1
            }
            return candidate
        }

        func removeApplication(gameID: UUID) {
            guard let appID = appIDs.removeValue(forKey: gameID) else {
                return
            }
            gameIDsByAppID.removeValue(forKey: appID)
        }

        func reset() {
            api = nil
            runtime = nil
            suiteIDs.removeAll(keepingCapacity: true)
            appIDs.removeAll(keepingCapacity: true)
            gameIDsByAppID.removeAll(keepingCapacity: true)
            nextAppID = 1
        }
    }

    private final class LaunchToken: @unchecked Sendable {
        private let lock = NSLock()
        private var cancelled = false

        var isCancelled: Bool {
            lock.lock()
            let value = cancelled
            lock.unlock()
            return value
        }

        func cancel() {
            lock.lock()
            cancelled = true
            lock.unlock()
        }
    }

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
        var consecutiveIdleNativePolls = 0
        var usesIdleNativeCadence = false

        let frameIntervalNanoseconds: UInt64
        let activePollIntervalNanoseconds: UInt64
        let idleNativePollIntervalNanoseconds: UInt64 = 100_000_000
        let idleNativePollThreshold: Int

        init(framesPerSecond: Int) {
            let normalizedFrameRate = min(
                max(framesPerSecond, 1),
                GameProfile.maximumFrameRate
            )
            frameIntervalNanoseconds = 1_000_000_000
                / UInt64(normalizedFrameRate)
            activePollIntervalNanoseconds = frameIntervalNanoseconds
            idleNativePollThreshold = max(normalizedFrameRate / 2, 1)
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

        func invalidateFrameGeneration() {
            lock.lock()
            frameGeneration = 0
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
        qos: .userInitiated
    )
    private let renderQueue = DispatchQueue(
        label: "com.phoneme.runtime.render",
        qos: .userInitiated
    )
    private let runtimeContext = RuntimeContext()
    private var api: PhoneMECAPI?
    private var runtime: PhoneMECAPI.RuntimeHandle?
    private var foregroundGameID: UUID?
    private var foregroundAppID: Int32?
    private var pendingForegroundGameID: UUID?
    private var launchToken = LaunchToken()
    private var pollTimer: DispatchSourceTimer?
    private var pollTimerIsSuspended = false
    private var runtimeIsSuspended = false
    private var runtimeSuspensionRequested = false
    private var shouldRunInForeground = true
    private var lastFrameGeneration: UInt64 = 0
    private var launchIdentifier = UUID()
    private let continuousInputBuffer = ContinuousInputBuffer()
    private var renderRequestBuffer = RenderRequestBuffer()
    private var framePollingFramesPerSecond = GameProfile.maximumFrameRate
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
        mediaTitle: String,
        mediaArtist: String,
        mediaArtworkPath: String?,
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
        let previousNeedsResume = runtimeIsSuspended
            || runtimeSuspensionRequested
        // Remove the public handles and stop polling immediately. The actual
        // resume/stop/destroy sequence stays on runtimeQueue so it cannot race
        // an in-flight suspend request or framebuffer operation.
        clearCurrentRuntime()

        let requestedFPS = frameRateLimit > 0
            ? min(
                max(frameRateLimit, 1),
                GameProfile.maximumFrameRate
            )
            : GameProfile.maximumFrameRate
        // Native input wakes the MIDP select loop directly, so the host bridge
        // does not need a 120 Hz polling floor. Framebuffer delivery and active
        // LCDUI synchronization follow the profile cadence up to 60 FPS without
        // changing the MIDlet's own timing or game-loop speed.
        framePollingFramesPerSecond = requestedFPS
        fpsMeasurementStart = Date.timeIntervalSinceReferenceDate
        setState(.starting)

        // Frame reads always run outside the main thread. These legacy fields
        // remain decodable for existing profiles but no longer raise cadence.
        _ = immediateProcessing
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
                if previousNeedsResume {
                    previousAPI.resume(previousRuntime)
                }
                previousAPI.stop(previousRuntime)
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

                Self.configureMediaMetadata(
                    title: mediaTitle,
                    artist: mediaArtist,
                    artworkPath: mediaArtworkPath
                )

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
                    self.runtimeIsSuspended = false
                    self.runtimeSuspensionRequested = false
                    self.setState(.running)
                    self.startPolling(
                        api: loadedAPI,
                        runtime: createdRuntime,
                        launchIdentifier: currentLaunchIdentifier
                    )
                    if !self.shouldRunInForeground {
                        self.enterBackground()
                    }
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

    func prepareApplications(_ applications: [UUID: URL]) {
        guard !applications.isEmpty else { return }
        let context = runtimeContext

        runtimeQueue.async { [weak self] in
            do {
                let (loadedAPI, createdRuntime) = try Self.ensureRuntime(
                    in: context
                )

                // fileInstaller owns MIDP initialization/finalization and is
                // intentionally used only before the MVM begins running.
                // Already prepared suites remain available for every isolate.
                guard !loadedAPI.isRunning(createdRuntime) else { return }

                for (gameID, jarURL) in applications
                    where context.suiteIDs[gameID] == nil {
                    let install = loadedAPI.installJar(
                        createdRuntime,
                        jarURL: jarURL
                    )
                    phoneMEMultitaskingLogger.info(
                        "Prepared game \(gameID.uuidString, privacy: .public): status=\(install.status), suite=\(install.suiteID ?? 0), installerStage=\(loadedAPI.lastInstallStage()), storeStage=\(loadedAPI.lastSuiteStoreStage())"
                    )
                    guard install.status == 0, let suiteID = install.suiteID else {
                        // A corrupt or unsupported JAR must not abort preparation
                        // for every other library item. Keep it unmapped so a
                        // later explicit launch reports the error for this game
                        // only, while valid suites remain available to MVM.
                        phoneMEMultitaskingLogger.error(
                            "Skipped game \(gameID.uuidString, privacy: .public) during preparation: status=\(install.status), installerStage=\(loadedAPI.lastInstallStage()), storeStage=\(loadedAPI.lastSuiteStoreStage())"
                        )
                        continue
                    }
                    context.suiteIDs[gameID] = suiteID
                }

                DispatchQueue.main.async {
                    guard let self else { return }
                    self.api = loadedAPI
                    self.runtime = createdRuntime
                }
            } catch {
                // Library preparation is opportunistic. Do not poison the
                // foreground session if runtime setup fails here; launching a
                // selected app will retry and surface a game-specific error.
                phoneMEMultitaskingLogger.error(
                    "Preparation failed: \(error.localizedDescription, privacy: .public)"
                )
            }
        }
    }

    func launchApplication(
        gameID: UUID,
        jarURL: URL,
        mainClass: String,
        mediaTitle: String,
        mediaArtist: String,
        mediaArtworkPath: String?,
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
        let requestedFPS = frameRateLimit > 0
            ? min(max(frameRateLimit, 1), GameProfile.maximumFrameRate)
            : GameProfile.maximumFrameRate
        framePollingFramesPerSecond = requestedFPS
        fpsMeasurementStart = Date.timeIntervalSinceReferenceDate
        _ = immediateProcessing
        _ = parallelScreenRedrawing

        let previousPendingGameID = pendingForegroundGameID
        launchToken.cancel()
        let currentLaunchToken = LaunchToken()
        launchToken = currentLaunchToken

        let currentLaunchIdentifier = UUID()
        launchIdentifier = currentLaunchIdentifier

        let previousForegroundGameID = foregroundGameID
        stopForegroundPolling()
        foregroundGameID = nil
        foregroundAppID = nil
        pendingForegroundGameID = gameID
        if let previousPendingGameID, previousPendingGameID != gameID {
            onApplicationStateChange?(previousPendingGameID, .active)
        }
        if previousForegroundGameID != nil {
            onForegroundApplicationChange?(nil)
        }
        setState(.starting)

        let context = runtimeContext
        let inputQueue = inputQueue
        let pollQueue = pollQueue
        let renderQueue = renderQueue
        runtimeQueue.async { [weak self] in
            do {
                // A foreground transition owns the process-global LCDUI and
                // framebuffer bridges. Wait for every operation submitted by
                // the previous screen before changing the NAMS foreground app.
                inputQueue.sync { }
                pollQueue.sync { }
                renderQueue.sync { }
                guard !currentLaunchToken.isCancelled else { return }

                let (loadedAPI, createdRuntime) = try Self.ensureRuntime(
                    in: context
                )

                var suiteID = context.suiteIDs[gameID]
                if suiteID == nil {
                    guard !loadedAPI.isRunning(createdRuntime) else {
                        throw PhoneMECoreError.applicationNotPrepared
                    }
                    let install = loadedAPI.installJar(
                        createdRuntime,
                        jarURL: jarURL
                    )
                    phoneMEMultitaskingLogger.info(
                        "Prepared launch game \(gameID.uuidString, privacy: .public): status=\(install.status), suite=\(install.suiteID ?? 0), installerStage=\(loadedAPI.lastInstallStage()), storeStage=\(loadedAPI.lastSuiteStoreStage())"
                    )
                    guard install.status == 0, let installedSuiteID = install.suiteID else {
                        throw PhoneMECoreError.launchFailed(install.status)
                    }
                    context.suiteIDs[gameID] = installedSuiteID
                    suiteID = installedSuiteID
                }

                guard !currentLaunchToken.isCancelled else { return }

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
                    throw PhoneMECoreError.launchFailed(keymapResult)
                }

                let appID: Int32
                if let existingAppID = context.appIDs[gameID] {
                    let existingState = loadedAPI.midletState(
                        createdRuntime,
                        appID: existingAppID
                    )
                    if existingState == .destroyed || existingState == .error {
                        context.removeApplication(gameID: gameID)
                        guard let replacementAppID = context.nextAvailableAppID() else {
                            throw PhoneMECoreError.tooManyApplications
                        }
                        appID = replacementAppID
                        context.appIDs[gameID] = replacementAppID
                        context.gameIDsByAppID[replacementAppID] = gameID
                        let result = loadedAPI.startMidlet(
                            createdRuntime,
                            suiteID: suiteID!,
                            mainClass: mainClass,
                            appID: replacementAppID,
                            screenWidth: screenWidth,
                            screenHeight: screenHeight
                        )
                        guard result == 0 else {
                            context.removeApplication(gameID: gameID)
                            throw PhoneMECoreError.launchFailed(result)
                        }
                    } else {
                        appID = existingAppID
                        if existingState == .paused {
                            let resumeResult = loadedAPI.resumeMidlet(
                                createdRuntime,
                                appID: existingAppID
                            )
                            guard resumeResult == 0 else {
                                throw PhoneMECoreError.launchFailed(resumeResult)
                            }
                        }
                        let foregroundResult = loadedAPI.setForeground(
                            createdRuntime,
                            appID: existingAppID,
                            screenWidth: screenWidth,
                            screenHeight: screenHeight
                        )
                        guard foregroundResult == 0 else {
                            throw PhoneMECoreError.launchFailed(foregroundResult)
                        }
                    }
                } else {
                    guard let newAppID = context.nextAvailableAppID() else {
                        throw PhoneMECoreError.tooManyApplications
                    }
                    appID = newAppID
                    context.appIDs[gameID] = newAppID
                    context.gameIDsByAppID[newAppID] = gameID
                    let result = loadedAPI.startMidlet(
                        createdRuntime,
                        suiteID: suiteID!,
                        mainClass: mainClass,
                        appID: newAppID,
                        screenWidth: screenWidth,
                        screenHeight: screenHeight
                    )
                    phoneMEMultitaskingLogger.info(
                        "Started game \(gameID.uuidString, privacy: .public): app=\(newAppID), suite=\(suiteID!), status=\(result)"
                    )
                    guard result == 0 else {
                        context.removeApplication(gameID: gameID)
                        throw PhoneMECoreError.launchFailed(result)
                    }
                }

                guard try Self.waitForForegroundApplication(
                    api: loadedAPI,
                    runtime: createdRuntime,
                    appID: appID,
                    screenWidth: screenWidth,
                    screenHeight: screenHeight,
                    token: currentLaunchToken
                ) else {
                    return
                }

                Self.configureMediaMetadata(
                    title: mediaTitle,
                    artist: mediaArtist,
                    artworkPath: mediaArtworkPath
                )

                DispatchQueue.main.async {
                    guard
                        let self,
                        self.launchIdentifier == currentLaunchIdentifier,
                        self.launchToken === currentLaunchToken,
                        !currentLaunchToken.isCancelled
                    else {
                        return
                    }
                    self.api = loadedAPI
                    self.runtime = createdRuntime
                    self.foregroundGameID = gameID
                    self.foregroundAppID = appID
                    self.pendingForegroundGameID = nil
                    self.runtimeIsSuspended = false
                    self.runtimeSuspensionRequested = false
                    self.setState(.running)
                    self.onApplicationStateChange?(gameID, .active)
                    self.onForegroundApplicationChange?(gameID)
                    self.startPolling(
                        api: loadedAPI,
                        runtime: createdRuntime,
                        launchIdentifier: currentLaunchIdentifier,
                        gameID: gameID,
                        appID: appID
                    )
                    if !self.shouldRunInForeground {
                        self.enterBackground()
                    }
                }
            } catch {
                phoneMEMultitaskingLogger.error(
                    "Launch failed for \(gameID.uuidString, privacy: .public): \(error.localizedDescription, privacy: .public)"
                )
                DispatchQueue.main.async { [weak self] in
                    guard
                        let self,
                        self.launchIdentifier == currentLaunchIdentifier,
                        self.launchToken === currentLaunchToken
                    else {
                        return
                    }
                    self.pendingForegroundGameID = nil
                    self.setState(.failed(error.localizedDescription))
                }
            }
        }
    }

    func hideCurrentApplication() {
        guard let gameID = foregroundGameID ?? pendingForegroundGameID else {
            return
        }

        launchToken.cancel()
        launchIdentifier = UUID()
        stopForegroundPolling()
        foregroundGameID = nil
        foregroundAppID = nil
        pendingForegroundGameID = nil
        onApplicationStateChange?(gameID, .active)
        onForegroundApplicationChange?(nil)

        let context = runtimeContext
        let inputQueue = inputQueue
        let pollQueue = pollQueue
        let renderQueue = renderQueue
        runtimeQueue.async {
            inputQueue.sync { }
            pollQueue.sync { }
            renderQueue.sync { }
            guard
                let api = context.api,
                let runtime = context.runtime
            else {
                return
            }
            _ = api.setForeground(
                runtime,
                appID: nil,
                screenWidth: 1,
                screenHeight: 1
            )
            Self.waitUntilNoForegroundApplication(
                api: api,
                runtime: runtime
            )
        }
    }

    func terminateApplication(gameID: UUID) {
        let context = runtimeContext
        let wasForeground = foregroundGameID == gameID
        let wasPending = pendingForegroundGameID == gameID
        let wasVisible = wasForeground || wasPending
        if wasPending {
            launchToken.cancel()
        }
        if wasVisible {
            launchIdentifier = UUID()
            stopForegroundPolling()
            foregroundGameID = nil
            foregroundAppID = nil
            pendingForegroundGameID = nil
            onForegroundApplicationChange?(nil)
        }

        let inputQueue = inputQueue
        let pollQueue = pollQueue
        let renderQueue = renderQueue
        runtimeQueue.async { [weak self] in
            if wasVisible {
                inputQueue.sync { }
                pollQueue.sync { }
                renderQueue.sync { }
            }
            guard
                let loadedAPI = context.api,
                let createdRuntime = context.runtime,
                let appID = context.appIDs[gameID]
            else {
                return
            }

            _ = loadedAPI.destroyMidlet(createdRuntime, appID: appID)
            context.removeApplication(gameID: gameID)
            let shouldShutdown = context.appIDs.isEmpty
            if shouldShutdown {
                loadedAPI.stop(createdRuntime)
                loadedAPI.destroyRuntime(createdRuntime)
                context.reset()
            }

            DispatchQueue.main.async {
                guard let self else { return }
                self.onApplicationStateChange?(gameID, .destroyed)
                if wasVisible {
                    self.setState(.stopped)
                }
                if shouldShutdown {
                    self.api = nil
                    self.runtime = nil
                    self.runtimeIsSuspended = false
                    self.runtimeSuspensionRequested = false
                }
            }
        }
    }

    func measureApplicationMemoryUsage(
        gameIDs: Set<UUID>,
        completion: @escaping ([UUID: UInt64]) -> Void
    ) {
        guard !gameIDs.isEmpty else {
            completion([:])
            return
        }

        let context = runtimeContext
        runtimeQueue.async {
            guard
                let loadedAPI = context.api,
                let createdRuntime = context.runtime
            else {
                DispatchQueue.main.async { completion([:]) }
                return
            }

            var usageByGameID: [UUID: UInt64] = [:]
            usageByGameID.reserveCapacity(gameIDs.count)
            for gameID in gameIDs {
                guard let appID = context.appIDs[gameID] else { continue }
                if let usedMemory = loadedAPI.midletUsedMemory(
                    createdRuntime,
                    appID: appID,
                    timeoutMilliseconds: 150
                ) {
                    usageByGameID[gameID] = usedMemory
                }
            }

            DispatchQueue.main.async {
                completion(usageByGameID)
            }
        }
    }

    nonisolated private static func ensureRuntime(
        in context: RuntimeContext
    ) throws -> (PhoneMECAPI, PhoneMECAPI.RuntimeHandle) {
        if let api = context.api, let runtime = context.runtime {
            return (api, runtime)
        }
        let api = try PhoneMECAPI.load()
        guard let runtime = api.createRuntime() else {
            throw PhoneMECoreError.runtimeCreationFailed
        }
        context.api = api
        context.runtime = runtime
        return (api, runtime)
    }

    nonisolated private static func waitForForegroundApplication(
        api: PhoneMECAPI,
        runtime: PhoneMECAPI.RuntimeHandle,
        appID: Int32,
        screenWidth: Int,
        screenHeight: Int,
        token: LaunchToken
    ) throws -> Bool {
        var settleDeadline = ProcessInfo.processInfo.systemUptime + 0.25
        var didReassertForeground = false

        while ProcessInfo.processInfo.systemUptime < settleDeadline {
            if token.isCancelled {
                return false
            }

            let state = api.midletState(runtime, appID: appID)
            if state == .destroyed || state == .error {
                throw PhoneMECoreError.foregroundActivationFailed
            }

            // NAMS state/display callbacks are optional and arrive late on a
            // few phoneME builds. Re-assert only after the proxy is known to be
            // active; otherwise retain the original start request and finish a
            // short serialized settle instead of producing a false timeout.
            if state == .active,
               !didReassertForeground,
               api.foregroundAppID(runtime) != appID {
                let result = api.setForeground(
                    runtime,
                    appID: appID,
                    screenWidth: screenWidth,
                    screenHeight: screenHeight
                )
                guard result == 0 else {
                    throw PhoneMECoreError.launchFailed(result)
                }
                didReassertForeground = true
                settleDeadline = ProcessInfo.processInfo.systemUptime + 0.25
            }

            Thread.sleep(forTimeInterval: 0.005)
        }
        return !token.isCancelled
    }

    nonisolated private static func waitUntilNoForegroundApplication(
        api: PhoneMECAPI,
        runtime: PhoneMECAPI.RuntimeHandle,
        timeout: TimeInterval = 2
    ) {
        let deadline = ProcessInfo.processInfo.systemUptime + timeout
        while api.foregroundAppID(runtime) != nil,
              ProcessInfo.processInfo.systemUptime < deadline {
            Thread.sleep(forTimeInterval: 0.005)
        }
    }

    nonisolated private static func configureMediaMetadata(
        title: String,
        artist: String,
        artworkPath: String?
    ) {
        let title = title.trimmingCharacters(in: .whitespacesAndNewlines)
        let artist = artist.trimmingCharacters(in: .whitespacesAndNewlines)
        title.withCString { titlePointer in
            artist.withCString { artistPointer in
                guard let artworkPath else {
                    phoneme_ios_media_set_application_metadata(
                        titlePointer,
                        artistPointer,
                        nil
                    )
                    return
                }
                artworkPath.withCString { artworkPointer in
                    phoneme_ios_media_set_application_metadata(
                        titlePointer,
                        artistPointer,
                        artworkPointer
                    )
                }
            }
        }
    }

    func stop() {
        let currentAPI = api
        let currentRuntime = runtime
        let needsResume = runtimeIsSuspended
            || runtimeSuspensionRequested
        launchToken.cancel()
        launchIdentifier = UUID()
        pendingForegroundGameID = nil
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
            if needsResume {
                currentAPI.resume(currentRuntime)
            }
            currentAPI.stop(currentRuntime)
            currentAPI.destroyRuntime(currentRuntime)
        }
    }

    func enterBackground() {
        shouldRunInForeground = false
        pauseHostPollingForBackground()

        // Do not call midp_suspend() for an iOS scene transition. MIDP turns
        // that into PAUSE_ALL_EVENT/pauseApp(), and many online games close
        // their TCP socket from pauseApp() even though the user only pressed
        // Home. Let iOS suspend the process naturally instead. When the
        // optional Core Location keeper is enabled, the VM thread continues
        // running in the background while rendering and LCDUI polling remain
        // stopped.
    }

    func enterForeground() {
        shouldRunInForeground = true
        if runtimeSuspensionRequested {
            resume()
        } else {
            resumeHostPollingAfterBackground()
        }
    }

    func suspend() {
        shouldRunInForeground = false
        suspendRuntimeIfNeeded()
    }

    private func suspendRuntimeIfNeeded() {
        guard
            !runtimeSuspensionRequested,
            let api,
            let runtime
        else {
            return
        }

        runtimeSuspensionRequested = true
        pauseHostPollingForBackground()

        let currentLaunchIdentifier = launchIdentifier
        let inputQueue = inputQueue
        let pollQueue = pollQueue
        let renderQueue = renderQueue
        runtimeQueue.async { [weak self] in
            // Stop all host-side C access before changing MIDP's global
            // suspend state. midp_suspend()/midp_resume() are not safe to race
            // framebuffer copies or Displayable event polling.
            inputQueue.sync { }
            pollQueue.sync { }
            renderQueue.sync { }
            api.suspend(runtime)

            DispatchQueue.main.async {
                guard
                    let self,
                    self.launchIdentifier == currentLaunchIdentifier,
                    self.runtime == runtime
                else {
                    return
                }
                self.runtimeIsSuspended = true
            }
        }
    }

    func resume() {
        shouldRunInForeground = true
        guard
            runtimeSuspensionRequested,
            let api,
            let runtime
        else {
            resumeHostPollingAfterBackground()
            return
        }

        runtimeSuspensionRequested = false
        let currentLaunchIdentifier = launchIdentifier
        runtimeQueue.async { [weak self] in
            // This command is serialized behind any pending suspend command.
            api.resume(runtime)

            DispatchQueue.main.async {
                guard
                    let self,
                    self.launchIdentifier == currentLaunchIdentifier,
                    self.runtime == runtime
                else {
                    return
                }

                self.runtimeIsSuspended = false
                guard !self.runtimeSuspensionRequested else {
                    return
                }
                self.resumeHostPollingAfterBackground()
            }
        }
    }

    private func pauseHostPollingForBackground() {
        if let pollTimer, !pollTimerIsSuspended {
            pollTimer.suspend()
            pollTimerIsSuspended = true
        }
        fpsFrameCount = 0
        fpsMeasurementStart = Date.timeIntervalSinceReferenceDate
        onFPSChange?(0)
    }

    private func resumeHostPollingAfterBackground() {
        // The MIDlet may continue without repainting while iOS is backgrounded.
        // Force the first visible poll to publish the retained screen again.
        renderRequestBuffer.invalidateFrameGeneration()
        if let pollTimer, pollTimerIsSuspended {
            pollTimer.resume()
            pollTimerIsSuspended = false
        }
        fpsMeasurementStart = Date.timeIntervalSinceReferenceDate
    }

    func sendKey(_ key: J2MEKey, pressed: Bool) {
        guard foregroundAppID != nil, let api, let runtime else { return }
        inputQueue.async {
            api.sendKey(runtime, key: key, pressed: pressed)
        }
    }

    func sendPointer(x: Int32, y: Int32, action: Int32) {
        guard foregroundAppID != nil, let api, let runtime else { return }

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
        guard foregroundAppID != nil, let api, let runtime else { return }
        inputQueue.async {
            api.selectLCDUICommand(runtime, id: id)
        }
    }

    func focusLCDUIItem(_ componentID: Int32) {
        guard foregroundAppID != nil, let api, let runtime else { return }
        inputQueue.async {
            api.focusLCDUIItem(runtime, componentID: componentID)
        }
    }

    func activateLCDUIItem(_ componentID: Int32) {
        guard foregroundAppID != nil, let api, let runtime else { return }
        inputQueue.async {
            api.activateLCDUIItem(runtime, componentID: componentID)
        }
    }

    func setLCDUIText(
        componentID: Int32,
        text: String,
        caretPosition: Int
    ) {
        guard foregroundAppID != nil, let api, let runtime else { return }
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
        guard foregroundAppID != nil, let api, let runtime else { return }
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
        guard foregroundAppID != nil, let api, let runtime else { return }
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
        guard foregroundAppID != nil, let api, let runtime else { return }
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
        guard foregroundAppID != nil, let api, let runtime else { return }
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
        launchIdentifier: UUID,
        gameID: UUID? = nil,
        appID: Int32? = nil
    ) {
        if let pollTimer {
            if pollTimerIsSuspended {
                pollTimer.resume()
                pollTimerIsSuspended = false
            }
            pollTimer.cancel()
        }

        let context = PollContext(
            framesPerSecond: framePollingFramesPerSecond
        )
        let renderBuffer = renderRequestBuffer
        let renderQueue = renderQueue
        let timer = DispatchSource.makeTimerSource(queue: pollQueue)

        // Poll at the capped Canvas cadence. Static native Form/List/Alert
        // screens back off to 10 Hz after half a second without bridge events;
        // key and pointer input still wake the VM immediately through the native
        // event pipe and do not depend on this timer.
        timer.schedule(
            deadline: .now(),
            repeating: .nanoseconds(
                Int(context.activePollIntervalNanoseconds)
            ),
            leeway: .nanoseconds(
                Int(min(
                    max(context.activePollIntervalNanoseconds / 8, 500_000),
                    2_000_000
                ))
            )
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

            if let gameID, let appID {
                let applicationState = api.midletState(runtime, appID: appID)
                if applicationState == .destroyed || applicationState == .error {
                    context.didReportRuntimeExit = true
                    DispatchQueue.main.async {
                        guard
                            let self,
                            self.launchIdentifier == launchIdentifier,
                            self.runtime == runtime
                        else {
                            return
                        }
                        self.finishApplication(
                            gameID: gameID,
                            state: applicationState
                        )
                    }
                    return
                }
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

            let isIdleNativeScreen =
                resolvedVisibleScreen?.usesNativeLCDUI == true
                && lcdUIEvents.isEmpty
            if isIdleNativeScreen {
                context.consecutiveIdleNativePolls += 1
                if !context.usesIdleNativeCadence,
                   context.consecutiveIdleNativePolls
                    >= context.idleNativePollThreshold {
                    context.usesIdleNativeCadence = true
                    timer.schedule(
                        deadline: .now() + .nanoseconds(
                            Int(context.idleNativePollIntervalNanoseconds)
                        ),
                        repeating: .nanoseconds(
                            Int(context.idleNativePollIntervalNanoseconds)
                        ),
                        leeway: .milliseconds(4)
                    )
                }
            } else {
                context.consecutiveIdleNativePolls = 0
                if context.usesIdleNativeCadence {
                    context.usesIdleNativeCadence = false
                    timer.schedule(
                        deadline: .now(),
                        repeating: .nanoseconds(
                            Int(context.activePollIntervalNanoseconds)
                        ),
                        leeway: .nanoseconds(
                            Int(min(
                                max(
                                    context.activePollIntervalNanoseconds / 8,
                                    500_000
                                ),
                                2_000_000
                            ))
                        )
                    )
                }
            }

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

    private func finishApplication(
        gameID: UUID,
        state: PhoneMECAPI.AppState
    ) {
        let context = runtimeContext
        launchToken.cancel()
        stopForegroundPolling()
        foregroundGameID = nil
        foregroundAppID = nil
        if pendingForegroundGameID == gameID {
            pendingForegroundGameID = nil
        }
        onApplicationStateChange?(gameID, state)
        onForegroundApplicationChange?(nil)
        setState(
            state == .error
                ? .failed("The J2ME application stopped unexpectedly.")
                : .stopped
        )
        runtimeQueue.async {
            context.removeApplication(gameID: gameID)
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
        let context = runtimeContext
        runtimeQueue.async {
            inputQueue.sync { }
            pollQueue.sync { }
            renderQueue.sync { }
            api.destroyRuntime(runtime)
            context.reset()
        }

        if exitCode < 0 {
            setState(.failed(
                PhoneMECoreError.launchFailed(exitCode).localizedDescription
            ))
        } else {
            setState(.stopped)
        }
    }

    private func stopForegroundPolling() {
        if let pollTimer {
            if pollTimerIsSuspended {
                pollTimer.resume()
            }
            pollTimer.cancel()
        }
        pollTimer = nil
        pollTimerIsSuspended = false
        lastFrameGeneration = 0
        continuousInputBuffer.reset()
        renderRequestBuffer = RenderRequestBuffer()
        fpsFrameCount = 0
        fpsMeasurementStart = Date.timeIntervalSinceReferenceDate
        onFPSChange?(0)
    }

    private func clearCurrentRuntime() {
        launchToken.cancel()
        pendingForegroundGameID = nil
        foregroundGameID = nil
        foregroundAppID = nil
        if let pollTimer {
            // Dispatch sources must be resumed before their final cancellation.
            if pollTimerIsSuspended {
                pollTimer.resume()
            }
            pollTimer.cancel()
        }
        pollTimer = nil
        pollTimerIsSuspended = false
        runtimeIsSuspended = false
        runtimeSuspensionRequested = false
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
