package com.thatmotor.kayak

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import com.thatmotor.kayak.ui.TelemetryScreen
import com.thatmotor.kayak.ui.TelemetryViewModel
import com.thatmotor.kayak.ui.TelemetryViewModelFactory
import com.thatmotor.kayak.ui.theme.KayakTabletTheme

/**
 * Hosts the operational [TelemetryScreen].
 *
 * Phase 3 wires the telemetry chain against the process-default network (the AP
 * binding from Unit 2 / the foreground service in Phase 5 supply the bound `Network`
 * later). The OkHttp client is built with a null network so it relies on
 * `bindProcessToNetwork`; per-socket pinning is added when the connection layer is
 * integrated. Manual dependency wiring is deliberate — no DI framework for v1.
 *
 * The ViewModel is obtained through [viewModels] so it (and its repository, started
 * once in [TelemetryViewModel.init]) survives configuration changes (rotation /
 * window resize on a tablet) instead of being torn down and re-`start()`-ed.
 */
class MainActivity : ComponentActivity() {

    private val viewModel: TelemetryViewModel by viewModels { TelemetryViewModelFactory() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            KayakTabletTheme {
                TelemetryScreen(viewModel = viewModel)
            }
        }
    }
}
