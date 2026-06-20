package com.thatmotor.kayak.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.thatmotor.kayak.domain.ConnectionState
import com.thatmotor.kayak.ui.theme.DangerRed
import com.thatmotor.kayak.ui.theme.OnSurface
import com.thatmotor.kayak.ui.theme.SafeGreen
import com.thatmotor.kayak.ui.theme.WarnAmber

/**
 * Compact pill showing the link [ConnectionState]. High-contrast solid fill so the
 * status reads at a glance in sunlight (project UX: sunlight readability).
 */
@Composable
fun StatusBadge(connection: ConnectionState, modifier: Modifier = Modifier) {
    val (label, color) = badgeContent(connection)
    Text(
        text = label,
        color = OnSurface,
        fontWeight = FontWeight.Bold,
        fontSize = 16.sp,
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(color)
            .padding(horizontal = 14.dp, vertical = 6.dp),
    )
}

private fun badgeContent(connection: ConnectionState): Pair<String, Color> = when (connection) {
    ConnectionState.Live -> "LIVE" to SafeGreen
    ConnectionState.Connecting -> "CONNECTING" to WarnAmber
    ConnectionState.Stale -> "LINK STALE" to DangerRed
    ConnectionState.Disconnected -> "DISCONNECTED" to DangerRed
}
