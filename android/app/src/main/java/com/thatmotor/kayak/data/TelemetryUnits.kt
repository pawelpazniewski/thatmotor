package com.thatmotor.kayak.data

/**
 * Pure conversions from the firmware's fixed-point wire encodings to physical
 * units. Kept separate from [TelemetryFrame] so the wire model is a faithful 1:1
 * mirror of the firmware JSON and the conversions are independently host-testable
 * (Pure ⊥ HAL, learned-patterns).
 */

private const val E7_SCALE = 1e7
private const val DEG10_SCALE = 10.0
private const val CMS_PER_MS = 100.0

/** `gps_lat_e7` / `gps_lon_e7` (degrees * 1e7) → decimal degrees. */
fun coordinateE7ToDegrees(e7: Int): Double = e7 / E7_SCALE

/** `imu_heading_deg10` (degrees * 10, [0,3599]) → degrees in [0.0, 359.9]. */
fun headingDeg10ToDegrees(deg10: Int): Double = deg10 / DEG10_SCALE

/** `gps_speed_cms` (cm/s) → m/s. */
fun speedCmsToMetersPerSecond(cms: Int): Double = cms / CMS_PER_MS
