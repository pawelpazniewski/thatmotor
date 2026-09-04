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

    /// Etykieta stanu hold (goto / spot-lock) do arkusza szczegółów.
    public static func holdStateLabel(_ state: HoldState) -> String {
        switch state {
        case .off: return "Wyłączony"
        case .active: return "Aktywny"
        case .paused: return "Pauza"
        case .unknown: return placeholder
        }
    }

    /// „Zaangażowane" źródło sterowania — stan `active` lub `paused`
    /// (off/unknown = nieaktywne). Wspólny predykat dla `modeLabel` i wyboru
    /// aktywnego celu, żeby priorytet goto>spot-lock nie rozjechał się między nimi.
    private static func isEngaged(_ state: HoldState) -> Bool {
        state == .active || state == .paused
    }

    /// Aktywny tryb sterowania. Goto ma pierwszeństwo (jak w `ControlBarView`);
    /// gdy goto zgasło, pokazujemy spot-lock; inaczej tryb ręczny.
    public static func modeLabel(spotLock: HoldState, goto: HoldState) -> String {
        if isEngaged(goto) {
            return goto == .paused ? "Goto (pauza)" : "Goto"
        }
        if isEngaged(spotLock) {
            return spotLock == .paused ? "Spot-lock (pauza)" : "Spot-lock"
        }
        return "Ręczny"
    }

    /// Dystans i namiar z AKTYWNEGO źródła celu — ta sama reguła priorytetu co
    /// `modeLabel` (goto > spot-lock, przez wspólny `isEngaged`). Zwraca gotowy
    /// tekst (`targetText`) z pól wybranego źródła albo `nil` w trybie ręcznym
    /// (brak celu). Widoki (HUD, arkusz) używają tego zamiast własnego wyboru
    /// źródła, dzięki czemu logika wyboru jest host-testowalna, nie w widoku.
    public static func activeTargetText(
        gotoState: HoldState, gotoErrM: Int, gotoBearingDeg10: Int,
        spotLockState: HoldState, spotLockErrM: Int, spotLockBearingDeg10: Int
    ) -> String? {
        if isEngaged(gotoState) {
            return targetText(errM: gotoErrM, bearingDeg10: gotoBearingDeg10)
        }
        if isEngaged(spotLockState) {
            return targetText(errM: spotLockErrM, bearingDeg10: spotLockBearingDeg10)
        }
        return nil
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

    /// Wartość logiczna po polsku do arkusza („Tak" / „Nie").
    public static func boolText(_ value: Bool) -> String {
        value ? "Tak" : "Nie"
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

    /// Czy regulacja trimu jest dozwolona — firmware stosuje trim (RAM,
    /// natychmiast) w DISARMED i ARMED, więc UI aktywuje sekcję dla obu tych
    /// stanów (ARMED: korekta neutralu na bieżąco w trakcie pływania).
    public static func isTrimEnabled(state: SystemState) -> Bool {
        state == .disarmed || state == .armed
    }

    /// Kurs po zastosowaniu LOKALNEGO offsetu kompasu (dostrajanego ręcznie
    /// przy wizualnej weryfikacji na mapie — patrz oś dzioba w `LakeMapView`),
    /// znormalizowany do [0, 360). Offset jest wyłącznie kosmetyczny/per-
    /// urządzenie: NIE dotyka firmware ani realnego sterowania (spot-lock/goto
    /// liczą kurs sam firmware, z surowej telemetrii IMU).
    public static func calibratedHeadingDegrees(_ headingDegrees: Double,
                                                offsetDegrees: Double) -> Double {
        let raw = (headingDegrees + offsetDegrees).truncatingRemainder(dividingBy: 360)
        return raw < 0 ? raw + 360 : raw
    }
}
