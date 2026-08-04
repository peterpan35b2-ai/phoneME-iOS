import Foundation

struct LCDUIState: Equatable, Sendable {
    struct Screen: Equatable, Sendable {
        var id: Int32
        var type: ComponentType
        var title: String
        var detail: String
        var contentWidth: Int
        var contentHeight: Int
        var scrollPosition: Int
        var isVisible: Bool
        var isFullScreen: Bool
        var nativeKind: ScreenKind?
    }

    struct Item: Equatable, Identifiable, Sendable {
        var id: Int32
        var parentID: Int32
        var formIndex: Int = -1
        var type: ComponentType
        var label: String
        var text: String
        var frame: CGRectData
        var isVisible: Bool
        var layout: Int = 0
        var appearanceMode: Int = 0
        var fitPolicy: Int = 0
        var isFocused: Bool = false
        var maxSize: Int = 0
        var constraints: Int = 0
        var caretPosition: Int = 0
        var value: Int = 0
        var maxValue: Int = 100
        var isInteractive: Bool = false
        var inputMode: Int = 0
        var fontFace: Int = 0
        var fontStyle: Int = 0
        var fontSize: Int = 0
        var date: Date = Date(timeIntervalSince1970: 0)
        var imageWidth: Int = 0
        var imageHeight: Int = 0
        var imageGeneration: UInt64 = 0
        var choices: [Choice] = []

        var orderedChoices: [Choice] {
            // Choice events are inserted in index order by applyChoice(_:).
            // Returning the stored array avoids sorting the same choices every
            // time SwiftUI evaluates a row or selection indicator.
            choices
        }
    }

    struct Choice: Equatable, Identifiable, Sendable {
        var index: Int
        var text: String
        var isSelected: Bool
        var imageKey: Int32?
        var fontFace: Int
        var fontStyle: Int
        var fontSize: Int

        var id: Int { index }
    }

    struct Command: Equatable, Identifiable, Sendable {
        var id: Int32
        var label: String
        var longLabel: String
        var type: Int
        var priority: Int
        var scope: Int
        var ownerID: Int32
        var order: Int

        var isItemScoped: Bool { scope == 1 || ownerID != 0 }
        var isListItemCommand: Bool { type == 8 }

        var negativeSoftKeyRank: Int? {
            switch type {
            case 3: return 0 // CANCEL
            case 2: return 1 // BACK
            case 6: return 2 // STOP
            case 7: return 3 // EXIT
            default: return nil
            }
        }
    }

    struct CommandLayout: Equatable, Sendable {
        var leftCommands: [Command]
        var rightCommand: Command?
    }

    struct CGRectData: Equatable, Sendable {
        var x: Int = 0
        var y: Int = 0
        var width: Int = 0
        var height: Int = 0
    }

    enum ScreenKind: Int32, Equatable, Sendable {
        case form = 0
        case list = 1
        case textBox = 2
        case alert = 3
        case menu = 4
    }

    enum ComponentType: Int32, Equatable, Sendable {
        case exclusiveChoice = 0
        case multipleChoice = 1
        case implicitChoice = 2
        case popupChoice = 3
        case customItem = 4
        case dateField = 5
        case progressGauge = 6
        case interactiveGauge = 7
        case plainImage = 8
        case hyperlinkImage = 9
        case buttonImage = 10
        case spacer = 11
        case plainString = 12
        case hyperlinkString = 13
        case buttonString = 14
        case textField = 15
        case nullAlert = 16
        case infoAlert = 17
        case warningAlert = 18
        case errorAlert = 19
        case alarmAlert = 20
        case confirmationAlert = 21
        case canvas = 22
        case form = 23
        case menu = 24

        var isAlert: Bool {
            rawValue >= ComponentType.nullAlert.rawValue
                && rawValue <= ComponentType.confirmationAlert.rawValue
        }

        var isChoice: Bool {
            switch self {
            case .exclusiveChoice, .multipleChoice, .implicitChoice, .popupChoice:
                return true
            default:
                return false
            }
        }
    }

    private static let imageMetadataMarker: Int32 = -1004
    private static let screenKindMetadataMarker: Int32 = -1006
    private static let alertMetadataMarker: Int32 = -1009
    private static let screenModeMetadataMarker: Int32 = -1007

    private enum EventKind: Int32 {
        case reset = 1
        case screenCreated = 2
        case screenUpdated = 3
        case screenShown = 4
        case screenHidden = 5
        case screenDeleted = 6
        case itemCreated = 7
        case itemUpdated = 8
        case itemShown = 9
        case itemHidden = 10
        case itemDeleted = 11
        case choiceElement = 12
        case choiceDeleted = 13
        case commandsReset = 14
        case command = 15
        case itemFocused = 16
    }

    var screen: Screen?
    private var screens: [Int32: Screen] = [:]
    var items: [Int32: Item] = [:]
    var commands: [Command] = []
    var focusedItemID: Int32?
    var generation: UInt64 = 0

    static let empty = LCDUIState()

    static func == (lhs: LCDUIState, rhs: LCDUIState) -> Bool {
        // `generation` is transport bookkeeping and does not affect anything
        // rendered by SwiftUI. Ignoring it prevents an otherwise identical
        // native LCDUI event from invalidating the entire screen.
        lhs.screen == rhs.screen
            && lhs.items == rhs.items
            && lhs.commands == rhs.commands
            && lhs.focusedItemID == rhs.focusedItemID
    }

    var isCanvasVisible: Bool {
        screen?.isVisible == true && screen?.type == .canvas
    }

    var hasNativeScreen: Bool {
        guard let screen else { return false }
        return screen.isVisible && screen.type != .canvas
    }

    var isCanvasFullScreen: Bool {
        isCanvasVisible && screen?.isFullScreen == true
    }

    var screenKind: ScreenKind {
        guard let screen else { return .form }
        if screen.type.isAlert {
            return .alert
        }
        if screen.type == .menu {
            return .menu
        }
        if let nativeKind = screen.nativeKind {
            return nativeKind
        }
        let items = visibleItems
        if screen.type == .form, items.count == 1, let item = items.first {
            if item.label.isEmpty, item.type.isChoice {
                return .list
            }
            if item.label.isEmpty, item.type == .textField {
                return .textBox
            }
        }
        return .form
    }

    var visibleItems: [Item] {
        guard let screen else { return [] }
        return items.values
            .filter { $0.parentID == screen.id && $0.isVisible }
            .sorted {
                let lhsIndex = $0.formIndex >= 0 ? $0.formIndex : Int.max
                let rhsIndex = $1.formIndex >= 0 ? $1.formIndex : Int.max
                if lhsIndex != rhsIndex {
                    return lhsIndex < rhsIndex
                }
                if $0.frame.y != $1.frame.y {
                    return $0.frame.y < $1.frame.y
                }
                return $0.id < $1.id
            }
    }

    var orderedCommands: [Command] {
        commands.sorted {
            // The phoneME command manager has already applied MIDP's command
            // type weights before emitting this order. Re-sorting by priority
            // here changes SCREEN/ITEM/OK/BACK placement and swaps soft keys.
            if $0.order != $1.order {
                return $0.order < $1.order
            }
            if $0.priority != $1.priority {
                return $0.priority < $1.priority
            }
            return $0.id < $1.id
        }
    }

    var listItemCommands: [Command] {
        guard screenKind == .list else { return [] }
        return orderedCommands.filter(\.isListItemCommand)
    }

    var commandLayout: CommandLayout {
        let ordered = screenKind == .list
            ? orderedCommands.filter { !$0.isListItemCommand }
            : orderedCommands
        let rightCommand = ordered
            .filter { $0.negativeSoftKeyRank != nil }
            .min {
                let lhsRank = $0.negativeSoftKeyRank ?? Int.max
                let rhsRank = $1.negativeSoftKeyRank ?? Int.max
                if lhsRank != rhsRank {
                    return lhsRank < rhsRank
                }
                if $0.priority != $1.priority {
                    return $0.priority < $1.priority
                }
                return $0.order < $1.order
            }

        return CommandLayout(
            leftCommands: ordered.filter { $0.id != rightCommand?.id },
            rightCommand: rightCommand
        )
    }

    mutating func apply(_ event: PhoneMECAPI.LCDUIEvent) {
        generation = max(generation, event.generation)
        guard let kind = EventKind(rawValue: event.kind) else { return }

        switch kind {
        case .reset:
            self = .empty
            generation = event.generation

        case .screenCreated, .screenUpdated, .screenShown:
            applyScreen(event, kind: kind)

        case .screenHidden:
            if var cached = screens[event.componentID] {
                cached.isVisible = false
                screens[event.componentID] = cached
            }
            if screen?.id == event.componentID {
                screen?.isVisible = false
            }

        case .screenDeleted:
            screens.removeValue(forKey: event.componentID)
            if screen?.id == event.componentID {
                screen = nil
            }
            items = items.filter { $0.value.parentID != event.componentID }
            focusedItemID = nil

        case .itemCreated, .itemUpdated, .itemShown:
            applyItem(event, kind: kind)

        case .itemHidden:
            items[event.componentID]?.isVisible = false

        case .itemDeleted:
            items.removeValue(forKey: event.componentID)
            if focusedItemID == event.componentID {
                focusedItemID = nil
            }

        case .itemFocused:
            focusedItemID = event.componentID
            for id in items.keys {
                items[id]?.isFocused = id == event.componentID
            }

        case .choiceElement:
            applyChoice(event)

        case .choiceDeleted:
            if event.index < 0 {
                items[event.componentID]?.choices.removeAll()
            } else {
                items[event.componentID]?.choices.removeAll {
                    $0.index == Int(event.index)
                }
            }

        case .commandsReset:
            commands.removeAll(keepingCapacity: true)

        case .command:
            let command = Command(
                id: event.componentID,
                label: event.text,
                longLabel: event.detail,
                type: Int(event.arguments.0),
                priority: Int(event.arguments.1),
                scope: Int(event.arguments.2),
                ownerID: event.arguments.3,
                order: Int(event.index)
            )
            commands.removeAll { $0.id == command.id }
            commands.append(command)
        }
    }

    private mutating func applyScreen(
        _ event: PhoneMECAPI.LCDUIEvent,
        kind: EventKind
    ) {
        guard let type = ComponentType(rawValue: event.componentType) else {
            return
        }
        var value = screens[event.componentID]
            ?? (screen?.id == event.componentID ? screen : nil)
            ?? Screen(
                id: event.componentID,
                type: type,
                title: "",
                detail: "",
                contentWidth: 0,
                contentHeight: 0,
                scrollPosition: 0,
                isVisible: false,
                isFullScreen: false,
                nativeKind: nil
            )

        value.type = type
        value.title = event.text
        value.detail = event.detail
        if event.arguments.3 == Self.screenKindMetadataMarker {
            value.nativeKind = ScreenKind(rawValue: event.arguments.0)
        } else if event.arguments.3 == Self.screenModeMetadataMarker {
            value.isFullScreen = event.arguments.0 != 0
        } else if event.arguments.3 == Self.imageMetadataMarker ||
                    event.arguments.3 == Self.alertMetadataMarker {
            // Pixel data is transferred separately through lcdUIImages.
        } else if kind == .screenShown || kind == .screenUpdated {
            value.contentWidth = max(Int(event.arguments.0), value.contentWidth)
            value.contentHeight = max(Int(event.arguments.1), value.contentHeight)
            value.scrollPosition = max(Int(event.arguments.2), 0)
        }
        if kind == .screenShown {
            if var previous = screen, previous.id != value.id {
                previous.isVisible = false
                screens[previous.id] = previous
            }
            value.isVisible = true
            screens[value.id] = value
            screen = value
        } else {
            screens[value.id] = value
            if screen?.id == value.id {
                screen = value
            }
        }
    }

    private mutating func applyItem(
        _ event: PhoneMECAPI.LCDUIEvent,
        kind: EventKind
    ) {
        guard let type = ComponentType(rawValue: event.componentType) else {
            return
        }
        var value = items[event.componentID] ?? Item(
            id: event.componentID,
            parentID: event.parentID,
            type: type,
            label: "",
            text: "",
            frame: CGRectData(),
            isVisible: false
        )

        value.parentID = event.parentID
        if event.index >= 0 {
            value.formIndex = Int(event.index)
        }
        value.type = type
        value.label = event.text

        switch event.arguments.3 {
        case -1001:
            value.maxSize = max(Int(event.arguments.0), 0)
            value.constraints = Int(event.arguments.1)
            value.caretPosition = max(Int(event.arguments.2), 0)
            value.text = event.detail

        case -1002:
            value.value = Int(event.arguments.0)
            value.maxValue = Int(event.arguments.1)
            value.isInteractive = event.arguments.2 != 0

        case -1003:
            value.inputMode = Int(event.arguments.1)
            value.date = Date(timeIntervalSince1970: TimeInterval(event.value64))
            value.text = event.detail

        case -1004:
            value.imageWidth = max(Int(event.arguments.0), 0)
            value.imageHeight = max(Int(event.arguments.1), 0)
            value.imageGeneration = UInt64(max(event.arguments.2, 0))
            value.text = event.detail

        case -1005:
            value.layout = Int(event.arguments.0)
            value.appearanceMode = Int(event.arguments.1)
            value.fitPolicy = Int(event.arguments.2)
            let packedFont = UInt64(bitPattern: event.value64)
            value.fontFace = Int(packedFont & 0xffff)
            value.fontStyle = Int((packedFont >> 16) & 0xffff)
            value.fontSize = Int((packedFont >> 32) & 0xffff)

        default:
            value.frame = CGRectData(
                x: Int(event.arguments.0),
                y: Int(event.arguments.1),
                width: max(Int(event.arguments.2), 0),
                height: max(Int(event.arguments.3), 0)
            )
            value.text = event.detail
        }

        if kind == .itemShown {
            value.isVisible = true
        }
        items[event.componentID] = value
    }

    private mutating func applyChoice(_ event: PhoneMECAPI.LCDUIEvent) {
        guard event.index >= 0 else { return }
        let index = Int(event.index)
        guard var item = items[event.componentID] else { return }
        item.fitPolicy = Int(event.arguments.2)
        let packedFont = UInt64(bitPattern: event.value64)
        let choice = Choice(
            index: index,
            text: event.text,
            isSelected: event.arguments.0 != 0,
            imageKey: event.arguments.3 < 0 ? event.arguments.3 : nil,
            fontFace: Int(packedFont & 0xffff),
            fontStyle: Int((packedFont >> 16) & 0xffff),
            fontSize: Int((packedFont >> 32) & 0xffff)
        )

        if let existingIndex = item.choices.firstIndex(where: {
            $0.index == index
        }) {
            item.choices[existingIndex] = choice
        } else {
            let insertionIndex = item.choices.firstIndex(where: {
                $0.index > index
            }) ?? item.choices.endIndex
            item.choices.insert(choice, at: insertionIndex)
        }
        items[event.componentID] = item
    }
}
