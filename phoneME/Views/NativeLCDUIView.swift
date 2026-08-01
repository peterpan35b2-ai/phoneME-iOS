import SwiftUI

#if os(iOS)
import UIKit

struct NativeLCDUIScreenView: View {
    @EnvironmentObject private var session: EmulatorSession

    let imageStore: LCDUIImageStore
    let state: LCDUIState
    let profile: GameProfile
    let showsListTitleInContent: Bool

    var body: some View {
        VStack(spacing: 0) {
            if let screen = state.screen {
                if state.screenKind != .alert {
                    screenHeader(
                        screen,
                        showsTitle: state.screenKind != .list || showsListTitleInContent
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
        .environment(
            \.font,
            .system(size: resolvedFontSize(profile.fontMedium, style: .body))
        )
        .background(surfaceBackground.ignoresSafeArea())
    }

    private var surfaceBackground: Color {
        switch state.screenKind {
        case .form, .menu:
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
            VStack(alignment: .leading, spacing: 5) {
                if showsTitle && !screen.title.isEmpty {
                    Text(screen.title)
                        .font(
                            .system(
                                size: resolvedFontSize(
                                    profile.fontLarge,
                                    style: .headline
                                ),
                                weight: .semibold
                            )
                        )
                        .accessibilityAddTraits(.isHeader)
                }

                if !screen.detail.isEmpty {
                    ScrollView(.horizontal, showsIndicators: false) {
                        Text(screen.detail)
                            .font(
                                .system(
                                    size: resolvedFontSize(
                                        profile.fontSmall,
                                        style: .caption1
                                    )
                                )
                            )
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                    .accessibilityLabel(screen.detail)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 16)
            .padding(.top, 12)
            .padding(.bottom, 10)
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
            NativeLCDUIListView(item: item)
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
            Button {
                session.selectLCDUICommand(command.id)
            } label: {
                HStack(spacing: 12) {
                    Text(command.displayLabel)
                        .foregroundStyle(.primary)
                    Spacer(minLength: 12)
                    Image(systemName: "chevron.right")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.tertiary)
                        .accessibilityHidden(true)
                }
                .frame(minHeight: 44)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
        }
        .listStyle(.insetGrouped)
        .scrollContentBackground(.hidden)
        .background(Color(uiColor: .systemGroupedBackground))
    }

    private var emptyContent: some View {
        ProgressView()
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .accessibilityLabel("Loading")
    }

    private func alertContent(_ screen: LCDUIState.Screen) -> some View {
        GeometryReader { geometry in
            ScrollView {
                VStack(spacing: 18) {
                    alertIcon(for: screen)

                    if !screen.title.isEmpty {
                        Text(screen.title)
                            .font(.title3.weight(.semibold))
                            .multilineTextAlignment(.center)
                            .fixedSize(horizontal: false, vertical: true)
                            .accessibilityAddTraits(.isHeader)
                    }

                    if !screen.detail.isEmpty {
                        Text(screen.detail)
                            .frame(maxWidth: .infinity)
                            .multilineTextAlignment(.center)
                            .fixedSize(horizontal: false, vertical: true)
                            .textSelection(.enabled)
                    }

                    ForEach(state.visibleItems) { item in
                        NativeLCDUIItemView(item: item)
                            .environmentObject(session)
                            .frame(maxWidth: .infinity)
                    }
                }
                .padding(24)
                .frame(maxWidth: 440)
                .background(
                    Color(uiColor: .secondarySystemGroupedBackground),
                    in: RoundedRectangle(cornerRadius: 18, style: .continuous)
                )
                .padding(.horizontal, 16)
                .padding(.vertical, 20)
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

    private func resolvedFontSize(
        _ value: Int,
        style: UIFont.TextStyle
    ) -> CGFloat {
        let base = CGFloat(min(max(value, 1), 128))
        guard profile.fontValuesAreScaledPixels else { return base }
        return UIFontMetrics(forTextStyle: style).scaledValue(for: base)
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
                    .font(.system(size: 48, weight: .semibold))
                    .foregroundStyle(fallbackTint)
            }
        }
        .frame(width: 96, height: 96)
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
            HStack(spacing: 12) {
                leftCommand(layout)
                rightCommand(layout)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 4)
            .frame(minHeight: 52)
            .background(.bar)
            .overlay(alignment: .top) {
                Divider()
            }
            .confirmationDialog(
                "Commands",
                isPresented: $isShowingCommandMenu,
                titleVisibility: .visible
            ) {
                ForEach(layout.leftCommands) { command in
                    Button(command.menuLabel) {
                        session.selectLCDUICommand(command.id)
                    }
                }
                Button("Dismiss", role: .cancel) { }
            }
        }
    }

    @ViewBuilder
    private func leftCommand(_ layout: LCDUIState.CommandLayout) -> some View {
        if layout.leftCommands.isEmpty {
            Color.clear
                .frame(maxWidth: .infinity, minHeight: 44)
                .accessibilityHidden(true)
        } else {
            Button {
                if layout.leftCommands.count == 1,
                   let command = layout.leftCommands.first {
                    session.selectLCDUICommand(command.id)
                } else {
                    isShowingCommandMenu = true
                }
            } label: {
                Text(
                    layout.leftCommands.count == 1
                        ? layout.leftCommands[0].displayLabel
                        : "Options"
                )
                .font(.body.weight(.semibold))
                .lineLimit(1)
                .frame(
                    maxWidth: .infinity,
                    minHeight: 44,
                    alignment: .leading
                )
                .contentShape(Rectangle())
            }
            .buttonStyle(LCDUIImmediateFeedbackButtonStyle())
            .foregroundStyle(Color.accentColor)
            .accessibilityHint(
                layout.leftCommands.count == 1
                    ? "Activates the left soft key"
                    : "Shows available commands"
            )
        }
    }

    @ViewBuilder
    private func rightCommand(_ layout: LCDUIState.CommandLayout) -> some View {
        if let command = layout.rightCommand {
            Button {
                session.selectLCDUICommand(command.id)
            } label: {
                Text(command.displayLabel)
                    .font(.body.weight(.semibold))
                    .lineLimit(1)
                    .frame(
                        maxWidth: .infinity,
                        minHeight: 44,
                        alignment: .trailing
                    )
                    .contentShape(Rectangle())
            }
            .buttonStyle(LCDUIImmediateFeedbackButtonStyle())
            .foregroundStyle(Color.accentColor)
            .accessibilityHint("Activates the right soft key")
        } else {
            Color.clear
                .frame(maxWidth: .infinity, minHeight: 44)
                .accessibilityHidden(true)
        }
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
        ScrollViewReader { proxy in
            ScrollView {
                GeometryReader { geometry in
                    Color.clear.preference(
                        key: LCDUIScrollOffsetPreferenceKey.self,
                        value: max(
                            Int(-geometry.frame(in: .named("lcdui-scroll")).minY),
                            0
                        )
                    )
                }
                .frame(height: 0)

                LazyVStack(alignment: .leading, spacing: 0) {
                    ForEach(
                        Array(state.visibleItems.enumerated()),
                        id: \.element.id
                    ) { index, item in
                        NativeLCDUIItemView(item: item)
                            .environmentObject(session)
                            .padding(.horizontal, 16)
                            .padding(.vertical, 12)
                            .id(item.id)

                        if index < state.visibleItems.count - 1,
                           item.type != .spacer {
                            Divider()
                                .padding(.leading, 16)
                        }
                    }
                }
                .background(
                    Color(uiColor: .secondarySystemGroupedBackground),
                    in: RoundedRectangle(cornerRadius: 12, style: .continuous)
                )
                .padding(.horizontal, 16)
                .padding(.vertical, 12)
            }
            .background(Color(uiColor: .systemGroupedBackground))
            .coordinateSpace(name: "lcdui-scroll")
            .scrollDismissesKeyboard(.interactively)
            .onPreferenceChange(LCDUIScrollOffsetPreferenceKey.self) { value in
                // Do not mutate SwiftUI state for every few pixels: that used
                // to re-evaluate the complete native Form while scrolling.
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
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func restoreScrollPosition(using proxy: ScrollViewProxy) {
        guard let requested = state.screen?.scrollPosition, requested > 0 else {
            return
        }
        let target = state.visibleItems.last(where: { $0.frame.y <= requested })
        if let target {
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
            .background(
                configuration.isPressed
                    ? Color.accentColor.opacity(0.14)
                    : Color.clear
            )
            .opacity(configuration.isPressed ? 0.86 : 1)
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

private struct NativeLCDUIChoiceLabel: View {
    @EnvironmentObject private var imageStore: LCDUIImageStore

    let choice: LCDUIState.Choice
    let fitPolicy: Int

    var body: some View {
        HStack(spacing: 10) {
            if let imageKey = choice.imageKey {
                NativeLCDUIChoiceImage(
                    imageStore: imageStore,
                    imageKey: imageKey
                )
            }

            styledText
                .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    @ViewBuilder
    private var styledText: some View {
        let text = Text(choice.text)
            .font(choice.swiftUIFont)
            .underline(choice.fontStyle & 4 != 0)
            .lineLimit(fitPolicy == 2 ? 1 : nil)

        if choice.fontStyle & 2 != 0 {
            text.italic()
        } else {
            text
        }
    }
}

private struct NativeLCDUIListView: View {
    @EnvironmentObject private var session: EmulatorSession
    @State private var pendingImplicitChoiceIndex: Int?

    let item: LCDUIState.Item

    var body: some View {
        Group {
            if item.orderedChoices.isEmpty {
                Text("No items")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .accessibilityLabel("Empty list")
            } else {
                List(item.orderedChoices) { choice in
                    choiceRow(choice)
                        .listRowInsets(
                            EdgeInsets(top: 3, leading: 16, bottom: 3, trailing: 16)
                        )
                        .listRowBackground(Color(uiColor: .systemBackground))
                }
                .listStyle(.plain)
                .scrollContentBackground(.hidden)
                .background(Color(uiColor: .systemBackground))
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
        Button {
            if effectiveType == .implicitChoice {
                showPendingFeedback(for: choice.index)
            }
            select(choice)
        } label: {
            HStack(spacing: 12) {
                if effectiveType != .implicitChoice {
                    selectionIndicator(for: choice)
                        .frame(width: 22)
                }

                choiceLabel(choice)

                if effectiveType == .implicitChoice {
                    if pendingImplicitChoiceIndex == choice.index {
                        ProgressView()
                            .controlSize(.small)
                            .accessibilityLabel("Opening")
                    } else {
                        Image(systemName: "chevron.right")
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(.tertiary)
                            .accessibilityHidden(true)
                    }
                }
            }
            .frame(maxWidth: .infinity, minHeight: 50, alignment: .leading)
            .background(
                effectiveType == .implicitChoice
                    && pendingImplicitChoiceIndex == choice.index
                    ? Color.accentColor.opacity(0.12)
                    : Color.clear
            )
            .contentShape(Rectangle())
        }
        .buttonStyle(LCDUIImmediateFeedbackButtonStyle())
        .accessibilityValue(
            effectiveType == .implicitChoice
                ? ""
                : (choice.isSelected ? "Selected" : "Not selected")
        )
    }

    @ViewBuilder
    private func selectionIndicator(for choice: LCDUIState.Choice) -> some View {
        switch effectiveType {
        case .multipleChoice:
            Image(systemName: choice.isSelected ? "checkmark.square.fill" : "square")
                .foregroundStyle(choice.isSelected ? Color.accentColor : .secondary)
                .accessibilityHidden(true)

        case .exclusiveChoice:
            Image(
                systemName: choice.isSelected
                    ? "largecircle.fill.circle"
                    : "circle"
            )
            .foregroundStyle(choice.isSelected ? Color.accentColor : .secondary)
            .accessibilityHidden(true)

        default:
            Color.clear
                .accessibilityHidden(true)
        }
    }

    @ViewBuilder
    private func choiceLabel(_ choice: LCDUIState.Choice) -> some View {
        NativeLCDUIChoiceLabel(choice: choice, fitPolicy: item.fitPolicy)
            .environmentObject(session)
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
                Button {
                    session.activateLCDUIItem(item.id)
                } label: {
                    styledString(item.contentLabel)
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)

            case .hyperlinkString:
                Button {
                    session.activateLCDUIItem(item.id)
                } label: {
                    styledString(item.contentLabel)
                }
                .buttonStyle(.borderless)

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
                    .buttonStyle(.borderedProminent)
                }

            case .customItem:
                labeledContent {
                    NativeLCDUICustomItem(item: item)
                        .environmentObject(session)
                }

            case .spacer:
                Color.clear
                    .frame(height: CGFloat(max(item.frame.height, 8)))

            default:
                EmptyView()
            }
        }
        .frame(
            maxWidth: item.expandsHorizontally ? .infinity : nil,
            alignment: item.horizontalAlignment
        )
        .frame(
            minHeight: item.frame.height > 0
                ? CGFloat(min(item.frame.height, 1_024))
                : nil
        )
        .background(
            item.isFocused && item.isInteractiveControl
                ? Color.accentColor.opacity(0.08)
                : Color.clear,
            in: RoundedRectangle(cornerRadius: 8, style: .continuous)
        )
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
                        session.setLCDUIChoice(
                            componentID: item.id,
                            index: choice.index,
                            selected: true
                        )
                    } label: {
                        HStack(spacing: 12) {
                            Image(
                                systemName: choice.isSelected
                                    ? "largecircle.fill.circle"
                                    : "circle"
                            )
                            .foregroundStyle(
                                choice.isSelected ? Color.accentColor : .secondary
                            )
                            NativeLCDUIChoiceLabel(
                                choice: choice,
                                fitPolicy: item.fitPolicy
                            )
                            .environmentObject(session)
                        }
                        .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(LCDUIImmediateFeedbackButtonStyle())
                    .accessibilityValue(choice.isSelected ? "Selected" : "Not selected")

                    if index < choices.count - 1 {
                        Divider()
                            .padding(.leading, 34)
                    }
                }
            }
        }
    }

    private var implicitChoice: some View {
        let choices = item.orderedChoices
        return labeledContent {
            VStack(spacing: 0) {
                ForEach(choices) { choice in
                    Button {
                        showPendingFeedback(for: choice.index)
                        session.setLCDUIChoice(
                            componentID: item.id,
                            index: choice.index,
                            selected: true
                        )
                    } label: {
                        HStack {
                            NativeLCDUIChoiceLabel(
                                choice: choice,
                                fitPolicy: item.fitPolicy
                            )
                            .environmentObject(session)
                            if pendingImplicitChoiceIndex == choice.index {
                                ProgressView()
                                    .controlSize(.small)
                                    .accessibilityLabel("Opening")
                            } else {
                                Image(systemName: "chevron.right")
                                    .font(.caption.weight(.semibold))
                                    .foregroundStyle(.tertiary)
                            }
                        }
                        .padding(.vertical, 10)
                        .padding(.horizontal, 4)
                        .background(
                            pendingImplicitChoiceIndex == choice.index
                                ? Color.accentColor.opacity(0.12)
                                : Color.clear
                        )
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(LCDUIImmediateFeedbackButtonStyle())

                    if choice.index != choices.last?.index {
                        Divider()
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
                        session.setLCDUIChoice(
                            componentID: item.id,
                            index: choice.index,
                            selected: !choice.isSelected
                        )
                    } label: {
                        HStack(spacing: 12) {
                            Image(
                                systemName: choice.isSelected
                                    ? "checkmark.square.fill"
                                    : "square"
                            )
                            .foregroundStyle(
                                choice.isSelected ? Color.accentColor : .secondary
                            )
                            NativeLCDUIChoiceLabel(
                                choice: choice,
                                fitPolicy: item.fitPolicy
                            )
                            .environmentObject(session)
                        }
                        .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(LCDUIImmediateFeedbackButtonStyle())
                    .accessibilityValue(
                        choice.isSelected ? "Selected" : "Not selected"
                    )

                    if index < choices.count - 1 {
                        Divider()
                            .padding(.leading, 34)
                    }
                }
            }
        }
    }

    @ViewBuilder
    private func styledString(_ value: String) -> some View {
        let text = Text(value)
            .font(item.swiftUIFont)
            .underline(item.fontStyle & 4 != 0)

        if item.fontStyle & 2 != 0 {
            text.italic()
        } else {
            text
        }
    }

    @ViewBuilder
    private func labeledContent<Content: View>(
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: 7) {
            if !item.label.isEmpty {
                Text(item.label)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.secondary)
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
                    .foregroundStyle(.secondary)
            }
        }
        .frame(
            maxWidth: .infinity,
            minHeight: minimumPlaceholderHeight,
            alignment: item.horizontalAlignment
        )
    }

    @ViewBuilder
    private func renderedImage(_ image: CGImage) -> some View {
        if scalesToFitContainer {
            Image(decorative: image, scale: 1)
                .resizable()
                .interpolation(.none)
                .scaledToFit()
        } else {
            // MIDP ImageItem uses the source image's native pixel dimensions.
            // Keeping the image non-resizable prevents a small Form icon from
            // being expanded to the full width offered by the Form row.
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
            minimumPlaceholderHeight: CGFloat(max(item.frame.height, 44)),
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
        LabeledContent {
            Picker(
                item.label,
                selection: Binding(
                    get: {
                        item.orderedChoices.first(where: \.isSelected)?.index
                            ?? item.orderedChoices.first?.index
                            ?? -1
                    },
                    set: { index in
                        session.setLCDUIChoice(
                            componentID: item.id,
                            index: index,
                            selected: true
                        )
                    }
                )
            ) {
                ForEach(item.orderedChoices) { choice in
                    NativeLCDUIChoiceLabel(
                        choice: choice,
                        fitPolicy: item.fitPolicy
                    )
                    .environmentObject(session)
                    .tag(choice.index)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
        } label: {
            if !item.label.isEmpty {
                Text(item.label)
            }
        }
    }
}

private struct NativeLCDUIInputSurface: ViewModifier {
    let isFocused: Bool

    func body(content: Content) -> some View {
        content
            .foregroundStyle(Color(uiColor: .label))
            .tint(Color(uiColor: .systemBlue))
            .padding(.horizontal, 10)
            .padding(.vertical, 8)
            .background(
                Color(uiColor: .tertiarySystemGroupedBackground),
                in: RoundedRectangle(cornerRadius: 8, style: .continuous)
            )
            .overlay {
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .stroke(
                        isFocused
                            ? Color(uiColor: .systemBlue)
                            : Color(uiColor: .separator),
                        lineWidth: isFocused ? 1.5 : 0.5
                    )
            }
    }
}

private extension View {
    func nativeLCDUIInputSurface(isFocused: Bool = false) -> some View {
        modifier(NativeLCDUIInputSurface(isFocused: isFocused))
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
        VStack(alignment: .leading, spacing: 7) {
            if !item.label.isEmpty {
                Text(item.label)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.secondary)
            }

            if isUneditable {
                Text(text)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .nativeLCDUIInputSurface()
                    .accessibilityAddTraits(.isStaticText)
            } else if isPassword {
                SecureField("", text: binding)
                    .textFieldStyle(.plain)
                    .keyboardType(keyboardType)
                    .textInputAutocapitalization(capitalization)
                    .autocorrectionDisabled(isNonPredictive || isSensitive)
                    .focused($isFocused)
                    .nativeLCDUIInputSurface(isFocused: isFocused)
            } else {
                TextField("", text: binding)
                    .textFieldStyle(.plain)
                    .keyboardType(keyboardType)
                    .textInputAutocapitalization(capitalization)
                    .autocorrectionDisabled(isNonPredictive || isSensitive)
                    .focused($isFocused)
                    .nativeLCDUIInputSurface(isFocused: isFocused)
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
        ZStack {
            Color(uiColor: .systemGroupedBackground)

            Group {
                if item.constraints & 0x20000 != 0 {
                    ScrollView {
                        Text(text)
                            .foregroundStyle(Color(uiColor: .label))
                            .frame(maxWidth: .infinity, alignment: .topLeading)
                            .textSelection(.enabled)
                            .padding(12)
                    }
                    .background(Color(uiColor: .tertiarySystemGroupedBackground))
                    .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                } else if item.constraints & 0x10000 != 0 {
                    SecureField("", text: binding)
                        .textFieldStyle(.plain)
                        .keyboardType(keyboardType)
                        .textInputAutocapitalization(capitalization)
                        .autocorrectionDisabled(isNonPredictive || isSensitive)
                        .focused($isFocused)
                        .nativeLCDUIInputSurface(isFocused: isFocused)
                        .padding(12)
                        .frame(maxHeight: .infinity, alignment: .top)
                } else {
                    TextEditor(text: binding)
                        .foregroundStyle(Color(uiColor: .label))
                        .tint(Color(uiColor: .systemBlue))
                        .focused($isFocused)
                        .keyboardType(keyboardType)
                        .textInputAutocapitalization(capitalization)
                        .autocorrectionDisabled(isNonPredictive || isSensitive)
                        .scrollContentBackground(.hidden)
                        .background(Color(uiColor: .tertiarySystemGroupedBackground))
                        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
                        .overlay {
                            RoundedRectangle(cornerRadius: 8, style: .continuous)
                                .stroke(
                                    isFocused
                                        ? Color(uiColor: .systemBlue)
                                        : Color(uiColor: .separator),
                                    lineWidth: isFocused ? 1.5 : 0.5
                                )
                        }
                        .padding(12)
                }
            }
            .background(
                Color(uiColor: .secondarySystemGroupedBackground),
                in: RoundedRectangle(cornerRadius: 12, style: .continuous)
            )
            .padding(16)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
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
        VStack(alignment: .leading, spacing: 7) {
            if !item.label.isEmpty {
                Text(item.label)
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(.secondary)
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

    @ViewBuilder
    private var indefiniteProgress: some View {
        switch item.value {
        case 2, 3:
            ProgressView()
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
        VStack(alignment: .leading, spacing: 7) {
            if !item.label.isEmpty {
                Text(item.label)
                    .font(.subheadline.weight(.semibold))
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
            item.label,
            selection: Binding(
                get: { date },
                set: { newValue in
                    date = newValue
                    session.setLCDUIDate(componentID: item.id, date: newValue)
                }
            ),
            displayedComponents: displayedComponents
        )
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

private extension LCDUIState.Choice {
    var swiftUIFont: Font {
        let pointSize: CGFloat
        switch fontSize {
        case 8: pointSize = 13
        case 16: pointSize = 21
        default: pointSize = 17
        }

        let weight: Font.Weight = fontStyle & 1 != 0 ? .bold : .regular
        let design: Font.Design = fontFace == 32 ? .monospaced : .default
        return .system(size: pointSize, weight: weight, design: design)
    }
}

private extension LCDUIState.Item {
    static let layoutRight = 2
    static let layoutCenter = 3
    static let layoutExpand = 2_048

    var swiftUIFont: Font {
        let pointSize: CGFloat
        switch fontSize {
        case 8: pointSize = 13
        case 16: pointSize = 21
        default: pointSize = 17
        }

        let weight: Font.Weight = fontStyle & 1 != 0 ? .bold : .regular
        let design: Font.Design = fontFace == 32 ? .monospaced : .default
        return .system(size: pointSize, weight: weight, design: design)
    }

    var contentLabel: String {
        if !text.isEmpty {
            return text
        }
        if !label.isEmpty {
            return label
        }
        return "Item"
    }

    var horizontalAlignment: Alignment {
        switch layout & 0x03 {
        case Self.layoutRight: return .trailing
        case Self.layoutCenter: return .center
        default: return .leading
        }
    }

    var expandsHorizontally: Bool {
        layout & Self.layoutExpand != 0
    }

    var isInteractiveControl: Bool {
        switch type {
        case .hyperlinkImage, .buttonImage, .hyperlinkString, .buttonString,
             .textField, .interactiveGauge, .dateField,
             .exclusiveChoice, .multipleChoice, .implicitChoice, .popupChoice,
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
        case 2: return "Back"
        case 3: return "Cancel"
        case 4: return "OK"
        case 5: return "Help"
        case 6: return "Stop"
        case 7: return "Exit"
        case 8: return "Select"
        default: return "Select"
        }
    }

    var menuLabel: String {
        longLabel.isEmpty ? displayLabel : longLabel
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
    let profile: GameProfile
    let showsListTitleInContent: Bool

    var body: some View {
        Text(state.screen?.title ?? "LCDUI")
            .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

#endif
