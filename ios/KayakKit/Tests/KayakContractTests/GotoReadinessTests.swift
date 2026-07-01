import Foundation
import Testing
@testable import KayakContract

@Suite("Bramka gotowości goto")
struct GotoReadinessTests {
    /// Buduje ramkę telemetrii z wybranymi polami istotnymi dla readiness.
    private func frame(state: Int, armReason: Int, gpsFix: Bool) -> Telemetry {
        let json = """
        {"state":\(state),"arm_reason":\(armReason),"rc_valid":true,
        "gps_fix":\(gpsFix),"gps_sats":8,"gps_lat_e7":0,"gps_lon_e7":0,
        "gps_speed_cms":0,"imu_ok":true,"imu_heading_deg10":0,"goto_state":0,
        "goto_target_lat_e7":0,"goto_target_lon_e7":0,"goto_err_m":0,
        "goto_bearing_deg10":0,"goto_arrived":false,"app_link_fresh":false,
        "spot_lock_state":0}
        """
        return try! JSONDecoder().decode(Telemetry.self, from: Data(json.utf8))
    }

    @Test("ARMED + fix → gotowe (nil)")
    func readyWhenArmedWithFix() {
        #expect(GotoReadiness.blockReason(frame(state: 1, armReason: 0, gpsFix: true)) == nil)
        #expect(GotoReadiness.isReady(frame(state: 1, armReason: 0, gpsFix: true)))
    }

    // Moc wyroczni: telemetria, która bez bramki by „przeciekła" (DISARMED, ale fix ok)
    // MUSI zwrócić powód, nie nil. Usunięcie sprawdzenia state → test FAILuje.
    @Test("DISARMED mimo fixu → powód, nie nil")
    func blocksWhenDisarmedEvenWithFix() {
        let reason = GotoReadiness.blockReason(frame(state: 0, armReason: 0, gpsFix: true))
        #expect(reason != nil)
    }

    @Test("arm_reason mapuje na czytelny powód")
    func mapsArmReason() {
        #expect(GotoReadiness.blockReason(frame(state: 0, armReason: 1, gpsFix: true)) == .noRC)
        #expect(GotoReadiness.blockReason(frame(state: 0, armReason: 2, gpsFix: true)) == .sticksNotNeutral)
        #expect(GotoReadiness.blockReason(frame(state: 0, armReason: 0, gpsFix: true)) == .notArmed)
    }

    @Test("ARMED bez fixu → brak fixu")
    func blocksWhenNoFix() {
        #expect(GotoReadiness.blockReason(frame(state: 1, armReason: 0, gpsFix: false)) == .noGpsFix)
    }

    @Test("każdy powód ma niepusty komunikat")
    func reasonsHaveMessages() {
        for r: GotoBlockReason in [.noRC, .sticksNotNeutral, .notArmed, .noGpsFix] {
            #expect(!r.message.isEmpty)
        }
    }
}
