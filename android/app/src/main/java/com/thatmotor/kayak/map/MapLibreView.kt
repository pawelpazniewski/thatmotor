package com.thatmotor.kayak.map

import android.content.ComponentCallbacks2
import android.content.res.Configuration
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.thatmotor.kayak.data.TelemetryFrame
import org.maplibre.android.MapLibre
import org.maplibre.android.maps.MapView

/**
 * Hosts a MapLibre [MapView] inside Compose. Bridges the Android view lifecycle to the
 * MapView's own lifecycle callbacks via [DisposableEffect] + [LifecycleEventObserver] so
 * the GL surface is created/destroyed and memory trimmed at the right times (without this,
 * returning from background renders a black map — Unit 8 acceptance criterion).
 *
 * Decisions live in pure functions ([buildOfflineStyleJson], [projectBoatPosition],
 * [rasterVisibility]); this Composable is the thin HAL adapter that feeds MapLibre.
 *
 * @param sources         resolved offline archive URIs (from [MapAssets]).
 * @param latestFrame     latest telemetry frame (or null); drives the boat marker.
 * @param showOrtho       whether the ortho raster overlay is visible.
 * @param followBoat      when true, the camera recenters on the boat as it moves.
 */
@Composable
fun MapLibreView(
    sources: OfflineStyleSources,
    latestFrame: TelemetryFrame?,
    showOrtho: Boolean,
    followBoat: Boolean,
    modifier: Modifier = Modifier,
) {
    val controller = remember { MapController() }
    MapLifecycle(controller)

    AndroidView(
        modifier = modifier,
        factory = { context ->
            MapLibre.getInstance(context)
            MapView(context).also { view ->
                controller.attach(view)
                view.onCreate(null)
                view.getMapAsync { map -> controller.onMapReady(map, sources) }
            }
        },
        update = {
            controller.onState(latestFrame = latestFrame, showOrtho = showOrtho, followBoat = followBoat)
        },
    )
}

/**
 * Bridge the composition lifecycle to the MapView callbacks and tear the view down on
 * disposal. Also forwards low-memory warnings (delivered via [ComponentCallbacks2], not the
 * lifecycle) so MapLibre can trim its GL/tile caches. Kept separate so [MapLibreView] stays
 * a single level of abstraction.
 */
@Composable
private fun MapLifecycle(controller: MapController) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val context = LocalContext.current
    DisposableEffect(lifecycleOwner, context) {
        val observer = LifecycleEventObserver { _, event -> controller.onLifecycleEvent(event) }
        lifecycleOwner.lifecycle.addObserver(observer)

        val memoryCallback = object : ComponentCallbacks2 {
            override fun onConfigurationChanged(newConfig: Configuration) = Unit
            @Deprecated("required by ComponentCallbacks2") override fun onLowMemory() =
                controller.onLowMemory()
            override fun onTrimMemory(level: Int) = controller.onLowMemory()
        }
        context.registerComponentCallbacks(memoryCallback)

        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            context.unregisterComponentCallbacks(memoryCallback)
            controller.destroy()
        }
    }
}
