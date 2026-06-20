package com.thatmotor.kayak.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

private val DarkColors = darkColorScheme(
    primary = DeepWater,
    background = Surface,
    surface = Surface,
    onBackground = OnSurface,
    onSurface = OnSurface,
    error = DangerRed,
)

// v1 ships a single high-contrast dark scheme tuned for sunlight readability.
@Composable
fun KayakTabletTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColors,
        content = content,
    )
}
