package com.thatmotor.kayak.map

import com.thatmotor.kayak.data.TelemetryFrame
import com.thatmotor.kayak.data.coordinateE7ToDegrees
import com.thatmotor.kayak.data.headingDeg10ToDegrees

/**
 * Pure projection of a [TelemetryFrame] to what the boat marker needs: a longitude,
 * latitude and (optional) heading. No MapLibre/Android dependency so it is host-testable
 * (Pure ⊥ HAL, learned-patterns); [BoatMarker] turns this into a GeoJSON string.
 *
 * Two independent "unknown" cases are modelled explicitly rather than faked with 0:
 *  - no GPS fix → there is no position, so the marker must be hidden ([Hidden]);
 *  - GPS fix but `imu_ok = false` → we have a position but no trustworthy heading, so
 *    [Positioned.headingDeg] is null and the marker must not rotate (drawing 0° would
 *    falsely claim "pointing north").
 */
sealed interface BoatPosition {
    /** No usable GPS fix — the marker is not drawn. */
    data object Hidden : BoatPosition

    /**
     * A known position; [headingDeg] is the compass heading in [0,360) degrees, or null
     * when the IMU is not reporting a fresh, usable heading.
     */
    data class Positioned(
        val lon: Double,
        val lat: Double,
        val headingDeg: Double?,
    ) : BoatPosition
}

/**
 * Project [frame] onto a [BoatPosition].
 *
 * @return [BoatPosition.Hidden] when the frame has no GPS fix; otherwise a
 *   [BoatPosition.Positioned] with the converted lon/lat and a heading that is null
 *   unless the IMU is OK.
 */
fun projectBoatPosition(frame: TelemetryFrame): BoatPosition {
    if (!frame.gpsFix) return BoatPosition.Hidden
    return BoatPosition.Positioned(
        lon = coordinateE7ToDegrees(frame.gpsLonE7),
        lat = coordinateE7ToDegrees(frame.gpsLatE7),
        headingDeg = if (frame.imuOk) headingDeg10ToDegrees(frame.imuHeadingDeg10) else null,
    )
}

/**
 * Render a [BoatPosition] as a GeoJSON FeatureCollection string for the boat source.
 * A hidden position yields an empty collection (the SymbolLayer draws nothing). A known
 * heading is emitted as a `heading` property that the layer's `iconRotate(get("heading"))`
 * reads; an unknown heading omits the property so the icon stays unrotated.
 */
fun boatPositionToGeoJson(position: BoatPosition): String = when (position) {
    BoatPosition.Hidden -> EMPTY_FEATURE_COLLECTION
    is BoatPosition.Positioned -> {
        val properties = position.headingDeg?.let { """{"heading":$it}""" } ?: "{}"
        """
        {"type":"FeatureCollection","features":[
          {"type":"Feature",
           "geometry":{"type":"Point","coordinates":[${position.lon},${position.lat}]},
           "properties":$properties}
        ]}
        """.trimIndent()
    }
}

private const val EMPTY_FEATURE_COLLECTION = """{"type":"FeatureCollection","features":[]}"""
