package com.thatmotor.kayak.map

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class MapAssetsTest {

    @Test
    fun `sources wires the four archive URIs with the right schemes`() {
        // Arrange: a stand-in internal files dir (the only Android-specific input).
        val filesDir = File("/data/user/0/com.thatmotor.kayak/files")

        // Act
        val sources = MapAssets.sources(filesDir)

        // Assert: the OSM vector reads straight from the APK asset via pmtiles://asset://.
        assertEquals(
            "pmtiles://asset://${MapAssets.OSM_PMTILES_ASSET}",
            sources.vectorPmtilesUri,
        )
        // Sprite/glyphs ship in assets and load via asset://.
        assertEquals("asset://${MapAssets.SPRITE_ASSET}", sources.spriteUri)
        assertEquals("asset://${MapAssets.GLYPHS_ASSET}", sources.glyphsUri)
    }

    @Test
    fun `ortho raster uri is an absolute filesystem path under the files dir`() {
        // Arrange
        val filesDir = File("/tmp/x")

        // Act
        val sources = MapAssets.sources(filesDir)

        // Assert: MBTiles cannot be read from an APK asset, so the URI must be the absolute
        // filesystem path under the files dir — oracle: dropping absolutePath or swapping the
        // scheme breaks one of these.
        assertTrue(
            "raster uri must use the mbtiles scheme",
            sources.rasterMbtilesUri.startsWith("mbtiles://"),
        )
        val expectedPath = File(filesDir, MapAssets.ORTHO_MBTILES_FILE).absolutePath
        assertEquals("mbtiles://$expectedPath", sources.rasterMbtilesUri)
        assertTrue(
            "raster path must contain the files dir absolute path",
            sources.rasterMbtilesUri.contains(filesDir.absolutePath),
        )
    }
}
