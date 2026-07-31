import Foundation

enum J2MEKey: Int32, CaseIterable, Identifiable {
    case up = -1
    case down = -2
    case left = -3
    case right = -4
    case fire = -5
    case softLeft = -6
    case softRight = -7

    case zero = 48
    case one = 49
    case two = 50
    case three = 51
    case four = 52
    case five = 53
    case six = 54
    case seven = 55
    case eight = 56
    case nine = 57
    case star = 42
    case pound = 35

    var id: Int32 { rawValue }

    static let configurableKeys: [J2MEKey] = [
        .up,
        .down,
        .left,
        .right,
        .fire,
        .softLeft,
        .softRight
    ]

    var mappingID: String {
        switch self {
        case .up: return "up"
        case .down: return "down"
        case .left: return "left"
        case .right: return "right"
        case .fire: return "fire"
        case .softLeft: return "softLeft"
        case .softRight: return "softRight"
        default: return "key\(rawValue)"
        }
    }

    var mappingTitle: String {
        switch self {
        case .up: return "UP"
        case .down: return "DOWN"
        case .left: return "LEFT"
        case .right: return "RIGHT"
        case .fire: return "FIRE"
        case .softLeft: return "SOFT1"
        case .softRight: return "SOFT2"
        default: return title
        }
    }

    var title: String {
        switch self {
        case .up: return "▲"
        case .down: return "▼"
        case .left: return "◀"
        case .right: return "▶"
        case .fire: return "OK"
        case .softLeft: return "L"
        case .softRight: return "R"
        case .zero: return "0"
        case .one: return "1"
        case .two: return "2"
        case .three: return "3"
        case .four: return "4"
        case .five: return "5"
        case .six: return "6"
        case .seven: return "7"
        case .eight: return "8"
        case .nine: return "9"
        case .star: return "*"
        case .pound: return "#"
        }
    }
}
