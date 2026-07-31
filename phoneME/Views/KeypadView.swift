import SwiftUI
#if canImport(UIKit)
import UIKit
#endif

enum KeyboardAdjustmentMode: Equatable {
    case none
    case position
    case size
}

struct KeyboardControlDescriptor: Identifiable, Equatable {
    let id: String
    let label: String
    let keys: [J2MEKey]
    let accessibilityLabel: String
    let emphasized: Bool
    let groupID: String
    let column: Int
    let row: Int
}

struct KeyboardLayoutDefinition {
    let metricColumns: Int
    let contentColumns: Int
    let rows: Int
    let keyHeightFactor: CGFloat
    let controls: [KeyboardControlDescriptor]
}

enum KeyboardLayoutCatalog {
    static func definition(for profile: GameProfile) -> KeyboardLayoutDefinition {
        definition(for: profile.resolvedKeyboardBaseType)
    }

    static func definition(
        for type: GameProfile.VirtualKeyboardType
    ) -> KeyboardLayoutDefinition {
        switch type == .custom ? .arrowsNumbers : type {
        case .numbersArrows:
            return split(numbersFirst: true)
        case .arrowsNumbers:
            return split(numbersFirst: false)
        case .phone:
            return phone(arrows: false)
        case .phoneArrows:
            return phone(arrows: true)
        case .numbers:
            return numbersOnly()
        case .arrows:
            return arrowsOnly()
        case .custom:
            return split(numbersFirst: false)
        }
    }

    static func selectableLayouts(
        hasCustomLayout: Bool
    ) -> [GameProfile.VirtualKeyboardType] {
        var layouts: [GameProfile.VirtualKeyboardType] = [
            .phone,
            .phoneArrows,
            .numbersArrows,
            .arrowsNumbers,
            .numbers,
            .arrows
        ]
        if hasCustomLayout {
            layouts.insert(.custom, at: 0)
        }
        return layouts
    }

    static func controlChoices(for profile: GameProfile) -> [KeyboardControlDescriptor] {
        definition(for: profile).controls
    }

    private static func split(numbersFirst: Bool) -> KeyboardLayoutDefinition {
        let numberStart = numbersFirst ? 0 : 3
        let directionStart = numbersFirst ? 3 : 0
        return KeyboardLayoutDefinition(
            metricColumns: 6,
            contentColumns: 6,
            rows: 4,
            keyHeightFactor: 1,
            controls: numberControls(startColumn: numberStart)
                + directionControls(startColumn: directionStart)
        )
    }

    private static func phone(arrows: Bool) -> KeyboardLayoutDefinition {
        let topMiddle = arrows
            ? descriptor(
                id: "menu",
                label: "M",
                keys: [],
                accessibilityLabel: "Menu",
                emphasized: false,
                groupID: "soft-keys",
                column: 1,
                row: 0
            )
            : key(.fire, label: "F", groupID: "soft-keys", column: 1, row: 0)

        var controls: [KeyboardControlDescriptor] = [
            key(.softLeft, groupID: "soft-keys", column: 0, row: 0),
            topMiddle,
            key(.softRight, groupID: "soft-keys", column: 2, row: 0)
        ]

        if arrows {
            controls += [
                key(.one, groupID: "numbers", column: 0, row: 1),
                key(.up, label: "↑", groupID: "directions", column: 1, row: 1),
                key(.three, groupID: "numbers", column: 2, row: 1),
                key(.left, label: "←", groupID: "directions", column: 0, row: 2),
                key(.fire, label: "F", groupID: "directions", column: 1, row: 2),
                key(.right, label: "→", groupID: "directions", column: 2, row: 2),
                key(.seven, groupID: "numbers", column: 0, row: 3),
                key(.down, label: "↓", groupID: "directions", column: 1, row: 3),
                key(.nine, groupID: "numbers", column: 2, row: 3),
                key(.star, groupID: "numbers", column: 0, row: 4),
                key(.zero, groupID: "numbers", column: 1, row: 4),
                key(.pound, groupID: "numbers", column: 2, row: 4)
            ]
        } else {
            let rows: [[J2MEKey]] = [
                [.one, .two, .three],
                [.four, .five, .six],
                [.seven, .eight, .nine],
                [.star, .zero, .pound]
            ]
            for (rowIndex, row) in rows.enumerated() {
                for (column, keyValue) in row.enumerated() {
                    controls.append(
                        key(
                            keyValue,
                            groupID: "numbers",
                            column: column,
                            row: rowIndex + 1
                        )
                    )
                }
            }
        }

        return KeyboardLayoutDefinition(
            metricColumns: 3,
            contentColumns: 3,
            rows: 5,
            keyHeightFactor: 0.75,
            controls: controls
        )
    }

    private static func numbersOnly() -> KeyboardLayoutDefinition {
        var controls: [KeyboardControlDescriptor] = [
            key(.softLeft, groupID: "soft-keys", column: 0, row: 0),
            key(.softRight, groupID: "soft-keys", column: 4, row: 0)
        ]
        let rows: [[J2MEKey]] = [
            [.one, .two, .three],
            [.four, .five, .six],
            [.seven, .eight, .nine],
            [.star, .zero, .pound]
        ]
        for (rowIndex, row) in rows.enumerated() {
            for (index, keyValue) in row.enumerated() {
                controls.append(
                    key(
                        keyValue,
                        groupID: "numbers",
                        column: index + 1,
                        row: rowIndex
                    )
                )
            }
        }
        return KeyboardLayoutDefinition(
            metricColumns: 6,
            contentColumns: 5,
            rows: 4,
            keyHeightFactor: 1,
            controls: controls
        )
    }

    private static func arrowsOnly() -> KeyboardLayoutDefinition {
        let controls: [KeyboardControlDescriptor] = [
            key(.softLeft, groupID: "soft-keys", column: 0, row: 0),
            diagonal("↖", [.up, .left], id: "up-left", column: 1, row: 0),
            key(.up, label: "↑", groupID: "directions", column: 2, row: 0),
            diagonal("↗", [.up, .right], id: "up-right", column: 3, row: 0),
            key(.softRight, groupID: "soft-keys", column: 4, row: 0),
            key(.left, label: "←", groupID: "directions", column: 1, row: 1),
            key(.fire, label: "F", groupID: "directions", column: 2, row: 1),
            key(.right, label: "→", groupID: "directions", column: 3, row: 1),
            diagonal("↙", [.down, .left], id: "down-left", column: 1, row: 2),
            key(.down, label: "↓", groupID: "directions", column: 2, row: 2),
            diagonal("↘", [.down, .right], id: "down-right", column: 3, row: 2)
        ]
        return KeyboardLayoutDefinition(
            metricColumns: 6,
            contentColumns: 5,
            rows: 3,
            keyHeightFactor: 1,
            controls: controls
        )
    }

    private static func numberControls(startColumn: Int) -> [KeyboardControlDescriptor] {
        let rows: [[J2MEKey]] = [
            [.one, .two, .three],
            [.four, .five, .six],
            [.seven, .eight, .nine],
            [.star, .zero, .pound]
        ]
        return rows.enumerated().flatMap { rowIndex, row in
            row.enumerated().map { column, keyValue in
                key(
                    keyValue,
                    groupID: "numbers",
                    column: startColumn + column,
                    row: rowIndex
                )
            }
        }
    }

    private static func directionControls(startColumn: Int) -> [KeyboardControlDescriptor] {
        [
            key(.softLeft, groupID: "soft-keys", column: startColumn, row: 0),
            key(.softRight, groupID: "soft-keys", column: startColumn + 2, row: 0),
            diagonal("↖", [.up, .left], id: "up-left", column: startColumn, row: 1),
            key(.up, label: "↑", groupID: "directions", column: startColumn + 1, row: 1),
            diagonal("↗", [.up, .right], id: "up-right", column: startColumn + 2, row: 1),
            key(.left, label: "←", groupID: "directions", column: startColumn, row: 2),
            key(.fire, label: "F", groupID: "directions", column: startColumn + 1, row: 2),
            key(.right, label: "→", groupID: "directions", column: startColumn + 2, row: 2),
            diagonal("↙", [.down, .left], id: "down-left", column: startColumn, row: 3),
            key(.down, label: "↓", groupID: "directions", column: startColumn + 1, row: 3),
            diagonal("↘", [.down, .right], id: "down-right", column: startColumn + 2, row: 3)
        ]
    }

    private static func key(
        _ key: J2MEKey,
        label: String? = nil,
        groupID: String,
        column: Int,
        row: Int
    ) -> KeyboardControlDescriptor {
        descriptor(
            id: "key-\(key.rawValue)",
            label: label ?? key.title,
            keys: [key],
            accessibilityLabel: accessibilityLabel(for: key),
            emphasized: key == .fire,
            groupID: groupID,
            column: column,
            row: row
        )
    }

    private static func diagonal(
        _ label: String,
        _ keys: [J2MEKey],
        id: String,
        column: Int,
        row: Int
    ) -> KeyboardControlDescriptor {
        descriptor(
            id: id,
            label: label,
            keys: keys,
            accessibilityLabel: id.replacingOccurrences(of: "-", with: " "),
            emphasized: false,
            groupID: "directions",
            column: column,
            row: row
        )
    }

    private static func descriptor(
        id: String,
        label: String,
        keys: [J2MEKey],
        accessibilityLabel: String,
        emphasized: Bool,
        groupID: String,
        column: Int,
        row: Int
    ) -> KeyboardControlDescriptor {
        KeyboardControlDescriptor(
            id: id,
            label: label,
            keys: keys,
            accessibilityLabel: accessibilityLabel,
            emphasized: emphasized,
            groupID: groupID,
            column: column,
            row: row
        )
    }

    private static func accessibilityLabel(for key: J2MEKey) -> String {
        switch key {
        case .up: return "Up"
        case .down: return "Down"
        case .left: return "Left"
        case .right: return "Right"
        case .fire: return "Fire"
        case .softLeft: return "Left soft key"
        case .softRight: return "Right soft key"
        default: return key.title
        }
    }
}

struct KeypadView: View {
    @EnvironmentObject private var session: EmulatorSession

    @Binding var profile: GameProfile
    let editMode: KeyboardAdjustmentMode
    let layoutRect: CGRect
    let displayRect: CGRect
    let onKeyActivity: (Bool) -> Void
    let onObscuresDisplayChange: (Bool) -> Void

    @State private var positionDragOrigins: [String: GameProfile.KeyboardControlOffset] = [:]
    @State private var resizeDragOrigins: [String: GameProfile.KeyboardGroupScale] = [:]
    @State private var selectedGroupID: String?

    var body: some View {
        GeometryReader { geometry in
            let definition = KeyboardLayoutCatalog.definition(for: profile)
            let frames = layoutFrames(
                definition: definition,
                size: geometry.size,
                layoutRect: layoutRect,
                customization: profile.effectiveKeyboardLayoutCustomization
            )
            let hidden = profile.effectiveKeyboardLayoutCustomization.hiddenControlIDs
            let visibleFrames = frames.filter { !hidden.contains($0.key) }
            let obscuresDisplay = visibleFrames.values.contains { $0.intersects(displayRect) }

            ZStack(alignment: .topLeading) {
                ForEach(definition.controls.filter { !hidden.contains($0.id) }) { control in
                    if let frame = frames[control.id] {
                        VirtualKeyButton(
                            control: control,
                            profile: profile,
                            editMode: editMode,
                            isGroupSelected: editMode == .size && selectedGroupID == control.groupID,
                            width: frame.width,
                            height: frame.height,
                            displayRect: displayRect,
                            obscuresDisplay: obscuresDisplay,
                            onKeyActivity: onKeyActivity,
                            onPositionDrag: { translation, ended in
                                updateControlPosition(
                                    control,
                                    translation: translation,
                                    ended: ended,
                                    definition: definition,
                                    size: geometry.size
                                )
                            },
                            onResizeDrag: { translation, ended in
                                updateGroupScale(
                                    control.groupID,
                                    translation: translation,
                                    ended: ended,
                                    size: geometry.size
                                )
                            }
                        )
                        .environmentObject(session)
                        .position(x: frame.midX, y: frame.midY)
                    }
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .onAppear {
                onObscuresDisplayChange(obscuresDisplay)
            }
            .onChange(of: obscuresDisplay) { value in
                onObscuresDisplayChange(value)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Virtual keyboard")
    }

    private func layoutFrames(
        definition: KeyboardLayoutDefinition,
        size: CGSize,
        layoutRect: CGRect,
        customization: GameProfile.KeyboardLayoutCustomization
    ) -> [String: CGRect] {
        guard size.width > 0, size.height > 0 else { return [:] }

        let sizingSize = layoutRect.size
        let baseGap = min(8, max(4, sizingSize.width / 82))
        let gap = baseGap
        let unscaledWidth = max(
            28,
            (sizingSize.width - gap * CGFloat(definition.metricColumns - 1))
                / CGFloat(definition.metricColumns)
        )
        let availableHeight = max(
            28,
            (sizingSize.height - gap * CGFloat(definition.rows - 1))
                / CGFloat(definition.rows)
        )
        let keyWidth = unscaledWidth
        let keyHeight = min(
            unscaledWidth * definition.keyHeightFactor,
            availableHeight
        )
        let contentWidth = keyWidth * CGFloat(definition.contentColumns)
            + gap * CGFloat(definition.contentColumns - 1)
        let contentHeight = keyHeight * CGFloat(definition.rows)
            + gap * CGFloat(definition.rows - 1)
        let startX = layoutRect.minX + (layoutRect.width - contentWidth) / 2
        let startY = layoutRect.maxY - contentHeight

        var frames: [String: CGRect] = [:]
        for control in definition.controls {
            let groupScale = customization.groupScales[control.groupID]
                ?? GameProfile.KeyboardGroupScale()
            let offset = customization.controlOffsets[control.id]
                ?? GameProfile.KeyboardControlOffset()
            let columnOffset = CGFloat(control.column) * (keyWidth + gap)
            let rowOffset = CGFloat(control.row) * (keyHeight + gap)
            let centerX = startX + columnOffset + keyWidth / 2
                + CGFloat(offset.x) * size.width
            let centerY = startY + rowOffset + keyHeight / 2
                + CGFloat(offset.y) * size.height
            let scaledWidth = max(24, keyWidth * CGFloat(groupScale.width))
            let scaledHeight = max(24, keyHeight * CGFloat(groupScale.height))
            frames[control.id] = CGRect(
                x: centerX - scaledWidth / 2,
                y: centerY - scaledHeight / 2,
                width: scaledWidth,
                height: scaledHeight
            )
        }
        return frames
    }

    private func updateControlPosition(
        _ control: KeyboardControlDescriptor,
        translation: CGSize,
        ended: Bool,
        definition: KeyboardLayoutDefinition,
        size: CGSize
    ) {
        guard size.width > 0, size.height > 0 else { return }
        let currentCustomization = profile.effectiveKeyboardLayoutCustomization
        let origin: GameProfile.KeyboardControlOffset
        if let existing = positionDragOrigins[control.id] {
            origin = existing
        } else {
            origin = currentCustomization.controlOffsets[control.id]
                ?? GameProfile.KeyboardControlOffset()
            positionDragOrigins[control.id] = origin
        }

        var candidateOffset = GameProfile.KeyboardControlOffset(
            x: origin.x + Double(translation.width / size.width),
            y: origin.y + Double(translation.height / size.height)
        )
        var candidateCustomization = currentCustomization
        candidateCustomization.controlOffsets[control.id] = candidateOffset
        let candidateFrames = layoutFrames(
            definition: definition,
            size: size,
            layoutRect: layoutRect,
            customization: candidateCustomization
        )

        if let candidateFrame = candidateFrames[control.id] {
            let otherFrames = candidateFrames
                .filter { $0.key != control.id && !candidateCustomization.hiddenControlIDs.contains($0.key) }
                .map(\.value)
            let snap = snapDelta(
                movingFrame: candidateFrame,
                otherFrames: otherFrames,
                containerSize: size
            )
            candidateOffset.x += Double(snap.width / size.width)
            candidateOffset.y += Double(snap.height / size.height)
        }

        profile.updateKeyboardLayoutCustomization { customization in
            customization.controlOffsets[control.id] = candidateOffset.isDefault
                ? nil
                : candidateOffset
        }

        if ended {
            positionDragOrigins[control.id] = nil
        }
    }

    private func updateGroupScale(
        _ groupID: String,
        translation: CGSize,
        ended: Bool,
        size: CGSize
    ) {
        let currentCustomization = profile.effectiveKeyboardLayoutCustomization
        let origin: GameProfile.KeyboardGroupScale
        if let existing = resizeDragOrigins[groupID] {
            origin = existing
        } else {
            origin = currentCustomization.groupScales[groupID]
                ?? GameProfile.KeyboardGroupScale()
            resizeDragOrigins[groupID] = origin
        }

        selectedGroupID = groupID
        let divisor = max(min(size.width, size.height), 1)
        var result = origin
        if abs(translation.width) > abs(translation.height) {
            result.width = snappedScale(origin.width + Double(translation.width / divisor))
        } else {
            result.height = snappedScale(origin.height - Double(translation.height / divisor))
        }

        profile.updateKeyboardLayoutCustomization { customization in
            customization.groupScales[groupID] = result.isDefault ? nil : result
        }

        if ended {
            resizeDragOrigins[groupID] = nil
        }
    }

    private func snappedScale(_ scale: Double) -> Double {
        let clamped = min(max(scale, 0.5), 1.8)
        if abs(clamped - 1) <= 0.06 {
            return 1
        }
        let otherScales = profile.effectiveKeyboardLayoutCustomization.groupScales.values
            .flatMap { [$0.width, $0.height] }
        if let match = otherScales.first(where: { abs($0 - clamped) <= 0.06 }) {
            return match
        }
        return clamped
    }

    private func snapDelta(
        movingFrame: CGRect,
        otherFrames: [CGRect],
        containerSize: CGSize
    ) -> CGSize {
        let radius: CGFloat = 9
        let movingX = [movingFrame.minX, movingFrame.midX, movingFrame.maxX]
        let movingY = [movingFrame.minY, movingFrame.midY, movingFrame.maxY]
        var targetX: [CGFloat] = [0, containerSize.width / 2, containerSize.width]
        var targetY: [CGFloat] = [0, containerSize.height / 2, containerSize.height]
        for frame in otherFrames {
            targetX += [frame.minX, frame.midX, frame.maxX]
            targetY += [frame.minY, frame.midY, frame.maxY]
        }
        return CGSize(
            width: closestSnapDelta(moving: movingX, targets: targetX, radius: radius),
            height: closestSnapDelta(moving: movingY, targets: targetY, radius: radius)
        )
    }

    private func closestSnapDelta(
        moving: [CGFloat],
        targets: [CGFloat],
        radius: CGFloat
    ) -> CGFloat {
        var best: CGFloat?
        for source in moving {
            for target in targets {
                let delta = target - source
                guard abs(delta) <= radius else { continue }
                if best == nil || abs(delta) < abs(best!) {
                    best = delta
                }
            }
        }
        return best ?? 0
    }
}

private struct VirtualKeyButton: View {
    @EnvironmentObject private var session: EmulatorSession
    @Environment(\.colorScheme) private var colorScheme

    let control: KeyboardControlDescriptor
    let profile: GameProfile
    let editMode: KeyboardAdjustmentMode
    let isGroupSelected: Bool
    let width: CGFloat
    let height: CGFloat
    let displayRect: CGRect
    let obscuresDisplay: Bool
    let onKeyActivity: (Bool) -> Void
    let onPositionDrag: (CGSize, Bool) -> Void
    let onResizeDrag: (CGSize, Bool) -> Void

    @State private var isPressed = false

    var body: some View {
        GeometryReader { geometry in
            keyContent(
                effectiveOpacity: effectiveOpacity(
                    for: geometry.frame(in: .named("emulatorSurface"))
                )
            )
        }
        .frame(width: width, height: height)
        .onDisappear {
            guard isPressed else { return }
            isPressed = false
            release()
        }
    }

    private func keyContent(effectiveOpacity: Double) -> some View {
        Text(control.label)
            .font(.system(size: min(width, height) * 0.36, weight: .medium))
            .foregroundStyle(labelColor)
            .frame(width: width, height: height)
            .background { buttonBackground(effectiveOpacity: effectiveOpacity) }
            .overlay {
                if editMode == .position {
                    editOutline
                } else if editMode == .size && isGroupSelected {
                    editOutline
                }
            }
            .contentShape(Rectangle())
            .scaleEffect(isPressed && editMode == .none ? 0.96 : 1)
            .animation(.easeOut(duration: 0.06), value: isPressed)
            .highPriorityGesture(
                DragGesture(
                    minimumDistance: 0,
                    coordinateSpace: .local
                )
                    .onChanged { value in
                        switch editMode {
                        case .none:
                            guard !isPressed else { return }
                            isPressed = true
                            press()
                        case .position:
                            onPositionDrag(value.translation, false)
                        case .size:
                            onResizeDrag(value.translation, false)
                        }
                    }
                    .onEnded { value in
                        switch editMode {
                        case .none:
                            guard isPressed else { return }
                            isPressed = false
                            release()
                        case .position:
                            onPositionDrag(value.translation, true)
                        case .size:
                            onResizeDrag(value.translation, true)
                        }
                    }
            )
            .accessibilityLabel(control.accessibilityLabel)
            .accessibilityAddTraits(.isButton)
    }

    private var editOutline: some View {
        RoundedRectangle(cornerRadius: 6, style: .continuous)
            .stroke(Color.accentColor, style: StrokeStyle(lineWidth: 2, dash: [5, 3]))
            .padding(2)
    }

    @ViewBuilder
    private func buttonBackground(effectiveOpacity: Double) -> some View {
        switch profile.buttonShape {
        case .oval:
            styledBackground(Capsule(), effectiveOpacity: effectiveOpacity)
        case .rectangle:
            styledBackground(Rectangle(), effectiveOpacity: effectiveOpacity)
        case .roundedRectangle:
            styledBackground(
                RoundedRectangle(cornerRadius: min(width, height) * 0.24, style: .continuous),
                effectiveOpacity: effectiveOpacity
            )
        }
    }

    @ViewBuilder
    private func styledBackground<S: Shape>(
        _ shape: S,
        effectiveOpacity: Double
    ) -> some View {
        let selected = isPressed || isGroupSelected
        if profile.usesNativeKeyboardPalette {
            let neutralOverlay = Color.primary.opacity(colorScheme == .dark ? 0.10 : 0.04)
            let pressedFill = Color.accentColor.opacity(max(effectiveOpacity, 0.72))
            let outline = Color.primary.opacity(colorScheme == .dark ? 0.24 : 0.16)

            shape
                .fill(.thinMaterial)
                .opacity(effectiveOpacity)
                .overlay(shape.fill(selected ? pressedFill : neutralOverlay))
                .overlay(shape.stroke(outline, lineWidth: 0.75))
        } else {
            let fillHex = selected
                ? profile.keyboardSelectedBackgroundHex
                : profile.keyboardBackgroundHex
            let fill = (Color(j2meHex: fillHex) ?? .gray).opacity(effectiveOpacity)
            let outline = (Color(j2meHex: profile.keyboardOutlineHex) ?? .white)
                .opacity(effectiveOpacity)

            shape
                .fill(fill)
                .overlay(shape.stroke(outline, lineWidth: 1))
        }
    }

    private func effectiveOpacity(for keyFrame: CGRect) -> Double {
        if editMode != .none {
            return 1
        }
        guard obscuresDisplay else { return 1 }
        if profile.forceOpacityForOffscreenKeys, !keyFrame.intersects(displayRect) {
            return 1
        }
        return profile.keyboardOpacity
    }

    private var labelColor: Color {
        let selected = isPressed || isGroupSelected
        if profile.usesNativeKeyboardPalette {
            return selected ? .white : .primary
        }
        let hex = selected
            ? profile.keyboardSelectedForegroundHex
            : profile.keyboardForegroundHex
        return Color(j2meHex: hex) ?? .primary
    }

    private func press() {
        guard !control.keys.isEmpty else { return }
        onKeyActivity(true)
#if canImport(UIKit)
        if profile.hapticFeedback {
            UIImpactFeedbackGenerator(style: control.emphasized ? .medium : .light).impactOccurred()
        }
#endif
        for key in control.keys {
            session.send(key, pressed: true)
        }
    }

    private func release() {
        for key in control.keys {
            session.send(key, pressed: false)
        }
        if !control.keys.isEmpty {
            onKeyActivity(false)
        }
    }
}

private extension Color {
    init?(j2meHex: String) {
        let cleaned = j2meHex.trimmingCharacters(in: CharacterSet.alphanumerics.inverted)
        guard cleaned.count == 6, let value = UInt64(cleaned, radix: 16) else { return nil }
        self.init(
            red: Double((value >> 16) & 0xFF) / 255,
            green: Double((value >> 8) & 0xFF) / 255,
            blue: Double(value & 0xFF) / 255
        )
    }
}
