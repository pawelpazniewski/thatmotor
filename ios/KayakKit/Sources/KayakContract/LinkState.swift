import Foundation

/// Stan łącza do silnika — dyskryminowany union (nie boolean flags).
public enum LinkState: Equatable, Sendable {
    case disconnected   // brak AP / WS zamknięty
    case joining        // trwa dołączanie do AP / otwieranie WS
    case connected      // świeża telemetria płynie
    case stale          // połączeni, ale ramka nie przyszła w oknie świeżości
}

/// Zdarzenia sterujące maszyną stanu łącza.
public enum LinkEvent: Sendable {
    case joinStarted
    case socketOpened
    case frameReceived
    case socketClosed
    /// Tik zegara z czasem od ostatniej ramki (do wykrycia „stale").
    case tick(sinceLastFrame: TimeInterval)
}

/// Czysta maszyna stanu łącza. Wygaszanie „nieświeżych" danych to warunek jakości
/// (stale), oddzielony od zerwania (disconnected) — apka nie „udaje żywego".
public struct LinkStateMachine: Sendable {
    public private(set) var state: LinkState
    public let stalenessThreshold: TimeInterval

    public init(stalenessThreshold: TimeInterval = 1.0, state: LinkState = .disconnected) {
        self.stalenessThreshold = stalenessThreshold
        self.state = state
    }

    public mutating func apply(_ event: LinkEvent) {
        switch event {
        case .joinStarted:
            state = .joining
        case .socketOpened, .frameReceived:
            state = .connected
        case .socketClosed:
            state = .disconnected
        case .tick(let since):
            // Tylko połączone łącze może zwietrzeć; zerwane/łączące się nie.
            if state == .connected && since > stalenessThreshold {
                state = .stale
            }
        }
    }
}
