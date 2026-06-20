package com.thatmotor.kayak.map

/**
 * Pure builder for the offline MapLibre style JSON, plus the stable source/layer IDs
 * the rest of the map package references. No Android/MapLibre dependency so the style
 * shape is host-testable on the JVM (Pure ⊥ HAL, learned-patterns); the MapLibre
 * adapter ([MapLibreView]) only feeds the produced JSON string into `Style.Builder`.
 *
 * The style references local archives by URI:
 *  - vector OSM via `pmtiles://` (native PMTiles support, MapLibre Android >= 11.7.0);
 *  - ortho raster via `mbtiles://` (a filesystem path — MBTiles cannot be read straight
 *    from an APK asset, so Unit 11 extracts it to internal storage first);
 *  - sprite and glyphs from the APK `assets/` directory via `asset://`.
 *
 * Layer order is explicit and load-bearing: OSM vector layers sit at the bottom, the
 * ortho raster goes ABOVE them (so toggling it on hides the vector beneath), and the
 * attribution/marker layers are added on top later by the live map.
 */

/** Stable identifiers shared by the style JSON and the runtime layer/source lookups. */
object MapIds {
    const val VECTOR_SOURCE = "osm-vector"
    const val RASTER_SOURCE = "ortho-raster"
    const val VECTOR_LAYER_BACKGROUND = "osm-background"
    const val VECTOR_LAYER_WATER = "osm-water"
    const val VECTOR_LAYER_ROADS = "osm-roads"
    const val RASTER_LAYER = "ortho"
    const val BOAT_SOURCE = "boat"
    const val BOAT_LAYER = "boat-marker"
}

/** Local archive locations, resolved by [MapAssets] / Unit 11 at runtime. */
data class OfflineStyleSources(
    /** `pmtiles://` URI of the OSM vector archive (e.g. `pmtiles://asset://maps/osm.pmtiles`). */
    val vectorPmtilesUri: String,
    /** `mbtiles://` URI of the ortho raster archive (a filesystem path). */
    val rasterMbtilesUri: String,
    /** `asset://` URI prefix (without extension) of the sprite sheet. */
    val spriteUri: String,
    /** `asset://` URI template of the glyph PBFs (with `{fontstack}`/`{range}`). */
    val glyphsUri: String,
)

/** A raster source's tile size; ortho WMTS tiles are 256 px. */
private const val RASTER_TILE_SIZE = 256

/** The ortho raster starts hidden; the operator toggles it via [LayerToggle]. */
private const val INITIAL_RASTER_VISIBILITY = "none"

/**
 * The mandatory OpenStreetMap attribution. ODbL requires visible credit; the live map
 * renders this string (Unit 8 attribution requirement / Unit 11 licensing notes).
 */
const val OSM_ATTRIBUTION = "© OpenStreetMap contributors"

/**
 * Build the offline style JSON from local [sources].
 *
 * The result is a MapLibre style document. Layer order, bottom→top:
 *  1. OSM vector background / water / roads (from the PMTiles source);
 *  2. the ortho raster (above the vector, initially hidden);
 * the boat marker layer is added on top at runtime by [BoatMarker].
 */
fun buildOfflineStyleJson(sources: OfflineStyleSources): String {
    val layers = listOf(
        vectorLayer(MapIds.VECTOR_LAYER_BACKGROUND, "background-fill", type = "fill"),
        vectorLayer(MapIds.VECTOR_LAYER_WATER, "water", type = "fill"),
        // `transportation` is LineString geometry — a fill layer would render nothing.
        vectorLayer(MapIds.VECTOR_LAYER_ROADS, "transportation", type = "line"),
        rasterLayer(),
    ).joinToString(",")

    return """
        {
          "version": 8,
          "name": "kayak-offline",
          "sprite": "${sources.spriteUri}",
          "glyphs": "${sources.glyphsUri}",
          "sources": {
            "${MapIds.VECTOR_SOURCE}": {
              "type": "vector",
              "url": "${sources.vectorPmtilesUri}",
              "attribution": "$OSM_ATTRIBUTION"
            },
            "${MapIds.RASTER_SOURCE}": {
              "type": "raster",
              "url": "${sources.rasterMbtilesUri}",
              "tileSize": $RASTER_TILE_SIZE
            }
          },
          "layers": [$layers]
        }
    """.trimIndent()
}

private fun vectorLayer(id: String, sourceLayer: String, type: String): String = """
    {
      "id": "$id",
      "type": "$type",
      "source": "${MapIds.VECTOR_SOURCE}",
      "source-layer": "$sourceLayer"
    }
""".trimIndent()

private fun rasterLayer(): String = """
    {
      "id": "${MapIds.RASTER_LAYER}",
      "type": "raster",
      "source": "${MapIds.RASTER_SOURCE}",
      "layout": { "visibility": "$INITIAL_RASTER_VISIBILITY" }
    }
""".trimIndent()
