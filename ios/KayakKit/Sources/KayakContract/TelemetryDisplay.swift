import Foundation

/// Czysta warstwa prezentacji telemetrii: formatowanie pól, bramka „wartość vs
/// myślnik" wg świeżości oraz gate regulacji trimu. BEZ SwiftUI — host-testowalna,
/// dzięki czemu widoki pozostają cienkie (składają gotowe stringi, nie liczą logiki).
public enum TelemetryDisplay {
    /// Znak wyświetlany zamiast wartości, gdy dane nie są świeże.
    public static let placeholder = "—"

    /// Jakość GPS: bez fixu pokazuje „brak fix" (a NIE „0 sat", które sugeruje
    /// świeży odczyt z zerem satelitów).
    public static func gpsQualityText(fix: Bool, sats: Int) -> String {
        guard fix else { return "brak fix" }
        return "\(sats) sat"
    }

    /// Etykieta stanu maszyny firmware do HUD/arkusza.
    public static func stateLabel(_ state: SystemState) -> String {
        switch state {
        case .disarmed: return "DISARMED"
        case .armed: return "ARMED"
        case .failsafe: return "FAILSAFE"
        case .escCalibration: return "KALIBRACJA ESC"
        case .deploy: return "WYSUWANIE"
        case .unknown: return placeholder
        }
    }

    /// Aktywny tryb sterowania. Goto ma pierwszeństwo (jak w `ControlBarView`);
    /// gdy goto zgasło, pokazujemy spot-lock; inaczej tryb ręczny.
    public static func modeLabel(spotLock: HoldState, goto: HoldState) -> String {
        switch goto {
        case .active: return "Goto"
        case .paused: return "Goto (pauza)"
        case .off, .unknown:
            switch spotLock {
            case .active: return "Spot-lock"
            case .paused: return "Spot-lock (pauza)"
            case .off, .unknown: return "Ręczny"
            }
        }
    }

    /// Dystans i namiar do celu, np. „123 m · 45°" (jak `ControlBarView`).
    /// `bearingDeg10` to dziesiąte części stopnia (deg10 → deg).
    public static func targetText(errM: Int, bearingDeg10: Int) -> String {
        String(format: "%d m · %.0f°", errM, Double(bearingDeg10) / 10.0)
    }

    /// Prędkość nad dnem i kurs dzioba, np. „1.5 m/s · 123°".
    /// `speedCms` w cm/s (→ m/s), `headingDeg10` w deg10 (→ deg).
    public static func speedHeadingText(speedCms: Int, headingDeg10: Int) -> String {
        String(format: "%.1f m/s · %.0f°", Double(speedCms) / 100.0, Double(headingDeg10) / 10.0)
    }

    /// Neutral serwa w µs ze znakiem, np. „+120 µs" / „-140 µs".
    public static func trimText(servoTrimUs: Int) -> String {
        String(format: "%+d µs", servoTrimUs)
    }

    /// Bramka świeżości: zwraca wartość, gdy dane świeże, inaczej myślnik.
    /// Zapobiega „zamrożonym" liczbom po utracie łącza.
    public static func displayed(_ value: String, isFresh: Bool) -> String {
        isFresh ? value : placeholder
    }

    /// Czy telemetria jest świeża — TYLKO gdy łącze jest `.connected`.
    /// `.stale`/`.disconnected`/`.joining` → nieświeże (widoki pokażą myślniki).
    public static func isFresh(_ link: LinkState) -> Bool {
        link == .connected
    }

    /// Czy regulacja trimu jest dozwolona — firmware stosuje trim WYŁĄCZNIE po
    /// rozbrojeniu, więc UI aktywuje sekcję tylko dla `.disarmed`.
    public static func isTrimEnabled(state: SystemState) -> Bool {
        state == .disarmed
    }
}
