import Foundation

/// Lokalna intencja trybu autonomicznego — którą komendę operator wcisnął ostatnio.
/// Telemetria firmware NIE rozróżnia app-hold od app-goto (oba to `SRC_GOTO`,
/// `goto_state == active`), więc etykietę liczymy lokalnie z tej intencji.
/// Po force-quit intencja gubi się → `.none` (neutralna etykieta z reconcilera).
public enum AutonomousIntent: Equatable, Sendable {
    case none
    case goto
    case hold
}
