import Foundation
import Testing
@testable import KayakContract

@Suite("Bramka gotowości spot-locka (kotwicy)")
struct SpotLockReadinessTests {
    /// Ramka telemetrii z polami istotnymi dla readiness.
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

    @Test("ARMED + fix → można zakotwiczyć")
    func engagesWhenArmedWithFix() {
        let t = frame(state: 1, armReason: 0, gpsFix: true)
        #expect(SpotLockReadiness.canEngage(t))
        #expect(SpotLockReadiness.blockReason(t) == nil)
    }

    // Moc wyroczni: ARMED, ale bez fixu (seed-fresh telemetria, która bez bramki fix
    // by „przeciekła") MUSI zablokować z powodem „brak GPS". Usunięcie bramki fixu → FAIL.
    @Test("ARMED bez fixu → zablokowane, powód brak GPS")
    func blocksWhenNoFix() {
        let t = frame(state: 1, armReason: 0, gpsFix: false)
        #expect(!SpotLockReadiness.canEngage(t))
        #expect(SpotLockReadiness.blockReason(t) == .noGpsFix)
    }

    @Test("brak telemetrii (nil) → zablokowane")
    func blocksWhenNoTelemetry() {
        #expect(!SpotLockReadiness.canEngage(nil))
        #expect(SpotLockReadiness.blockReason(nil) != nil)
    }

    @Test("DISARMED mimo fixu → zablokowane (spot-lock tylko w ARMED, R9)")
    func blocksWhenDisarmed() {
        #expect(!SpotLockReadiness.canEngage(frame(state: 0, armReason: 1, gpsFix: true)))
    }
}
