import Foundation

/// Czysta bramka gotowości spot-locka (kotwicy). Warunki startu są identyczne jak
/// goto (ARMED + świeży fix — R6/R9), więc dla istniejącej telemetrii delegujemy do
/// `GotoReadiness`. Różnica: spot-lock bramkujemy też, gdy telemetrii brak (nil) —
/// bez znanego stanu nie pozwalamy zakotwiczyć.
///
/// Świeżość łącza NIE jest tu bramką (jak w `GotoReadiness`) — to warstwa transportu.
public enum SpotLockReadiness {
    /// Zwraca powód blokady albo `nil`, gdy można zakotwiczyć. `nil` telemetria →
    /// `.noGpsFix` (brak znanego fixu).
    public static func blockReason(_ telemetry: Telemetry?) -> GotoBlockReason? {
        guard let t = telemetry else { return .noGpsFix }
        return GotoReadiness.blockReason(t)
    }

    /// `true`, gdy telemetria dopuszcza start kotwicy (ARMED + świeży fix).
    public static func canEngage(_ telemetry: Telemetry?) -> Bool {
        blockReason(telemetry) == nil
    }
}
