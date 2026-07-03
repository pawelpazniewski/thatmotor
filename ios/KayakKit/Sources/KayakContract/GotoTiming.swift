import Foundation

/// Parametry czasowe łącza, spójne z comms-watchdogiem firmware
/// (`GOTO_COMMS_TIMEOUT_MS_DEFAULT = 1500`).
public enum GotoTiming {
    /// Próg świeżości telemetrii (s) — po tym czasie bez ramki WS UI pokazuje
    /// „Brak danych" (`LinkState.stale`). Firmware NIE pauzuje już goto przy utracie
    /// linku (latch trwały) — to wyłącznie wskaźnik jakości łącza w apce.
    public static let commsTimeoutSeconds: Double = 1.5
}
