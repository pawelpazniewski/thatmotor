import Foundation
import CoreLocation
import Observation
import KayakContract

/// Koordynator aplikacji — spina telemetrię, komendy, cel goto i waypointy.
/// Cienki klient: żadnej logiki regulatora, tylko orkiestracja wywołań kontraktu
/// i prezentacja. Bez keepalive/idle-timer — firmware nie pauzuje przy utracie
/// linku (latch trwały), więc ekran może gasnąć, a łódź płynie dalej.
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
    private var pendingGeofencedTarget: LatLonE7?
    /// Czy ostatnio wystawiony cel leży na wodzie — liczone w widoku z warstwy wody
    /// mapy w chwili tapnięcia. Cele z waypointu/telemetrii traktujemy jak „na wodzie".
    private var stagedOnWater = true

    init(commands: any CommandSending = CommandClient(),
         telemetry: TelemetryStore = TelemetryStore()) {
        self.commands = commands
        self.telemetry = telemetry
        self.waypoints = WaypointStore(fileURL: Self.waypointStoreURL())
        self.waypointList = waypoints.waypoints
    }

    /// Czy można zapisać waypoint (jest świeży fix).
    var canSaveWaypoint: Bool { telemetry.latest?.gpsFix ?? false }

    // MARK: - Cykl życia

    func start() {
        telemetry.start()
    }

    func stop() {
        telemetry.stop()
    }

    /// Powrót z tła/wygaszenia (`scenePhase == .active`): reconnect WS i uzgodnienie
    /// lokalnej intencji ze stanem firmware. Gdy firmware zakończył tryb — czyścimy
    /// intencję i pinezkę; gdy tryb wciąż żyje — odtwarzamy pinezkę z telemetrii (R8).
    func resync() {
        telemetry.reconnect()
        reconcileAutonomousState()
    }

    /// Etykieta trybu autonomicznego liczona lokalnie (intencja × telemetria).
    var autonomousLabel: AutonomousLabel {
        AutonomousModeReconciler.reconcile(
            intent: autonomousIntent,
            gotoState: telemetry.latest?.gotoState ?? .off,
            spotLockState: telemetry.latest?.spotLockState ?? .off)
    }

    private func reconcileAutonomousState() {
        switch autonomousLabel {
        case .cleared:
            autonomousIntent = .none
            target.clear()
        default:
            restoreTargetPinFromTelemetry()
        }
    }

    /// Odtwarza pinezkę celu z telemetrii, gdy tryb żyje, a app nie ma jej lokalnie
    /// (np. po force-quit). Współrzędne z firmware są już w prawidłowym zakresie e7.
    private func restoreTargetPinFromTelemetry() {
        guard target.staged == nil,
              let t = telemetry.latest, t.gotoState != .off else { return }
        target.stage(target: LatLonE7(latE7: t.gotoTargetLatE7, lonE7: t.gotoTargetLonE7))
        stagedOnWater = true
    }

    // MARK: - Tap-to-goto

    func handleMapTap(_ coordinate: CLLocationCoordinate2D, isOnWater: Bool) {
        target.stage(coordinate: coordinate)
        stagedOnWater = isOnWater
    }

    /// „Płyń do punktu" — miękka bariera geofence, potem wysyła. Ostrzegamy tylko,
    /// gdy tap trafił poza wodę (wg warstwy wody mapy); waypoint/telemetria nie ostrzega.
    func requestGoto() {
        guard let staged = target.staged else { return }
        autonomousIntent = .goto
        if !stagedOnWater {
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

    // MARK: - Trim serwa (R5) — regulacja neutrala, firmware honoruje tylko DISARMED

    /// Przesuń neutral serwa w lewo o krok firmware (`trim_left`).
    func trimLeft() {
        Task {
            do { try await commands.send(.trimLeft); lastActionMessage = "Trim w lewo" }
            catch let apiError as ApiError { lastActionMessage = apiError.message }
            catch { lastActionMessage = "Trim: błąd wysłania" }
        }
    }

    /// Przesuń neutral serwa w prawo o krok firmware (`trim_right`).
    func trimRight() {
        Task {
            do { try await commands.send(.trimRight); lastActionMessage = "Trim w prawo" }
            catch let apiError as ApiError { lastActionMessage = apiError.message }
            catch { lastActionMessage = "Trim: błąd wysłania" }
        }
    }

    /// Zapisz bieżący neutral serwa do NVS (`trim_save`) — przeżywa restart.
    func saveTrim() {
        Task {
            do { try await commands.send(.trimSave); lastActionMessage = "Trim zapisany" }
            catch let apiError as ApiError { lastActionMessage = apiError.message }
            catch { lastActionMessage = "Zapis trimu: błąd wysłania" }
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
        stagedOnWater = true
    }

    func deleteWaypoint(_ waypoint: Waypoint) {
        try? waypoints.delete(id: waypoint.id)
        waypointList = waypoints.waypoints
    }

    // MARK: - Zasoby

    private static func waypointStoreURL() -> URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        try? FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)
        return base.appendingPathComponent("waypoints.json")
    }
}
