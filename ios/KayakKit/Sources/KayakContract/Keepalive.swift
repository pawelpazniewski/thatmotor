import Foundation

/// Parametry czasowe goto, spójne z comms-watchdogiem firmware
/// (`GOTO_COMMS_TIMEOUT_MS_DEFAULT = 1500`).
public enum GotoTiming {
    /// Timeout linku po stronie firmware (s). Po tym czasie bez świeżej komendy
    /// goto → PAUSED.
    public static let commsTimeoutSeconds: Double = 1.5

    /// Interwał keepalive (~2 Hz) — konserwatywnie poniżej watchdogu, by nie
    /// wpaść w pauzę przy pojedynczym zgubionym pakiecie.
    public static let keepaliveIntervalSeconds: Double = 1.0
}

/// Czysta decyzja: czy keepalive ma ponawiać komendę goto w tym cyklu?
/// Ponawiamy TYLKO gdy mamy cel i goto realnie żyje (active/paused) — po STOP
/// (`gotoState == .off`) natychmiast przestajemy.
public enum KeepaliveDecision {
    public static func shouldResend(gotoState: HoldState, hasTarget: Bool) -> Bool {
        guard hasTarget else { return false }
        switch gotoState {
        case .active, .paused: return true
        case .off, .unknown: return false
        }
    }

    /// Czy podczas tego stanu należy blokować auto-lock ekranu (idle timer).
    /// Tylko przy aktywnym przejeździe — pauza/off przywraca auto-lock.
    public static func shouldDisableIdleTimer(gotoState: HoldState) -> Bool {
        gotoState == .active
    }
}
