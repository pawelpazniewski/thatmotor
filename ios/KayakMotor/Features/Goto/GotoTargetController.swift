import Foundation
import CoreLocation
import Observation
import KayakContract

/// Stan pojedynczego aktywnego celu goto (jeden cel — zero tras). Źródłem może być
/// dotknięcie mapy albo waypoint; nowy cel zastępuje poprzedni.
@MainActor
@Observable
final class GotoTargetController {
    enum SendState: Equatable {
        case idle
        case sending
        case sent
        case failed(String)
    }

    private(set) var staged: LatLonE7?
    private(set) var stagedCoordinate: CLLocationCoordinate2D?
    private(set) var sendState: SendState = .idle

    /// Ustawia cel z dotknięcia mapy — odrzuca współrzędne poza zakresem.
    func stage(coordinate: CLLocationCoordinate2D) {
        guard let target = LatLonE7(latDegrees: coordinate.latitude, lonDegrees: coordinate.longitude) else {
            return
        }
        staged = target
        stagedCoordinate = coordinate
        sendState = .idle
    }

    /// Ustawia cel z zapisanego waypointu.
    func stage(target: LatLonE7) {
        staged = target
        stagedCoordinate = CLLocationCoordinate2D(latitude: target.latDegrees, longitude: target.lonDegrees)
        sendState = .idle
    }

    func markSending() { sendState = .sending }
    func markSent() { sendState = .sent }
    func markFailed(_ message: String) { sendState = .failed(message) }

    func clear() {
        staged = nil
        stagedCoordinate = nil
        sendState = .idle
    }
}
