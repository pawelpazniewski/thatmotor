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

    init(socket: TelemetrySocket = TelemetrySocket()) {
        self.socket = socket
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

    /// Stan łodzi do renderu — nil pozycja, gdy brak świeżego fixu.
    var boat: BoatRenderState {
        guard let t = latest, t.gpsFix else {
            return BoatRenderState(coordinate: nil, headingDegrees: 0)
        }
        return BoatRenderState(
            coordinate: CLLocationCoordinate2D(
                latitude: Coordinate.fromE7(t.gpsLatE7),
                longitude: Coordinate.fromE7(t.gpsLonE7)),
            headingDegrees: t.headingDegrees)
    }

    /// Powód, gdy goto nie może wystartować (nil = gotowe). `nil` też bez telemetrii.
    var gotoBlockReason: GotoBlockReason? {
        guard let t = latest else { return nil }
        return GotoReadiness.blockReason(t)
    }
}
