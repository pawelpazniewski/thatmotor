package com.thatmotor.kayak.ui.components

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.thatmotor.kayak.data.TelemetryFrame
import com.thatmotor.kayak.data.headingDeg10ToDegrees
import java.util.Locale

/**
 * Compass panel: heading and IMU calibration. Heading reads "—" when the IMU is not
 * OK (no fresh rotation vector) rather than a misleading 0° (mirrors the firmware
 * `imu_ok` contract and the boat-marker rule "no rotation when imu not ok").
 */
@Composable
fun CompassCard(frame: TelemetryFrame?, modifier: Modifier = Modifier) {
    InfoCard(title = "Compass", modifier = modifier) {
        if (frame == null) {
            LabeledValue(label = "Heading", value = "—")
            return@InfoCard
        }
        LabeledValue(label = "Heading", value = headingText(frame))
        LabeledValue(label = "IMU", value = if (frame.imuOk) "OK" else "no data")
        LabeledValue(label = "Calibration", value = "${frame.imuCalib}/3")
    }
}

private fun headingText(frame: TelemetryFrame): String {
    if (!frame.imuOk) return "—"
    return String.format(Locale.US, "%.1f°", headingDeg10ToDegrees(frame.imuHeadingDeg10))
}
