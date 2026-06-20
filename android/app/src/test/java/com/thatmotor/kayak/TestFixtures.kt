package com.thatmotor.kayak

import com.thatmotor.kayak.data.TelemetryFrame

/** Load a JSON fixture from `src/test/resources/` by name. */
fun loadFixture(name: String): String {
    val stream = object {}.javaClass.classLoader?.getResourceAsStream(name)
        ?: error("fixture not found on test classpath: $name")
    return stream.bufferedReader().use { it.readText() }
}

/**
 * Build a [TelemetryFrame] for tests with healthy DISARMED defaults; override only
 * the fields under test. Keeps the indicator/availability tests focused on the one
 * field that drives the assertion (oracle power).
 */
fun telemetryFrame(
    state: Int = 0,
    armReason: Int = 0,
    calibrated: Boolean = true,
    gpsFix: Boolean = true,
    gpsSats: Int = 9,
    gpsLatE7: Int = 520000000,
    gpsLonE7: Int = 210000000,
    gpsSpeedCms: Int = 0,
    imuOk: Boolean = true,
    imuHeadingDeg10: Int = 900,
    imuCalib: Int = 3,
): TelemetryFrame = TelemetryFrame(
    state = state,
    armReason = armReason,
    rcValid = true,
    ch1Us = 1500L,
    ch2Us = 1500L,
    ch3Us = 1500L,
    ch4Us = 1500L,
    ch1PeriodUs = 20000L,
    ch2PeriodUs = 20000L,
    ch1Valid = true,
    ch2Valid = true,
    servoUs = 1500L,
    escUs = 1500L,
    servoTrimUs = 0,
    source = 0,
    settingsValid = true,
    calibrated = calibrated,
    defaultsUsed = false,
    nvsError = false,
    gpsFix = gpsFix,
    gpsSats = gpsSats,
    gpsLatE7 = gpsLatE7,
    gpsLonE7 = gpsLonE7,
    gpsSpeedCms = gpsSpeedCms,
    imuOk = imuOk,
    imuHeadingDeg10 = imuHeadingDeg10,
    imuCalib = imuCalib,
)
