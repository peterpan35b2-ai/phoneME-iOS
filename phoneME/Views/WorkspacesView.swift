import SwiftUI
#if canImport(UIKit)
import UIKit
#endif

struct WorkspacesView: View {
    @EnvironmentObject private var workspaceStore: WorkspaceStore
    @EnvironmentObject private var runtimeStore: WorkspaceRuntimeStore

    @State private var renameWorkspace: EmulatorWorkspace?
    @State private var renameText = ""
    @State private var deleteWorkspace: EmulatorWorkspace?

    var body: some View {
        Group {
            if workspaceStore.workspaces.isEmpty {
                PhoneMEEmptyStateView(
                    title: "No Workspaces",
                    message: "Create a workspace, then add multiple J2ME screens and control the focused screen with one virtual keyboard.",
                    systemImage: "rectangle.3.group",
                    actionTitle: "Create Workspace",
                    action: createWorkspace
                )
            } else {
                List {
                    Section {
                        ForEach(workspaceStore.workspaces) { workspace in
                            NavigationLink {
                                WorkspaceDetailView(workspaceID: workspace.id)
                            } label: {
                                WorkspaceRow(
                                    workspace: workspace,
                                    runningPanelCount: workspace.panels.reduce(into: 0) {
                                        count,
                                        panel in
                                        if runtimeStore.isRunning(panelID: panel.id) {
                                            count += 1
                                        }
                                    }
                                )
                            }
                            .contextMenu {
                                Button {
                                    beginRename(workspace)
                                } label: {
                                    Label("Rename", systemImage: "pencil")
                                }
                                Button(role: .destructive) {
                                    deleteWorkspace = workspace
                                } label: {
                                    Label("Delete Workspace", systemImage: "trash")
                                }
                            }
                            .swipeActions(edge: .trailing, allowsFullSwipe: false) {
                                Button(role: .destructive) {
                                    deleteWorkspace = workspace
                                } label: {
                                    Label("Delete", systemImage: "trash")
                                }

                                Button {
                                    beginRename(workspace)
                                } label: {
                                    Label("Rename", systemImage: "pencil")
                                }
                                .tint(.blue)
                            }
                        }
                    } footer: {
                        Text("A workspace keeps its screen layout and isolated app data for every panel.")
                    }
                }
                .listStyle(.insetGrouped)
            }
        }
        .navigationTitle("Workspaces")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button(action: createWorkspace) {
                    Label("Create Workspace", systemImage: "plus")
                }
            }
        }
        .alert("Rename Workspace", isPresented: renameAlertBinding) {
            TextField("Workspace name", text: $renameText)
            Button("Cancel", role: .cancel) {}
            Button("Save") {
                guard let renameWorkspace else { return }
                workspaceStore.renameWorkspace(
                    id: renameWorkspace.id,
                    to: renameText
                )
            }
        }
        .confirmationDialog(
            "Delete this workspace?",
            isPresented: deleteDialogBinding,
            titleVisibility: .visible
        ) {
            Button("Delete Workspace & App Data", role: .destructive) {
                deleteSelectedWorkspace(deleteData: true)
            }
            Button("Delete Workspace Only", role: .destructive) {
                deleteSelectedWorkspace(deleteData: false)
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Deleting app data also removes saves and settings isolated inside every screen in this workspace.")
        }
    }

    private var renameAlertBinding: Binding<Bool> {
        Binding(
            get: { renameWorkspace != nil },
            set: { if !$0 { renameWorkspace = nil } }
        )
    }

    private var deleteDialogBinding: Binding<Bool> {
        Binding(
            get: { deleteWorkspace != nil },
            set: { if !$0 { deleteWorkspace = nil } }
        )
    }

    private func createWorkspace() {
        _ = workspaceStore.createWorkspace()
    }

    private func beginRename(_ workspace: EmulatorWorkspace) {
        renameText = workspace.name
        renameWorkspace = workspace
    }

    private func deleteSelectedWorkspace(deleteData: Bool) {
        guard let workspace = deleteWorkspace else { return }
        deleteWorkspace = nil
        runtimeStore.shutdown(workspace: workspace, deleteData: deleteData)
        _ = workspaceStore.deleteWorkspace(id: workspace.id)
    }
}

private struct WorkspaceRow: View {
    let workspace: EmulatorWorkspace
    let runningPanelCount: Int

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "rectangle.3.group")
                .font(.title3)
                .foregroundStyle(.tint)
                .frame(width: 36, height: 36)
                .background(Color.accentColor.opacity(0.12))
                .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))

            VStack(alignment: .leading, spacing: 3) {
                Text(workspace.name)
                    .font(.headline)
                    .lineLimit(1)

                Text(panelSummary)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }

            Spacer(minLength: 8)

            if runningPanelCount > 0 {
                Label("\(runningPanelCount)", systemImage: "play.circle.fill")
                    .font(.subheadline)
                    .foregroundStyle(.green)
                    .labelStyle(.titleAndIcon)
                    .accessibilityLabel(
                        L10n.format("%d running screens", runningPanelCount)
                    )
            }
        }
        .padding(.vertical, 3)
    }

    private var panelSummary: String {
        let count = workspace.panels.count
        return count == 1
            ? L10n.format("%d screen", count)
            : L10n.format("%d screens", count)
    }
}

struct WorkspaceDetailView: View {
    @Environment(\.dismiss) private var dismiss
    @EnvironmentObject private var library: GameLibrary
    @EnvironmentObject private var profiles: GameProfileStore
    @EnvironmentObject private var workspaceStore: WorkspaceStore
    @EnvironmentObject private var runtimeStore: WorkspaceRuntimeStore

    let workspaceID: UUID

    @State private var focusedPanelID: UUID?
    @State private var isEditingLayout = false
    @State private var showsKeyboard = true
    @State private var showGamePicker = false
    @State private var configuringPanelID: UUID?
    @State private var replacingPanelID: UUID?
    @State private var showResetLayoutConfirmation = false
    @State private var panelViewportSize = CGSize.zero
    @State private var workspaceVisibleOriginX: CGFloat = 0
    @State private var errorMessage: String?

    private var workspace: EmulatorWorkspace? {
        workspaceStore.workspace(id: workspaceID)
    }

    private var focusedPanel: EmulatorWorkspacePanel? {
        guard let focusedPanelID else { return nil }
        return workspace?.panels.first { $0.id == focusedPanelID }
    }

    var body: some View {
        Group {
            if let workspace {
                GeometryReader { outerGeometry in
                    let tabBarGap = hiddenTabBarContentHeight
                    let preHiddenTabBarSize = CGSize(
                        width: outerGeometry.size.width,
                        height: max(
                            outerGeometry.size.height - tabBarGap,
                            1
                        )
                    )
                    let keypadHeight = activeKeyboardHeight(
                        for: preHiddenTabBarSize
                    )

                    VStack(spacing: 0) {
                        workspaceCanvas(
                            workspace,
                            bottomScrollStripHeight: tabBarGap
                        )

                        if keypadHeight > 0,
                           let panel = focusedPanel,
                           let session = runtimeStore.session(for: panel.id) {
                            WorkspaceFocusedKeypad(
                                gameTitle: game(for: panel)?.title
                                    ?? L10n.string("Unavailable"),
                                profile: panelProfileBinding(panel.id),
                                session: session
                            )
                            .frame(height: keypadHeight)
                        }
                    }
                    .background(Color.black)
                }
            } else {
                PhoneMEEmptyStateView(
                    title: "Workspace Unavailable",
                    message: "This workspace no longer exists.",
                    systemImage: "exclamationmark.triangle"
                )
            }
        }
        .navigationTitle(workspace?.name ?? L10n.string("Workspace"))
        .phoneMETabBarHidden()
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        .navigationBarBackButtonHidden(true)
#endif
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button {
                    dismiss()
                } label: {
                    Label("Back", systemImage: "chevron.left")
                }
            }

            ToolbarItemGroup(placement: .primaryAction) {
                Button {
                    if hasRunningPanels {
                        stopAllScreens()
                    } else {
                        launchAllPanels()
                    }
                } label: {
                    Label(
                        hasRunningPanels
                            ? "Stop All Screens"
                            : "Run All Screens",
                        systemImage: hasRunningPanels
                            ? "stop.fill"
                            : "play.fill"
                    )
                }
                .disabled(!hasRunningPanels && !canLaunchAnyPanel)

                Button {
                    showsKeyboard.toggle()
                } label: {
                    Label(
                        showsKeyboard ? "Hide Keyboard" : "Show Keyboard",
                        systemImage: "keyboard"
                    )
                }
                .disabled(focusedPanel == nil)

                Button {
                    focusedPanelID = nil
                    isEditingLayout.toggle()
                } label: {
                    Label(
                        isEditingLayout ? "Done" : "Edit Layout",
                        systemImage: isEditingLayout ? "checkmark" : "arrow.up.left.and.arrow.down.right"
                    )
                }

                Button {
                    showGamePicker = true
                } label: {
                    Label("Add Screen", systemImage: "plus")
                }
                .disabled(
                    (workspace?.panels.count ?? 0) >= WorkspaceStore.maximumPanelCount
                )

                Menu {
                    Button {
                        showResetLayoutConfirmation = true
                    } label: {
                        Label("Reset Layout", systemImage: "rectangle.3.group")
                    }
                    .disabled(workspace?.panels.isEmpty != false)

                } label: {
                    Label("More", systemImage: "ellipsis.circle")
                }
            }
        }
        .sheet(isPresented: $showGamePicker) {
            PhoneMENavigationStack {
                WorkspaceGamePicker { game in
                    addPanel(for: game)
                    showGamePicker = false
                }
            }
        }
        .sheet(item: configuringPanelBinding) { panel in
            PhoneMENavigationStack {
                WorkspacePanelSettingsView(
                    gameTitle: game(for: panel)?.title
                        ?? L10n.string("Unavailable"),
                    initialProfile: panel.profile
                ) { profile in
                    apply(profile: profile, to: panel.id)
                    configuringPanelID = nil
                }
            }
        }
        .sheet(isPresented: replacingPanelBinding) {
            PhoneMENavigationStack {
                WorkspaceGamePicker { game in
                    replaceFocusedPanel(with: game)
                    replacingPanelID = nil
                }
            }
        }
        .confirmationDialog(
            "Reset screen layout?",
            isPresented: $showResetLayoutConfirmation,
            titleVisibility: .visible
        ) {
            Button("Reset Layout", role: .destructive) {
                workspaceStore.resetLayout(
                    in: workspaceID,
                    viewportSize: panelViewportSize
                )
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("All screens will return to an evenly arranged layout.")
        }
        .alert("Error", isPresented: errorAlertBinding) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(errorMessage ?? "")
        }
        .onAppear {
            runtimeStore.activateWorkspace(workspaceID)
        }
        .onDisappear {
            runtimeStore.deactivateWorkspace(workspaceID)
        }
    }

    @ViewBuilder
    private func workspaceCanvas(
        _ workspace: EmulatorWorkspace,
        bottomScrollStripHeight: CGFloat
    ) -> some View {
        GeometryReader { geometry in
            let viewportSize = geometry.size
            let availablePanelSize = CGSize(
                width: viewportSize.width,
                height: max(
                    viewportSize.height - bottomScrollStripHeight,
                    1
                )
            )
            let contentWidth = workspaceContentWidth(
                workspace,
                viewportWidth: viewportSize.width
            )

            ScrollView(.horizontal, showsIndicators: isEditingLayout) {
                    ZStack(alignment: .topLeading) {
                        Color.black
                            .contentShape(Rectangle())
                            .onTapGesture {
                                focusedPanelID = nil
                            }

                        if isEditingLayout {
                            WorkspaceGridView()
                        }

                        if workspace.panels.isEmpty {
                            VStack(spacing: 12) {
                                Image(systemName: "rectangle.dashed.badge.plus")
                                    .font(.system(size: 42, weight: .regular))
                                    .foregroundStyle(.secondary)
                                Text("Empty Workspace")
                                    .font(.headline)
                                    .foregroundStyle(.white)
                                Text("Add a screen, choose a J2ME app, then drag and resize it here.")
                                    .font(.subheadline)
                                    .foregroundStyle(.secondary)
                                    .multilineTextAlignment(.center)
                                    .padding(.horizontal, 36)
                                Button("Add Screen") {
                                    showGamePicker = true
                                }
                                .buttonStyle(.borderedProminent)
                            }
                            .frame(
                                width: viewportSize.width,
                                height: availablePanelSize.height
                            )
                        } else {
                            ForEach(workspace.panels) { panel in
                                let isRunning = runtimeStore.isRunning(
                                    panelID: panel.id
                                )
                                WorkspacePanelCard(
                                    panel: panel,
                                    game: game(for: panel),
                                    session: runtimeStore.session(for: panel.id),
                                    canvasSize: availablePanelSize,
                                    visibleBounds: CGRect(
                                        x: workspaceVisibleOriginX,
                                        y: 0,
                                        width: availablePanelSize.width,
                                        height: availablePanelSize.height
                                    ),
                                    isFocused: focusedPanelID == panel.id,
                                    isRunning: isRunning,
                                    isEditingLayout: isEditingLayout,
                                    isZoomed: focusedPanelID == panel.id
                                        && isRunning
                                        && !isEditingLayout,
                                    onFocus: {
                                        focusedPanelID = panel.id
                                    },
                                    onShowOverview: {
                                        focusedPanelID = nil
                                    },
                                    onMove: { frame in
                                        workspaceStore.updatePanelFrame(
                                            id: panel.id,
                                            in: workspaceID,
                                            frame: frame
                                        )
                                    },
                                    onResize: { frame in
                                        workspaceStore.updatePanelFrame(
                                            id: panel.id,
                                            in: workspaceID,
                                            frame: frame
                                        )
                                    },
                                    onConfigure: {
                                        configuringPanelID = panel.id
                                    },
                                    onReplace: {
                                        replacingPanelID = panel.id
                                    },
                                    onRun: {
                                        start(panel)
                                    },
                                    onRestart: {
                                        restart(panel)
                                    },
                                    onStop: {
                                        runtimeStore.shutdown(
                                            panelID: panel.id,
                                            deleteData: false
                                        )
                                    },
                                    onRemove: {
                                        remove(panel)
                                    }
                                )
                                .id(panel.id)
                            }
                        }
                    }
                    .frame(
                        width: contentWidth,
                        height: viewportSize.height,
                        alignment: .topLeading
                    )
                    .contentShape(Rectangle())
                    .background(
                        GeometryReader { contentGeometry in
                            Color.clear.preference(
                                key: WorkspaceScrollOffsetPreferenceKey.self,
                                value: -contentGeometry.frame(
                                    in: .named("workspace-canvas-scroll")
                                ).minX
                            )
                        }
                    )
                }
                .background(Color.black)
                .coordinateSpace(name: "workspace-canvas-scroll")
                .onPreferenceChange(
                    WorkspaceScrollOffsetPreferenceKey.self
                ) { offset in
                    let maximumOffset = max(
                        contentWidth - viewportSize.width,
                        0
                    )
                    let visibleOriginX = min(
                        max(offset, 0),
                        maximumOffset
                    )
                    guard abs(
                        workspaceVisibleOriginX - visibleOriginX
                    ) > 0.5 else {
                        return
                    }
                    workspaceVisibleOriginX = visibleOriginX
                }
                .onAppear {
                    updatePanelViewportSize(availablePanelSize)
                }
                .onChange(of: availablePanelSize) { size in
                    updatePanelViewportSize(size)
                }
        }
    }

    private func workspaceContentWidth(
        _ workspace: EmulatorWorkspace,
        viewportWidth: CGFloat
    ) -> CGFloat {
        let rightEdge = max(
            workspace.panels.map(\.frame.maxX).max() ?? 1,
            1
        )
        let editingBuffer = isEditingLayout ? 1.0 : 0
        return max(
            viewportWidth,
            viewportWidth * CGFloat(rightEdge + editingBuffer)
        )
    }

    private func updatePanelViewportSize(_ size: CGSize) {
        guard size.width > 1, size.height > 1 else { return }
        guard abs(panelViewportSize.width - size.width) > 0.5
                || abs(panelViewportSize.height - size.height) > 0.5 else {
            return
        }
        panelViewportSize = size
    }

    private var hiddenTabBarContentHeight: CGFloat {
#if os(iOS)
        49
#else
        0
#endif
    }

    private var shouldShowKeyboard: Bool {
        guard showsKeyboard,
              !isEditingLayout,
              let panel = focusedPanel else {
            return false
        }
        return panel.profile.showVirtualKeyboard
    }

    private func activeKeyboardHeight(for size: CGSize) -> CGFloat {
        guard shouldShowKeyboard,
              let panel = focusedPanel,
              let session = runtimeStore.session(for: panel.id),
              !session.isPresentingNativeLCDUI else {
            return 0
        }

        if size.width > size.height {
            return min(max(size.height * 0.40, 170), 230)
        }
        return min(max(size.height * 0.36, 230), 320)
    }

    private var configuringPanelBinding: Binding<EmulatorWorkspacePanel?> {
        Binding(
            get: {
                guard let configuringPanelID else { return nil }
                return workspace?.panels.first { $0.id == configuringPanelID }
            },
            set: { value in
                if value == nil {
                    configuringPanelID = nil
                }
            }
        )
    }

    private var replacingPanelBinding: Binding<Bool> {
        Binding(
            get: { replacingPanelID != nil },
            set: { if !$0 { replacingPanelID = nil } }
        )
    }

    private var errorAlertBinding: Binding<Bool> {
        Binding(
            get: { errorMessage != nil },
            set: { if !$0 { errorMessage = nil } }
        )
    }

    private func game(for panel: EmulatorWorkspacePanel) -> Game? {
        library.games.first { $0.id == panel.gameID }
    }

    private func panelProfileBinding(_ panelID: UUID) -> Binding<GameProfile> {
        Binding(
            get: {
                workspaceStore.workspace(id: workspaceID)?
                    .panels.first { $0.id == panelID }?
                    .profile ?? .default
            },
            set: { profile in
                workspaceStore.updatePanelProfile(
                    id: panelID,
                    in: workspaceID,
                    profile: profile
                )
            }
        )
    }

    private var canLaunchAnyPanel: Bool {
        guard let workspace else { return false }
        return workspace.panels.contains {
            game(for: $0) != nil
                && !runtimeStore.isRunning(panelID: $0.id)
        }
    }

    private var hasRunningPanels: Bool {
        guard let workspace else { return false }
        return workspace.panels.contains {
            runtimeStore.isRunning(panelID: $0.id)
        }
    }

    private func launchAllPanels() {
        guard let workspace else { return }
        for panel in workspace.panels
            where game(for: panel) != nil
                && !runtimeStore.isRunning(panelID: panel.id) {
            start(panel)
        }
    }

    private func start(_ panel: EmulatorWorkspacePanel) {
        if runtimeStore.session(for: panel.id) == nil {
            launch(panel)
        } else {
            restart(panel)
        }
    }

    private func launch(_ panel: EmulatorWorkspacePanel) {
        guard let game = game(for: panel) else { return }
        do {
            let jarURL = try library.prepareJarForLaunch(game)
            runtimeStore.launch(
                panel: panel,
                workspaceID: workspaceID,
                game: game,
                jarURL: jarURL,
                artworkURL: library.iconURL(for: game)
            )
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func restart(_ panel: EmulatorWorkspacePanel) {
        guard let latestPanel = workspace?.panels.first(where: {
            $0.id == panel.id
        }), let game = game(for: latestPanel) else {
            return
        }
        do {
            let jarURL = try library.prepareJarForLaunch(game)
            runtimeStore.restart(
                panel: latestPanel,
                workspaceID: workspaceID,
                game: game,
                jarURL: jarURL,
                artworkURL: library.iconURL(for: game)
            )
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    private func addPanel(for game: Game) {
        guard let panel = workspaceStore.addPanel(
            gameID: game.id,
            profile: profiles.profile(for: game),
            to: workspaceID,
            viewportSize: panelViewportSize
        ) else {
            errorMessage = L10n.format(
                "A workspace can contain up to %d screens.",
                WorkspaceStore.maximumPanelCount
            )
            return
        }
        focusedPanelID = panel.id
        launch(panel)
    }

    private func apply(profile: GameProfile, to panelID: UUID) {
        workspaceStore.updatePanelProfile(
            id: panelID,
            in: workspaceID,
            profile: profile
        )
        guard let panel = workspace?.panels.first(where: { $0.id == panelID }) else {
            return
        }
        restart(panel)
    }

    private func replaceFocusedPanel(with game: Game) {
        guard let replacingPanelID else { return }
        runtimeStore.shutdown(
            panelID: replacingPanelID,
            deleteData: true
        ) { [weak workspaceStore, weak runtimeStore] in
            guard let workspaceStore, let runtimeStore else { return }
            workspaceStore.replacePanelGame(
                id: replacingPanelID,
                in: workspaceID,
                gameID: game.id,
                profile: profiles.profile(for: game)
            )
            guard let panel = workspaceStore.workspace(id: workspaceID)?
                .panels.first(where: { $0.id == replacingPanelID }) else {
                return
            }
            do {
                let jarURL = try library.prepareJarForLaunch(game)
                runtimeStore.launch(
                    panel: panel,
                    workspaceID: workspaceID,
                    game: game,
                    jarURL: jarURL,
                    artworkURL: library.iconURL(for: game)
                )
            } catch {
                errorMessage = error.localizedDescription
            }
        }
    }

    private func remove(_ panel: EmulatorWorkspacePanel) {
        runtimeStore.shutdown(panelID: panel.id, deleteData: true)
        _ = workspaceStore.removePanel(
            id: panel.id,
            from: workspaceID,
            viewportSize: panelViewportSize
        )
        if focusedPanelID == panel.id {
            focusedPanelID = nil
        }
    }

    private func stopAllScreens() {
        guard let workspace else { return }
        for panel in workspace.panels {
            runtimeStore.shutdown(panelID: panel.id, deleteData: false)
        }
    }
}

private struct WorkspaceScrollOffsetPreferenceKey: PreferenceKey {
    static let defaultValue: CGFloat = 0

    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = nextValue()
    }
}

private struct WorkspacePanelCard: View {
    let panel: EmulatorWorkspacePanel
    let game: Game?
    let session: EmulatorSession?
    let canvasSize: CGSize
    let visibleBounds: CGRect
    let isFocused: Bool
    let isRunning: Bool
    let isEditingLayout: Bool
    let isZoomed: Bool
    let onFocus: () -> Void
    let onShowOverview: () -> Void
    let onMove: (EmulatorWorkspaceFrame) -> Void
    let onResize: (EmulatorWorkspaceFrame) -> Void
    let onConfigure: () -> Void
    let onReplace: () -> Void
    let onRun: () -> Void
    let onRestart: () -> Void
    let onStop: () -> Void
    let onRemove: () -> Void

    @GestureState private var moveTranslation = CGSize.zero
    @GestureState private var resizeTranslation = CGSize.zero
    @State private var animatedCenter: CGPoint?

    private let titleBarHeight: CGFloat = 36

    var body: some View {
        let layoutRect = panel.frame.rect(in: canvasSize)
        let normalRect = panel.profile.preserveAspectRatio
            ? aspectFittedWindowRect(in: layoutRect)
            : layoutRect
        let presentedRect = isZoomed
            ? focusedRect(baseRect: layoutRect)
            : (isEditingLayout ? layoutRect : normalRect)
        let moveOffset = previewMoveOffset(baseRect: layoutRect)
        let resizeScale = previewResizeScale(baseRect: layoutRect)

        VStack(spacing: 0) {
            titleBar(baseRect: layoutRect)
                .frame(height: titleBarHeight)

            ZStack(alignment: .bottomTrailing) {
                WorkspacePanelSurface(
                    game: game,
                    session: session,
                    isRunning: isRunning,
                    isZoomed: isZoomed,
                    profile: panel.profile,
                    capturesHardwareKeyboard: isFocused && !isEditingLayout
                )
                .equatable()
                .contentShape(Rectangle())
                .simultaneousGesture(
                    TapGesture().onEnded(onFocus),
                    including: isEditingLayout ? .none : .all
                )

                if isEditingLayout {
                    editSurfaceOverlay(baseRect: layoutRect)
                }
            }
        }
        .frame(
            width: max(presentedRect.width, 1),
            height: max(presentedRect.height, titleBarHeight + 1)
        )
        .background(Color.black)
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
        .overlay {
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(
                    isFocused ? Color.accentColor : Color.white.opacity(0.24),
                    lineWidth: isFocused ? 3 : 1
                )
        }
        .shadow(
            color: .black.opacity(
                isZoomed ? 0.55 : (isEditingLayout ? 0.22 : 0.12)
            ),
            radius: isZoomed ? 12 : (isEditingLayout ? 3 : 1),
            y: isZoomed ? 5 : 1
        )
        .scaleEffect(
            x: resizeScale.width,
            y: resizeScale.height,
            anchor: .topLeading
        )
        .position(
            x: animatedCenter?.x ?? presentedRect.midX,
            y: animatedCenter?.y ?? presentedRect.midY
        )
        .offset(isZoomed ? .zero : moveOffset)
        .zIndex(isZoomed ? 20 : (isFocused ? 5 : 0))
        .onAppear {
            animatedCenter = CGPoint(
                x: presentedRect.midX,
                y: presentedRect.midY
            )
        }
        .onChange(of: presentedRect) { newRect in
            let targetCenter = CGPoint(
                x: newRect.midX,
                y: newRect.midY
            )
            guard animatedCenter != targetCenter else { return }

            if isEditingLayout {
                animatedCenter = targetCenter
            } else {
                withAnimation(.spring(response: 0.28, dampingFraction: 0.86)) {
                    animatedCenter = targetCenter
                }
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(game?.title ?? L10n.string("Unavailable screen"))
        .accessibilityValue(isFocused ? L10n.string("Focused") : "")
    }

    private func titleBar(baseRect: CGRect) -> some View {
        HStack(spacing: 6) {
            Button(action: onFocus) {
                HStack(spacing: 6) {
                    if isEditingLayout {
                        Image(systemName: "line.3.horizontal")
                            .accessibilityHidden(true)
                    }

                    Text(game?.title ?? L10n.string("Unavailable"))
                        .font(.caption.weight(.semibold))
                        .lineLimit(1)
                        .foregroundStyle(.white)

                    Spacer(minLength: 4)
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .highPriorityGesture(
                moveGesture(baseRect: baseRect),
                including: isEditingLayout ? .all : .none
            )

            if isZoomed {
                Button(action: onShowOverview) {
                    Image(systemName: "rectangle.grid.2x2")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.white)
                        .frame(width: 28, height: 28)
                        .background(.white.opacity(0.14))
                        .clipShape(Circle())
                        .contentShape(Circle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Show All Screens")
            }

            runStopButton
            panelMenu
        }
        .padding(.leading, 8)
        .padding(.trailing, 5)
        .background(.black.opacity(isEditingLayout ? 0.78 : 0.68))
    }

    private func editSurfaceOverlay(baseRect: CGRect) -> some View {
        ZStack(alignment: .bottomTrailing) {
            Color.black.opacity(0.001)
                .contentShape(Rectangle())
                .onTapGesture(perform: onFocus)
                .highPriorityGesture(moveGesture(baseRect: baseRect))

            Image(systemName: "arrow.up.left.and.arrow.down.right")
                .font(.caption.weight(.bold))
                .foregroundStyle(.white)
                .frame(width: 34, height: 34)
                .background(Color.accentColor)
                .clipShape(Circle())
                .padding(6)
                .contentShape(Circle())
                .highPriorityGesture(resizeGesture(baseRect: baseRect))
                .accessibilityLabel("Resize screen")
        }
    }

    private var panelMenu: some View {
        Menu {
            Button(action: onConfigure) {
                Label("Display Settings", systemImage: "rectangle.and.pencil.and.ellipsis")
            }
            Button(action: onReplace) {
                Label("Replace App", systemImage: "arrow.triangle.2.circlepath")
            }
            Button(action: onRestart) {
                Label("Restart Screen", systemImage: "arrow.clockwise")
            }
            Button(action: onStop) {
                Label("Stop Screen", systemImage: "stop.circle")
            }
            .disabled(!isRunning)
            Divider()
            Button(role: .destructive, action: onRemove) {
                Label("Remove Screen", systemImage: "trash")
            }
        } label: {
            Image(systemName: "ellipsis.circle.fill")
                .font(.body)
                .foregroundStyle(.white)
                .frame(width: 28, height: 28)
                .contentShape(Circle())
        }
        .accessibilityLabel("Screen options")
    }

    private var runStopButton: some View {
        Button(action: isRunning ? onStop : onRun) {
            Image(systemName: isRunning ? "stop.fill" : "play.fill")
                .font(.caption2.weight(.bold))
                .foregroundStyle(.white)
                .frame(width: 28, height: 28)
                .background(.white.opacity(0.14))
                .clipShape(Circle())
                .contentShape(Circle())
        }
        .buttonStyle(.plain)
        .disabled(!isRunning && game == nil)
        .accessibilityLabel(
            isRunning
                ? L10n.string("Stop Screen")
                : L10n.string("Run Screen")
        )
    }

    private var contentAspectRatio: CGFloat {
        var width = CGFloat(max(panel.profile.screenWidth, 1))
        var height = CGFloat(max(panel.profile.screenHeight, 1))
        let orientation = panel.profile.lockedOrientation
            ?? panel.profile.orientation
        if orientation == .portrait, width > height {
            swap(&width, &height)
        } else if orientation == .landscape, height > width {
            swap(&width, &height)
        }
        return min(max(width / max(height, 1), 0.25), 4)
    }

    private func aspectFittedWindowRect(in bounds: CGRect) -> CGRect {
        guard bounds.width > 0, bounds.height > titleBarHeight else {
            return bounds
        }

        let aspectRatio = contentAspectRatio
        let maximumContentHeight = max(bounds.height - titleBarHeight, 1)
        let contentSize: CGSize
        if bounds.width / maximumContentHeight > aspectRatio {
            contentSize = CGSize(
                width: maximumContentHeight * aspectRatio,
                height: maximumContentHeight
            )
        } else {
            contentSize = CGSize(
                width: bounds.width,
                height: bounds.width / aspectRatio
            )
        }
        let windowSize = CGSize(
            width: contentSize.width,
            height: contentSize.height + titleBarHeight
        )
        return CGRect(
            x: bounds.midX - windowSize.width / 2,
            y: bounds.midY - windowSize.height / 2,
            width: windowSize.width,
            height: windowSize.height
        )
    }

    private func focusedRect(baseRect: CGRect) -> CGRect {
        guard isZoomed,
              !isEditingLayout,
              canvasSize.width > 0,
              canvasSize.height > 0 else {
            return baseRect
        }

        let margin = min(
            max(min(canvasSize.width, canvasSize.height) * 0.018, 4),
            10
        )
        let availableRect = CGRect(
            x: visibleBounds.minX + margin,
            y: visibleBounds.minY + margin,
            width: max(visibleBounds.width - margin * 2, 1),
            height: max(visibleBounds.height - margin * 2, 1)
        )
        let targetRect = panel.profile.preserveAspectRatio
            ? aspectFittedWindowRect(in: availableRect)
            : availableRect
        let targetSize = targetRect.size
        let minimumX = availableRect.minX
        let maximumX = max(
            availableRect.maxX - targetSize.width,
            minimumX
        )
        let minimumY = availableRect.minY
        let maximumY = max(
            availableRect.maxY - targetSize.height,
            minimumY
        )
        let x = min(
            max(baseRect.midX - targetSize.width / 2, minimumX),
            maximumX
        )
        let y = min(
            max(baseRect.midY - targetSize.height / 2, minimumY),
            maximumY
        )

        return CGRect(origin: CGPoint(x: x, y: y), size: targetSize)
    }

    private func previewMoveOffset(baseRect: CGRect) -> CGSize {
        guard moveTranslation != .zero else { return .zero }
        let rect = movedRect(
            baseRect,
            translation: moveTranslation,
            snapped: false
        )
        return CGSize(
            width: rect.minX - baseRect.minX,
            height: rect.minY - baseRect.minY
        )
    }

    private func previewResizeScale(baseRect: CGRect) -> CGSize {
        guard resizeTranslation != .zero,
              baseRect.width > 0,
              baseRect.height > 0 else {
            return CGSize(width: 1, height: 1)
        }
        let rect = resizedRect(
            baseRect,
            translation: resizeTranslation,
            snapped: false
        )
        return CGSize(
            width: rect.width / baseRect.width,
            height: rect.height / baseRect.height
        )
    }

    private func moveGesture(baseRect: CGRect) -> some Gesture {
        DragGesture(minimumDistance: 4, coordinateSpace: .global)
            .updating($moveTranslation) { value, state, _ in
                state = value.translation
            }
            .onEnded { value in
                let rect = movedRect(
                    baseRect,
                    translation: value.translation,
                    snapped: true
                )
                onMove(.from(rect: rect, in: canvasSize))
            }
    }

    private func resizeGesture(baseRect: CGRect) -> some Gesture {
        DragGesture(minimumDistance: 2, coordinateSpace: .global)
            .updating($resizeTranslation) { value, state, _ in
                state = value.translation
            }
            .onEnded { value in
                let rect = resizedRect(
                    baseRect,
                    translation: value.translation,
                    snapped: true
                )
                onResize(.from(rect: rect, in: canvasSize))
            }
    }

    private func movedRect(
        _ baseRect: CGRect,
        translation: CGSize,
        snapped: Bool
    ) -> CGRect {
        let step: CGFloat = snapped ? 8 : 1
        let x = snap(baseRect.minX + translation.width, step: step)
        let y = snap(baseRect.minY + translation.height, step: step)
        let maximumX = max(
            canvasSize.width
                * CGFloat(EmulatorWorkspaceFrame.maximumHorizontalExtent)
                - baseRect.width,
            0
        )
        return CGRect(
            x: min(max(x, 0), maximumX),
            y: min(max(y, 0), max(canvasSize.height - baseRect.height, 0)),
            width: baseRect.width,
            height: baseRect.height
        )
    }

    private func resizedRect(
        _ baseRect: CGRect,
        translation: CGSize,
        snapped: Bool
    ) -> CGRect {
        let step: CGFloat = snapped ? 8 : 1
        let availableHeight = max(canvasSize.height - baseRect.minY, 1)
        let maximumWidth = max(
            canvasSize.width
                * CGFloat(EmulatorWorkspaceFrame.maximumWidth),
            1
        )
        let minimumWidth = min(
            max(canvasSize.width * 0.18, 112),
            maximumWidth
        )
        let minimumHeight = min(
            max(canvasSize.height * 0.18, 112),
            availableHeight
        )

        if panel.profile.preserveAspectRatio {
            let aspectRatio = contentAspectRatio
            let rawWidth = max(baseRect.width + translation.width, 1)
            let rawWindowHeight = max(
                baseRect.height + translation.height,
                titleBarHeight + 1
            )
            let rawContentHeight = max(
                rawWindowHeight - titleBarHeight,
                1
            )
            let usesWidth = abs(translation.width)
                >= abs(translation.height) * aspectRatio
            var contentHeight = usesWidth
                ? snap(rawWidth, step: step) / aspectRatio
                : snap(rawContentHeight, step: step)
            let minimumAspectContentHeight = max(
                max(minimumHeight - titleBarHeight, 1),
                minimumWidth / aspectRatio
            )
            let maximumAspectContentHeight = min(
                max(availableHeight - titleBarHeight, 1),
                maximumWidth / aspectRatio
            )
            contentHeight = min(
                max(
                    contentHeight,
                    min(
                        minimumAspectContentHeight,
                        maximumAspectContentHeight
                    )
                ),
                maximumAspectContentHeight
            )
            let width = contentHeight * aspectRatio
            let height = contentHeight + titleBarHeight
            return CGRect(
                x: baseRect.minX,
                y: baseRect.minY,
                width: width,
                height: height
            )
        }

        let width = min(
            max(snap(baseRect.width + translation.width, step: step), minimumWidth),
            maximumWidth
        )
        let height = min(
            max(snap(baseRect.height + translation.height, step: step), minimumHeight),
            availableHeight
        )
        return CGRect(
            x: baseRect.minX,
            y: baseRect.minY,
            width: width,
            height: height
        )
    }

    private func snap(_ value: CGFloat, step: CGFloat) -> CGFloat {
        guard step > 1 else { return value }
        return (value / step).rounded() * step
    }
}

private struct WorkspacePanelSurface: View, Equatable {
    let game: Game?
    let session: EmulatorSession?
    let isRunning: Bool
    let isZoomed: Bool
    let profile: GameProfile
    let capturesHardwareKeyboard: Bool

    static func == (
        lhs: WorkspacePanelSurface,
        rhs: WorkspacePanelSurface
    ) -> Bool {
        lhs.game?.id == rhs.game?.id
            && lhs.session === rhs.session
            && lhs.isRunning == rhs.isRunning
            && lhs.isZoomed == rhs.isZoomed
            && lhs.profile == rhs.profile
            && lhs.capturesHardwareKeyboard == rhs.capturesHardwareKeyboard
    }

    var body: some View {
        Group {
            if game == nil {
                missingGame
            } else if let session, isRunning || sessionHasFailed(session) {
                WorkspaceRunningPanelSurface(
                    session: session,
                    profile: profile,
                    isZoomed: isZoomed,
                    capturesHardwareKeyboard: capturesHardwareKeyboard
                )
            } else {
                stopped
            }
        }
    }

    private func sessionHasFailed(_ session: EmulatorSession) -> Bool {
        if case .failed = session.state {
            return true
        }
        return false
    }

    private var missingGame: some View {
        VStack(spacing: 8) {
            Image(systemName: "questionmark.app.dashed")
                .font(.title2)
            Text("App Unavailable")
                .font(.caption.weight(.semibold))
        }
        .foregroundStyle(.secondary)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.black)
    }

    private var stopped: some View {
        VStack(spacing: 8) {
            Image(systemName: "stop.circle")
                .font(.title2)
            Text("Stopped")
                .font(.caption.weight(.semibold))
        }
        .foregroundStyle(.secondary)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.black)
    }
}

private struct WorkspaceRunningPanelSurface: View {
    @ObservedObject var session: EmulatorSession

    let profile: GameProfile
    let isZoomed: Bool
    let capturesHardwareKeyboard: Bool

    var body: some View {
        Group {
            switch session.state {
            case .idle, .stopped:
                VStack(spacing: 8) {
                    Image(systemName: "stop.circle")
                        .font(.title2)
                    Text("Stopped")
                        .font(.caption.weight(.semibold))
                }
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color.black)

            case .starting, .running, .failed:
                activeSurface
            }
        }
    }

    private var activeSurface: some View {
        ZStack {
            Color.black

            if session.isPresentingNativeLCDUI {
                NativeLCDUIScreenView(
                    imageStore: session.lcdUIImageStore,
                    state: session.lcdUI,
                    showsTitleInContent: true
                )
                .environmentObject(session)
            } else {
                FrameSurface(
                    frameStore: session.frameStore,
                    profile: profile,
                    capturesHardwareKeyboard: capturesHardwareKeyboard,
                    fitsEntireFrame: true,
                    forcesNearestNeighborFit: isZoomed
                )
                .environmentObject(session)
            }

            switch session.state {
            case .starting:
                ProgressView()
                    .tint(.white)
                    .padding(12)
                    .background(.black.opacity(0.55))
                    .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
            case .failed(let message):
                VStack(spacing: 7) {
                    Image(systemName: "exclamationmark.triangle.fill")
                    Text(message)
                        .font(.caption2)
                        .multilineTextAlignment(.center)
                        .lineLimit(4)
                }
                .foregroundStyle(.white)
                .padding(10)
                .background(.red.opacity(0.78))
                .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))
                .padding(8)
            default:
                EmptyView()
            }
        }
    }
}

private struct WorkspaceFocusedKeypad: View {
    let gameTitle: String
    @Binding var profile: GameProfile
    let session: EmulatorSession

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: "scope")
                    .foregroundStyle(.tint)
                Text(L10n.format("Controlling %@", gameTitle))
                    .font(.caption.weight(.semibold))
                    .lineLimit(1)
                Spacer()
            }
            .padding(.horizontal, 12)
            .frame(height: 34)
            .background(.regularMaterial)
            .overlay(alignment: .top) {
                Divider()
            }

            GeometryReader { geometry in
                KeypadView(
                    profile: $profile,
                    editMode: .none,
                    layoutRect: CGRect(
                        x: 6,
                        y: 4,
                        width: max(geometry.size.width - 12, 0),
                        height: max(geometry.size.height - 8, 0)
                    ),
                    displayRect: .null,
                    onKeyActivity: { _ in },
                    onObscuresDisplayChange: { _ in }
                )
                .environmentObject(session)
            }
            .background(Color.workspaceKeyboardBackground)
        }
    }
}

private struct WorkspaceGridView: View {
    var body: some View {
        GeometryReader { geometry in
            Path { path in
                let step: CGFloat = 24
                var x: CGFloat = 0
                while x <= geometry.size.width {
                    path.move(to: CGPoint(x: x, y: 0))
                    path.addLine(to: CGPoint(x: x, y: geometry.size.height))
                    x += step
                }
                var y: CGFloat = 0
                while y <= geometry.size.height {
                    path.move(to: CGPoint(x: 0, y: y))
                    path.addLine(to: CGPoint(x: geometry.size.width, y: y))
                    y += step
                }
            }
            .stroke(Color.white.opacity(0.10), lineWidth: 0.5)
        }
        .background(Color.white.opacity(0.025))
        .allowsHitTesting(false)
    }
}

private struct WorkspaceGamePicker: View {
    @Environment(\.dismiss) private var dismiss
    @EnvironmentObject private var library: GameLibrary

    let onSelect: (Game) -> Void

    @State private var searchText = ""

    private var games: [Game] {
        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !query.isEmpty else {
            return library.games.sorted {
                $0.title.localizedStandardCompare($1.title) == .orderedAscending
            }
        }
        return library.games.filter { game in
            game.title.localizedCaseInsensitiveContains(query)
                || game.vendor.localizedCaseInsensitiveContains(query)
                || game.fileName.localizedCaseInsensitiveContains(query)
        }.sorted {
            $0.title.localizedStandardCompare($1.title) == .orderedAscending
        }
    }

    var body: some View {
        Group {
            if library.games.isEmpty {
                PhoneMEEmptyStateView(
                    title: "No Apps",
                    message: "Import a J2ME JAR file in Library before adding a screen.",
                    systemImage: "square.stack.3d.up.slash"
                )
            } else {
                List(games) { game in
                    Button {
                        onSelect(game)
                    } label: {
                        WorkspaceGamePickerRow(
                            game: game,
                            iconURL: library.iconURL(for: game)
                        )
                    }
                    .buttonStyle(.plain)
                }
                .listStyle(.insetGrouped)
                .searchable(text: $searchText, prompt: "Search Apps")
            }
        }
        .navigationTitle("Choose App")
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
#endif
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("Cancel") {
                    dismiss()
                }
            }
        }
    }
}

private struct WorkspaceGamePickerRow: View {
    let game: Game
    let iconURL: URL?

    var body: some View {
        HStack(spacing: 12) {
            WorkspaceGameIcon(iconURL: iconURL)
                .frame(width: 36, height: 36)

            VStack(alignment: .leading, spacing: 2) {
                Text(game.title)
                    .font(.headline)
                    .foregroundStyle(.primary)
                    .lineLimit(1)
                Text(game.vendor.isEmpty ? game.fileName : game.vendor)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }

            Spacer()
        }
        .contentShape(Rectangle())
    }
}

private struct WorkspaceGameIcon: View {
    let iconURL: URL?

    var body: some View {
        Group {
            if let image {
                image
                    .resizable()
                    .interpolation(.none)
                    .scaledToFit()
            } else {
                Image(systemName: "gamecontroller.fill")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background(Color.secondary.opacity(0.12))
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }

    private var image: Image? {
        guard let iconURL,
              let data = try? Data(contentsOf: iconURL) else {
            return nil
        }
#if canImport(UIKit)
        guard let image = UIImage(data: data) else { return nil }
        return Image(uiImage: image)
#else
        return nil
#endif
    }
}

private struct WorkspacePanelSettingsView: View {
    @Environment(\.dismiss) private var dismiss

    let gameTitle: String
    let onApply: (GameProfile) -> Void

    @State private var profile: GameProfile
    @State private var selectedPresetID: String

    init(
        gameTitle: String,
        initialProfile: GameProfile,
        onApply: @escaping (GameProfile) -> Void
    ) {
        self.gameTitle = gameTitle
        self.onApply = onApply
        let profile = initialProfile.normalized()
        _profile = State(initialValue: profile)
        _selectedPresetID = State(
            initialValue: WorkspaceResolutionPreset.id(
                width: profile.screenWidth,
                height: profile.screenHeight
            ) ?? "custom"
        )
    }

    var body: some View {
        Form {
            Section {
                Picker("Resolution", selection: $selectedPresetID) {
                    ForEach(WorkspaceResolutionPreset.all) { preset in
                        Text(preset.title)
                            .tag(preset.id)
                    }
                    Text("Custom")
                        .tag("custom")
                }
                .onChange(of: selectedPresetID) { presetID in
                    guard let preset = WorkspaceResolutionPreset.all.first(where: {
                        $0.id == presetID
                    }) else {
                        return
                    }
                    profile.screenWidth = preset.width
                    profile.screenHeight = preset.height
                }

                HStack {
                    TextField("Width", value: $profile.screenWidth, formatter: integerFormatter)
#if canImport(UIKit)
                        .keyboardType(.numberPad)
#endif
                    Text("×")
                        .foregroundStyle(.secondary)
                    TextField("Height", value: $profile.screenHeight, formatter: integerFormatter)
#if canImport(UIKit)
                        .keyboardType(.numberPad)
#endif
                }
                .onChange(of: profile.screenWidth) { _ in
                    updatePresetSelection()
                }
                .onChange(of: profile.screenHeight) { _ in
                    updatePresetSelection()
                }

                Toggle("Preserve Aspect Ratio", isOn: $profile.preserveAspectRatio)

                Picker("Scale", selection: $profile.scaleType) {
                    ForEach(GameProfile.ScaleType.allCases) { type in
                        Text(type.title)
                            .tag(type)
                    }
                }
            } header: {
                Text("Display")
            }

            Section("Performance") {
                Toggle(
                    "Override FPS",
                    isOn: Binding(
                        get: { profile.isFrameRateOverrideEnabled },
                        set: { profile.isFrameRateOverrideEnabled = $0 }
                    )
                )
                if profile.isFrameRateOverrideEnabled {
                    Stepper(
                        L10n.format("Frame rate: %d FPS", profile.frameRateLimit),
                        value: $profile.frameRateLimit,
                        in: 1...GameProfile.maximumFrameRate
                    )
                }
                Toggle("Show FPS", isOn: $profile.showFPS)
                Stepper(
                    L10n.format(
                        "Java heap: %d MiB",
                        profile.effectiveHeapSizeMegabytes
                    ),
                    value: Binding(
                        get: { profile.effectiveHeapSizeMegabytes },
                        set: { profile.effectiveHeapSizeMegabytes = $0 }
                    ),
                    in: GameProfile.minimumHeapSizeMegabytes...GameProfile.maximumHeapSizeMegabytes
                )
                Toggle("Image Filtering", isOn: $profile.filtering)
            }

            Section("Input") {
                Toggle("Show Virtual Keyboard", isOn: $profile.showVirtualKeyboard)
                Picker("Keylayout", selection: $profile.virtualKeyboardType) {
                    ForEach(
                        GameProfile.VirtualKeyboardType.allCases.filter {
                            $0 != .custom
                        }
                    ) { type in
                        Text(type.title)
                            .tag(type)
                    }
                }
            }
        }
        .navigationTitle(gameTitle)
#if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
#endif
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("Cancel") {
                    dismiss()
                }
            }
            ToolbarItem(placement: .confirmationAction) {
                Button("Apply") {
                    onApply(profile.normalized())
                    dismiss()
                }
            }
        }
    }

    private var integerFormatter: NumberFormatter {
        let formatter = NumberFormatter()
        formatter.numberStyle = .none
        formatter.minimum = 1
        formatter.maximum = 2048
        return formatter
    }

    private func updatePresetSelection() {
        selectedPresetID = WorkspaceResolutionPreset.id(
            width: profile.screenWidth,
            height: profile.screenHeight
        ) ?? "custom"
    }
}

private struct WorkspaceResolutionPreset: Identifiable {
    let width: Int
    let height: Int

    var id: String { "\(width)x\(height)" }
    var title: String { "\(width) × \(height)" }

    static let all = GameProfile.screenPresets.map {
        WorkspaceResolutionPreset(width: $0.width, height: $0.height)
    }

    static func id(width: Int, height: Int) -> String? {
        all.first { $0.width == width && $0.height == height }?.id
    }
}

private extension Color {
    static var workspaceKeyboardBackground: Color {
#if canImport(UIKit)
        Color(uiColor: .secondarySystemBackground)
#else
        Color.gray.opacity(0.2)
#endif
    }
}
