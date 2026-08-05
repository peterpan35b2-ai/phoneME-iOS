import CoreGraphics
import Foundation

struct EmulatorWorkspace: Identifiable, Codable, Equatable {
    var id: UUID
    var name: String
    var createdAt: Date
    var modifiedAt: Date
    var panels: [EmulatorWorkspacePanel]

    init(
        id: UUID = UUID(),
        name: String,
        createdAt: Date = Date(),
        modifiedAt: Date = Date(),
        panels: [EmulatorWorkspacePanel] = []
    ) {
        self.id = id
        self.name = name
        self.createdAt = createdAt
        self.modifiedAt = modifiedAt
        self.panels = panels
    }
}

struct EmulatorWorkspacePanel: Identifiable, Codable, Equatable {
    var id: UUID
    var gameID: UUID
    var frame: EmulatorWorkspaceFrame
    var profile: GameProfile
    var createdAt: Date

    init(
        id: UUID = UUID(),
        gameID: UUID,
        frame: EmulatorWorkspaceFrame,
        profile: GameProfile,
        createdAt: Date = Date()
    ) {
        self.id = id
        self.gameID = gameID
        self.frame = frame.normalized()
        self.profile = profile.normalized()
        self.createdAt = createdAt
    }
}

struct EmulatorWorkspaceFrame: Codable, Equatable {
    var x: Double
    var y: Double
    var width: Double
    var height: Double

    static let minimumWidth = 0.18
    static let minimumHeight = 0.18
    static let maximumWidth = 4.0
    static let maximumHorizontalExtent = 12.0

    static let `default` = EmulatorWorkspaceFrame(
        x: 0,
        y: 0,
        width: 0.5,
        height: 0.5
    )

    var maxX: Double { x + width }
    var maxY: Double { y + height }

    func normalized() -> EmulatorWorkspaceFrame {
        let width = min(
            max(width, Self.minimumWidth),
            Self.maximumWidth
        )
        let height = min(max(height, Self.minimumHeight), 1)
        return EmulatorWorkspaceFrame(
            x: min(
                max(x, 0),
                max(Self.maximumHorizontalExtent - width, 0)
            ),
            y: min(max(y, 0), 1 - height),
            width: width,
            height: height
        )
    }

    func rect(in size: CGSize) -> CGRect {
        let normalized = normalized()
        return CGRect(
            x: CGFloat(normalized.x) * size.width,
            y: CGFloat(normalized.y) * size.height,
            width: CGFloat(normalized.width) * size.width,
            height: CGFloat(normalized.height) * size.height
        )
    }

    static func from(rect: CGRect, in size: CGSize) -> EmulatorWorkspaceFrame {
        guard size.width > 0, size.height > 0 else { return .default }
        return EmulatorWorkspaceFrame(
            x: Double(rect.minX / size.width),
            y: Double(rect.minY / size.height),
            width: Double(rect.width / size.width),
            height: Double(rect.height / size.height)
        ).normalized()
    }
}
