package com.thatmotor.kayak.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.thatmotor.kayak.domain.SafetyIndicators
import com.thatmotor.kayak.ui.theme.DangerRed
import com.thatmotor.kayak.ui.theme.OnSurface
import com.thatmotor.kayak.ui.theme.SafeGreen
import com.thatmotor.kayak.ui.theme.WarnAmber

/** A single banner line: text + fill colour. Internal so ordering stays testable-by-eye. */
private data class Banner(val text: String, val color: Color)

/**
 * Stack of the active safety banners, most critical first. Each [SafetyIndicators]
 * flag maps to one banner; several may show at once (e.g. LINK DOWN over ARMED).
 *
 * Severity order: FAILSAFE → LINK DOWN → ARMED → UNCALIBRATED. When nothing is
 * active a calm "DISARMED — drive stopped" line confirms the safe state rather than
 * leaving the operator guessing.
 */
@Composable
fun SafetyBanner(indicators: SafetyIndicators, modifier: Modifier = Modifier) {
    val banners = activeBanners(indicators)
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        banners.forEach { banner -> BannerRow(banner) }
    }
}

@Composable
private fun BannerRow(banner: Banner) {
    Text(
        text = banner.text,
        color = OnSurface,
        fontWeight = FontWeight.Bold,
        fontSize = 20.sp,
        textAlign = TextAlign.Center,
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .background(banner.color)
            .padding(vertical = 12.dp),
    )
}

private fun activeBanners(indicators: SafetyIndicators): List<Banner> {
    val banners = buildList {
        if (indicators.failsafe) add(Banner("FAILSAFE — DRIVE STOPPED", DangerRed))
        if (indicators.linkDown) add(Banner("LINK DOWN", DangerRed))
        if (indicators.armed) add(Banner("ARMED", WarnAmber))
        if (indicators.uncalibrated) add(Banner("UNCALIBRATED", WarnAmber))
    }
    return banners.ifEmpty { listOf(Banner("DISARMED — drive stopped", SafeGreen)) }
}
