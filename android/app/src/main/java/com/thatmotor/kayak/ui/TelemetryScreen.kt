package com.thatmotor.kayak.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.ui.components.CommandBar
import com.thatmotor.kayak.ui.components.CompassCard
import com.thatmotor.kayak.ui.components.GpsCard
import com.thatmotor.kayak.ui.components.SafetyBanner
import com.thatmotor.kayak.ui.components.StatusBadge
import com.thatmotor.kayak.ui.components.WebPanelLink
import com.thatmotor.kayak.ui.theme.OnSurface

/**
 * The operational telemetry screen: safety banners, link badge + web-panel shortcut,
 * GPS / compass cards and the command bar. Pure rendering — all decisions arrive via
 * [TelemetryViewModel.state] (host-tested derivations). Command outcomes surface as a
 * snackbar so a rejection never crashes the UI.
 */
@Composable
fun TelemetryScreen(viewModel: TelemetryViewModel, modifier: Modifier = Modifier) {
    val feedback by viewModel.feedback.collectAsStateWithLifecycle()
    val snackbarHostState = remember { SnackbarHostState() }

    LaunchedEffect(feedback) {
        val current = feedback ?: return@LaunchedEffect
        snackbarHostState.showSnackbar(feedbackMessage(current))
        viewModel.dismissFeedback()
    }

    Scaffold(
        modifier = modifier.fillMaxSize(),
        snackbarHost = { SnackbarHost(snackbarHostState) },
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            // Each section collects its own deduplicated slice so the 10 Hz frame
            // stream only recomposes the GPS/Compass cards, not the whole column.
            HeaderRow(connectionBadge = {
                val connection by viewModel.connection.collectAsStateWithLifecycle()
                StatusBadge(connection = connection)
            })
            SafetyBannerSection(viewModel)
            FrameCardsSection(viewModel)
            CommandBarSection(viewModel)
        }
    }
}

@Composable
private fun SafetyBannerSection(viewModel: TelemetryViewModel) {
    val indicators by viewModel.indicators.collectAsStateWithLifecycle()
    SafetyBanner(indicators = indicators)
}

@Composable
private fun FrameCardsSection(viewModel: TelemetryViewModel) {
    val frame by viewModel.latestFrame.collectAsStateWithLifecycle()
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        GpsCard(frame = frame, modifier = Modifier.weight(1f))
        CompassCard(frame = frame, modifier = Modifier.weight(1f))
    }
}

@Composable
private fun CommandBarSection(viewModel: TelemetryViewModel) {
    val availability by viewModel.availability.collectAsStateWithLifecycle()
    val isSending by viewModel.isSending.collectAsStateWithLifecycle()
    CommandBar(
        availability = availability,
        isSending = isSending,
        onCommand = { command: Command -> viewModel.sendCommand(command) },
    )
}

@Composable
private fun HeaderRow(connectionBadge: @Composable () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text = "Kayak Motor", color = OnSurface, fontWeight = FontWeight.Bold, fontSize = 22.sp)
        Row(
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            connectionBadge()
            WebPanelLink()
        }
    }
}

private fun feedbackMessage(feedback: CommandFeedback): String = when (feedback) {
    is CommandFeedback.Success -> feedback.message
    is CommandFeedback.Rejected -> "Rejected: ${feedback.message}"
    is CommandFeedback.TransportError -> feedback.message
}
