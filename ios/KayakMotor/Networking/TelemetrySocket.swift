import Foundation
import Network
import KayakContract

/// Zdarzenie strumienia telemetrii — steruje maszyną `LinkStateMachine` i widokiem.
enum TelemetryStreamEvent: Sendable {
    case opened
    case frame(Telemetry)
    case closed
}

/// Strumień telemetrii WS (`GET /ws`) przez Network.framework, pinowany do `.wifi`
/// (`prohibitExpensivePaths`) — ubezpieczenie na deprioryzację bez-internetowego
/// Wi‑Fi. Dekoduje ramki na `Telemetry`; nieparsowalne ramki są pomijane (log-and-
/// continue), nie wywracają strumienia.
///
/// `@unchecked Sendable`: jedyny współdzielony stan (`connection`) jest przypisany raz
/// przed startem i tylko anulowany; handlery dotykają wyłącznie Sendable continuation.
final class TelemetrySocket: @unchecked Sendable {
    private let url: URL
    private let queue = DispatchQueue(label: "telemetry.ws")
    private var connection: NWConnection?

    init(url: URL = URL(string: "ws://192.168.4.1/ws")!) {
        self.url = url
    }

    func events() -> AsyncStream<TelemetryStreamEvent> {
        AsyncStream { continuation in
            let params = NWParameters.tcp
            params.requiredInterfaceType = .wifi
            params.prohibitExpensivePaths = true
            let ws = NWProtocolWebSocket.Options()
            ws.autoReplyPing = true
            params.defaultProtocolStack.applicationProtocols.insert(ws, at: 0)

            let connection = NWConnection(to: .url(url), using: params)
            self.connection = connection

            connection.stateUpdateHandler = { state in
                switch state {
                case .ready:
                    continuation.yield(.opened)
                    self.receive(on: connection, into: continuation)
                case .failed, .cancelled:
                    continuation.yield(.closed)
                    continuation.finish()
                default:
                    break
                }
            }
            continuation.onTermination = { _ in connection.cancel() }
            connection.start(queue: queue)
        }
    }

    private func receive(on connection: NWConnection,
                         into continuation: AsyncStream<TelemetryStreamEvent>.Continuation) {
        connection.receiveMessage { data, _, _, error in
            if let data, !data.isEmpty,
               let telemetry = try? JSONDecoder().decode(Telemetry.self, from: data) {
                continuation.yield(.frame(telemetry))
            }
            if error != nil {
                continuation.yield(.closed)
                continuation.finish()
                return
            }
            self.receive(on: connection, into: continuation)
        }
    }

    func stop() {
        connection?.cancel()
        connection = nil
    }
}
