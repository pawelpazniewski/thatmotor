package com.thatmotor.kayak.map

import androidx.lifecycle.Lifecycle
import com.thatmotor.kayak.data.TelemetryFrame
import org.maplibre.android.camera.CameraUpdateFactory
import org.maplibre.android.geometry.LatLng
import org.maplibre.android.maps.MapLibreMap
import org.maplibre.android.maps.MapView
import org.maplibre.android.maps.Style
import org.maplibre.android.style.sources.GeoJsonSource

/**
 * Imperative bridge between Compose state and the MapLibre [MapView]. Holds the mutable
 * view/map/style/marker references so [MapLibreView] (the Composable) stays declarative and
 * a single level of abstraction. Single responsibility: forward lifecycle + state to
 * MapLibre. All methods run on the UI thread (driven by Compose/lifecycle callbacks).
 */
class MapController {

    private var mapView: MapView? = null
    private var map: MapLibreMap? = null
    private var style: Style? = null
    private var boatMarker: BoatMarker? = null

    /** Latest state, applied once the style is ready (and re-applied on every update). */
    private var pendingOrtho = false

    /**
     * Hot-path guards (~10 Hz updates): the last values actually pushed to MapLibre, so we
     * skip the JNI layer/source mutations when nothing changed. [BoatPosition] is a data
     * class, so the `equals` comparison is free.
     */
    private var lastOrtho: Boolean? = null
    private var lastPosition: BoatPosition? = null

    fun attach(view: MapView) {
        mapView = view
    }

    /**
     * Style load: build the offline JSON, install the boat source/layer above the ortho
     * raster, and apply the initial ortho visibility.
     */
    fun onMapReady(readyMap: MapLibreMap, sources: OfflineStyleSources) {
        map = readyMap
        val styleJson = buildOfflineStyleJson(sources)
        readyMap.setStyle(Style.Builder().fromJson(styleJson)) { loaded ->
            loaded.addSource(BoatMarker.createSource())
            loaded.addLayer(BoatMarker.createLayer(MapAssets.BOAT_ICON))
            style = loaded
            val source: GeoJsonSource? = loaded.getSourceAs(MapIds.BOAT_SOURCE)
            boatMarker = source?.let { BoatMarker(it) }
            applyRasterVisible(loaded, pendingOrtho)
            lastOrtho = pendingOrtho
        }
    }

    /**
     * Apply the latest Compose state. No-op for marker/raster until the style has loaded
     * (a load race, not defensive code); ortho intent is remembered so it applies on load.
     */
    fun onState(latestFrame: TelemetryFrame?, showOrtho: Boolean, followBoat: Boolean) {
        pendingOrtho = showOrtho
        val loadedStyle = style ?: return

        if (showOrtho != lastOrtho) {
            applyRasterVisible(loadedStyle, showOrtho)
            lastOrtho = showOrtho
        }

        val position = latestFrame?.let { projectBoatPosition(it) } ?: BoatPosition.Hidden
        if (position != lastPosition) {
            boatMarker?.update(position)
            if (followBoat && position is BoatPosition.Positioned) {
                recenter(position)
            }
            lastPosition = position
        }
    }

    private fun recenter(position: BoatPosition.Positioned) {
        // LatLng is (latitude, longitude); keep zoom/bearing/tilt unchanged. easeCamera (not
        // moveCamera) animates over ~one frame period so 10 Hz follow glides instead of
        // teleporting. Only called on a real position change (guarded in onState).
        map?.easeCamera(
            CameraUpdateFactory.newLatLng(LatLng(position.lat, position.lon)),
            CAMERA_EASE_MS,
        )
    }

    fun onLifecycleEvent(event: Lifecycle.Event) {
        val view = mapView ?: return
        when (event) {
            Lifecycle.Event.ON_START -> view.onStart()
            Lifecycle.Event.ON_RESUME -> view.onResume()
            Lifecycle.Event.ON_PAUSE -> view.onPause()
            Lifecycle.Event.ON_STOP -> view.onStop()
            else -> Unit
        }
    }

    /** Forward a low-memory warning so MapLibre can trim its tile/GL caches. */
    fun onLowMemory() {
        mapView?.onLowMemory()
    }

    /** Final teardown on composition disposal; releases the GL surface and references. */
    fun destroy() {
        mapView?.onStop()
        mapView?.onDestroy()
        mapView = null
        map = null
        style = null
        boatMarker = null
        lastOrtho = null
        lastPosition = null
    }

    private companion object {
        /** Camera ease duration for follow mode (~one 10 Hz frame period). */
        const val CAMERA_EASE_MS = 100
    }
}
