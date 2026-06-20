package com.thatmotor.kayak

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import com.thatmotor.kayak.session.SessionPolicy
import com.thatmotor.kayak.session.SessionState
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
 *
 * The nav screen holds `FLAG_KEEP_SCREEN_ON` (Unit 10) so the display never dims while
 * the operator is watching telemetry on the water. The decision is delegated to the
 * host-tested [SessionPolicy] (Pure ⊥ HAL); this Activity only maps it to a window flag.
 */
class MainActivity : ComponentActivity() {

    private val viewModel: TelemetryViewModel by viewModels { TelemetryViewModelFactory() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        applyKeepScreenOn()
        setContent {
            KayakTabletTheme {
                TelemetryScreen(viewModel = viewModel)
            }
        }
    }

    private fun applyKeepScreenOn() {
        if (SessionPolicy.shouldKeepScreenOn(SessionState.ACTIVE)) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }
}
