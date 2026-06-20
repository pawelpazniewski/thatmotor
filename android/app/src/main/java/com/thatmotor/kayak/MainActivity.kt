package com.thatmotor.kayak

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.thatmotor.kayak.net.ApConnectionManager
import com.thatmotor.kayak.session.SessionEvent
import com.thatmotor.kayak.session.SessionPolicy
import com.thatmotor.kayak.session.SessionState
import com.thatmotor.kayak.session.TelemetryService
import com.thatmotor.kayak.ui.TelemetryScreen
import com.thatmotor.kayak.ui.TelemetryViewModel
import com.thatmotor.kayak.ui.TelemetryViewModelFactory
import com.thatmotor.kayak.ui.theme.KayakTabletTheme
import kotlinx.coroutines.launch

/**
 * Hosts the operational [TelemetryScreen] and owns the session lifecycle (Phase 5 /
 * Unit 10).
 *
 * The [TelemetryService] foreground service keeps the telemetry WebSocket + AP binding
 * alive across background→foreground transitions. This Activity is the only caller that
 * drives it:
 *  - starts the foreground service and binds to it in [onStart];
 *  - feeds the live [TelemetryViewModel.connection] phase into the notification
 *    ([TelemetryService.updateStatus]);
 *  - relays screen on/off (Activity foreground/background) to the wake-lock policy
 *    ([TelemetryService.onScreenStateChanged]) via [onStart]/[onStop];
 *  - sets the teardown hook ([TelemetryService.onSessionStopped]) so the session is
 *    cleaned up exactly once when the service is destroyed
 *    (`stop` → `unregisterNetworkCallback` + `bindProcessToNetwork(null)`).
 *
 * The nav-screen window holds `FLAG_KEEP_SCREEN_ON` while the session is active; the
 * decision is delegated to the host-tested [SessionPolicy] (Pure ⊥ HAL).
 */
class MainActivity : ComponentActivity() {

    private val viewModel: TelemetryViewModel by viewModels { TelemetryViewModelFactory() }

    private val apConnectionManager: ApConnectionManager by lazy {
        ApConnectionManager(
            requireNotNull(getSystemService(ConnectivityManager::class.java)) {
                "ConnectivityManager unavailable"
            },
        )
    }

    private var service: TelemetryService? = null
    private var isBound: Boolean = false
    private var sessionState: SessionState = SessionState.STOPPED

    private val notificationPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { /* status optional */ }

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val bound = (binder as? TelemetryService.LocalBinder)?.service ?: return
            service = bound
            // Teardown hook: invoked once when the service is destroyed. Stops the
            // telemetry source and releases the AP binding
            // (unregisterNetworkCallback + bindProcessToNetwork(null)).
            bound.onSessionStopped = {
                viewModel.stopSession()
                apConnectionManager.disconnect()
            }
            // Screen is on while bound from a foregrounded Activity.
            bound.onScreenStateChanged(isScreenOn = true)
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            service = null
            isBound = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        requestNotificationPermissionIfNeeded()
        collectConnectionStatus()
        setContent {
            KayakTabletTheme {
                TelemetryScreen(viewModel = viewModel)
            }
        }
    }

    override fun onStart() {
        super.onStart()
        sessionState = SessionPolicy.nextState(SessionEvent.STARTED)
        applyKeepScreenOn()
        TelemetryService.start(this)
        // Binding is async; the screen-on hand-off happens in onServiceConnected once
        // the binder is available. Track the bind result so onStop only unbinds a
        // connection that actually registered (a failed bind would otherwise make
        // unbindService throw IllegalArgumentException).
        isBound = bindService(
            TelemetryService.intent(this),
            serviceConnection,
            Context.BIND_AUTO_CREATE,
        )
    }

    override fun onStop() {
        // Activity backgrounded → screen no longer guaranteed on; hand off keep-alive to
        // the wake lock per SessionPolicy (active + screen off → hold lock).
        service?.onScreenStateChanged(isScreenOn = false)
        if (isBound) {
            unbindService(serviceConnection)
            isBound = false
        }
        service = null
        super.onStop()
    }

    override fun onDestroy() {
        // A real exit (not a configuration change) ends the session and tears down the
        // foreground service, which triggers onSessionStopped → full cleanup.
        if (SessionPolicy.shouldTearDownSession(isFinishing)) {
            sessionState = SessionPolicy.nextState(SessionEvent.STOPPED)
            applyKeepScreenOn()
            TelemetryService.stop(this)
        }
        super.onDestroy()
    }

    private fun collectConnectionStatus() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.connection.collect { connection ->
                    service?.updateStatus(SessionPolicy.notificationStatusFor(connection))
                }
            }
        }
    }

    private fun requestNotificationPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return
        val granted = ContextCompat.checkSelfPermission(
            this,
            Manifest.permission.POST_NOTIFICATIONS,
        ) == PackageManager.PERMISSION_GRANTED
        if (!granted) {
            notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    private fun applyKeepScreenOn() {
        if (SessionPolicy.shouldKeepScreenOn(sessionState)) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }
}
