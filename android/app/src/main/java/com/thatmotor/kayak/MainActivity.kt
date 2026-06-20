package com.thatmotor.kayak

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.lifecycle.lifecycleScope
import com.thatmotor.kayak.net.CommandApi
import com.thatmotor.kayak.net.EspHttpClient
import com.thatmotor.kayak.net.TelemetrySocket
import com.thatmotor.kayak.repository.TelemetryRepository
import com.thatmotor.kayak.ui.TelemetryScreen
import com.thatmotor.kayak.ui.TelemetryViewModel
import com.thatmotor.kayak.ui.theme.KayakTabletTheme

/**
 * Hosts the operational [TelemetryScreen].
 *
 * Phase 3 wires the telemetry chain against the process-default network (the AP
 * binding from Unit 2 / the foreground service in Phase 5 supply the bound `Network`
 * later). The OkHttp client is built with a null network so it relies on
 * `bindProcessToNetwork`; per-socket pinning is added when the connection layer is
 * integrated. Manual dependency wiring is deliberate — no DI framework for v1.
 */
class MainActivity : ComponentActivity() {

    private val httpClient by lazy { EspHttpClient.build(network = null) }
    private val telemetrySocket by lazy { TelemetrySocket(httpClient) }
    private val commandApi by lazy { CommandApi(httpClient) }
    private val repository by lazy {
        TelemetryRepository(scope = lifecycleScope, socket = telemetrySocket)
    }
    private val viewModel by lazy { TelemetryViewModel(repository, commandApi) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        repository.start()
        setContent {
            KayakTabletTheme {
                TelemetryScreen(viewModel = viewModel)
            }
        }
    }

    override fun onDestroy() {
        repository.stop()
        super.onDestroy()
    }
}
