import Combine
import Foundation

#if os(iOS)
import CoreLocation
import OSLog
import UIKit

private let backgroundExecutionLogger = Logger(
    subsystem: "dev.phoneme.emulator",
    category: "BackgroundExecution"
)

@MainActor
final class BackgroundExecutionController: NSObject, ObservableObject {
    static let preferenceKey = "keepJ2MERunningInBackground"

    enum ApplicationPhase: String {
        case active
        case inactive
        case background
    }

    enum Status: Equatable {
        case disabled
        case waitingForApplication
        case readyForBackground
        case requestingPermission
        case alwaysPermissionRequired
        case active
        case denied
        case restricted
        case locationServicesDisabled
        case failed(String)

        var description: String {
            switch self {
            case .disabled:
                return L10n.string("Disabled")
            case .waitingForApplication:
                return L10n.string("Starts when a J2ME application is running")
            case .readyForBackground:
                return L10n.string("Ready for background use")
            case .requestingPermission:
                return L10n.string("Allow Location, then choose Always Allow")
            case .alwaysPermissionRequired:
                return L10n.string("Choose Always in Location Services")
            case .active:
                return L10n.string("Background execution is active")
            case .denied:
                return L10n.string("Location access is denied")
            case .restricted:
                return L10n.string("Location access is restricted")
            case .locationServicesDisabled:
                return L10n.string("Location Services are turned off")
            case .failed(let message):
                return message
            }
        }

        var requiresSystemSettings: Bool {
            switch self {
            case .alwaysPermissionRequired, .denied, .restricted,
                 .locationServicesDisabled:
                return true
            default:
                return false
            }
        }
    }

    @Published private(set) var isEnabled: Bool
    @Published private(set) var status: Status
    @Published private(set) var isKeepingAlive = false

    private let locationManager: CLLocationManager
    private var runningApplicationCount = 0
    private var applicationPhase: ApplicationPhase = .active
    private var isRequestingAuthorization = false
    private var didRequestAlwaysAuthorization = false

    override init() {
        let defaults = UserDefaults.standard
        let storedValue = defaults.object(
            forKey: Self.preferenceKey
        ) as? Bool
        let enabled = storedValue ?? false
        isEnabled = enabled
        status = enabled ? .waitingForApplication : .disabled
        locationManager = CLLocationManager()
        super.init()

        locationManager.delegate = self
        locationManager.activityType = .other

        // This feature does not consume location coordinates. Three-kilometre
        // accuracy lets Core Location avoid powering high-accuracy GPS while
        // retaining the continuous background execution contract.
        locationManager.desiredAccuracy = kCLLocationAccuracyThreeKilometers
        locationManager.distanceFilter = 3_000

        // Automatic pauses can end the background execution window until the
        // app is launched again. Keep them disabled for connection stability;
        // energy is instead saved by coarse accuracy and by running this
        // manager only during the inactive/background scene phases.
        locationManager.pausesLocationUpdatesAutomatically = false
        locationManager.allowsBackgroundLocationUpdates = false
        locationManager.showsBackgroundLocationIndicator = false
    }

    func setEnabled(_ enabled: Bool) {
        guard isEnabled != enabled else {
            reevaluate()
            return
        }

        isEnabled = enabled
        UserDefaults.standard.set(enabled, forKey: Self.preferenceKey)
        if !enabled {
            isRequestingAuthorization = false
        }
        reevaluate()
    }

    func setRunningApplicationCount(_ count: Int) {
        runningApplicationCount = max(0, count)
        reevaluate()
    }

    func setApplicationPhase(_ phase: ApplicationPhase) {
        let previousPhase = applicationPhase
        applicationPhase = phase

        // Returning to active after a permission sheet means the user has
        // finished that interaction. If authorization is still When In Use,
        // reevaluate() will expose the Settings action instead of remaining in
        // a permanent "requesting" state.
        if phase == .active, previousPhase != .active,
           isRequestingAuthorization {
            isRequestingAuthorization = false
        }

        reevaluate()
    }

    func openSystemSettings() {
        guard let url = URL(string: UIApplication.openSettingsURLString) else {
            return
        }
        UIApplication.shared.open(url)
    }

    private func reevaluate() {
        guard isEnabled else {
            stopLocationUpdates(status: .disabled)
            return
        }

        guard CLLocationManager.locationServicesEnabled() else {
            stopLocationUpdates(status: .locationServicesDisabled)
            return
        }

        switch locationManager.authorizationStatus {
        case .authorizedAlways:
            isRequestingAuthorization = false

            guard runningApplicationCount > 0 else {
                stopLocationUpdates(status: .waitingForApplication)
                return
            }

            // Start while the scene is still inactive, before iOS completes
            // the foreground-to-background transition. Starting standard
            // location services only after entering background is unreliable
            // and may be rejected by the system.
            guard applicationPhase != .active else {
                stopLocationUpdates(status: .readyForBackground)
                return
            }

            startLocationUpdatesIfNeeded()

        case .authorizedWhenInUse:
            stopLocationUpdates(
                status: isRequestingAuthorization
                    ? .requestingPermission
                    : .alwaysPermissionRequired
            )
            requestAlwaysAuthorizationIfPossible()

        case .notDetermined:
            stopLocationUpdates(status: .requestingPermission)
            requestWhenInUseAuthorizationIfPossible()

        case .denied:
            isRequestingAuthorization = false
            stopLocationUpdates(status: .denied)

        case .restricted:
            isRequestingAuthorization = false
            stopLocationUpdates(status: .restricted)

        @unknown default:
            isRequestingAuthorization = false
            stopLocationUpdates(
                status: .failed("Unknown location authorization state")
            )
        }
    }

    private func requestWhenInUseAuthorizationIfPossible() {
        guard
            !isRequestingAuthorization,
            applicationPhase == .active,
            UIApplication.shared.applicationState == .active
        else {
            return
        }

        isRequestingAuthorization = true
        status = .requestingPermission
        locationManager.requestWhenInUseAuthorization()
    }

    private func requestAlwaysAuthorizationIfPossible() {
        guard
            !didRequestAlwaysAuthorization,
            !isRequestingAuthorization,
            applicationPhase == .active,
            UIApplication.shared.applicationState == .active
        else {
            return
        }

        // iOS presents the Always option as an upgrade after When In Use has
        // been granted. Calling requestAlwaysAuthorization() directly from the
        // not-determined state can leave the app with only provisional access.
        didRequestAlwaysAuthorization = true
        isRequestingAuthorization = true
        status = .requestingPermission
        locationManager.requestAlwaysAuthorization()
    }

    private func startLocationUpdatesIfNeeded() {
        isRequestingAuthorization = false
        guard !isKeepingAlive else {
            status = .active
            return
        }

        locationManager.allowsBackgroundLocationUpdates = true
        locationManager.showsBackgroundLocationIndicator = false
        locationManager.startUpdatingLocation()
        isKeepingAlive = true
        status = .active
        backgroundExecutionLogger.info(
            "Background keeper started during \(self.applicationPhase.rawValue, privacy: .public) for \(self.runningApplicationCount) J2ME application(s)"
        )
    }

    private func stopLocationUpdates(status: Status) {
        locationManager.stopUpdatingLocation()
        locationManager.allowsBackgroundLocationUpdates = false
        if isKeepingAlive {
            isKeepingAlive = false
            backgroundExecutionLogger.info(
                "Background execution keeper stopped"
            )
        }
        self.status = status
    }
}

extension BackgroundExecutionController: @preconcurrency CLLocationManagerDelegate {
    func locationManagerDidChangeAuthorization(
        _ manager: CLLocationManager
    ) {
        isRequestingAuthorization = false
        reevaluate()
    }

    func locationManager(
        _ manager: CLLocationManager,
        didUpdateLocations locations: [CLLocation]
    ) {
        // The emulator does not consume location data. A location event can be
        // the final signal that provisional Always authorization became
        // permanent, and some iOS versions do not send another authorization
        // callback for that transition.
        if manager.authorizationStatus == .authorizedAlways,
           status != .active {
            reevaluate()
        }
    }

    func locationManager(
        _ manager: CLLocationManager,
        didFailWithError error: Error
    ) {
        let nsError = error as NSError
        if nsError.domain == kCLErrorDomain,
           nsError.code == CLError.locationUnknown.rawValue {
            return
        }

        backgroundExecutionLogger.error(
            "Location keeper failed: \(error.localizedDescription, privacy: .public)"
        )
        stopLocationUpdates(status: .failed(error.localizedDescription))
    }
}

#else

@MainActor
final class BackgroundExecutionController: ObservableObject {
    static let preferenceKey = "keepJ2MERunningInBackground"

    enum ApplicationPhase {
        case active
        case inactive
        case background
    }

    enum Status: Equatable {
        case disabled

        var description: String { L10n.string("Unavailable") }
        var requiresSystemSettings: Bool { false }
    }

    @Published private(set) var isEnabled = false
    @Published private(set) var status: Status = .disabled
    @Published private(set) var isKeepingAlive = false

    func setEnabled(_ enabled: Bool) {}
    func setRunningApplicationCount(_ count: Int) {}
    func setApplicationPhase(_ phase: ApplicationPhase) {}
    func openSystemSettings() {}
}

#endif
