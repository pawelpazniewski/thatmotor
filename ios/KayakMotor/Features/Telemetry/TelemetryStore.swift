import Foundation
import CoreLocation
import Observation
import KayakContract

/// Obserwowalny store telemetrii: konsumuje strumień WS, prowadzi maszynę stanu
/// łącza i wystawia ostatnią ramkę + stan łodzi dla mapy/HUD. Nie liczy nawigacji
/// — tylko prezentuje (cienki klient).
@MainActor
@Observable
final class TelemetryStore {
    private(set) var latest: Telemetry?
    private(set) var linkState: LinkState = .disconnected

    private var machine = LinkStateMachine(stalenessThreshold: GotoTiming.commsTimeoutSeconds)
    private let socket: TelemetrySocket
    private var lastFrameAt = Date.distantPast
    private var streamTask: Task<Void, Never>?
    private var tickTask: Task<Void, Never>?

    /// Lokalny, ręczny offset kompasu (stopnie) do wizualnej weryfikacji/dostrojenia
    /// kierunku na mapie (oś dzioba w `LakeMapView`, "Kurs" w szczegółach) — NIE
    /// dotyka firmware ani sterowania. Trwały per-urządzenie (`UserDefaults`).
    var compassOffsetDegrees: Double {
        didSet { UserDefaults.standard.set(compassOffsetDegrees, forKey: Self.compassOffsetKey) }
    }
    private static let compassOffsetKey = "compassOffsetDegrees"

    init(socket: TelemetrySocket = TelemetrySocket()) {
        self.socket = socket
        self.compassOffsetDegrees = UserDefaults.standard.double(forKey: Self.compassOffsetKey)
    }

    func start() {
        guard streamTask == nil else { return }
        machine.apply(.joinStarted)
        linkState = machine.state
        streamTask = Task { [weak self] in
            guard let self else { return }
            for await event in self.socket.events() {
                self.handle(event)
            }
        }
        tickTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(500))
                self?.tick()
            }
        }
    }

    func stop() {
        streamTask?.cancel(); streamTask = nil
        tickTask?.cancel(); tickTask = nil
        socket.stop()
        machine.apply(.socketClosed)
        linkState = machine.state
    }

    /// Wznawia strumień WS po powrocie z tła (iOS zawiesza socket). `socket.events()`
    /// tworzy świeże `NWConnection`, więc wystarczy stop→start.
    func reconnect() {
        stop()
        start()
    }

    private func handle(_ event: TelemetryStreamEvent) {
        switch event {
        case .opened:
            machine.apply(.socketOpened)
        case .frame(let telemetry):
            latest = telemetry
            lastFrameAt = Date()
            machine.apply(.frameReceived)
        case .closed:
            machine.apply(.socketClosed)
        }
        linkState = machine.state
    }

    private func tick() {
        machine.apply(.tick(sinceLastFrame: Date().timeIntervalSince(lastFrameAt)))
        linkState = machine.state
    }

    /// Stan łodzi do renderu — nil pozycja, gdy brak świeżego fixu. Kurs po
    /// zastosowaniu `compassOffsetDegrees` (patrz jego dokumentacja).
    var boat: BoatRenderState {
        guard let t = latest, t.gpsFix else {
            return BoatRenderState(coordinate: nil, headingDegrees: 0)
        }
        return BoatRenderState(
            coordinate: CLLocationCoordinate2D(
                latitude: Coordinate.fromE7(t.gpsLatE7),
                longitude: Coordinate.fromE7(t.gpsLonE7)),
            headingDegrees: TelemetryDisplay.calibratedHeadingDegrees(
                t.headingDegrees, offsetDegrees: compassOffsetDegrees))
    }

    /// Powód, gdy goto nie może wystartować (nil = gotowe). `nil` też bez telemetrii.
    var gotoBlockReason: GotoBlockReason? {
        guard let t = latest else { return nil }
        return GotoReadiness.blockReason(t)
    }
}
