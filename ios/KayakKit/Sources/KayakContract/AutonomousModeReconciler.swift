import Foundation

/// Wyświetlana etykieta trybu autonomicznego — wynik uzgodnienia lokalnej intencji
/// z telemetrią firmware. `.cleared` = brak trybu (nic nie pokazujemy).
public enum AutonomousLabel: Equatable, Sendable {
    case cleared
    case anchor              // app-hold: „Kotwica"
    case navigatingToTarget  // app-goto: „Jadę do celu"
    case holdingPosition     // goto żyje, ale intencja zgubiona (po force-quit)
    case remoteOverride      // pilot przejął CH3 (spot_lock_state active, goto off)

    /// Tekst do UI; `nil` gdy nic nie pokazujemy (`.cleared`).
    public var text: String? {
        switch self {
        case .cleared: return nil
        case .anchor: return "Kotwica"
        case .navigatingToTarget: return "Jadę do celu"
        case .holdingPosition: return "Trzymam pozycję"
        case .remoteOverride: return "Pilot przejął (CH3)"
        }
    }
}

/// Czyste uzgodnienie: lokalna intencja × telemetria → etykieta trybu.
///
/// Telemetria NIE rozróżnia app-hold od app-goto (oba `SRC_GOTO`, `goto_state`),
/// więc gdy goto żyje, etykietę wybiera intencja. Gdy goto zgasło, ale
/// `spot_lock_state` jest aktywny — to pilot przejął sterowanie CH3 (nie app).
public enum AutonomousModeReconciler {
    public static func reconcile(intent: AutonomousIntent,
                                 gotoState: HoldState,
                                 spotLockState: HoldState) -> AutonomousLabel {
        switch gotoState {
        case .active, .paused:
            switch intent {
            case .hold: return .anchor
            case .goto: return .navigatingToTarget
            case .none: return .holdingPosition
            }
        case .off, .unknown:
            switch spotLockState {
            case .active, .paused: return .remoteOverride
            case .off, .unknown: return .cleared
            }
        }
    }
}
