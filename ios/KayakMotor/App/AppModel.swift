import Foundation
import CoreLocation
import Observation
import UIKit
import KayakContract

/// Koordynator aplikacji — spina telemetrię, komendy, cel goto, keepalive i
/// waypointy. Cienki klient: żadnej logiki regulatora, tylko orkiestracja wywołań
/// kontraktu i prezentacja.
@MainActor
@Observable
final class AppModel {
    let telemetry: TelemetryStore
    let target = GotoTargetController()

    /// Lokalna intencja trybu autonomicznego (którą komendę wciśnięto) — używana
    /// tylko do etykiety UI; telemetria nie rozróżnia app-hold od app-goto.
    private(set) var autonomousIntent: AutonomousIntent = .none

    /// Kopia listy waypointów w warstwie obserwowalnej (`WaypointStore` to zwykła
    /// klasa persystencji — jej mutacje same nie odświeżają widoku).
    private(set) var waypointList: [Waypoint] = []

    /// Ostrzeżenie „poza akwenem" (R7) — miękkie, z potwierdzeniem, nie blokuje.
    var showGeofenceWarning = false
    var lastActionMessage: String?

    private let waypoints: WaypointStore
    private let commands: any CommandSending
    private let keepalive = KeepaliveController()
    private let lakePolygon: [GeoPoint]
    private var pendingGeofencedTarget: LatLonE7?

    init(commands: any CommandSending = CommandClient(),
         telemetry: TelemetryStore = TelemetryStore()) {
        self.commands = commands
        self.telemetry = telemetry
        self.waypoints = WaypointStore(fileURL: Self.waypointStoreURL())
        self.lakePolygon = Self.loadLakePolygon()
        self.waypointList = waypoints.waypoints
    }

    /// Czy można zapisać waypoint (jest świeży fix).
    var canSaveWaypoint: Bool { telemetry.latest?.gpsFix ?? false }

    // MARK: - Cykl życia

    func start() {
        telemetry.start()
        keepalive.start { [weak self] in
            await self?.keepaliveTick()
        }
    }

    func stop() {
        keepalive.stop()
        telemetry.stop()
        UIApplication.shared.isIdleTimerDisabled = false
    }

    private func keepaliveTick() async {
        let gotoState = telemetry.latest?.gotoState ?? .off
        UIApplication.shared.isIdleTimerDisabled =
            KeepaliveDecision.shouldDisableIdleTimer(gotoState: gotoState)
        guard let staged = target.staged,
              KeepaliveDecision.shouldResend(gotoState: gotoState, hasTarget: true) else {
            return
        }
        try? await commands.send(.goto(staged))
    }

    // MARK: - Tap-to-goto

    func handleMapTap(_ coordinate: CLLocationCoordinate2D) {
        target.stage(coordinate: coordinate)
    }

    /// „Płyń do punktu" — sprawdza miękką barierę geofence, potem wysyła.
    func requestGoto() {
        guard let staged = target.staged, let coordinate = target.stagedCoordinate else { return }
        autonomousIntent = .goto
        let point = GeoPoint(latDegrees: coordinate.latitude, lonDegrees: coordinate.longitude)
        if !lakePolygon.isEmpty && !WaterGeofence.contains(point, polygon: lakePolygon) {
            pendingGeofencedTarget = staged
            showGeofenceWarning = true
            return
        }
        Task { await sendGoto(staged) }
    }

    /// Potwierdzenie po ostrzeżeniu — wysyła mimo to (bezpieczeństwo w firmware/RC).
    func confirmGeofencedGoto() {
        showGeofenceWarning = false
        guard let staged = pendingGeofencedTarget else { return }
        pendingGeofencedTarget = nil
        Task { await sendGoto(staged) }
    }

    private func sendGoto(_ targetE7: LatLonE7) async {
        target.markSending()
        do {
            try await commands.send(.goto(targetE7))
            target.markSent()
        } catch let apiError as ApiError {
            target.markFailed(apiError.message)
        } catch {
            target.markFailed(error.localizedDescription)
        }
    }

    // MARK: - Bezpieczeństwo (R6) — app nigdy nie uzbraja

    /// STOP: natychmiastowy `goto_cancel`, bez rozbrajania, bez potwierdzenia.
    func stopGoto() {
        autonomousIntent = .none
        target.clear()
        Task {
            do { try await commands.send(.gotoCancel); lastActionMessage = "STOP wysłany" }
            catch { lastActionMessage = "STOP: błąd wysłania" }
        }
    }

    /// Rozbrój: `disarm` (kill silnika).
    func disarm() {
        Task {
            do { try await commands.send(.disarm); lastActionMessage = "Rozbrojono" }
            catch { lastActionMessage = "Rozbrój: błąd wysłania" }
        }
    }

    /// Spot-lock — kotwica w bieżącej pozycji łodzi. Wysyła `hold`; firmware łapie
    /// własny fix i latchuje `SRC_GOTO`. Intencję zapamiętujemy lokalnie (etykieta).
    func requestSpotLock() {
        autonomousIntent = .hold
        Task {
            do { try await commands.send(.hold); lastActionMessage = "Kotwica — trzymam pozycję" }
            catch { lastActionMessage = "Spot-lock: błąd wysłania" }
        }
    }

    /// Usuń/abortuj cel goto: kasuje pinezkę (zatrzymuje resend keepalive), a gdy
    /// nawigacja już trwa na firmware — dodatkowo wysyła `goto_cancel`.
    func clearTarget() {
        let wasActive = (telemetry.latest?.gotoState ?? .off) != .off
        autonomousIntent = .none
        target.clear()
        guard wasActive else { return }
        Task {
            do { try await commands.send(.gotoCancel); lastActionMessage = "Nawigacja anulowana" }
            catch { lastActionMessage = "Anuluj: błąd wysłania" }
        }
    }

    // MARK: - Waypointy (R4)

    /// Zapisuje realną pozycję łódki jako nazwany waypoint (odrzuca brak fixu).
    func saveCurrentPosition(name: String) {
        guard let telemetry = telemetry.latest,
              let waypoint = Waypoint.fromBoat(telemetry, name: name) else {
            lastActionMessage = "Brak fixu — nie zapisano"
            return
        }
        try? waypoints.add(waypoint)
        waypointList = waypoints.waypoints
    }

    func selectWaypoint(_ waypoint: Waypoint) {
        target.stage(target: waypoint.target)
    }

    func deleteWaypoint(_ waypoint: Waypoint) {
        try? waypoints.delete(id: waypoint.id)
        waypointList = waypoints.waypoints
    }

    // MARK: - Zasoby

    private static func loadLakePolygon() -> [GeoPoint] {
        guard let url = Bundle.main.url(forResource: "lake", withExtension: "geojson"),
              let data = try? Data(contentsOf: url),
              let polygon = try? LakeContour.polygon(fromGeoJSON: data) else {
            return []
        }
        return polygon
    }

    private static func waypointStoreURL() -> URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        try? FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)
        return base.appendingPathComponent("waypoints.json")
    }
}
