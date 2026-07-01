import Foundation
import Testing
@testable import KayakContract

@Suite("Dekodowanie ramki telemetrii WS")
struct TelemetryDecodingTests {
    /// Reprezentatywna ramka WS (podzbiór pól firmware + pola nadmiarowe, które
    /// muszą być tolerowane).
    private func decode(_ json: String) throws -> Telemetry {
        try JSONDecoder().decode(Telemetry.self, from: Data(json.utf8))
    }

    private let armedFrame = """
    {"state":1,"arm_reason":0,"rc_valid":true,"ch1_us":1500,"ch2_us":1500,
    "servo_us":1500,"esc_us":1000,"gps_fix":true,"gps_sats":9,
    "gps_lat_e7":522297000,"gps_lon_e7":210122000,"gps_speed_cms":150,
    "imu_ok":true,"imu_heading_deg10":1234,"spot_lock_state":0,
    "goto_state":1,"goto_target_lat_e7":522300000,"goto_target_lon_e7":210130000,
    "goto_err_m":42,"goto_bearing_deg10":905,"goto_arrived":false,
    "app_link_fresh":true}
    """

    @Test("dekoduje pola i skalowania")
    func decodesFieldsAndScaling() throws {
        let t = try decode(armedFrame)
        #expect(t.state == .armed)
        #expect(t.armReason == .ready)
        #expect(t.gpsFix)
        #expect(t.gpsSats == 9)
        #expect(t.gpsLatE7 == 522_297_000)
        #expect(t.gotoState == .active)
        #expect(t.gotoErrM == 42)
        // Skalowania:
        #expect(abs(t.headingDegrees - 123.4) < 1e-9)          // deg10 → deg
        #expect(abs(t.speedMetersPerSecond - 1.5) < 1e-9)      // cm/s → m/s
        #expect(abs(t.gotoBearingDegrees - 90.5) < 1e-9)
    }

    // Kontrakt "niezmienny", ale klient ma być odporny na nieznane wartości enumów.
    @Test("nieznany state → .unknown zamiast crashu")
    func unknownStateFallsBack() throws {
        let t = try decode("""
        {"state":9,"arm_reason":7,"rc_valid":false,"gps_fix":false,"gps_sats":0,
        "gps_lat_e7":0,"gps_lon_e7":0,"gps_speed_cms":0,"imu_ok":false,
        "imu_heading_deg10":0,"goto_state":5,"goto_target_lat_e7":0,
        "goto_target_lon_e7":0,"goto_err_m":0,"goto_bearing_deg10":0,
        "goto_arrived":false,"app_link_fresh":false,"spot_lock_state":0}
        """)
        #expect(t.state == .unknown)
        #expect(t.armReason == .unknown)
        #expect(t.gotoState == .unknown)
    }

    @Test("negatywne e7 (półkula S/W) dekodują się poprawnie")
    func negativeHemispheres() throws {
        let t = try decode("""
        {"state":0,"arm_reason":1,"rc_valid":false,"gps_fix":true,"gps_sats":5,
        "gps_lat_e7":-338688000,"gps_lon_e7":-1512093000,"gps_speed_cms":0,
        "imu_ok":true,"imu_heading_deg10":0,"goto_state":0,"goto_target_lat_e7":0,
        "goto_target_lon_e7":0,"goto_err_m":0,"goto_bearing_deg10":0,
        "goto_arrived":false,"app_link_fresh":false,"spot_lock_state":0}
        """)
        #expect(t.gpsLatE7 == -338_688_000)
        #expect(t.gpsLonE7 == -1_512_093_000)
    }
}
