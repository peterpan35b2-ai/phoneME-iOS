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

    enum Status: Equatable {
        case disabled
        case waitingForApplication
        case readyForBackground
        case requestingPermission
        case active
        case denied
        case restricted
        case locationServicesDisabled
        case failed(String)

        var description: String {
            switch self {
            case .disabled:
                return "Disabled"
            case .waitingForApplication:
                return "Starts when a J2ME application is running"
            case .readyForBackground:
                return "Ready for background use"
            case .requestingPermission:
                return "Location permission is required"
            case .active:
                return "Background execution is active"
            case .denied:
                return "Location access is denied"
            case .restricted:
                return "Location access is restricted"
            case .locationServicesDisabled:
                return "Location Services are turned off"
            case .failed(let message):
                return message
            }
        }

        var requiresSystemSettings: Bool {
            switch self {
            case .denied, .restricted, .locationServicesDisabled:
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
    private var hasRunningApplications = false
    private var shouldKeepAlive = false
    private var isRequestingAuthorization = false

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
        locationManager.desiredAccuracy = kCLLocationAccuracyThreeKilometers
        locationManager.distanceFilter = 3_000
        locationManager.pausesLocationUpdatesAutomatically = false
        locationManager.allowsBackgroundLocationUpdates = true
        locationManager.showsBackgroundLocationIndicator = true
    }

    func setEnabled(_ enabled: Bool) {
        guard isEnabled != enabled else {
            reevaluate()
            return
        }

        isEnabled = enabled
        UserDefaults.standard.set(enabled, forKey: Self.preferenceKey)
        reevaluate()
    }

    func setHasRunningApplications(_ hasRunningApplications: Bool) {
        guard self.hasRunningApplications != hasRunningApplications else {
            reevaluate()
            return
        }

        self.hasRunningApplications = hasRunningApplications
        reevaluate()
    }

    func setShouldKeepAlive(_ shouldKeepAlive: Bool) {
        guard self.shouldKeepAlive != shouldKeepAlive else {
            reevaluate()
            return
        }

        self.shouldKeepAlive = shouldKeepAlive
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

        guard hasRunningApplications else {
            stopLocationUpdates(status: .waitingForApplication)
            return
        }

        guard CLLocationManager.locationServicesEnabled() else {
            stopLocationUpdates(status: .locationServicesDisabled)
            return
        }

        switch locationManager.authorizationStatus {
        case .authorizedAlways, .authorizedWhenInUse:
            guard shouldKeepAlive else {
                stopLocationUpdates(status: .readyForBackground)
                return
            }
            startLocationUpdatesIfNeeded()

        case .notDetermined:
            stopLocationUpdates(status: .requestingPermission)
            guard
                !isRequestingAuthorization,
                UIApplication.shared.applicationState == .active
            else {
                return
            }
            isRequestingAuthorization = true
            locationManager.requestWhenInUseAuthorization()

        case .denied:
            stopLocationUpdates(status: .denied)

        case .restricted:
            stopLocationUpdates(status: .restricted)

        @unknown default:
            stopLocationUpdates(
                status: .failed("Unknown location authorization state")
            )
        }
    }

    private func startLocationUpdatesIfNeeded() {
        isRequestingAuthorization = false
        guard !isKeepingAlive else {
            status = .active
            return
        }

        locationManager.startUpdatingLocation()
        isKeepingAlive = true
        status = .active
        backgroundExecutionLogger.info(
            "Background execution keeper started"
        )
    }

    private func stopLocationUpdates(status: Status) {
        isRequestingAuthorization = false
        if isKeepingAlive {
            locationManager.stopUpdatingLocation()
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
        // The emulator does not consume location data. Receiving a coarse
        // location update keeps iOS background execution available while a
        // J2ME application is running, matching UTM's optional behavior.
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

    enum Status: Equatable {
        case disabled

        var description: String { "Unavailable" }
        var requiresSystemSettings: Bool { false }
    }

    @Published private(set) var isEnabled = false
    @Published private(set) var status: Status = .disabled
    @Published private(set) var isKeepingAlive = false

    func setEnabled(_ enabled: Bool) {}
    func setHasRunningApplications(_ hasRunningApplications: Bool) {}
    func setShouldKeepAlive(_ shouldKeepAlive: Bool) {}
    func openSystemSettings() {}
}

#endif
