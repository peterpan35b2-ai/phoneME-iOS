import SwiftUI

#if os(iOS)
import UIKit

private enum LCDUIStyle {
    static let rowIconSize: CGFloat = 32
    static let rowIconCornerRadius: CGFloat = 7
    static let cardCornerRadius: CGFloat = 18
    static let controlCornerRadius: CGFloat = 10
    static let contentInset: CGFloat = 16
    static let listRowMinimumHeight: CGFloat = 48
    static let maximumContentWidth: CGFloat = 560
}

private struct LCDUIEmptyState: View {
    let title: String
    let systemImage: String
    let detail: String?
    var showsProgress = false

    var body: some View {
        VStack(spacing: 10) {
            Image(systemName: systemImage)
                .font(.system(size: 30, weight: .semibold))
                .foregroundStyle(.secondary)
                .accessibilityHidden(true)

            Text(title)
                .font(.headline)

            if let detail, !detail.isEmpty {
                Text(detail)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }

            if showsProgress {
                ProgressView()
                    .controlSize(.small)
                    .padding(.top, 2)
            }
        }
        .padding(24)
        .frame(maxWidth: 360)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityElement(children: .combine)
    }
}

struct NativeLCDUIScreenView: View {
    @EnvironmentObject private var session: EmulatorSession

    let imageStore: LCDUIImageStore
    let state: LCDUIState
    let showsTitleInContent: Bool

    var body: some View {
        VStack(spacing: 0) {
            if let screen = state.screen {
                if state.screenKind != .alert {
                    screenHeader(
                        screen,
                        showsTitle: showsTitleInContent
                    )
                }

                Group {
                    switch state.screenKind {
                    case .alert:
                        alertContent(screen)
                    case .list:
                        listContent
                    case .textBox:
                        textBoxContent
                    case .menu:
                        menuContent
                    case .form:
                        formContent
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .layoutPriority(1)
            }

            LCDUICommandBar(state: state)
                .environmentObject(session)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .environmentObject(imageStore)
        .environment(\.font, Font.body)
        .background(surfaceBackground.ignoresSafeArea())
    }

    private var surfaceBackground: Color {
        switch state.screenKind {
        case .form, .list, .menu:
            return Color(uiColor: .systemGroupedBackground)
        default:
            return Color(uiColor: .systemBackground)
        }
    }

    @ViewBuilder
    private func screenHeader(
        _ screen: LCDUIState.Screen,
        showsTitle: Bool
    ) -> some View {
        if (showsTitle && !screen.title.isEmpty) || !screen.detail.isEmpty {
            VStack(alignment: .leading, spacing: 6) {
                if showsTitle && !screen.title.isEmpty {
                    Text(screen.title)
                        .font(.headline)
                        .lineLimit(2)
                        .fixedSize(horizontal: false, vertical: true)
                        .accessibilityAddTraits(.isHeader)
                }

                if !screen.detail.isEmpty {
                    Text(screen.detail)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                        .lineLimit(3)
                        .fixedSize(horizontal: false, vertical: true)
                        .textSelection(.enabled)
                }
            }
            .frame(maxWidth: LCDUIStyle.maximumContentWidth, alignment: .leading)
            .frame(maxWidth: .infinity, alignment: .center)
            .padding(.horizontal, LCDUIStyle.contentInset)
            .padding(.vertical, 12)
            .background(surfaceBackground)
        }
    }

    private var formContent: some View {
        NativeLCDUIFormView(state: state)
            .environmentObject(session)
    }

    @ViewBuilder
    private var listContent: some View {
        if let item = state.visibleItems.first {
            NativeLCDUIListView(
                item: item,
                itemCommands: state.listItemCommands
            )
            .environmentObject(session)
        } else {
            emptyContent
        }
    }

    @ViewBuilder
    private var textBoxContent: some View {
        if let item = state.visibleItems.first {
            NativeLCDUITextBox(item: item)
                .environmentObject(session)
        } else {
            emptyContent
        }
    }

    private var menuContent: some View {
        List(state.orderedCommands) { command in
            Button(role: command.buttonRole) {
                session.selectLCDUICommand(command.id)
            } label: {
                HStack(spacing: 12) {
                    Image(systemName: command.systemImage)
                        .font(.system(size: 15, weight: .semibold))
                        .foregroundStyle(command.tintStyle)
                        .frame(
                            width: LCDUIStyle.rowIconSize,
                            height: LCDUIStyle.rowIconSize
                        )
                        .background(
                            command.iconBackground,
                            in: RoundedRectangle(
                                cornerRadius: LCDUIStyle.rowIconCornerRadius,
                                style: .continuous
                            )
                        )

                    VStack(alignment: .leading, spacing: 2) {
                        Text(command.displayLabel)
                            .font(.headline)
                            .foregroundStyle(command.tintStyle)
                            .lineLimit(1)

                        if !command.longLabel.isEmpty,
                           command.longLabel != command.label {
                            Text(command.longLabel)
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                                .lineLimit(2)
                        }
                    }

                    Spacer(minLength: 8)

                    Image(systemName: "chevron.right")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.tertiary)
                        .accessibilityHidden(true)
                }
                .padding(.vertical, 2)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
        }
        .listStyle(.insetGrouped)
    }

    private var emptyContent: some View {
        LCDUIEmptyState(
            title: "Waiting for content",
            systemImage: "rectangle.stack.badge.clock",
            detail: "The application is preparing this screen.",
            showsProgress: true
        )
        .accessibilityLabel("Loading")
    }

    private func alertContent(_ screen: LCDUIState.Screen) -> some View {
        GeometryReader { geometry in
            ScrollView {
                VStack(spacing: 16) {
                    alertIcon(for: screen)

                    VStack(spacing: 8) {
                        if !screen.title.isEmpty {
                            Text(screen.title)
                                .font(.title3.weight(.semibold))
                                .multilineTextAlignment(.center)
                                .fixedSize(horizontal: false, vertical: true)
                                .accessibilityAddTraits(.isHeader)
                        }

                        if !screen.detail.isEmpty {
                            Text(screen.detail)
                                .font(.body)
                                .foregroundStyle(.secondary)
                                .frame(maxWidth: .infinity)
                                .multilineTextAlignment(.center)
                                .fixedSize(horizontal: false, vertical: true)
                                .textSelection(.enabled)
                        }
                    }

                    if !state.visibleItems.isEmpty {
                        Divider()

                        VStack(spacing: 12) {
                            ForEach(state.visibleItems) { item in
                                NativeLCDUIItemView(item: item)
                                    .environmentObject(session)
                                    .frame(maxWidth: .infinity)
                            }
                        }
                    }
                }
                .padding(20)
                .frame(maxWidth: 420)
                .background(
                    Color(uiColor: .secondarySystemGroupedBackground),
                    in: RoundedRectangle(
                        cornerRadius: LCDUIStyle.cardCornerRadius,
                        style: .continuous
                    )
                )
                .overlay {
                    RoundedRectangle(
                        cornerRadius: LCDUIStyle.cardCornerRadius,
                        style: .continuous
                    )
                    .stroke(Color(uiColor: .separator).opacity(0.25), lineWidth: 0.5)
                }
                .shadow(color: Color.black.opacity(0.08), radius: 18, y: 8)
                .padding(.horizontal, LCDUIStyle.contentInset)
                .frame(
                    maxWidth: .infinity,
                    minHeight: geometry.size.height,
                    alignment: .center
                )
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .clipped()
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(uiColor: .systemGroupedBackground))
        .layoutPriority(1)
    }

    private func alertIcon(for screen: LCDUIState.Screen) -> some View {
        NativeLCDUIAlertIcon(
            imageStore: imageStore,
            componentID: screen.id,
            fallbackSymbol: alertSymbol(for: screen.type),
            fallbackTint: alertTint(for: screen.type)
        )
    }

    private func alertSymbol(for type: LCDUIState.ComponentType) -> String {
        switch type {
        case .infoAlert: return "info.circle.fill"
        case .warningAlert: return "exclamationmark.triangle.fill"
        case .errorAlert: return "xmark.octagon.fill"
        case .alarmAlert: return "alarm.fill"
        case .confirmationAlert: return "questionmark.circle.fill"
        default: return "bell.fill"
        }
    }

    private func alertTint(for type: LCDUIState.ComponentType) -> Color {
        switch type {
        case .warningAlert, .alarmAlert: return .orange
        case .errorAlert: return .red
        case .confirmationAlert: return .blue
        default: return .accentColor
        }
    }
}

private struct NativeLCDUIAlertIcon: View {
    @ObservedObject private var imageSlot: LCDUIImageSlot

    let fallbackSymbol: String
    let fallbackTint: Color

    init(
        imageStore: LCDUIImageStore,
        componentID: Int32,
        fallbackSymbol: String,
        fallbackTint: Color
    ) {
        _imageSlot = ObservedObject(
            wrappedValue: imageStore.slot(for: componentID)
        )
        self.fallbackSymbol = fallbackSymbol
        self.fallbackTint = fallbackTint
    }

    var body: some View {
        Group {
            if let image = imageSlot.image,
               !image.isPhoneMEBlankAlertPlaceholder {
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.none)
                    .scaledToFit()
            } else {
                Image(systemName: fallbackSymbol)
                    .font(.system(size: 36, weight: .semibold))
                    .foregroundStyle(fallbackTint)
            }
        }
        .frame(width: 56, height: 56)
        .accessibilityHidden(true)
    }
}

struct LCDUICommandBar: View {
    @EnvironmentObject private var session: EmulatorSession
    @State private var isShowingCommandMenu = false

    let state: LCDUIState

    var body: some View {
        let layout = state.commandLayout

        if !layout.leftCommands.isEmpty || layout.rightCommand != nil {
            HStack(spacing: 10) {
                leftCommand(layout)
                rightCommand(layout)
            }
            .frame(maxWidth: LCDUIStyle.maximumContentWidth)
            .frame(maxWidth: .infinity)
            .padding(.horizontal, LCDUIStyle.contentInset)
            .padding(.vertical, 10)
            .background(.regularMaterial)
            .overlay(alignment: .top) {
                Divider()
            }
            .confirmationDialog(
                "Options",
                isPresented: $isShowingCommandMenu,
                titleVisibility: .visible
            ) {
                ForEach(layout.leftCommands) { command in
                    Button(command.menuLabel, role: command.buttonRole) {
                        session.selectLCDUICommand(command.id)
                    }
                }
                Button("Cancel", role: .cancel) { }
            }
        }
    }

    @ViewBuilder
    private func leftCommand(_ layout: LCDUIState.CommandLayout) -> some View {
        if layout.leftCommands.count == 1,
           let command = layout.leftCommands.first {
            commandButton(command, prominent: command.buttonRole == nil)
        } else if layout.leftCommands.count > 1 {
            Button {
                isShowingCommandMenu = true
            } label: {
                commandLabel("Options", systemImage: "ellipsis.circle")
            }
            .buttonStyle(.borderedProminent)
            .frame(maxWidth: .infinity)
            .accessibilityLabel(
                L10n.format(
                    "Options, %d commands",
                    layout.leftCommands.count
                )
            )
        } else {
            Spacer(minLength: 0)
        }
    }

    @ViewBuilder
    private func rightCommand(_ layout: LCDUIState.CommandLayout) -> some View {
        if let command = layout.rightCommand {
            commandButton(command, prominent: false)
        } else {
            Spacer(minLength: 0)
        }
    }

    @ViewBuilder
    private func commandButton(
        _ command: LCDUIState.Command,
        prominent: Bool
    ) -> some View {
        if prominent {
            Button(role: command.buttonRole) {
                session.selectLCDUICommand(command.id)
            } label: {
                commandLabel(command.displayLabel, systemImage: command.systemImage)
            }
            .buttonStyle(.borderedProminent)
            .frame(maxWidth: .infinity)
        } else {
            Button(role: command.buttonRole) {
                session.selectLCDUICommand(command.id)
            } label: {
                commandLabel(command.displayLabel, systemImage: command.systemImage)
            }
            .buttonStyle(.bordered)
            .tint(command.softKeyTint)
            .frame(maxWidth: .infinity)
        }
    }

    private func commandLabel(
        _ title: String,
        systemImage: String
    ) -> some View {
        Label(title, systemImage: systemImage)
            .font(.body.weight(.semibold))
            .lineLimit(1)
            .minimumScaleFactor(0.8)
            .frame(maxWidth: .infinity, minHeight: 32)
            .contentShape(Rectangle())
    }
}

private final class LCDUIScrollPositionReporter: ObservableObject {
    var lastReportedPosition = -1
}

private struct NativeLCDUIFormView: View {
    @EnvironmentObject private var session: EmulatorSession
    @StateObject private var scrollReporter = LCDUIScrollPositionReporter()

    let state: LCDUIState

    var body: some View {
        let items = state.visibleItems

        Group {
            if items.isEmpty {
                LCDUIEmptyState(
                    title: "No content",
                    systemImage: "rectangle.stack",
                    detail: "This form does not contain any visible items."
                )
            } else {
                ScrollViewReader { proxy in
                    ScrollView {
                        LazyVStack(spacing: 0) {
                            GeometryReader { geometry in
                                Color.clear.preference(
                                    key: LCDUIScrollOffsetPreferenceKey.self,
                                    value: max(
                                        Int(
                                            -geometry.frame(
                                                in: .named("lcdui-scroll")
                                            ).minY
                                        ),
                                        0
                                    )
                                )
                            }
                            .frame(height: 0)

                            ForEach(Array(items.enumerated()), id: \.element.id) { index, item in
                                NativeLCDUIItemView(item: item)
                                    .environmentObject(session)
                                    .id(item.id)
                                    .padding(item.formRowInsets)

                                if index < items.count - 1,
                                   !item.hidesFormRowSeparator {
                                    Divider()
                                        .padding(.leading, 16)
                                }
                            }
                        }
                        .background(Color(uiColor: .secondarySystemGroupedBackground))
                        .clipShape(
                            RoundedRectangle(
                                cornerRadius: LCDUIStyle.controlCornerRadius,
                                style: .continuous
                            )
                        )
                        .padding(.horizontal, LCDUIStyle.contentInset)
                        .padding(.vertical, LCDUIStyle.contentInset)
                    }
                    .coordinateSpace(name: "lcdui-scroll")
                    .phoneMEScrollDismissesKeyboardInteractively()
                    .onPreferenceChange(LCDUIScrollOffsetPreferenceKey.self) { value in
                        guard abs(value - scrollReporter.lastReportedPosition) >= 12 else {
                            return
                        }
                        scrollReporter.lastReportedPosition = value
                        session.setLCDUIScrollPosition(value)
                    }
                    .onAppear {
                        restoreScrollPosition(using: proxy)
                    }
                    .onChange(of: state.screen?.scrollPosition) { _ in
                        restoreScrollPosition(using: proxy)
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func restoreScrollPosition(using proxy: ScrollViewProxy) {
        guard let requested = state.screen?.scrollPosition, requested > 0 else {
            return
        }
        if let target = state.visibleItems.last(where: { $0.frame.y <= requested }) {
            proxy.scrollTo(target.id, anchor: .top)
        }
    }
}

private struct LCDUIScrollOffsetPreferenceKey: PreferenceKey {
    static var defaultValue = 0

    static func reduce(value: inout Int, nextValue: () -> Int) {
        value = nextValue()
    }
}

private struct LCDUIImmediateFeedbackButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .opacity(configuration.isPressed ? 0.55 : 1)
            .contentShape(Rectangle())
            .transaction { transaction in
                transaction.animation = nil
            }
    }
}

private struct NativeLCDUIChoiceImage: View {
    @ObservedObject private var imageSlot: LCDUIImageSlot

    init(imageStore: LCDUIImageStore, imageKey: Int32) {
        _imageSlot = ObservedObject(
            wrappedValue: imageStore.slot(for: imageKey)
        )
    }

    var body: some View {
        Group {
            if let image = imageSlot.image {
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.none)
                    .scaledToFit()
                    .frame(width: 32, height: 32)
                    .accessibilityHidden(true)
            }
        }
    }
}

private enum LCDUIChoiceCellAccessory {
    case disclosure
    case checkbox(Bool)
    case radio(Bool)
    case progress
}

private struct LCDUIChoiceCell: View {
    let imageStore: LCDUIImageStore
    let choice: LCDUIState.Choice
    let fitPolicy: Int
    let accessory: LCDUIChoiceCellAccessory
    var usesRegularListFont = false

    var body: some View {
        HStack(spacing: 12) {
            if let imageKey = choice.imageKey {
                NativeLCDUIChoiceImage(
                    imageStore: imageStore,
                    imageKey: imageKey
                )
                .frame(
                    width: LCDUIStyle.rowIconSize,
                    height: LCDUIStyle.rowIconSize
                )
                .clipShape(
                    RoundedRectangle(
                        cornerRadius: LCDUIStyle.rowIconCornerRadius,
                        style: .continuous
                    )
                )
            }

            Text(choice.text)
                .font(usesRegularListFont ? choice.swiftUIListFont : choice.swiftUIFont)
                .foregroundStyle(.primary)
                .lineLimit(fitPolicy == 2 ? 1 : 2)
                .frame(maxWidth: .infinity, alignment: .leading)

            accessoryView
        }
        .padding(.vertical, 2)
        .contentShape(Rectangle())
    }


    @ViewBuilder
    private var accessoryView: some View {
        switch accessory {
        case .disclosure:
            Image(systemName: "chevron.right")
                .font(.caption.weight(.semibold))
                .foregroundStyle(.tertiary)
                .accessibilityHidden(true)
        case .checkbox(let isSelected):
            Image(systemName: isSelected ? "checkmark.square.fill" : "square")
                .font(.body)
                .foregroundStyle(isSelected ? Color.accentColor : .secondary)
                .accessibilityHidden(true)
        case .radio(let isSelected):
            Image(systemName: isSelected ? "largecircle.fill.circle" : "circle")
                .font(.body)
                .foregroundStyle(isSelected ? Color.accentColor : .secondary)
                .accessibilityHidden(true)
        case .progress:
            ProgressView()
                .controlSize(.small)
                .accessibilityLabel("Opening")
        }
    }
}

private struct NativeLCDUIListView: View {
    @EnvironmentObject private var session: EmulatorSession
    @State private var pendingImplicitChoiceIndex: Int?

    let item: LCDUIState.Item
    let itemCommands: [LCDUIState.Command]

    var body: some View {
        let choices = item.orderedChoices

        Group {
            if choices.isEmpty {
                LCDUIEmptyState(
                    title: "No items",
                    systemImage: "list.bullet.rectangle",
                    detail: "This list is currently empty."
                )
                .accessibilityLabel("Empty list")
            } else {
                List(choices) { choice in
                    choiceRow(choice)
                }
                .environment(\.defaultMinListRowHeight, LCDUIStyle.listRowMinimumHeight)
                .listStyle(.insetGrouped)
            }
        }
    }

    private var effectiveType: LCDUIState.ComponentType {
        // A public javax.microedition.lcdui.List can never be POPUP. Older
        // iOS cores shifted Choice.IMPLICIT (3) into the zero-based POPUP enum,
        // so a POPUP peer on a List screen must retain IMPLICIT menu behavior.
        item.type == .popupChoice ? .implicitChoice : item.type
    }

    private func select(_ choice: LCDUIState.Choice) {
        session.focusLCDUIItem(item.id)
        let selected = effectiveType == .multipleChoice
            ? !choice.isSelected
            : true
        session.setLCDUIChoice(
            componentID: item.id,
            index: choice.index,
            selected: selected
        )
    }

    private func showPendingFeedback(for index: Int) {
        pendingImplicitChoiceIndex = index
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 900_000_000)
            if pendingImplicitChoiceIndex == index {
                pendingImplicitChoiceIndex = nil
            }
        }
    }

    @ViewBuilder
    private func choiceRow(_ choice: LCDUIState.Choice) -> some View {
        if itemCommands.isEmpty {
            choiceButton(choice)
        } else {
            choiceButton(choice)
                .contextMenu {
                    ForEach(itemCommands) { command in
                        Button(role: command.buttonRole) {
                            session.selectLCDUIListItemCommand(
                                componentID: item.id,
                                index: choice.index,
                                commandID: command.id
                            )
                        } label: {
                            Label(
                                command.displayLabel,
                                systemImage: command.systemImage
                            )
                        }
                    }
                }
                .accessibilityHint("Touch and hold for actions")
        }
    }

    private func choiceButton(_ choice: LCDUIState.Choice) -> some View {
        Button {
            if effectiveType == .implicitChoice {
                showPendingFeedback(for: choice.index)
            }
            select(choice)
        } label: {
            LCDUIChoiceCell(
                imageStore: session.lcdUIImageStore,
                choice: choice,
                fitPolicy: item.fitPolicy,
                accessory: listAccessory(for: choice),
                usesRegularListFont: true
            )
        }
        .buttonStyle(.plain)
        .accessibilityValue(
            effectiveType == .implicitChoice
                ? ""
                : (choice.isSelected ? "Selected" : "Not selected")
        )
    }

    private func listAccessory(
        for choice: LCDUIState.Choice
    ) -> LCDUIChoiceCellAccessory {
        if effectiveType == .implicitChoice {
            return pendingImplicitChoiceIndex == choice.index
                ? .progress
                : .disclosure
        }
        switch effectiveType {
        case .multipleChoice:
            return .checkbox(choice.isSelected)
        case .exclusiveChoice:
            return .radio(choice.isSelected)
        default:
            return .disclosure
        }
    }
}

private struct NativeLCDUIItemView: View {
    @EnvironmentObject private var session: EmulatorSession
    @State private var pendingImplicitChoiceIndex: Int?

    let item: LCDUIState.Item

    var body: some View {
        Group {
            switch item.type {
            case .textField:
                NativeLCDUITextField(item: item)
                    .environmentObject(session)

            case .exclusiveChoice:
                exclusiveChoice

            case .implicitChoice:
                implicitChoice

            case .multipleChoice:
                multipleChoice

            case .popupChoice:
                NativeLCDUIPopupChoice(item: item)
                    .environmentObject(session)

            case .interactiveGauge:
                NativeLCDUIGauge(item: item)
                    .environmentObject(session)

            case .progressGauge:
                NativeLCDUIProgressGauge(item: item)

            case .dateField:
                NativeLCDUIDateField(item: item)
                    .environmentObject(session)

            case .buttonString:
                labeledContent {
                    Button {
                        session.activateLCDUIItem(item.id)
                    } label: {
                        styledString(item.contentLabel)
                            .frame(maxWidth: .infinity, minHeight: 32)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .frame(maxWidth: .infinity)
                }

            case .hyperlinkString:
                labeledContent {
                    Button {
                        session.activateLCDUIItem(item.id)
                    } label: {
                        HStack(spacing: 8) {
                            styledString(item.contentLabel)
                                .frame(maxWidth: .infinity, alignment: .leading)

                            Image(systemName: "arrow.up.right")
                                .font(.caption.weight(.semibold))
                                .accessibilityHidden(true)
                        }
                        .frame(minHeight: 44)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .foregroundStyle(.tint)
                }

            case .plainString:
                labeledContent {
                    styledString(item.text)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .textSelection(.enabled)
                }

            case .plainImage:
                labeledContent {
                    nativePixelImage
                }

            case .hyperlinkImage:
                labeledContent {
                    Button {
                        session.activateLCDUIItem(item.id)
                    } label: {
                        nativePixelImage
                    }
                    .buttonStyle(.borderless)
                }

            case .buttonImage:
                labeledContent {
                    Button {
                        session.activateLCDUIItem(item.id)
                    } label: {
                        nativePixelImage
                    }
                    .buttonStyle(.plain)
                }

            case .customItem:
                labeledContent {
                    NativeLCDUICustomItem(item: item)
                        .environmentObject(session)
                }

            case .spacer:
                Color.clear
                    .frame(height: CGFloat(min(max(item.frame.height, 8), 48)))

            default:
                EmptyView()
            }
        }
        .frame(maxWidth: .infinity, alignment: item.horizontalAlignment)
    }

    private var nativePixelImage: some View {
        NativeLCDUIItemImage(
            imageStore: session.lcdUIImageStore,
            item: item,
            placeholderSymbol: "photo"
        )
    }

    private var exclusiveChoice: some View {
        let choices = item.orderedChoices
        return labeledContent {
            VStack(spacing: 0) {
                ForEach(Array(choices.enumerated()), id: \.element.id) { index, choice in
                    Button {
                        session.focusLCDUIItem(item.id)
                        session.setLCDUIChoice(
                            componentID: item.id,
                            index: choice.index,
                            selected: true
                        )
                    } label: {
                        LCDUIChoiceCell(
                            imageStore: session.lcdUIImageStore,
                            choice: choice,
                            fitPolicy: item.fitPolicy,
                            accessory: .radio(choice.isSelected)
                        )
                        .frame(minHeight: 44)
                    }
                    .buttonStyle(LCDUIImmediateFeedbackButtonStyle())
                    .accessibilityValue(choice.isSelected ? "Selected" : "Not selected")

                    if index < choices.count - 1 {
                        Divider()
                            .padding(.leading, choice.imageKey == nil ? 0 : 44)
                    }
                }
            }
        }
    }

    private var implicitChoice: some View {
        let choices = item.orderedChoices
        return labeledContent {
            VStack(spacing: 0) {
                ForEach(Array(choices.enumerated()), id: \.element.id) { index, choice in
                    Button {
                        session.focusLCDUIItem(item.id)
                        showPendingFeedback(for: choice.index)
                        session.setLCDUIChoice(
                            componentID: item.id,
                            index: choice.index,
                            selected: true
                        )
                    } label: {
                        LCDUIChoiceCell(
                            imageStore: session.lcdUIImageStore,
                            choice: choice,
                            fitPolicy: item.fitPolicy,
                            accessory: pendingImplicitChoiceIndex == choice.index
                                ? .progress
                                : .disclosure
                        )
                        .frame(minHeight: 44)
                    }
                    .buttonStyle(LCDUIImmediateFeedbackButtonStyle())

                    if index < choices.count - 1 {
                        Divider()
                            .padding(.leading, choice.imageKey == nil ? 0 : 44)
                    }
                }
            }
        }
    }

    private func showPendingFeedback(for index: Int) {
        pendingImplicitChoiceIndex = index
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 900_000_000)
            if pendingImplicitChoiceIndex == index {
                pendingImplicitChoiceIndex = nil
            }
        }
    }

    private var multipleChoice: some View {
        let choices = item.orderedChoices
        return labeledContent {
            VStack(spacing: 0) {
                ForEach(Array(choices.enumerated()), id: \.element.id) { index, choice in
                    Button {
                        session.focusLCDUIItem(item.id)
                        session.setLCDUIChoice(
                            componentID: item.id,
                            index: choice.index,
                            selected: !choice.isSelected
                        )
                    } label: {
                        LCDUIChoiceCell(
                            imageStore: session.lcdUIImageStore,
                            choice: choice,
                            fitPolicy: item.fitPolicy,
                            accessory: .checkbox(choice.isSelected)
                        )
                        .frame(minHeight: 44)
                    }
                    .buttonStyle(LCDUIImmediateFeedbackButtonStyle())
                    .accessibilityValue(
                        choice.isSelected ? "Selected" : "Not selected"
                    )

                    if index < choices.count - 1 {
                        Divider()
                            .padding(.leading, choice.imageKey == nil ? 0 : 44)
                    }
                }
            }
        }
    }

    private func styledString(_ value: String) -> some View {
        Text(value)
            .font(item.swiftUIFont)
            .underline(item.fontStyle & 4 != 0)
    }

    @ViewBuilder
    private func labeledContent<Content: View>(
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            if !item.label.isEmpty {
                Text(item.label)
                    .font(.footnote.weight(.medium))
                    .foregroundStyle(.secondary)
                    .textCase(nil)
                    .fixedSize(horizontal: false, vertical: true)
            }
            content()
        }
    }
}

private struct NativeLCDUIItemImage: View {
    @ObservedObject private var imageSlot: LCDUIImageSlot

    let item: LCDUIState.Item
    let placeholderSymbol: String
    let minimumPlaceholderHeight: CGFloat?
    let scalesToFitContainer: Bool

    init(
        imageStore: LCDUIImageStore,
        item: LCDUIState.Item,
        placeholderSymbol: String,
        minimumPlaceholderHeight: CGFloat? = nil,
        scalesToFitContainer: Bool = false
    ) {
        _imageSlot = ObservedObject(
            wrappedValue: imageStore.slot(for: item.id)
        )
        self.item = item
        self.placeholderSymbol = placeholderSymbol
        self.minimumPlaceholderHeight = minimumPlaceholderHeight
        self.scalesToFitContainer = scalesToFitContainer
    }

    var body: some View {
        Group {
            if let image = imageSlot.image {
                renderedImage(image)
                    .accessibilityLabel(item.contentLabel)
            } else {
                Label(item.contentLabel, systemImage: placeholderSymbol)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, minHeight: 72)
                    .background(
                        Color(uiColor: .tertiarySystemFill),
                        in: RoundedRectangle(
                            cornerRadius: LCDUIStyle.controlCornerRadius,
                            style: .continuous
                        )
                    )
            }
        }
        .frame(
            maxWidth: .infinity,
            minHeight: minimumPlaceholderHeight,
            alignment: item.horizontalAlignment
        )
        .clipShape(
            RoundedRectangle(
                cornerRadius: LCDUIStyle.controlCornerRadius,
                style: .continuous
            )
        )
    }

    @ViewBuilder
    private func renderedImage(_ image: CGImage) -> some View {
        if scalesToFitContainer || image.width > 320 {
            Image(decorative: image, scale: 1)
                .resizable()
                .interpolation(.none)
                .scaledToFit()
                .frame(maxHeight: 320)
        } else {
            Image(decorative: image, scale: 1)
                .interpolation(.none)
        }
    }
}

private struct NativeLCDUICustomItem: View {
    @EnvironmentObject private var session: EmulatorSession
    @State private var pointerIsDown = false

    let item: LCDUIState.Item

    var body: some View {
        customContent
            .overlay {
                GeometryReader { proxy in
                    Color.clear
                        .contentShape(Rectangle())
                        .gesture(pointerGesture(viewSize: proxy.size))
                }
            }
            .accessibilityLabel(item.contentLabel)
    }

    private var customContent: some View {
        NativeLCDUIItemImage(
            imageStore: session.lcdUIImageStore,
            item: item,
            placeholderSymbol: "rectangle.dashed",
            minimumPlaceholderHeight: CGFloat(
                min(max(item.frame.height, 80), 320)
            ),
            scalesToFitContainer: true
        )
    }

    private func pointerGesture(viewSize: CGSize) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .local)
            .onChanged { value in
                let action: Int32 = pointerIsDown ? 3 : 1
                if !pointerIsDown {
                    pointerIsDown = true
                    session.focusLCDUIItem(item.id)
                }
                sendPointer(
                    at: value.location,
                    action: action,
                    viewSize: viewSize
                )
            }
            .onEnded { value in
                guard pointerIsDown else { return }
                pointerIsDown = false
                sendPointer(
                    at: value.location,
                    action: 2,
                    viewSize: viewSize
                )
            }
    }

    private func sendPointer(
        at location: CGPoint,
        action: Int32,
        viewSize: CGSize
    ) {
        let contentWidth = session.lcdUIImages[item.id]?.width
            ?? max(Int(item.frame.width), 1)
        let contentHeight = session.lcdUIImages[item.id]?.height
            ?? max(Int(item.frame.height), 1)
        let viewWidth = max(viewSize.width, 1)
        let viewHeight = max(viewSize.height, 1)
        let x = min(
            max(Int(location.x / viewWidth * CGFloat(contentWidth)), 0),
            contentWidth - 1
        )
        let y = min(
            max(Int(location.y / viewHeight * CGFloat(contentHeight)), 0),
            contentHeight - 1
        )
        session.sendPointer(x: Int32(x), y: Int32(y), action: action)
    }
}

private struct NativeLCDUIPopupChoice: View {
    @EnvironmentObject private var session: EmulatorSession

    let item: LCDUIState.Item

    var body: some View {
        HStack(spacing: 12) {
            Text(item.label.isEmpty ? L10n.string("Selection") : item.label)
                .foregroundStyle(.primary)
                .frame(maxWidth: .infinity, alignment: .leading)

            Picker(
                "",
                selection: Binding(
                    get: {
                        item.orderedChoices.first(where: \.isSelected)?.index
                            ?? item.orderedChoices.first?.index
                            ?? -1
                    },
                    set: { index in
                        session.focusLCDUIItem(item.id)
                        session.setLCDUIChoice(
                            componentID: item.id,
                            index: index,
                            selected: true
                        )
                    }
                )
            ) {
                ForEach(item.orderedChoices) { choice in
                    Text(choice.text)
                        .tag(choice.index)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
        }
        .frame(minHeight: 44)
    }
}

private struct NativeLCDUITextField: View {
    @EnvironmentObject private var session: EmulatorSession
    @FocusState private var isFocused: Bool
    @State private var text: String

    let item: LCDUIState.Item

    init(item: LCDUIState.Item) {
        self.item = item
        _text = State(initialValue: item.text)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if !item.label.isEmpty {
                Text(item.label)
                    .font(.footnote.weight(.medium))
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            if isUneditable {
                Text(text)
                    .foregroundStyle(.primary)
                    .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                    .padding(.horizontal, 12)
                    .background(
                        Color(uiColor: .tertiarySystemFill),
                        in: RoundedRectangle(
                            cornerRadius: LCDUIStyle.controlCornerRadius,
                            style: .continuous
                        )
                    )
                    .accessibilityAddTraits(.isStaticText)
            } else if isPassword {
                SecureField("", text: binding)
                    .textFieldStyle(.plain)
                    .lcdUIInputSurface()
                    .keyboardType(keyboardType)
                    .textInputAutocapitalization(capitalization)
                    .autocorrectionDisabled(isNonPredictive || isSensitive)
                    .submitLabel(.done)
                    .focused($isFocused)
                    .onSubmit { isFocused = false }
            } else {
                TextField("", text: binding)
                    .textFieldStyle(.plain)
                    .lcdUIInputSurface()
                    .keyboardType(keyboardType)
                    .textInputAutocapitalization(capitalization)
                    .autocorrectionDisabled(isNonPredictive || isSensitive)
                    .submitLabel(.done)
                    .focused($isFocused)
                    .onSubmit { isFocused = false }
            }

            if item.maxSize > 0, !isUneditable {
                Text("\(text.count) / \(item.maxSize)")
                    .font(.caption2.monospacedDigit())
                    .foregroundStyle(.tertiary)
                    .frame(maxWidth: .infinity, alignment: .trailing)
                    .accessibilityLabel(
                        L10n.format(
                            "%d of %d characters",
                            text.count,
                            item.maxSize
                        )
                    )
            }
        }
        .toolbar {
            ToolbarItemGroup(placement: .keyboard) {
                Spacer()
                Button("Done") {
                    isFocused = false
                }
            }
        }
        .onChange(of: isFocused) { focused in
            if focused {
                session.focusLCDUIItem(item.id)
            }
        }
        .onChange(of: item.text) { newValue in
            if newValue != text {
                text = newValue
            }
        }
    }

    private var binding: Binding<String> {
        Binding(
            get: { text },
            set: { newValue in
                let limited = item.limitedText(newValue)
                text = limited
                session.setLCDUIText(
                    componentID: item.id,
                    text: limited,
                    caretPosition: limited.count
                )
            }
        )
    }

    private var baseConstraint: Int { item.constraints & 0xFFFF }
    private var isPassword: Bool { item.constraints & 0x10000 != 0 }
    private var isUneditable: Bool { item.constraints & 0x20000 != 0 }
    private var isSensitive: Bool { item.constraints & 0x40000 != 0 }
    private var isNonPredictive: Bool { item.constraints & 0x80000 != 0 }

    private var keyboardType: UIKeyboardType {
        switch baseConstraint {
        case 1: return .emailAddress
        case 2: return .numberPad
        case 3: return .phonePad
        case 4: return .URL
        case 5: return .decimalPad
        default: return .default
        }
    }

    private var capitalization: TextInputAutocapitalization {
        if item.constraints & 0x200000 != 0 {
            return .sentences
        }
        if item.constraints & 0x100000 != 0 {
            return .words
        }
        return .never
    }
}

private struct NativeLCDUITextBox: View {
    @EnvironmentObject private var session: EmulatorSession
    @FocusState private var isFocused: Bool
    @State private var text: String

    let item: LCDUIState.Item

    init(item: LCDUIState.Item) {
        self.item = item
        _text = State(initialValue: item.text)
    }

    var body: some View {
        VStack(spacing: 8) {
            Group {
                if item.constraints & 0x20000 != 0 {
                    ScrollView {
                        Text(text)
                            .frame(maxWidth: .infinity, alignment: .topLeading)
                            .textSelection(.enabled)
                            .padding(12)
                    }
                    .background(
                        Color(uiColor: .secondarySystemGroupedBackground),
                        in: RoundedRectangle(
                            cornerRadius: LCDUIStyle.controlCornerRadius,
                            style: .continuous
                        )
                    )
                } else if item.constraints & 0x10000 != 0 {
                    SecureField("", text: binding)
                        .textFieldStyle(.plain)
                        .lcdUIInputSurface()
                        .keyboardType(keyboardType)
                        .textInputAutocapitalization(capitalization)
                        .autocorrectionDisabled(isNonPredictive || isSensitive)
                        .submitLabel(.done)
                        .focused($isFocused)
                        .onSubmit { isFocused = false }
                        .frame(maxHeight: .infinity, alignment: .top)
                } else {
                    TextEditor(text: binding)
                        .focused($isFocused)
                        .keyboardType(keyboardType)
                        .textInputAutocapitalization(capitalization)
                        .autocorrectionDisabled(isNonPredictive || isSensitive)
                        .phoneMEScrollContentBackgroundHidden()
                        .padding(8)
                        .background(
                            Color(uiColor: .secondarySystemGroupedBackground),
                            in: RoundedRectangle(
                                cornerRadius: LCDUIStyle.controlCornerRadius,
                                style: .continuous
                            )
                        )
                }
            }

            if item.maxSize > 0, item.constraints & 0x20000 == 0 {
                Text("\(text.count) / \(item.maxSize)")
                    .font(.caption2.monospacedDigit())
                    .foregroundStyle(.tertiary)
                    .frame(maxWidth: .infinity, alignment: .trailing)
            }
        }
        .frame(maxWidth: LCDUIStyle.maximumContentWidth, maxHeight: .infinity)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(.horizontal, LCDUIStyle.contentInset)
        .padding(.vertical, 12)
        .background(Color(uiColor: .systemGroupedBackground))
        .toolbar {
            ToolbarItemGroup(placement: .keyboard) {
                Spacer()
                Button("Done") {
                    isFocused = false
                }
            }
        }
        .onAppear {
            if item.constraints & 0x20000 == 0 {
                isFocused = true
            }
        }
        .onChange(of: isFocused) { focused in
            if focused {
                session.focusLCDUIItem(item.id)
            }
        }
        .onChange(of: item.text) { newValue in
            if newValue != text {
                text = newValue
            }
        }
    }

    private var binding: Binding<String> {
        Binding(
            get: { text },
            set: { newValue in
                let limited = item.limitedText(newValue)
                text = limited
                session.setLCDUIText(
                    componentID: item.id,
                    text: limited,
                    caretPosition: limited.count
                )
            }
        )
    }

    private var baseConstraint: Int { item.constraints & 0xFFFF }
    private var isSensitive: Bool { item.constraints & 0x40000 != 0 }
    private var isNonPredictive: Bool { item.constraints & 0x80000 != 0 }

    private var keyboardType: UIKeyboardType {
        switch baseConstraint {
        case 1: return .emailAddress
        case 2: return .numberPad
        case 3: return .phonePad
        case 4: return .URL
        case 5: return .decimalPad
        default: return .default
        }
    }

    private var capitalization: TextInputAutocapitalization {
        if item.constraints & 0x200000 != 0 {
            return .sentences
        }
        if item.constraints & 0x100000 != 0 {
            return .words
        }
        return .never
    }
}

private struct NativeLCDUIProgressGauge: View {
    let item: LCDUIState.Item

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(item.label.isEmpty ? L10n.string("Progress") : item.label)
                    .font(.footnote.weight(.medium))
                    .foregroundStyle(.secondary)

                Spacer()

                if item.maxValue > 0 {
                    Text(progressValue)
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
            }

            if item.maxValue == -1 {
                indefiniteProgress
            } else {
                ProgressView(
                    value: Double(max(item.value, 0)),
                    total: Double(max(item.maxValue, 1))
                )
                .accessibilityValue("\(item.value) of \(item.maxValue)")
            }
        }
    }

    private var progressValue: String {
        let maximum = max(item.maxValue, 1)
        let percent = Int((Double(max(item.value, 0)) / Double(maximum) * 100).rounded())
        return "\(min(percent, 100))%"
    }

    @ViewBuilder
    private var indefiniteProgress: some View {
        switch item.value {
        case 2, 3:
            HStack(spacing: 10) {
                ProgressView()
                Text("Loading…")
                    .foregroundStyle(.secondary)
            }
            .accessibilityLabel("Loading")
        case 1:
            ProgressView(value: 1, total: 1)
                .accessibilityLabel("Paused")
        default:
            ProgressView(value: 0, total: 1)
                .accessibilityLabel("Idle")
        }
    }
}

private struct NativeLCDUIGauge: View {
    @EnvironmentObject private var session: EmulatorSession
    @State private var value: Double

    let item: LCDUIState.Item

    init(item: LCDUIState.Item) {
        self.item = item
        _value = State(initialValue: Double(item.value))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(item.label.isEmpty ? L10n.string("Value") : item.label)
                    .font(.footnote.weight(.medium))
                    .foregroundStyle(.secondary)

                Spacer()

                Text("\(Int(value)) / \(max(item.maxValue, 1))")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }

            Slider(
                value: Binding(
                    get: { value },
                    set: { newValue in
                        value = newValue
                        session.setLCDUIGauge(
                            componentID: item.id,
                            value: Int(newValue.rounded())
                        )
                    }
                ),
                in: 0...Double(max(item.maxValue, 1)),
                step: 1
            )
            .accessibilityValue("\(Int(value)) of \(item.maxValue)")
        }
        .onChange(of: item.value) { newValue in
            value = Double(newValue)
        }
    }
}

private struct NativeLCDUIDateField: View {
    @EnvironmentObject private var session: EmulatorSession
    @State private var date: Date

    let item: LCDUIState.Item

    init(item: LCDUIState.Item) {
        self.item = item
        _date = State(initialValue: item.date)
    }

    var body: some View {
        DatePicker(
            item.label.isEmpty ? "Date & Time" : item.label,
            selection: Binding(
                get: { date },
                set: { newValue in
                    date = newValue
                    session.setLCDUIDate(componentID: item.id, date: newValue)
                }
            ),
            displayedComponents: displayedComponents
        )
        .datePickerStyle(.compact)
        .environment(\.timeZone, resolvedTimeZone)
        .onChange(of: item.date) { newValue in
            date = newValue
        }
    }

    private var displayedComponents: DatePickerComponents {
        switch item.inputMode {
        case 1: return .hourAndMinute
        case 2: return .date
        default: return [.date, .hourAndMinute]
        }
    }

    private var resolvedTimeZone: TimeZone {
        TimeZone(identifier: item.text) ?? .current
    }
}

private extension View {
    func lcdUIInputSurface() -> some View {
        padding(.horizontal, 12)
            .frame(minHeight: 44)
            .background(
                Color(uiColor: .tertiarySystemFill),
                in: RoundedRectangle(
                    cornerRadius: LCDUIStyle.controlCornerRadius,
                    style: .continuous
                )
            )
            .overlay {
                RoundedRectangle(
                    cornerRadius: LCDUIStyle.controlCornerRadius,
                    style: .continuous
                )
                .stroke(Color(uiColor: .separator).opacity(0.2), lineWidth: 0.5)
            }
    }
}

private extension LCDUIState.Choice {
    var swiftUIFont: Font {
        resolvedFont(weight: fontStyle & 1 != 0 ? .bold : .regular)
    }

    var swiftUIListFont: Font {
        resolvedFont(weight: .regular)
    }

    private func resolvedFont(weight: Font.Weight) -> Font {
        let pointSize: CGFloat
        switch fontSize {
        case 8: pointSize = 13
        case 16: pointSize = 21
        default: pointSize = 17
        }

        let design: Font.Design = fontFace == 32 ? .monospaced : .default
        var font = Font.system(size: pointSize, weight: weight, design: design)
        if fontStyle & 2 != 0 {
            font = font.italic()
        }
        return font
    }
}

private extension LCDUIState.Item {
    static let layoutRight = 2
    static let layoutCenter = 3
    var swiftUIFont: Font {
        let pointSize: CGFloat
        switch fontSize {
        case 8: pointSize = 13
        case 16: pointSize = 21
        default: pointSize = 17
        }

        let weight: Font.Weight = fontStyle & 1 != 0 ? .bold : .regular
        let design: Font.Design = fontFace == 32 ? .monospaced : .default
        var font = Font.system(size: pointSize, weight: weight, design: design)
        if fontStyle & 2 != 0 {
            font = font.italic()
        }
        return font
    }

    var contentLabel: String {
        if !text.isEmpty {
            return text
        }
        if !label.isEmpty {
            return label
        }
        return L10n.string("Item")
    }

    var horizontalAlignment: Alignment {
        switch layout & 0x03 {
        case Self.layoutRight: return .trailing
        case Self.layoutCenter: return .center
        default: return .leading
        }
    }

    var formRowInsets: EdgeInsets {
        switch type {
        case .spacer:
            return EdgeInsets()
        default:
            return EdgeInsets(top: 10, leading: 16, bottom: 10, trailing: 16)
        }
    }

    var hidesFormRowSeparator: Bool {
        switch type {
        case .spacer, .exclusiveChoice, .multipleChoice, .implicitChoice,
             .customItem:
            return true
        default:
            return false
        }
    }

    func limitedText(_ value: String) -> String {
        guard maxSize > 0 else { return value }
        return String(value.prefix(maxSize))
    }
}

private extension LCDUIState.Command {
    var displayLabel: String {
        if !label.isEmpty {
            return label
        }
        if !longLabel.isEmpty {
            return longLabel
        }
        switch type {
        case 2: return L10n.string("Back")
        case 3: return L10n.string("Cancel")
        case 4: return L10n.string("OK")
        case 5: return L10n.string("Help")
        case 6: return L10n.string("Stop")
        case 7: return L10n.string("Exit")
        case 8: return L10n.string("Select")
        default: return L10n.string("Select")
        }
    }

    var menuLabel: String {
        longLabel.isEmpty ? displayLabel : longLabel
    }

    var buttonRole: ButtonRole? {
        switch type {
        case 6, 7:
            return .destructive
        default:
            return nil
        }
    }

    var systemImage: String {
        switch type {
        case 2: return "chevron.backward"
        case 3: return "xmark"
        case 4: return "checkmark"
        case 5: return "questionmark.circle"
        case 6: return "stop.fill"
        case 7: return "rectangle.portrait.and.arrow.right"
        case 8: return "hand.tap"
        default: return "ellipsis"
        }
    }

    var tintStyle: Color {
        switch type {
        case 6, 7: return .red
        default: return .primary
        }
    }

    var softKeyTint: Color {
        switch type {
        case 6, 7: return .red
        default: return .accentColor
        }
    }

    var iconBackground: Color {
        switch type {
        case 6, 7:
            return Color.red.opacity(0.12)
        default:
            return Color.accentColor.opacity(0.12)
        }
    }
}

private extension CGImage {
    var isPhoneMEBlankAlertPlaceholder: Bool {
        guard
            width == 16,
            height == 16,
            bitsPerPixel == 32,
            bytesPerRow >= width * 4,
            let data = dataProvider?.data,
            let bytes = CFDataGetBytePtr(data),
            CFDataGetLength(data) >= bytesPerRow * height
        else {
            return false
        }

        for row in 0..<height {
            let rowOffset = row * bytesPerRow
            for column in 0..<width {
                let pixelOffset = rowOffset + column * 4
                if bytes[pixelOffset] < 250 ||
                    bytes[pixelOffset + 1] < 250 ||
                    bytes[pixelOffset + 2] < 250 {
                    return false
                }
            }
        }
        return true
    }
}

#else

struct NativeLCDUIScreenView: View {
    @ObservedObject var imageStore: LCDUIImageStore
    let state: LCDUIState
    let showsTitleInContent: Bool

    var body: some View {
        Text(state.screen?.title ?? "LCDUI")
            .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

#endif
