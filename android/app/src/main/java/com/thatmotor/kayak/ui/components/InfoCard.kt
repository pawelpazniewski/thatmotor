package com.thatmotor.kayak.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.thatmotor.kayak.ui.theme.OnSurface
import com.thatmotor.kayak.ui.theme.OnSurfaceMuted
import com.thatmotor.kayak.ui.theme.SurfaceRaised

/**
 * Shared card scaffold for the telemetry panels: a bold title over a column of
 * [LabeledValue] rows on a raised, high-contrast surface. Extracted so GpsCard /
 * CompassCard stay focused on their data (coding-rules pkt 3: shared layout once).
 */
@Composable
fun InfoCard(title: String, modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Card(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = SurfaceRaised),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = title,
                color = OnSurface,
                fontWeight = FontWeight.Bold,
                fontSize = 18.sp,
            )
            content()
        }
    }
}

/** One label/value row inside an [InfoCard], high contrast for sunlight legibility. */
@Composable
fun LabeledValue(label: String, value: String, modifier: Modifier = Modifier) {
    Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(text = label, color = OnSurfaceMuted, fontSize = 16.sp)
        Text(text = value, color = OnSurface, fontWeight = FontWeight.SemiBold, fontSize = 16.sp)
    }
}
