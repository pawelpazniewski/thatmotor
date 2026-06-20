package com.thatmotor.kayak.map

import com.thatmotor.kayak.telemetryFrame
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BoatMarkerProjectionTest {

    @Test
    fun `a frame with a fix projects to converted lon lat and heading`() {
        // Arrange: 52.0N, 21.0E, heading 90.0 (deg10 = 900), IMU OK.
        val frame = telemetryFrame(
            gpsFix = true,
            gpsLatE7 = 520000000,
            gpsLonE7 = 210000000,
            imuOk = true,
            imuHeadingDeg10 = 900,
        )

        // Act
        val position = projectBoatPosition(frame)

        // Assert
        assertTrue(position is BoatPosition.Positioned)
        position as BoatPosition.Positioned
        assertEquals(21.0, position.lon, 1e-9)
        assertEquals(52.0, position.lat, 1e-9)
        assertEquals(90.0, position.headingDeg!!, 1e-9)
    }

    @Test
    fun `a fix in the western southern hemisphere projects negative lon lat`() {
        // Arrange: 34.6S, 58.4W (Buenos Aires) — both coordinates negative.
        val frame = telemetryFrame(
            gpsFix = true,
            gpsLatE7 = -346000000,
            gpsLonE7 = -584000000,
            imuOk = true,
            imuHeadingDeg10 = 900,
        )

        // Act
        val position = projectBoatPosition(frame)

        // Assert: the sign must survive the conversion (oracle: an abs()/clamp would fail).
        assertTrue(position is BoatPosition.Positioned)
        position as BoatPosition.Positioned
        assertEquals(-58.4, position.lon, 1e-9)
        assertEquals(-34.6, position.lat, 1e-9)

        // And the minus sign must reach the GeoJSON coordinates [lon, lat].
        val json = boatPositionToGeoJson(position)
        assertTrue(json.contains("[-58.4,-34.6]"))
    }

    @Test
    fun `no GPS fix hides the marker`() {
        // Arrange: gps_fix=false — there is no position to draw.
        val frame = telemetryFrame(gpsFix = false)

        // Act
        val position = projectBoatPosition(frame)

        // Assert: oracle — a Positioned result would leak stale/0 coordinates.
        assertEquals(BoatPosition.Hidden, position)
    }

    @Test
    fun `imu not ok yields a null heading rather than zero`() {
        // Arrange: valid fix but the IMU is not reporting — heading is unknown, not north.
        val frame = telemetryFrame(gpsFix = true, imuOk = false, imuHeadingDeg10 = 0)

        // Act
        val position = projectBoatPosition(frame)

        // Assert: heading must be null, not 0.0 (0.0 would falsely claim "pointing north").
        assertTrue(position is BoatPosition.Positioned)
        assertNull((position as BoatPosition.Positioned).headingDeg)
    }

    @Test
    fun `a hidden position serialises to an empty feature collection`() {
        // Act
        val json = boatPositionToGeoJson(BoatPosition.Hidden)

        // Assert: empty features so the SymbolLayer draws nothing.
        assertTrue(json.contains("\"features\":[]"))
    }

    @Test
    fun `a positioned heading is emitted as a heading property`() {
        // Arrange
        val position = BoatPosition.Positioned(lon = 21.0, lat = 52.0, headingDeg = 123.4)

        // Act
        val json = boatPositionToGeoJson(position)

        // Assert: coordinates are [lon, lat] (GeoJSON order) and heading drives iconRotate.
        assertTrue(json.contains("[21.0,52.0]"))
        assertTrue(json.contains("\"heading\":123.4"))
    }

    @Test
    fun `an unknown heading omits the heading property so the icon stays unrotated`() {
        // Arrange
        val position = BoatPosition.Positioned(lon = 21.0, lat = 52.0, headingDeg = null)

        // Act
        val json = boatPositionToGeoJson(position)

        // Assert: no heading property → iconRotate(get("heading")) yields no rotation.
        assertFalse(json.contains("\"heading\""))
        assertTrue(json.contains("\"properties\":{}"))
    }
}
