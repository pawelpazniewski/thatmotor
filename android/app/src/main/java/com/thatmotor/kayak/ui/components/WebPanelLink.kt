package com.thatmotor.kayak.ui.components

import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.util.Log
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext

/** Web panel base URL on the ESP32 SoftAP (R8: configuration stays on the web panel). */
const val WEB_PANEL_URL = "http://192.168.4.1"

/**
 * Shortcut that opens the ESP32 web panel (parameters / ESC calibration) in the
 * browser. v1 keeps configuration on the web panel, so this is the bridge from the
 * operational tablet UI to it (plan R8). Failure to resolve a browser is logged, not
 * crashed (coding-rules pkt 4).
 */
@Composable
fun WebPanelLink(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    OutlinedButton(
        onClick = {
            try {
                context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(WEB_PANEL_URL)))
            } catch (e: ActivityNotFoundException) {
                Log.w("WebPanelLink", "No browser to open $WEB_PANEL_URL", e)
            }
        },
        modifier = modifier,
    ) {
        Text(text = "Web panel")
    }
}
