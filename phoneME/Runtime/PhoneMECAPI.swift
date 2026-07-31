import CoreGraphics
import Foundation

final class PhoneMECAPI: @unchecked Sendable {
    struct RuntimeHandle: @unchecked Sendable, Equatable {
        fileprivate let rawValue: UnsafeMutableRawPointer
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

    private init(layout: PhoneMERuntimeLayout) {
        self.layout = layout
    }

    static func load(gameID: UUID) throws -> PhoneMECAPI {
        PhoneMECAPI(layout: try PhoneMERuntimeResources.prepare(for: gameID))
    }

    func createRuntime() -> RuntimeHandle? {
        guard let rawRuntime = phoneme_create() else {
            return nil
        }

        let result = layout.homeURL.path.withCString { homePath in
            layout.classesURL.path.withCString { classesPath in
                phoneme_configure(rawRuntime, homePath, classesPath)
            }
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

        var pixels = Data(count: Int(requiredByteCount))
        let writtenByteCount = pixels.withUnsafeMutableBytes { rawBuffer in
            let buffer = rawBuffer.bindMemory(to: UInt8.self)
            return phoneme_copy_lcdui_image_rgba(
                runtime?.rawValue,
                componentID,
                buffer.baseAddress,
                Int32(buffer.count),
                &width,
                &height,
                &generation
            )
        }

        guard
            writtenByteCount == requiredByteCount,
            let image = Self.makeImage(
                pixels: pixels,
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

        // Fill provider-owned storage directly. The previous Array -> Data
        // conversion allocated and copied a second full RGBA frame every tick.
        var pixels = Data(count: Int(requiredByteCount))
        let writtenByteCount = pixels.withUnsafeMutableBytes { rawBuffer in
            let buffer = rawBuffer.bindMemory(to: UInt8.self)
            return phoneme_copy_frame_rgba(
                runtime?.rawValue,
                buffer.baseAddress,
                Int32(buffer.count),
                &width,
                &height,
                &generation
            )
        }

        guard
            writtenByteCount == requiredByteCount,
            Int(width) * Int(height) * 4 <= pixels.count,
            let image = Self.makeImage(
                pixels: pixels,
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
        pixels: Data,
        width: Int,
        height: Int,
        shouldInterpolate: Bool,
        isOpaque: Bool
    ) -> CGImage? {
        guard width > 0, height > 0, width * height * 4 <= pixels.count else {
            return nil
        }
        // CGDataProvider retains the immutable CFData backing store. Avoid a
        // second full RGBA copy for every frame and every native LCDUI image.
        guard let provider = CGDataProvider(data: pixels as CFData) else {
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
        case let .launchFailed(code):
            return "Failed to start application (error \(code))."
        }
    }
}
