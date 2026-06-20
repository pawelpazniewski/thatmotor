package com.thatmotor.kayak.map

import org.maplibre.android.maps.Style
import org.maplibre.android.style.layers.Property
import org.maplibre.android.style.layers.PropertyFactory
import org.maplibre.android.style.layers.RasterLayer

/**
 * Toggles the ortho raster overlay on the loaded [Style]. The visibility decision is a
 * pure function ([rasterVisibility]) so it is host-testable; the MapLibre side effect
 * ([applyRasterVisible]) stays a thin adapter (Pure ⊥ HAL, learned-patterns).
 */

/**
 * Map a desired on/off state to the MapLibre `visibility` constant.
 *
 * @return [Property.VISIBLE] when [visible], else [Property.NONE].
 */
fun rasterVisibility(visible: Boolean): String =
    if (visible) Property.VISIBLE else Property.NONE

/**
 * Apply the ortho raster visibility on [style]. No-op (returns false) if the style has
 * no raster layer yet (e.g. called before the style finished loading) — a guard, not
 * defensive over-engineering, because the toggle and style load race on separate UI ticks.
 *
 * @return true if the layer was found and updated.
 */
fun applyRasterVisible(style: Style, visible: Boolean): Boolean {
    val layer = style.getLayer(MapIds.RASTER_LAYER) as? RasterLayer ?: return false
    layer.setProperties(PropertyFactory.visibility(rasterVisibility(visible)))
    return true
}
