import CoreGraphics
import Foundation

private final class PhoneMEPixelBufferPool: @unchecked Sendable {
    private struct Buffer {
        let pointer: UnsafeMutableRawPointer
        let capacity: Int
    }

    private let lock = NSLock()
    private let maximumRetainedBuffers: Int
    private var available: [Buffer] = []

    init(maximumRetainedBuffers: Int) {
        self.maximumRetainedBuffers = max(maximumRetainedBuffers, 1)
    }

    func acquire(minimumCapacity: Int) -> PhoneMEPixelBufferLease {
        let requiredCapacity = max(minimumCapacity, 1)
        lock.lock()
        var bestIndex: Int?
        for index in available.indices
            where available[index].capacity >= requiredCapacity {
            guard let currentBest = bestIndex else {
                bestIndex = index
                continue
            }
            if available[index].capacity < available[currentBest].capacity {
                bestIndex = index
            }
        }

        if let bestIndex {
            let buffer = available.remove(at: bestIndex)
            lock.unlock()
            return PhoneMEPixelBufferLease(
                pointer: buffer.pointer,
                capacity: buffer.capacity,
                pool: self
            )
        }
        lock.unlock()

        let roundedCapacity = (requiredCapacity + 4_095) & ~4_095
        return PhoneMEPixelBufferLease(
            pointer: UnsafeMutableRawPointer.allocate(
                byteCount: roundedCapacity,
                alignment: 64
            ),
            capacity: roundedCapacity,
            pool: self
        )
    }

    fileprivate func recycle(
        pointer: UnsafeMutableRawPointer,
        capacity: Int
    ) {
        lock.lock()
        if available.count < maximumRetainedBuffers {
            available.append(Buffer(pointer: pointer, capacity: capacity))
            lock.unlock()
        } else {
            lock.unlock()
            pointer.deallocate()
        }
    }

    deinit {
        for buffer in available {
            buffer.pointer.deallocate()
        }
    }
}

private final class PhoneMEPixelBufferLease: @unchecked Sendable {
    let pointer: UnsafeMutableRawPointer
    let capacity: Int
    private var pool: PhoneMEPixelBufferPool?

    init(
        pointer: UnsafeMutableRawPointer,
        capacity: Int,
        pool: PhoneMEPixelBufferPool
    ) {
        self.pointer = pointer
        self.capacity = capacity
        self.pool = pool
    }

    deinit {
        pool?.recycle(pointer: pointer, capacity: capacity)
        pool = nil
    }
}

private let phoneMEPixelBufferRelease: CGDataProviderReleaseDataCallback = {
    info,
    _,
    _ in
    guard let info else { return }
    Unmanaged<PhoneMEPixelBufferLease>.fromOpaque(info).release()
}

final class PhoneMECAPI: @unchecked Sendable {
    struct RuntimeHandle: @unchecked Sendable, Equatable {
        fileprivate let rawValue: UnsafeMutableRawPointer
    }

    enum AppState: Int32, Sendable {
        case none = 0
        case active = 1
        case paused = 2
        case destroyed = 3
        case error = 4
    }

    enum PushBackgroundPolicy: Int32, Sendable {
        case foregroundOnly = 0
        case systemManaged = 1
    }

    enum PushRequestKind: Int32, Sendable {
        case connection = 1
        case alarm = 2
    }

    struct PushLaunchRequest: Sendable {
        let requestID: UInt64
        let kind: PushRequestKind
        let createdAtMillis: Int64
        let target: String
        let midlet: String
    }

    struct LCDUIEvent: Sendable {
        let kind: Int32
        let componentID: Int32
        let parentID: Int32
        let componentType: Int32
        let index: Int32
        let arguments: (Int32, Int32, Int32, Int32)
        let value64: Int64
        let generation: UInt64
        let text: String
        let detail: String
    }

    private static let rgbColorSpace = CGColorSpaceCreateDeviceRGB()

    private let layout: PhoneMERuntimeLayout
    private let frameBufferPool = PhoneMEPixelBufferPool(
        maximumRetainedBuffers: 3
    )
    private let imageBufferPool = PhoneMEPixelBufferPool(
        maximumRetainedBuffers: 12
    )

    private init(layout: PhoneMERuntimeLayout) {
        self.layout = layout
    }

    static func load() throws -> PhoneMECAPI {
        PhoneMECAPI(layout: try PhoneMERuntimeResources.prepare())
    }

    static func load(gameID: UUID) throws -> PhoneMECAPI {
        _ = gameID
        return try load()
    }

    func createRuntime() -> RuntimeHandle? {
        guard let rawRuntime = phoneme_create() else {
            return nil
        }

        let result = layout.homeURL.path.withCString { homePath in
            phoneme_configure(rawRuntime, homePath, nil)
        }

        guard result == 0 else {
            phoneme_destroy(rawRuntime)
            return nil
        }
        return RuntimeHandle(rawValue: rawRuntime)
    }

    func destroyRuntime(_ runtime: RuntimeHandle?) {
        phoneme_destroy(runtime?.rawValue)
    }

    func configureKeymap(
        _ runtime: RuntimeHandle?,
        up: Int32,
        down: Int32,
        left: Int32,
        right: Int32,
        fire: Int32,
        softLeft: Int32,
        softRight: Int32
    ) -> Int32 {
        phoneme_configure_keymap(
            runtime?.rawValue,
            up,
            down,
            left,
            right,
            fire,
            softLeft,
            softRight
        )
    }

    func installJar(
        _ runtime: RuntimeHandle?,
        jarURL: URL
    ) -> (status: Int32, suiteID: Int32?) {
        var suiteID: Int32 = 0
        let status = jarURL.path.withCString { path in
            phoneme_install_jar(runtime?.rawValue, path, &suiteID)
        }
        return (status, status == 0 ? suiteID : nil)
    }

    func lastInstallStage() -> Int32 {
        phoneme_last_install_stage()
    }

    func lastSuiteStoreStage() -> Int32 {
        phoneme_last_suite_store_stage()
    }

    func startSystem(_ runtime: RuntimeHandle?) -> Int32 {
        phoneme_start_system(runtime?.rawValue)
    }

    func startMidlet(
        _ runtime: RuntimeHandle?,
        suiteID: Int32,
        mainClass: String,
        appID: Int32,
        screenWidth: Int,
        screenHeight: Int
    ) -> Int32 {
        mainClass.withCString { className in
            phoneme_start_midlet(
                runtime?.rawValue,
                suiteID,
                className,
                appID,
                Int32(clamping: screenWidth),
                Int32(clamping: screenHeight)
            )
        }
    }

    func setForeground(
        _ runtime: RuntimeHandle?,
        appID: Int32?,
        screenWidth: Int,
        screenHeight: Int
    ) -> Int32 {
        phoneme_set_foreground(
            runtime?.rawValue,
            appID ?? 0,
            Int32(clamping: screenWidth),
            Int32(clamping: screenHeight)
        )
    }

    func pauseMidlet(_ runtime: RuntimeHandle?, appID: Int32) -> Int32 {
        phoneme_pause_midlet(runtime?.rawValue, appID)
    }

    func resumeMidlet(_ runtime: RuntimeHandle?, appID: Int32) -> Int32 {
        phoneme_resume_midlet(runtime?.rawValue, appID)
    }

    func destroyMidlet(_ runtime: RuntimeHandle?, appID: Int32) -> Int32 {
        phoneme_destroy_midlet(runtime?.rawValue, appID)
    }

    func setPushBackgroundPolicy(
        _ runtime: RuntimeHandle?,
        suiteID: Int32,
        policy: PushBackgroundPolicy
    ) -> Int32 {
        phoneme_push_set_background_policy(
            runtime?.rawValue,
            suiteID,
            policy.rawValue
        )
    }

    func notifyPushConnectionAvailable(
        _ runtime: RuntimeHandle?,
        suiteID: Int32,
        connection: String,
        sourceAddress: String? = nil,
        receivedAtMillis: Int64
    ) -> Int32 {
        connection.withCString { connectionValue in
            guard let sourceAddress else {
                return phoneme_push_notify_connection_available(
                    runtime?.rawValue,
                    suiteID,
                    connectionValue,
                    receivedAtMillis
                )
            }
            return sourceAddress.withCString { sourceValue in
                phoneme_push_notify_connection_available_from_source(
                    runtime?.rawValue,
                    suiteID,
                    connectionValue,
                    sourceValue,
                    receivedAtMillis
                )
            }
        }
    }

    func pollPushLaunchRequests(
        _ runtime: RuntimeHandle?,
        suiteID: Int32,
        nowMillis: Int64,
        backgroundExecutionGranted: Bool,
        maximumCount: Int = 32
    ) -> [PushLaunchRequest] {
        guard let runtime, maximumCount > 0 else { return [] }
        var rawRequests = Array(
            repeating: PhoneMEPushLaunchRequest(),
            count: maximumCount
        )
        let copied = rawRequests.withUnsafeMutableBufferPointer { buffer in
            phoneme_push_poll_launch_requests(
                runtime.rawValue,
                suiteID,
                nowMillis,
                backgroundExecutionGranted ? 1 : 0,
                buffer.baseAddress,
                Int32(clamping: buffer.count)
            )
        }
        guard copied > 0 else { return [] }
        return rawRequests.prefix(Int(copied)).compactMap { raw in
            guard let kind = PushRequestKind(rawValue: raw.kind) else {
                return nil
            }
            var target = raw.target
            var midlet = raw.midlet
            return PushLaunchRequest(
                requestID: raw.request_id,
                kind: kind,
                createdAtMillis: raw.created_at_millis,
                target: Self.string(from: &target),
                midlet: Self.string(from: &midlet)
            )
        }
    }

    func acknowledgePushLaunchRequest(
        _ runtime: RuntimeHandle?,
        suiteID: Int32,
        requestID: UInt64
    ) -> Int32 {
        phoneme_push_acknowledge_launch_request(
            runtime?.rawValue,
            suiteID,
            requestID
        )
    }

    func midletState(
        _ runtime: RuntimeHandle?,
        appID: Int32
    ) -> AppState {
        AppState(rawValue: phoneme_midlet_state(runtime?.rawValue, appID))
            ?? .none
    }

    func foregroundAppID(_ runtime: RuntimeHandle?) -> Int32? {
        let appID = phoneme_foreground_app_id(runtime?.rawValue)
        return appID > 0 ? appID : nil
    }

    func midletUsedMemory(
        _ runtime: RuntimeHandle?,
        appID: Int32,
        timeoutMilliseconds: Int32 = 200
    ) -> UInt64? {
        let bytes = phoneme_midlet_used_memory(
            runtime?.rawValue,
            appID,
            timeoutMilliseconds
        )
        return bytes >= 0 ? UInt64(bytes) : nil
    }

    func startJar(
        _ runtime: RuntimeHandle?,
        jarURL: URL,
        mainClass: String,
        screenWidth: Int,
        screenHeight: Int
    ) -> Int32 {
        jarURL.path.withCString { path in
            mainClass.withCString { className in
                phoneme_start_jar(
                    runtime?.rawValue,
                    path,
                    className,
                    Int32(clamping: screenWidth),
                    Int32(clamping: screenHeight)
                )
            }
        }
    }

    func stop(_ runtime: RuntimeHandle?) {
        phoneme_stop(runtime?.rawValue)
    }

    func suspend(_ runtime: RuntimeHandle?) {
        phoneme_suspend(runtime?.rawValue)
    }

    func resume(_ runtime: RuntimeHandle?) {
        phoneme_resume(runtime?.rawValue)
    }

    func isRunning(_ runtime: RuntimeHandle?) -> Bool {
        phoneme_is_running(runtime?.rawValue) != 0
    }

    func lastExitCode(_ runtime: RuntimeHandle?) -> Int32 {
        phoneme_last_exit_code(runtime?.rawValue)
    }

    func sendKey(_ runtime: RuntimeHandle?, key: J2MEKey, pressed: Bool) {
        phoneme_send_key(runtime?.rawValue, key.rawValue, pressed ? 1 : 0)
    }

    func sendPointer(
        _ runtime: RuntimeHandle?,
        x: Int32,
        y: Int32,
        action: Int32
    ) {
        phoneme_send_pointer(runtime?.rawValue, x, y, action)
    }

    func drainLCDUIEvents(
        _ runtime: RuntimeHandle?,
        maximumCount: Int = 512
    ) -> [LCDUIEvent] {
        guard let runtime, maximumCount > 0 else { return [] }

        var events: [LCDUIEvent] = []
        events.reserveCapacity(min(maximumCount, 64))
        var rawEvent = PhoneMELCDUIEvent()
        while events.count < maximumCount,
              phoneme_poll_lcdui_event(runtime.rawValue, &rawEvent) != 0 {
            var text = rawEvent.text
            var detail = rawEvent.detail
            events.append(
                LCDUIEvent(
                    kind: rawEvent.kind,
                    componentID: rawEvent.component_id,
                    parentID: rawEvent.parent_id,
                    componentType: rawEvent.component_type,
                    index: rawEvent.index,
                    arguments: (
                        rawEvent.arg0,
                        rawEvent.arg1,
                        rawEvent.arg2,
                        rawEvent.arg3
                    ),
                    value64: rawEvent.value64,
                    generation: rawEvent.generation,
                    text: Self.string(from: &text),
                    detail: Self.string(from: &detail)
                )
            )
            rawEvent = PhoneMELCDUIEvent()
        }
        return events
    }

    func selectLCDUICommand(_ runtime: RuntimeHandle?, id: Int32) {
        phoneme_lcdui_select_command(runtime?.rawValue, id)
    }

    func focusLCDUIItem(_ runtime: RuntimeHandle?, componentID: Int32) {
        phoneme_lcdui_focus_item(runtime?.rawValue, componentID)
    }

    func activateLCDUIItem(_ runtime: RuntimeHandle?, componentID: Int32) {
        phoneme_lcdui_activate_item(runtime?.rawValue, componentID)
    }

    func setLCDUIText(
        _ runtime: RuntimeHandle?,
        componentID: Int32,
        text: String,
        caretPosition: Int
    ) {
        text.withCString { value in
            phoneme_lcdui_set_text(
                runtime?.rawValue,
                componentID,
                value,
                Int32(clamping: caretPosition)
            )
        }
    }

    func setLCDUIChoice(
        _ runtime: RuntimeHandle?,
        componentID: Int32,
        index: Int,
        selected: Bool
    ) {
        phoneme_lcdui_set_choice(
            runtime?.rawValue,
            componentID,
            Int32(clamping: index),
            selected ? 1 : 0
        )
    }

    func setLCDUIGauge(
        _ runtime: RuntimeHandle?,
        componentID: Int32,
        value: Int
    ) {
        phoneme_lcdui_set_gauge(
            runtime?.rawValue,
            componentID,
            Int32(clamping: value)
        )
    }

    func setLCDUIDate(
        _ runtime: RuntimeHandle?,
        componentID: Int32,
        date: Date
    ) {
        phoneme_lcdui_set_date(
            runtime?.rawValue,
            componentID,
            Int64(date.timeIntervalSince1970)
        )
    }

    func setLCDUIScrollPosition(
        _ runtime: RuntimeHandle?,
        position: Int
    ) {
        phoneme_lcdui_set_scroll_position(
            runtime?.rawValue,
            Int32(clamping: position)
        )
    }

    private static func string<T>(from tuple: inout T) -> String {
        withUnsafePointer(to: &tuple) { pointer in
            pointer.withMemoryRebound(
                to: CChar.self,
                capacity: 768
            ) { characters in
                String(cString: characters)
            }
        }
    }

    func copyLCDUIImage(
        _ runtime: RuntimeHandle?,
        componentID: Int32
    ) -> (image: CGImage, generation: UInt64)? {
        var width: Int32 = 0
        var height: Int32 = 0
        var generation: UInt64 = 0
        let requiredByteCount = phoneme_copy_lcdui_image_rgba(
            runtime?.rawValue,
            componentID,
            nil,
            0,
            &width,
            &height,
            &generation
        )

        guard requiredByteCount > 0, width > 0, height > 0 else {
            return nil
        }

        let lease = imageBufferPool.acquire(
            minimumCapacity: Int(requiredByteCount)
        )
        let writtenByteCount = phoneme_copy_lcdui_image_rgba(
            runtime?.rawValue,
            componentID,
            lease.pointer.assumingMemoryBound(to: UInt8.self),
            Int32(clamping: lease.capacity),
            &width,
            &height,
            &generation
        )

        guard
            writtenByteCount == requiredByteCount,
            let image = Self.makeImage(
                lease: lease,
                byteCount: Int(writtenByteCount),
                width: Int(width),
                height: Int(height),
                shouldInterpolate: true,
                isOpaque: false
            )
        else {
            return nil
        }
        return (image, generation)
    }

    func copyFrame(
        _ runtime: RuntimeHandle?,
        after previousGeneration: UInt64
    ) -> (image: CGImage, generation: UInt64)? {
        var width: Int32 = 0
        var height: Int32 = 0
        var generation: UInt64 = 0
        let requiredByteCount = phoneme_copy_frame_rgba(
            runtime?.rawValue,
            nil,
            0,
            &width,
            &height,
            &generation
        )

        guard
            requiredByteCount > 0,
            width > 0,
            height > 0,
            generation != previousGeneration
        else {
            return nil
        }

        // The provider retains this lease until Core Graphics releases the
        // image. Returned storage is recycled instead of allocating a fresh
        // Data object for every changed framebuffer generation.
        let lease = frameBufferPool.acquire(
            minimumCapacity: Int(requiredByteCount)
        )
        let writtenByteCount = phoneme_copy_frame_rgba(
            runtime?.rawValue,
            lease.pointer.assumingMemoryBound(to: UInt8.self),
            Int32(clamping: lease.capacity),
            &width,
            &height,
            &generation
        )

        guard
            writtenByteCount == requiredByteCount,
            Int(width) * Int(height) * 4 <= Int(writtenByteCount),
            let image = Self.makeImage(
                lease: lease,
                byteCount: Int(writtenByteCount),
                width: Int(width),
                height: Int(height),
                shouldInterpolate: false,
                isOpaque: true
            )
        else {
            return nil
        }

        return (image, generation)
    }

    private static func makeImage(
        lease: PhoneMEPixelBufferLease,
        byteCount: Int,
        width: Int,
        height: Int,
        shouldInterpolate: Bool,
        isOpaque: Bool
    ) -> CGImage? {
        guard
            width > 0,
            height > 0,
            byteCount > 0,
            width * height * 4 <= byteCount,
            byteCount <= lease.capacity
        else {
            return nil
        }

        let retainedLease = Unmanaged.passRetained(lease).toOpaque()
        guard let provider = CGDataProvider(
            dataInfo: retainedLease,
            data: lease.pointer,
            size: byteCount,
            releaseData: phoneMEPixelBufferRelease
        ) else {
            Unmanaged<PhoneMEPixelBufferLease>
                .fromOpaque(retainedLease)
                .release()
            return nil
        }
        return CGImage(
            width: width,
            height: height,
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: width * 4,
            space: rgbColorSpace,
            bitmapInfo: CGBitmapInfo(
                rawValue: (
                    isOpaque
                        ? CGImageAlphaInfo.noneSkipLast
                        : CGImageAlphaInfo.last
                ).rawValue
            ),
            provider: provider,
            decode: nil,
            shouldInterpolate: shouldInterpolate,
            intent: .defaultIntent
        )
    }
}

enum PhoneMECoreError: LocalizedError {
    case runtimeNotLinked
    case runtimeResourcesMissing
    case runtimeCreationFailed
    case mainClassMissing
    case applicationNotPrepared
    case tooManyApplications
    case foregroundActivationFailed
    case launchFailed(Int32)

    var errorDescription: String? {
        switch self {
        case .runtimeNotLinked:
            return "Runtime is not available."
        case .runtimeResourcesMissing:
            return "The bundled phoneME runtime resources are missing."
        case .runtimeCreationFailed:
            return "Failed to create runtime."
        case .mainClassMissing:
            return "The JAR manifest does not contain a valid MIDlet-1 class."
        case .applicationNotPrepared:
            return "This application was imported after multitasking started. Close the running applications once so it can be prepared."
        case .tooManyApplications:
            return "The maximum of 64 simultaneous J2ME applications has been reached."
        case .foregroundActivationFailed:
            return "The J2ME application could not become active in the foreground."
        case let .launchFailed(code):
            return "Failed to start application (error \(code))."
        }
    }
}
