package com.thatmotor.kayak.map

import java.io.File

/**
 * Resolves the local map archive locations into the URIs the style JSON expects.
 *
 * Archive placement (see android/maps/README.md, Unit 11):
 *  - the OSM PMTiles ships inside the APK `assets/` (read straight via `pmtiles://asset://`);
 *  - the ortho MBTiles is too large / must be a real file, so Unit 11's tooling places it
 *    in app internal storage and we point `mbtiles://` at that filesystem path;
 *  - sprite/glyphs ship in `assets/style/` and load via `asset://`.
 *
 * Building the URIs is pure (no Android Context), so it is host-testable; the only Android
 * concern (where internal storage lives) is passed in as [internalFilesDir].
 */
object MapAssets {

    const val OSM_PMTILES_ASSET = "maps/osm.pmtiles"
    const val ORTHO_MBTILES_FILE = "ortho.mbtiles"
    const val SPRITE_ASSET = "style/sprite"
    const val GLYPHS_ASSET = "style/glyphs/{fontstack}/{range}.pbf"

    /** Sprite icon id (declared in the sprite sheet) used for the boat marker. */
    const val BOAT_ICON = "boat"

    /**
     * Build the [OfflineStyleSources] for a given [internalFilesDir] (the app's
     * `filesDir`). The ortho MBTiles is referenced by its absolute filesystem path.
     */
    fun sources(internalFilesDir: File): OfflineStyleSources = OfflineStyleSources(
        vectorPmtilesUri = "pmtiles://asset://$OSM_PMTILES_ASSET",
        rasterMbtilesUri = "mbtiles://${File(internalFilesDir, ORTHO_MBTILES_FILE).absolutePath}",
        spriteUri = "asset://$SPRITE_ASSET",
        glyphsUri = "asset://$GLYPHS_ASSET",
    )
}
