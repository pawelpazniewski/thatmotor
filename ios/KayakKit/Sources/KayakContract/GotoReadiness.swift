import Foundation

/// Powód, dla którego goto nie może wystartować — wyprowadzony z telemetrii.
/// App nie uzbraja: gdy nie ARMED, komunikat kieruje operatora do RC.
public enum GotoBlockReason: Equatable, Sendable {
    case noRC                // arm_reason == NO_RC
    case sticksNotNeutral    // arm_reason == THROTTLE_NOT_NEUTRAL
    case notArmed            // inny powód braku uzbrojenia
    case noGpsFix            // brak świeżego fixu

    public var message: String {
        switch self {
        case .noRC: return "Brak sygnału RC — sprawdź nadajnik"
        case .sticksNotNeutral: return "Drążki nie w neutralu"
        case .notArmed: return "Uzbrój silnik na nadajniku RC"
        case .noGpsFix: return "Brak fixu GPS"
        }
    }
}

/// Czysta bramka gotowości goto. Zwraca `nil`, gdy warunki startu są spełnione
/// (ARMED + fix). Priorytet: najpierw brak uzbrojenia (z detalem z `arm_reason`),
/// potem brak fixu.
///
/// Uwaga: stan łącza (WS/AP) NIE jest tu bramką — to warstwa transportu (LinkState).
/// Świeżość `app_link_fresh` dotyczy podtrzymania trwającego goto, nie warunku startu
/// (przed pierwszą komendą link z definicji nie jest „fresh").
public enum GotoReadiness {
    public static func blockReason(_ t: Telemetry) -> GotoBlockReason? {
        guard t.state == .armed else {
            switch t.armReason {
            case .noRC: return .noRC
            case .throttleNotNeutral: return .sticksNotNeutral
            default: return .notArmed
            }
        }
        if !t.gpsFix { return .noGpsFix }
        return nil
    }

    public static func isReady(_ t: Telemetry) -> Bool {
        blockReason(t) == nil
    }
}
