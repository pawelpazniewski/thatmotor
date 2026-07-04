import Foundation

/// Stan maszyny stanów firmware (`state`, 0-4). Nieznane wartości → `.unknown`,
/// by przyszłe pola nie crashowały cienkiego klienta.
public enum SystemState: Int, Sendable {
    case disarmed = 0
    case armed = 1
    case failsafe = 2
    case escCalibration = 3
    case deploy = 4
    case unknown = -1
}

/// Powód, dla którego łódka nie jest ARMED (`arm_reason`, 0-4).
public enum ArmReason: Int, Sendable {
    case ready = 0
    case noRC = 1
    case throttleNotNeutral = 2
    case calibrating = 3
    case settingsApplying = 4
    case unknown = -1
}

/// Stan nawigacji goto (`goto_state`) i spot-lock (`spot_lock_state`).
public enum HoldState: Int, Sendable {
    case off = 0
    case active = 1
    case paused = 2
    case unknown = -1
}

/// Jedna ramka telemetrii WS (`GET /ws`, ~10 Hz). Dekoduje TYLKO pola
/// konsumowane przez apkę; nadmiarowe pola firmware są ignorowane.
public struct Telemetry: Decodable, Sendable {
    public let state: SystemState
    public let armReason: ArmReason
    public let rcValid: Bool
    public let gpsFix: Bool
    public let gpsSats: Int
    public let gpsLatE7: Int32
    public let gpsLonE7: Int32
    public let gpsSpeedCms: Int
    public let imuOk: Bool
    public let imuHeadingDeg10: Int
    public let gotoState: HoldState
    public let gotoTargetLatE7: Int32
    public let gotoTargetLonE7: Int32
    public let gotoErrM: Int
    public let gotoBearingDeg10: Int
    public let gotoArrived: Bool
    public let appLinkFresh: Bool
    public let spotLockState: HoldState
    public let imuCalib: Int
    public let spotLockErrM: Int
    public let spotLockBearingDeg10: Int
    public let servoTrimUs: Int

    private enum CodingKeys: String, CodingKey {
        case state, arm_reason, rc_valid, gps_fix, gps_sats
        case gps_lat_e7, gps_lon_e7, gps_speed_cms
        case imu_ok, imu_heading_deg10
        case goto_state, goto_target_lat_e7, goto_target_lon_e7
        case goto_err_m, goto_bearing_deg10, goto_arrived
        case app_link_fresh, spot_lock_state
        case imu_calib, spot_lock_err_m, spot_lock_bearing_deg10, servo_trim_us
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        state = SystemState(rawValue: try c.decode(Int.self, forKey: .state)) ?? .unknown
        armReason = ArmReason(rawValue: try c.decode(Int.self, forKey: .arm_reason)) ?? .unknown
        rcValid = try c.decode(Bool.self, forKey: .rc_valid)
        gpsFix = try c.decode(Bool.self, forKey: .gps_fix)
        gpsSats = try c.decode(Int.self, forKey: .gps_sats)
        gpsLatE7 = try c.decode(Int32.self, forKey: .gps_lat_e7)
        gpsLonE7 = try c.decode(Int32.self, forKey: .gps_lon_e7)
        gpsSpeedCms = try c.decode(Int.self, forKey: .gps_speed_cms)
        imuOk = try c.decode(Bool.self, forKey: .imu_ok)
        imuHeadingDeg10 = try c.decode(Int.self, forKey: .imu_heading_deg10)
        gotoState = HoldState(rawValue: try c.decode(Int.self, forKey: .goto_state)) ?? .unknown
        gotoTargetLatE7 = try c.decode(Int32.self, forKey: .goto_target_lat_e7)
        gotoTargetLonE7 = try c.decode(Int32.self, forKey: .goto_target_lon_e7)
        gotoErrM = try c.decode(Int.self, forKey: .goto_err_m)
        gotoBearingDeg10 = try c.decode(Int.self, forKey: .goto_bearing_deg10)
        gotoArrived = try c.decode(Bool.self, forKey: .goto_arrived)
        appLinkFresh = try c.decode(Bool.self, forKey: .app_link_fresh)
        spotLockState = HoldState(rawValue: try c.decode(Int.self, forKey: .spot_lock_state)) ?? .unknown
        // Nowe pola tolerują starszy firmware (brak klucza → 0).
        imuCalib = try c.decodeIfPresent(Int.self, forKey: .imu_calib) ?? 0
        spotLockErrM = try c.decodeIfPresent(Int.self, forKey: .spot_lock_err_m) ?? 0
        spotLockBearingDeg10 = try c.decodeIfPresent(Int.self, forKey: .spot_lock_bearing_deg10) ?? 0
        servoTrimUs = try c.decodeIfPresent(Int.self, forKey: .servo_trim_us) ?? 0
    }

    /// Kurs dzioba w stopniach (z `imu_heading_deg10`).
    public var headingDegrees: Double { Double(imuHeadingDeg10) / 10.0 }

    /// Prędkość nad dnem w m/s (z `gps_speed_cms`).
    public var speedMetersPerSecond: Double { Double(gpsSpeedCms) / 100.0 }

    /// Namiar do celu goto w stopniach (z `goto_bearing_deg10`).
    public var gotoBearingDegrees: Double { Double(gotoBearingDeg10) / 10.0 }

    /// Namiar spot-lock w stopniach (z `spot_lock_bearing_deg10`).
    public var spotLockBearingDegrees: Double { Double(spotLockBearingDeg10) / 10.0 }

    /// Pozycja łódki jako para stopni (z `gps_*_e7`).
    public var boatLatLon: LatLonE7 { LatLonE7(latE7: gpsLatE7, lonE7: gpsLonE7) }
}
