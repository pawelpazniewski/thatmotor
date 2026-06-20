package com.thatmotor.kayak.ui.components

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.thatmotor.kayak.data.TelemetryFrame
import com.thatmotor.kayak.data.coordinateE7ToDegrees
import com.thatmotor.kayak.data.speedCmsToMetersPerSecond
import java.util.Locale

/**
 * GPS panel: fix status, satellites, position and ground speed. Pulls physical
 * units from the host-tested [coordinateE7ToDegrees] / [speedCmsToMetersPerSecond]
 * conversions; renders placeholders when there is no frame or no fix.
 */
@Composable
fun GpsCard(frame: TelemetryFrame?, modifier: Modifier = Modifier) {
    InfoCard(title = "GPS", modifier = modifier) {
        if (frame == null) {
            LabeledValue(label = "Fix", value = "—")
            return@InfoCard
        }
        LabeledValue(label = "Fix", value = if (frame.gpsFix) "YES" else "NO")
        LabeledValue(label = "Satellites", value = frame.gpsSats.toString())
        LabeledValue(label = "Position", value = positionText(frame))
        LabeledValue(label = "Speed", value = speedText(frame))
    }
}

private fun positionText(frame: TelemetryFrame): String {
    if (!frame.gpsFix) return "no fix"
    val lat = coordinateE7ToDegrees(frame.gpsLatE7)
    val lon = coordinateE7ToDegrees(frame.gpsLonE7)
    return String.format(Locale.US, "%.5f, %.5f", lat, lon)
}

private fun speedText(frame: TelemetryFrame): String =
    String.format(Locale.US, "%.1f m/s", speedCmsToMetersPerSecond(frame.gpsSpeedCms))
