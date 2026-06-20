package com.thatmotor.kayak.data

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * One telemetry snapshot pushed by the ESP32 over `WS /ws` (~10 Hz).
 *
 * Field names, types, units and ranges mirror the firmware contract EXACTLY
 * (`components/web_panel/src/ws_telemetry.c` :: `snapshot_to_json` and the
 * `control_loop_snapshot` struct in `components/control_loop/include/control_loop.h`).
 * Raw integer encodings are kept verbatim here; physical conversions live in
 * [TelemetryUnits] so the wire model stays a faithful 1:1 of the firmware JSON.
 *
 * Firmware integer widths are unsigned on the C side; Kotlin has no unsigned
 * primitives in the public model, so each field uses the smallest signed Kotlin
 * type that losslessly holds the firmware range:
 *  - C `uint32_t` (pulse widths, periods) → [Long] (max ~4.29e9 > Int range).
 *  - C `uint16_t` (`gps_speed_cms`, `imu_heading_deg10`) → [Int].
 *  - C `uint8_t` (`gps_sats`, `imu_calib`, `arm_reason`) → [Int].
 *  - C `int16_t` (`servo_trim_us`) → [Int]; C `int32_t` (`*_e7`) → [Int].
 */
@Serializable
data class TelemetryFrame(
    /** Control state machine state, 0..4 (see [com.thatmotor.kayak.domain.MotorState]). */
    val state: Int,
    /** Why arming is blocked, 0..4 (`sm_arm_reason`; 0 = READY). */
    @SerialName("arm_reason") val armReason: Int,
    /** Debounced RC validity. */
    @SerialName("rc_valid") val rcValid: Boolean,
    @SerialName("ch1_us") val ch1Us: Long,
    @SerialName("ch2_us") val ch2Us: Long,
    @SerialName("ch3_us") val ch3Us: Long,
    @SerialName("ch4_us") val ch4Us: Long,
    @SerialName("ch1_period_us") val ch1PeriodUs: Long,
    @SerialName("ch2_period_us") val ch2PeriodUs: Long,
    @SerialName("ch1_valid") val ch1Valid: Boolean,
    @SerialName("ch2_valid") val ch2Valid: Boolean,
    @SerialName("servo_us") val servoUs: Long,
    @SerialName("esc_us") val escUs: Long,
    /** Active signed servo neutral trim, microseconds. */
    @SerialName("servo_trim_us") val servoTrimUs: Int,
    /** Settings provenance (`settings_source`). */
    val source: Int,
    @SerialName("settings_valid") val settingsValid: Boolean,
    /** false → UNCALIBRATED safety indicator. */
    val calibrated: Boolean,
    @SerialName("defaults_used") val defaultsUsed: Boolean,
    @SerialName("nvs_error") val nvsError: Boolean,
    /** GPS has a usable fix. */
    @SerialName("gps_fix") val gpsFix: Boolean,
    /** Satellites used in the fix. */
    @SerialName("gps_sats") val gpsSats: Int,
    /** Latitude in degrees * 1e7 (negative for S). */
    @SerialName("gps_lat_e7") val gpsLatE7: Int,
    /** Longitude in degrees * 1e7 (negative for W). */
    @SerialName("gps_lon_e7") val gpsLonE7: Int,
    /** Ground speed in cm/s. */
    @SerialName("gps_speed_cms") val gpsSpeedCms: Int,
    /** Fresh rotation-vector data is flowing. */
    @SerialName("imu_ok") val imuOk: Boolean,
    /** Heading in degrees * 10, [0, 3599]. */
    @SerialName("imu_heading_deg10") val imuHeadingDeg10: Int,
    /** SH-2 accuracy / calibration status, 0..3. */
    @SerialName("imu_calib") val imuCalib: Int,
)
