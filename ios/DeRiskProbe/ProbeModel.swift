import Foundation
import Network
import NetworkExtension

/// Konfiguracja AP silnika (zweryfikowana w firmware: sdkconfig / wifi_ap.c).
enum MotorAP {
    static let ssid = "kayak-motor"
    static let passphrase = "CHANGE-ME-kayak"
    static let host = "192.168.4.1"
    static let port: UInt16 = 80
}

/// Model bramki de-risk: 3 testy (join AP, HTTP POST, WebSocket) + log z pomiarami.
/// Cała sieć wymuszona na interfejs Wi‑Fi (`requiredInterfaceType = .wifi` +
/// `prohibitExpensivePaths`) — ubezpieczenie na deprioryzację bez-internetowego Wi‑Fi.
@MainActor
@Observable
final class ProbeModel {
    private(set) var log: [String] = []
    private(set) var frameCount = 0
    private(set) var isBusy = false

    private var wsConnection: NWConnection?

    // MARK: - Log

    private func append(_ line: String) {
        let stamp = String(format: "%.2f", Date().timeIntervalSince1970.truncatingRemainder(dividingBy: 100_000))
        log.insert("[\(stamp)] \(line)", at: 0)
    }

    // MARK: - Test 1: Dołączenie do AP

    func joinAP() async {
        isBusy = true
        defer { isBusy = false }
        let start = Date()
        append("Join: proszę o dołączenie do \(MotorAP.ssid)…")
        let config = NEHotspotConfiguration(ssid: MotorAP.ssid, passphrase: MotorAP.passphrase, isWEP: false)
        config.joinOnce = false
        do {
            try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
                NEHotspotConfigurationManager.shared.apply(config) { error in
                    if let error, (error as NSError).code != NEHotspotConfigurationError.alreadyAssociated.rawValue {
                        cont.resume(throwing: error)
                    } else {
                        cont.resume()
                    }
                }
            }
            append(String(format: "Join: OK po %.1f s", Date().timeIntervalSince(start)))
        } catch {
            append("Join: BŁĄD — \(error.localizedDescription). Fallback: dołącz ręcznie w Ustawieniach.")
        }
    }

    // MARK: - Test 2: HTTP POST (URLSession)

    func httpPost() async {
        isBusy = true
        defer { isBusy = false }
        let start = Date()
        append("HTTP: POST /api/command {\"cmd\":\"goto_cancel\"}…")
        var request = URLRequest(url: URL(string: "http://\(MotorAP.host)/api/command")!)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = Data(#"{"cmd":"goto_cancel"}"#.utf8)
        request.timeoutInterval = 8

        let config = URLSessionConfiguration.ephemeral
        config.waitsForConnectivity = true
        config.timeoutIntervalForResource = 10
        let session = URLSession(configuration: config)
        do {
            let (data, response) = try await session.data(for: request)
            let status = (response as? HTTPURLResponse)?.statusCode ?? -1
            let body = String(data: data, encoding: .utf8) ?? "<binarne>"
            append(String(format: "HTTP: %d w %.2f s — %@", status, Date().timeIntervalSince(start), body))
        } catch {
            append("HTTP: BŁĄD — \(error.localizedDescription) (sprawdź uprawnienie Local Network)")
        }
    }

    // MARK: - Test 3: WebSocket (Network.framework, pin .wifi)

    func startWebSocket() {
        stopWebSocket()
        frameCount = 0
        append("WS: łączę ws://\(MotorAP.host)/ws (pin .wifi)…")

        let params = NWParameters.tcp
        params.requiredInterfaceType = .wifi
        params.prohibitExpensivePaths = true
        let wsOptions = NWProtocolWebSocket.Options()
        wsOptions.autoReplyPing = true
        params.defaultProtocolStack.applicationProtocols.insert(wsOptions, at: 0)

        let endpoint = NWEndpoint.url(URL(string: "ws://\(MotorAP.host)/ws")!)
        let connection = NWConnection(to: endpoint, using: params)
        wsConnection = connection

        connection.stateUpdateHandler = { [weak self] state in
            Task { @MainActor in
                switch state {
                case .ready:
                    self?.append("WS: READY — odbieram ramki…")
                    self?.receiveNext()
                case .failed(let error):
                    self?.append("WS: FAILED — \(error.localizedDescription)")
                case .waiting(let error):
                    self?.append("WS: WAITING — \(error.localizedDescription)")
                default:
                    break
                }
            }
        }
        connection.start(queue: .global(qos: .userInitiated))
    }

    private func receiveNext() {
        wsConnection?.receiveMessage { [weak self] data, _, _, error in
            Task { @MainActor in
                guard let self else { return }
                if let error {
                    self.append("WS: błąd odbioru — \(error.localizedDescription)")
                    return
                }
                if let data, !data.isEmpty {
                    self.frameCount += 1
                    if self.frameCount == 1 || self.frameCount % 10 == 0 {
                        self.append("WS: ramek=\(self.frameCount) (\(data.count) B)")
                    }
                }
                self.receiveNext()
            }
        }
    }

    func stopWebSocket() {
        wsConnection?.cancel()
        wsConnection = nil
    }
}
