package com.thatmotor.kayak.map

import org.maplibre.android.style.expressions.Expression
import org.maplibre.android.style.layers.PropertyFactory
import org.maplibre.android.style.layers.SymbolLayer
import org.maplibre.android.style.sources.GeoJsonSource

/**
 * MapLibre adapter for the boat position marker: a single [GeoJsonSource] + [SymbolLayer]
 * (not the LocationComponent, which assumes the device's own GPS). The icon rotates by the
 * feature's `heading` property; the projection ([projectBoatPosition]) decides position and
 * whether a heading exists, so this adapter stays thin (Pure ⊥ HAL, learned-patterns).
 *
 * Updates go through [update] (which calls [GeoJsonSource.setGeoJson]); callers MUST invoke
 * it on the UI thread because MapLibre style mutations are not thread-safe.
 */
class BoatMarker(private val source: GeoJsonSource) {

    /**
     * Push a new [position] to the source. Call on the UI thread. A hidden position sends
     * an empty FeatureCollection, so the layer simply draws nothing — no add/remove churn.
     */
    fun update(position: BoatPosition) {
        source.setGeoJson(boatPositionToGeoJson(position))
    }

    companion object {
        /**
         * Create the boat source seeded empty (marker hidden until the first fix). The
         * caller adds it and [createLayer] to the style and then drives it via [update].
         */
        fun createSource(): GeoJsonSource =
            GeoJsonSource(MapIds.BOAT_SOURCE, boatPositionToGeoJson(BoatPosition.Hidden))

        /**
         * Build the symbol layer that draws [iconName] (a sprite in the offline style)
         * rotated by the feature's `heading`. `iconAllowOverlap`/`iconIgnorePlacement`
         * keep the single marker always visible regardless of label collision.
         */
        fun createLayer(iconName: String): SymbolLayer =
            SymbolLayer(MapIds.BOAT_LAYER, MapIds.BOAT_SOURCE).withProperties(
                PropertyFactory.iconImage(iconName),
                PropertyFactory.iconRotate(Expression.get("heading")),
                PropertyFactory.iconAllowOverlap(true),
                PropertyFactory.iconIgnorePlacement(true),
            )
    }
}
