package com.thatmotor.kayak.map

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.maplibre.android.style.layers.Property

class OfflineStyleBuilderTest {

    private val sources = OfflineStyleSources(
        vectorPmtilesUri = "pmtiles://asset://maps/osm.pmtiles",
        rasterMbtilesUri = "mbtiles:///data/user/0/app/files/ortho.mbtiles",
        spriteUri = "asset://style/sprite",
        glyphsUri = "asset://style/glyphs/{fontstack}/{range}.pbf",
    )

    @Test
    fun `builder emits both sources with their archive URIs`() {
        // Act
        val json = buildOfflineStyleJson(sources)

        // Assert: the two offline archive URIs must be wired into the source URLs.
        assertTrue("missing pmtiles vector url", json.contains("\"pmtiles://asset://maps/osm.pmtiles\""))
        assertTrue("missing mbtiles raster url", json.contains("\"mbtiles:///data/user/0/app/files/ortho.mbtiles\""))
        assertTrue("missing vector source id", json.contains("\"${MapIds.VECTOR_SOURCE}\""))
        assertTrue("missing raster source id", json.contains("\"${MapIds.RASTER_SOURCE}\""))
    }

    @Test
    fun `builder wires sprite and glyphs from assets`() {
        // Act
        val json = buildOfflineStyleJson(sources)

        // Assert
        assertTrue(json.contains("\"sprite\": \"asset://style/sprite\""))
        assertTrue(json.contains("\"glyphs\": \"asset://style/glyphs/{fontstack}/{range}.pbf\""))
    }

    @Test
    fun `raster layer is ordered above the vector layers`() {
        // Act
        val json = buildOfflineStyleJson(sources)

        // Assert: the ortho raster must come AFTER (above) the vector layers so toggling
        // it on covers the OSM vector. Oracle: a reversed order would put RASTER_LAYER first.
        val vectorIndex = json.indexOf("\"${MapIds.VECTOR_LAYER_BACKGROUND}\"")
        val rasterIndex = json.indexOf("\"${MapIds.RASTER_LAYER}\"")
        assertTrue("vector layer not found", vectorIndex >= 0)
        assertTrue("raster layer not found", rasterIndex >= 0)
        assertTrue("raster must be above vector", rasterIndex > vectorIndex)
    }

    @Test
    fun `roads layer is a line layer because transportation geometry is linear`() {
        // Act
        val json = buildOfflineStyleJson(sources)

        // Assert: the `transportation` source-layer is LineString geometry, so the roads
        // layer MUST be type "line". Oracle: a regression to "fill" renders no roads and
        // fails this test. Extract the roads layer object and check its declared type.
        val roadsAnchor = "\"${MapIds.VECTOR_LAYER_ROADS}\""
        val roadsIndex = json.indexOf(roadsAnchor)
        assertTrue("roads layer not found", roadsIndex >= 0)
        val roadsTypeIndex = json.indexOf("\"type\"", roadsIndex)
        val nextLayerIndex = json.indexOf("\"id\"", roadsIndex + roadsAnchor.length)
        val roadsObjectEnd = if (nextLayerIndex >= 0) nextLayerIndex else json.length
        assertTrue("roads layer has no type", roadsTypeIndex in 0 until roadsObjectEnd)
        assertTrue(
            "roads layer must be type line, not fill",
            json.substring(roadsTypeIndex, roadsObjectEnd).contains("\"line\""),
        )
    }

    @Test
    fun `raster starts hidden so the operator opts in`() {
        // Act
        val json = buildOfflineStyleJson(sources)

        // Assert: the ortho overlay defaults to none (vector OSM shows first).
        assertTrue(json.contains("\"visibility\": \"none\""))
    }

    @Test
    fun `OSM attribution is present on the vector source`() {
        // Act
        val json = buildOfflineStyleJson(sources)

        // Assert: ODbL requires visible credit.
        assertTrue(json.contains(OSM_ATTRIBUTION))
    }

    @Test
    fun `rasterVisibility maps the toggle to MapLibre constants`() {
        // Assert: oracle pins both branches (a constant return would fail one of them).
        assertEquals(Property.VISIBLE, rasterVisibility(visible = true))
        assertEquals(Property.NONE, rasterVisibility(visible = false))
    }
}
