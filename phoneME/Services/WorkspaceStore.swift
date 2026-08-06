import Combine
import CoreGraphics
import Foundation

@MainActor
final class WorkspaceStore: ObservableObject {
    static let maximumPanelCount = 12

    @Published private(set) var workspaces: [EmulatorWorkspace] = []

    private let fileManager: FileManager
    private let storage: PhoneMEStorageController
    private let fpsOverrideOffMigrationKey = "phoneME.workspaces.fpsOverrideOffV9"

    private var metadataURL: URL {
        storage.rootURL.appendingPathComponent(
            "workspaces.json",
            isDirectory: false
        )
    }

    init(
        storage: PhoneMEStorageController,
        fileManager: FileManager = .default
    ) {
        self.storage = storage
        self.fileManager = fileManager
        reloadFromStorage()
    }

    func reloadFromStorage() {
        try? fileManager.createDirectory(
            at: storage.rootURL,
            withIntermediateDirectories: true
        )
        load()
    }

    func workspace(id: UUID) -> EmulatorWorkspace? {
        workspaces.first { $0.id == id }
    }

    @discardableResult
    func createWorkspace(named requestedName: String? = nil) -> EmulatorWorkspace {
        let trimmedName = requestedName?.trimmingCharacters(
            in: .whitespacesAndNewlines
        ) ?? ""
        let workspace = EmulatorWorkspace(
            name: trimmedName.isEmpty ? nextWorkspaceName() : trimmedName
        )
        workspaces.append(workspace)
        sortWorkspaces()
        persist()
        return workspace
    }

    func renameWorkspace(id: UUID, to requestedName: String) {
        let name = requestedName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty,
              let index = workspaces.firstIndex(where: { $0.id == id }) else {
            return
        }
        workspaces[index].name = name
        touchWorkspace(at: index)
        sortWorkspaces()
        persist()
    }

    @discardableResult
    func deleteWorkspace(id: UUID) -> EmulatorWorkspace? {
        guard let index = workspaces.firstIndex(where: { $0.id == id }) else {
            return nil
        }
        let removed = workspaces.remove(at: index)
        persist()
        return removed
    }

    @discardableResult
    func addPanel(
        gameID: UUID,
        profile: GameProfile,
        to workspaceID: UUID,
        viewportSize: CGSize
    ) -> EmulatorWorkspacePanel? {
        guard let workspaceIndex = workspaces.firstIndex(where: {
            $0.id == workspaceID
        }), workspaces[workspaceIndex].panels.count < Self.maximumPanelCount else {
            return nil
        }

        let viewportSize = resolvedViewportSize(viewportSize)
        let existingPanels = workspaces[workspaceIndex].panels
        let usesAutomaticLayout = isUsingAutomaticLayout(
            existingPanels,
            viewportSize: viewportSize
        )
        var panel = EmulatorWorkspacePanel(
            gameID: gameID,
            frame: .default,
            profile: profile
        )

        if usesAutomaticLayout {
            var arrangedPanels = existingPanels
            arrangedPanels.append(panel)
            applySmartLayout(
                to: &arrangedPanels,
                viewportSize: viewportSize
            )
            panel = arrangedPanels.removeLast()
            workspaces[workspaceIndex].panels = arrangedPanels
        } else {
            panel.frame = nextAvailablePanelFrame(
                for: panel.profile,
                beside: existingPanels,
                viewportSize: viewportSize
            )
        }

        workspaces[workspaceIndex].panels.append(panel)
        touchWorkspace(at: workspaceIndex)
        persist()
        return panel
    }

    @discardableResult
    func removePanel(
        id panelID: UUID,
        from workspaceID: UUID,
        viewportSize: CGSize
    ) -> EmulatorWorkspacePanel? {
        guard let workspaceIndex = workspaces.firstIndex(where: {
            $0.id == workspaceID
        }), let panelIndex = workspaces[workspaceIndex].panels.firstIndex(where: {
            $0.id == panelID
        }) else {
            return nil
        }
        let viewportSize = resolvedViewportSize(viewportSize)
        let usesAutomaticLayout = isUsingAutomaticLayout(
            workspaces[workspaceIndex].panels,
            viewportSize: viewportSize
        )
        let panel = workspaces[workspaceIndex].panels.remove(at: panelIndex)
        if usesAutomaticLayout {
            applySmartLayout(
                to: &workspaces[workspaceIndex].panels,
                viewportSize: viewportSize
            )
        }
        touchWorkspace(at: workspaceIndex)
        persist()
        return panel
    }

    func updatePanelFrame(
        id panelID: UUID,
        in workspaceID: UUID,
        frame: EmulatorWorkspaceFrame
    ) {
        guard let workspaceIndex = workspaces.firstIndex(where: {
            $0.id == workspaceID
        }), let panelIndex = workspaces[workspaceIndex].panels.firstIndex(where: {
            $0.id == panelID
        }) else {
            return
        }

        workspaces[workspaceIndex].panels[panelIndex].frame = frame.normalized()
        resolveCollisions(
            in: &workspaces[workspaceIndex].panels,
            keeping: panelID
        )
        touchWorkspace(at: workspaceIndex)
        persist()
    }

    func updatePanelProfile(
        id panelID: UUID,
        in workspaceID: UUID,
        profile: GameProfile
    ) {
        updatePanel(id: panelID, in: workspaceID) { panel in
            panel.profile = profile.normalized()
        }
    }

    func replacePanelGame(
        id panelID: UUID,
        in workspaceID: UUID,
        gameID: UUID,
        profile: GameProfile
    ) {
        updatePanel(id: panelID, in: workspaceID) { panel in
            panel.gameID = gameID
            panel.profile = profile.normalized()
        }
    }

    func resetLayout(
        in workspaceID: UUID,
        viewportSize: CGSize
    ) {
        guard let workspaceIndex = workspaces.firstIndex(where: {
            $0.id == workspaceID
        }) else {
            return
        }
        applySmartLayout(
            to: &workspaces[workspaceIndex].panels,
            viewportSize: resolvedViewportSize(viewportSize)
        )
        touchWorkspace(at: workspaceIndex)
        persist()
    }

    private func updatePanel(
        id panelID: UUID,
        in workspaceID: UUID,
        update: (inout EmulatorWorkspacePanel) -> Void
    ) {
        guard let workspaceIndex = workspaces.firstIndex(where: {
            $0.id == workspaceID
        }), let panelIndex = workspaces[workspaceIndex].panels.firstIndex(where: {
            $0.id == panelID
        }) else {
            return
        }
        update(&workspaces[workspaceIndex].panels[panelIndex])
        touchWorkspace(at: workspaceIndex)
        persist()
    }

    private struct SmartLayoutCandidate {
        let frames: [EmulatorWorkspaceFrame]
        let score: Double
    }

    private func resolvedViewportSize(_ size: CGSize) -> CGSize {
        guard size.width > 1, size.height > 1 else {
            return CGSize(width: 390, height: 700)
        }
        return size
    }

    private func panelResolution(
        _ panel: EmulatorWorkspacePanel
    ) -> (width: Double, height: Double) {
        var width = Double(max(panel.profile.screenWidth, 1))
        var height = Double(max(panel.profile.screenHeight, 1))
        let orientation = panel.profile.lockedOrientation
            ?? panel.profile.orientation

        switch orientation {
        case .portrait where width > height:
            swap(&width, &height)
        case .landscape where height > width:
            swap(&width, &height)
        default:
            break
        }
        return (width, height)
    }

    private func smartPanelFrames(
        for panels: [EmulatorWorkspacePanel],
        viewportSize: CGSize
    ) -> [EmulatorWorkspaceFrame] {
        guard !panels.isEmpty else { return [] }

        let viewportSize = resolvedViewportSize(viewportSize)
        let viewportAspect = max(
            Double(viewportSize.width / viewportSize.height),
            0.1
        )
        let resolutions = panels.map(panelResolution)
        if panels.count == 1, let resolution = resolutions.first {
            let aspectRatio = min(
                max(resolution.width / max(resolution.height, 1), 0.25),
                4
            )
            var height = 1.0
            var width = aspectRatio / viewportAspect
            if width > 1 {
                height /= width
                width = 1
            }
            return [
                EmulatorWorkspaceFrame(
                    x: max((1 - width) / 2, 0),
                    y: max((1 - height) / 2, 0),
                    width: width,
                    height: height
                ).normalized()
            ]
        }

        let maximumArea = max(
            resolutions.map { $0.width * $0.height }.max() ?? 1,
            1
        )
        let resolutionScales = resolutions.map { resolution in
            let relativeArea = max(
                resolution.width * resolution.height / maximumArea,
                0.001
            )
            return min(max(pow(relativeArea, 0.10), 0.86), 1)
        }
        let maximumRows = min(
            panels.count,
            min(
                4,
                Int(floor(1 / EmulatorWorkspaceFrame.minimumHeight))
            )
        )

        var bestCandidate: SmartLayoutCandidate?
        for rowCount in 1...max(maximumRows, 1) {
            let candidate = smartLayoutCandidate(
                panels: panels,
                resolutions: resolutions,
                resolutionScales: resolutionScales,
                viewportAspect: viewportAspect,
                rowCount: rowCount
            )
            if bestCandidate == nil || candidate.score < bestCandidate!.score {
                bestCandidate = candidate
            }
        }
        return bestCandidate?.frames ?? panels.map { _ in .default }
    }

    private func smartLayoutCandidate(
        panels: [EmulatorWorkspacePanel],
        resolutions: [(width: Double, height: Double)],
        resolutionScales: [Double],
        viewportAspect: Double,
        rowCount: Int
    ) -> SmartLayoutCandidate {
        let gap = 0.008
        let laneHeight = max(
            (1 - gap * Double(max(rowCount - 1, 0))) / Double(rowCount),
            EmulatorWorkspaceFrame.minimumHeight
        )
        var rowEnds = Array(repeating: 0.0, count: rowCount)
        var rowAssignments = Array(repeating: 0, count: panels.count)
        var frames = Array(repeating: EmulatorWorkspaceFrame.default, count: panels.count)

        for panelIndex in panels.indices {
            let resolution = resolutions[panelIndex]
            let aspectRatio = min(
                max(resolution.width / max(resolution.height, 1), 0.25),
                4
            )
            var height = min(
                max(
                    laneHeight * resolutionScales[panelIndex],
                    EmulatorWorkspaceFrame.minimumHeight
                ),
                laneHeight
            )
            var width = height * aspectRatio / viewportAspect

            if width < EmulatorWorkspaceFrame.minimumWidth {
                width = EmulatorWorkspaceFrame.minimumWidth
                height = min(width * viewportAspect / aspectRatio, laneHeight)
            } else if width > EmulatorWorkspaceFrame.maximumWidth {
                width = EmulatorWorkspaceFrame.maximumWidth
                height = min(width * viewportAspect / aspectRatio, laneHeight)
            }

            let row = rowEnds.indices.min { lhs, rhs in
                if rowEnds[lhs] != rowEnds[rhs] {
                    return rowEnds[lhs] < rowEnds[rhs]
                }
                return lhs < rhs
            } ?? 0
            let laneY = Double(row) * (laneHeight + gap)
            let frame = EmulatorWorkspaceFrame(
                x: rowEnds[row],
                y: laneY + max((laneHeight - height) / 2, 0),
                width: width,
                height: height
            ).normalized()
            frames[panelIndex] = frame
            rowAssignments[panelIndex] = row
            rowEnds[row] = frame.maxX + gap
        }

        let rowWidths = rowEnds.map { max($0 - gap, 0) }
        for row in 0..<rowCount {
            let centeringOffset = max((1 - rowWidths[row]) / 2, 0)
            guard centeringOffset > 0 else { continue }
            for panelIndex in panels.indices where rowAssignments[panelIndex] == row {
                frames[panelIndex].x += centeringOffset
                frames[panelIndex] = frames[panelIndex].normalized()
            }
        }

        let contentWidth = max(rowWidths.max() ?? 1, 0)
        let shortestRow = rowWidths.min() ?? contentWidth
        let overflowPenalty = max(contentWidth - 1, 0)
        let underfillPenalty = max(1 - contentWidth, 0) * 0.12
        let rowPenalty = Double(max(rowCount - 1, 0)) * 0.30
        let imbalancePenalty = max(contentWidth - shortestRow, 0) * 0.04

        return SmartLayoutCandidate(
            frames: frames,
            score: overflowPenalty
                + underfillPenalty
                + rowPenalty
                + imbalancePenalty
        )
    }

    private func applySmartLayout(
        to panels: inout [EmulatorWorkspacePanel],
        viewportSize: CGSize
    ) {
        let frames = smartPanelFrames(
            for: panels,
            viewportSize: viewportSize
        )
        for panelIndex in panels.indices {
            panels[panelIndex].frame = frames[panelIndex]
        }
    }

    private func legacyPackedPanelFrames(
        count: Int
    ) -> [EmulatorWorkspaceFrame] {
        guard count > 0 else { return [] }
        if count == 1 {
            return [EmulatorWorkspaceFrame(x: 0, y: 0, width: 1, height: 1)]
        }
        if count == 2 {
            return (0..<2).map { index in
                EmulatorWorkspaceFrame(
                    x: Double(index) * 0.5,
                    y: 0,
                    width: 0.5,
                    height: 1
                )
            }
        }
        return (0..<count).map { index in
            let column = index / 2
            let row = index % 2
            return EmulatorWorkspaceFrame(
                x: Double(column) * 0.5,
                y: Double(row) * 0.5,
                width: 0.5,
                height: 0.5
            )
        }
    }

    private func isUsingAutomaticLayout(
        _ panels: [EmulatorWorkspacePanel],
        viewportSize: CGSize
    ) -> Bool {
        guard !panels.isEmpty else { return true }
        return isUsingSmartLayout(panels, viewportSize: viewportSize)
            || isUsingLegacyPackedLayout(panels)
            || isUsingLegacyAutomaticLayout(panels)
    }

    private func isUsingSmartLayout(
        _ panels: [EmulatorWorkspacePanel],
        viewportSize: CGSize
    ) -> Bool {
        let expectedFrames = smartPanelFrames(
            for: panels,
            viewportSize: viewportSize
        )
        return zip(panels, expectedFrames).allSatisfy { pair in
            framesAreApproximatelyEqual(pair.0.frame, pair.1)
        }
    }

    private func isUsingLegacyPackedLayout(
        _ panels: [EmulatorWorkspacePanel]
    ) -> Bool {
        let expectedFrames = legacyPackedPanelFrames(count: panels.count)
        return zip(panels, expectedFrames).allSatisfy { pair in
            framesAreApproximatelyEqual(pair.0.frame, pair.1)
        }
    }

    private func isUsingLegacyAutomaticLayout(
        _ panels: [EmulatorWorkspacePanel]
    ) -> Bool {
        guard !panels.isEmpty else { return false }
        return panels.enumerated().allSatisfy { pair in
            framesAreApproximatelyEqual(
                pair.element.frame,
                legacyPanelFrame(for: pair.offset)
            )
        }
    }

    private func legacyPanelFrame(for index: Int) -> EmulatorWorkspaceFrame {
        let column = index % 2
        let row = (index / 2) % 2
        let page = index / 4
        let cascade = min(Double(page) * 0.025, 0.10)
        return EmulatorWorkspaceFrame(
            x: 0.035 + Double(column) * 0.49 + cascade,
            y: 0.035 + Double(row) * 0.47 + cascade,
            width: 0.44,
            height: 0.42
        ).normalized()
    }

    private func framesAreApproximatelyEqual(
        _ lhs: EmulatorWorkspaceFrame,
        _ rhs: EmulatorWorkspaceFrame
    ) -> Bool {
        let tolerance = 0.000_1
        return abs(lhs.x - rhs.x) <= tolerance
            && abs(lhs.y - rhs.y) <= tolerance
            && abs(lhs.width - rhs.width) <= tolerance
            && abs(lhs.height - rhs.height) <= tolerance
    }

    private func nextAvailablePanelFrame(
        for profile: GameProfile,
        beside panels: [EmulatorWorkspacePanel],
        viewportSize: CGSize
    ) -> EmulatorWorkspaceFrame {
        let viewportSize = resolvedViewportSize(viewportSize)
        let viewportAspect = max(
            Double(viewportSize.width / viewportSize.height),
            0.1
        )
        var width = Double(max(profile.screenWidth, 1))
        var height = Double(max(profile.screenHeight, 1))
        let orientation = profile.lockedOrientation ?? profile.orientation
        if orientation == .portrait, width > height {
            swap(&width, &height)
        } else if orientation == .landscape, height > width {
            swap(&width, &height)
        }

        let aspectRatio = min(max(width / max(height, 1), 0.25), 4)
        var normalizedHeight = 0.48
        var normalizedWidth = normalizedHeight * aspectRatio / viewportAspect
        if normalizedWidth < EmulatorWorkspaceFrame.minimumWidth {
            normalizedWidth = EmulatorWorkspaceFrame.minimumWidth
            normalizedHeight = normalizedWidth * viewportAspect / aspectRatio
        }
        normalizedHeight = min(
            max(normalizedHeight, EmulatorWorkspaceFrame.minimumHeight),
            1
        )
        normalizedWidth = min(
            max(normalizedWidth, EmulatorWorkspaceFrame.minimumWidth),
            EmulatorWorkspaceFrame.maximumWidth
        )

        let verticalPositions = [0.0, max(1 - normalizedHeight, 0)]
        let horizontalStep = 0.02
        var x = 0.0
        while x + normalizedWidth <= EmulatorWorkspaceFrame.maximumHorizontalExtent {
            for y in verticalPositions {
                let candidate = EmulatorWorkspaceFrame(
                    x: x,
                    y: y,
                    width: normalizedWidth,
                    height: normalizedHeight
                ).normalized()
                if panels.allSatisfy({ !framesOverlap(candidate, $0.frame) }) {
                    return candidate
                }
            }
            x += horizontalStep
        }

        let rightEdge = panels.map(\.frame.maxX).max() ?? 0
        return EmulatorWorkspaceFrame(
            x: rightEdge + 0.008,
            y: 0,
            width: normalizedWidth,
            height: normalizedHeight
        ).normalized()
    }

    private func resolveCollisions(
        in panels: inout [EmulatorWorkspacePanel],
        keeping fixedPanelID: UUID
    ) {
        guard panels.contains(where: { $0.id == fixedPanelID }) else {
            return
        }

        var queue = [fixedPanelID]
        var queueIndex = 0
        var remainingMoves = max(panels.count * panels.count * 2, 1)

        while queueIndex < queue.count, remainingMoves > 0 {
            let sourceID = queue[queueIndex]
            queueIndex += 1
            guard let sourceIndex = panels.firstIndex(where: {
                $0.id == sourceID
            }) else {
                continue
            }
            let sourceFrame = panels[sourceIndex].frame
            let candidateIndices = panels.indices
                .filter { panels[$0].id != sourceID }
                .sorted { lhs, rhs in
                    let lhsFrame = panels[lhs].frame
                    let rhsFrame = panels[rhs].frame
                    if lhsFrame.x != rhsFrame.x {
                        return lhsFrame.x < rhsFrame.x
                    }
                    return lhsFrame.y < rhsFrame.y
                }

            for targetIndex in candidateIndices {
                guard panels[targetIndex].id != fixedPanelID,
                      framesOverlap(sourceFrame, panels[targetIndex].frame) else {
                    continue
                }

                var movedFrame = panels[targetIndex].frame
                movedFrame.x = sourceFrame.maxX
                movedFrame = movedFrame.normalized()
                guard movedFrame != panels[targetIndex].frame else {
                    continue
                }

                panels[targetIndex].frame = movedFrame
                queue.append(panels[targetIndex].id)
                remainingMoves -= 1
                if remainingMoves == 0 {
                    break
                }
            }
        }
    }

    private func framesOverlap(
        _ lhs: EmulatorWorkspaceFrame,
        _ rhs: EmulatorWorkspaceFrame
    ) -> Bool {
        let epsilon = 0.000_001
        let overlapsHorizontally = lhs.x < rhs.maxX - epsilon
            && lhs.maxX > rhs.x + epsilon
        let overlapsVertically = lhs.y < rhs.maxY - epsilon
            && lhs.maxY > rhs.y + epsilon
        return overlapsHorizontally && overlapsVertically
    }

    private func nextWorkspaceName() -> String {
        let existingNames = Set(workspaces.map { $0.name.lowercased() })
        var index = workspaces.count + 1
        while existingNames.contains(
            L10n.format("Workspace %d", index).lowercased()
        ) {
            index += 1
        }
        return L10n.format("Workspace %d", index)
    }

    private func touchWorkspace(at index: Int) {
        workspaces[index].modifiedAt = Date()
    }

    private func sortWorkspaces() {
        workspaces.sort { lhs, rhs in
            if lhs.modifiedAt != rhs.modifiedAt {
                return lhs.modifiedAt > rhs.modifiedAt
            }
            return lhs.name.localizedStandardCompare(rhs.name) == .orderedAscending
        }
    }

    private func load() {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        guard let data = try? Data(contentsOf: metadataURL),
              let decoded = try? decoder.decode(
                [EmulatorWorkspace].self,
                from: data
              ) else {
            workspaces = []
            UserDefaults.standard.set(true, forKey: fpsOverrideOffMigrationKey)
            return
        }
        let defaults = UserDefaults.standard
        let shouldDisableInheritedFPSOverride = !defaults.bool(
            forKey: fpsOverrideOffMigrationKey
        )
        var migratedLegacyLayout = false
        var migratedLegacyFramePacing = false
        var migratedInheritedFPSOverride = false
        workspaces = decoded.map { workspace in
            var workspace = workspace
            workspace.panels = workspace.panels.map { panel in
                var panel = panel
                panel.frame = panel.frame.normalized()
                if panel.profile.framePacingMode == .cap {
                    migratedLegacyFramePacing = true
                }
                panel.profile = panel.profile.normalized()
                if shouldDisableInheritedFPSOverride,
                   panel.profile.framePacingMode == .overrideGameLoop,
                   panel.profile.frameRateLimit == GameProfile.defaultFrameRate {
                    panel.profile.framePacingMode = .native
                    migratedInheritedFPSOverride = true
                }
                return panel
            }
            if isUsingLegacyAutomaticLayout(workspace.panels)
                || isUsingLegacyPackedLayout(workspace.panels) {
                applySmartLayout(
                    to: &workspace.panels,
                    viewportSize: resolvedViewportSize(.zero)
                )
                migratedLegacyLayout = true
            }
            return workspace
        }
        sortWorkspaces()
        if shouldDisableInheritedFPSOverride {
            defaults.set(true, forKey: fpsOverrideOffMigrationKey)
        }
        if migratedLegacyLayout || migratedLegacyFramePacing
            || migratedInheritedFPSOverride {
            persist()
        }
    }

    private func persist() {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        guard let data = try? encoder.encode(workspaces) else { return }
        try? data.write(to: metadataURL, options: .atomic)
    }
}

@MainActor
final class WorkspaceRuntimeStore: ObservableObject {
    @Published private(set) var sessions: [UUID: EmulatorSession] = [:]
    @Published private(set) var runningApplicationCount = 0

    private var workspaceIDsByPanelID: [UUID: UUID] = [:]
    private var cancellables: [UUID: AnyCancellable] = [:]
    private var visibleWorkspaceIDs = Set<UUID>()
    private var applicationIsActive = true

    func session(for panelID: UUID) -> EmulatorSession? {
        sessions[panelID]
    }

    func isRunning(panelID: UUID) -> Bool {
        guard let application = sessions[panelID]?
            .runningApplications[panelID] else {
            return false
        }
        return Self.isActive(application.state)
    }

    func activateWorkspace(_ workspaceID: UUID) {
        visibleWorkspaceIDs.insert(workspaceID)
        guard applicationIsActive else { return }
        for (panelID, ownerID) in workspaceIDsByPanelID
            where ownerID == workspaceID {
            sessions[panelID]?.resume()
        }
    }

    func deactivateWorkspace(_ workspaceID: UUID) {
        visibleWorkspaceIDs.remove(workspaceID)
        for (panelID, ownerID) in workspaceIDsByPanelID
            where ownerID == workspaceID {
            sessions[panelID]?.suspend()
        }
    }

    func launch(
        panel: EmulatorWorkspacePanel,
        workspaceID: UUID,
        game: Game,
        jarURL: URL,
        artworkURL: URL?
    ) {
        if let existing = sessions[panel.id], existing.isRunning(panel.id) {
            workspaceIDsByPanelID[panel.id] = workspaceID
            if applicationIsActive && visibleWorkspaceIDs.contains(workspaceID) {
                existing.resume()
            }
            return
        }

        let session = EmulatorSession()
        sessions[panel.id] = session
        workspaceIDsByPanelID[panel.id] = workspaceID
        observe(session, panelID: panel.id)

        session.launch(
            game: runtimeGame(for: panel, source: game),
            jarURL: jarURL,
            artworkURL: artworkURL,
            profile: panel.profile
        )
        if !applicationIsActive || !visibleWorkspaceIDs.contains(workspaceID) {
            session.suspend()
        }
        recomputeRunningCount()
    }

    func restart(
        panel: EmulatorWorkspacePanel,
        workspaceID: UUID,
        game: Game,
        jarURL: URL,
        artworkURL: URL?
    ) {
        shutdown(panelID: panel.id, deleteData: false) { [weak self] in
            self?.launch(
                panel: panel,
                workspaceID: workspaceID,
                game: game,
                jarURL: jarURL,
                artworkURL: artworkURL
            )
        }
    }

    func shutdown(
        panelID: UUID,
        deleteData: Bool,
        completion: (@MainActor @Sendable () -> Void)? = nil
    ) {
        cancellables.removeValue(forKey: panelID)?.cancel()
        workspaceIDsByPanelID.removeValue(forKey: panelID)
        let finish: @MainActor @Sendable () -> Void = {
            if deleteData {
                PhoneMERuntimeResources.removeStorage(for: panelID)
            }
            completion?()
        }
        if let session = sessions.removeValue(forKey: panelID) {
            session.shutdown(completion: finish)
        } else {
            finish()
        }
        recomputeRunningCount()
    }

    func shutdown(workspace: EmulatorWorkspace, deleteData: Bool) {
        for panel in workspace.panels {
            shutdown(panelID: panel.id, deleteData: deleteData)
        }
        visibleWorkspaceIDs.remove(workspace.id)
    }

    func configureJIT(enabled: Bool) {
        for session in sessions.values {
            session.configureJIT(enabled: enabled)
        }
    }

    func refreshJITStatus() {
        for session in sessions.values {
            session.refreshJITStatus()
        }
    }

    func suspendAll() {
        applicationIsActive = false
        for session in sessions.values {
            session.suspend()
        }
    }

    func resumeVisibleWorkspaces() {
        applicationIsActive = true
        for (panelID, workspaceID) in workspaceIDsByPanelID
            where visibleWorkspaceIDs.contains(workspaceID) {
            sessions[panelID]?.resume()
        }
    }

    private func observe(_ session: EmulatorSession, panelID: UUID) {
        cancellables[panelID] = Publishers.CombineLatest(
            session.$state,
            session.$runningApplications
        )
        .receive(on: RunLoop.main)
        .sink { [weak self] _, _ in
            Task { @MainActor in
                self?.recomputeRunningCount()
            }
        }
    }

    private func recomputeRunningCount() {
        runningApplicationCount = sessions.values.reduce(into: 0) { count, session in
            count += session.runningApplications.values.reduce(into: 0) {
                applicationCount,
                application in
                if Self.isActive(application.state) {
                    applicationCount += 1
                }
            }
        }
    }

    private static func isActive(
        _ state: RunningJ2MEApplication.State
    ) -> Bool {
        switch state {
        case .starting, .foreground, .background, .paused:
            return true
        case .failed:
            return false
        }
    }

    private func runtimeGame(
        for panel: EmulatorWorkspacePanel,
        source game: Game
    ) -> Game {
        Game(
            id: panel.id,
            title: game.title,
            vendor: game.vendor,
            version: game.version,
            mainClass: game.mainClass,
            fileName: game.fileName,
            iconFileName: game.iconFileName,
            importedAt: game.importedAt,
            lastPlayedAt: game.lastPlayedAt,
            playCount: game.playCount
        )
    }
}
