package com.thatmotor.kayak.session

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.util.Log
import com.thatmotor.kayak.MainActivity
import com.thatmotor.kayak.R

/**
 * Foreground service that keeps the telemetry session (WebSocket + AP binding) alive
 * while the operator is on the water, even if the app is backgrounded
 * (`foregroundServiceType="connectedDevice"`). Phase 5 / Unit 10.
 *
 * Thin HAL adapter: every keep-alive decision lives in the host-tested [SessionPolicy]
 * (Pure ⊥ HAL); this class only maps those decisions onto framework primitives
 * (foreground notification, [PowerManager] wake lock) and — critically — guarantees
 * teardown of *all* held resources in [onDestroy]:
 *   - the session cleanup hook (repository `stop`, `unregisterNetworkCallback`,
 *     `bindProcessToNetwork(null)`) provided by the caller;
 *   - the partial wake lock (released so it never dangles — coding-rules pkt 13).
 *
 * The wake lock is acquired only when the screen is off (see
 * [SessionPolicy.shouldHoldWakeLock]); while the screen is on the CPU is already awake
 * via `FLAG_KEEP_SCREEN_ON`, so acquiring one would only waste battery.
 */
class TelemetryService : Service() {

    /** Binder exposing the session controls to the hosting Activity. */
    inner class LocalBinder : Binder() {
        val service: TelemetryService get() = this@TelemetryService
    }

    private val binder = LocalBinder()

    private val mainHandler = Handler(Looper.getMainLooper())

    /**
     * Cleanup hook for the session resources owned outside the service (telemetry
     * repository + AP connection callback). Set by the Activity after binding; invoked
     * exactly once on teardown. Kept here so the service is the single owner of
     * teardown sequencing without duplicating the AP/repository logic.
     */
    @Volatile
    var onSessionStopped: (() -> Unit)? = null

    private var wakeLock: PowerManager.WakeLock? = null
    private var sessionState: SessionState = SessionState.STOPPED

    /** Last status pushed to the notification; used to dedupe 10 Hz updates. */
    private var lastStatusText: String? = null

    /** True while the wake lock should be held; drives the periodic re-acquire. */
    @Volatile
    private var wantsWakeLock: Boolean = false

    /** Built once (channel + PendingIntent are stable for the service lifetime). */
    private val notificationManager: NotificationManager by lazy {
        getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    }
    private val contentIntent: PendingIntent by lazy {
        PendingIntent.getActivity(
            this,
            PENDING_INTENT_REQUEST_CODE,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE,
        )
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        ensureChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        sessionState = SessionPolicy.nextState(SessionEvent.STARTED)
        startSessionForeground()
        // Restart if the system kills us mid-session — the session must survive.
        return START_STICKY
    }

    private fun startSessionForeground() {
        val notification = buildNotification(STATUS_STARTING)
        // The connected-device foreground type must be passed explicitly from API 30+
        // (mandatory on targetSdk 34 / Android 14); API 29 starts without the type arg.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    /**
     * Refresh the foreground notification with the current link [statusText]. Deduped:
     * the telemetry layer maps a 4-value [com.thatmotor.kayak.domain.ConnectionState] at
     * ~10 Hz, but the text rarely changes, so identical updates are dropped to avoid
     * flooding the NotificationManager (binder IPC + Notification rebuild).
     */
    fun updateStatus(statusText: String) {
        if (!SessionPolicy.shouldUpdateStatus(sessionState)) return
        if (statusText == lastStatusText) return
        lastStatusText = statusText
        notificationManager.notify(NOTIFICATION_ID, buildNotification(statusText))
    }

    /**
     * Acquire or release the partial wake lock to match [SessionPolicy]. Called by the
     * Activity on screen on/off transitions. Idempotent and self-balancing: it never
     * leaks a second lock and never over-releases.
     */
    fun onScreenStateChanged(isScreenOn: Boolean) {
        if (SessionPolicy.shouldHoldWakeLock(sessionState, isScreenOn)) {
            wantsWakeLock = true
            acquireWakeLock()
            scheduleWakeLockReacquire()
        } else {
            wantsWakeLock = false
            mainHandler.removeCallbacks(reacquireRunnable)
            releaseWakeLock()
        }
    }

    private fun acquireWakeLock() {
        if (wakeLock?.isHeld == true) return
        val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, WAKE_LOCK_TAG).apply {
            setReferenceCounted(false)
            acquire(WAKE_LOCK_TIMEOUT_MS)
        }
    }

    /**
     * Re-acquire the lock before the [WAKE_LOCK_TIMEOUT_MS] safety-net expires, so a
     * long screen-off session (e.g. an overnight crossing) keeps the CPU awake and the
     * WebSocket alive. The timeout still bounds a leaked acquire — if the session ends,
     * [onScreenStateChanged]/[onDestroy] clears [wantsWakeLock] and cancels the loop.
     */
    private fun scheduleWakeLockReacquire() {
        mainHandler.removeCallbacks(reacquireRunnable)
        mainHandler.postDelayed(reacquireRunnable, WAKE_LOCK_REACQUIRE_MS)
    }

    private val reacquireRunnable = Runnable {
        if (wantsWakeLock) {
            // Refresh the timeout: release the still-held lock first so acquireWakeLock
            // (guarded on isHeld) actually takes a fresh one with a full timeout window.
            releaseWakeLock()
            acquireWakeLock()
            scheduleWakeLockReacquire()
        }
    }

    private fun releaseWakeLock() {
        wakeLock?.let { lock ->
            if (lock.isHeld) lock.release()
        }
        wakeLock = null
    }

    override fun onDestroy() {
        sessionState = SessionPolicy.nextState(SessionEvent.STOPPED)
        wantsWakeLock = false
        mainHandler.removeCallbacks(reacquireRunnable)
        releaseWakeLock()
        // Tear down the externally-owned session resources exactly once.
        val cleanup = onSessionStopped
        onSessionStopped = null
        try {
            cleanup?.invoke()
        } catch (e: RuntimeException) {
            // Never let a cleanup failure prevent the service from finishing teardown.
            Log.e(TAG, "Session cleanup failed", e)
        } finally {
            // super.onDestroy() must run even if cleanup throws an Error/Throwable,
            // otherwise the service finishes uncleanly.
            super.onDestroy()
        }
    }

    private fun buildNotification(statusText: String): Notification =
        Notification.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.session_notification_title))
            .setContentText(statusText)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(contentIntent)
            .setOngoing(true)
            .build()

    private fun ensureChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            getString(R.string.session_notification_channel),
            NotificationManager.IMPORTANCE_LOW,
        ).apply { setShowBadge(false) }
        notificationManager.createNotificationChannel(channel)
    }

    companion object {
        private const val TAG = "TelemetryService"
        private const val CHANNEL_ID = "telemetry_session"
        private const val NOTIFICATION_ID = 1
        private const val PENDING_INTENT_REQUEST_CODE = 0
        private const val WAKE_LOCK_TAG = "kayak:telemetry-session"
        private const val STATUS_STARTING = "Starting session…"

        /**
         * Safety net so a crashed/leaked acquire cannot drain the battery forever; a
         * real session releases the lock far sooner via [onScreenStateChanged] /
         * [onDestroy]. While the session legitimately needs it, the lock is re-acquired
         * before this expires (see [scheduleWakeLockReacquire]).
         */
        private const val WAKE_LOCK_TIMEOUT_MS = 4L * 60 * 60 * 1000

        /**
         * Re-acquire interval, comfortably shorter than [WAKE_LOCK_TIMEOUT_MS] so the
         * lock never lapses during a long screen-off session (overnight crossing).
         */
        private const val WAKE_LOCK_REACQUIRE_MS = 3L * 60 * 60 * 1000

        /** Intent addressing this service; used for start/stop/bind. */
        fun intent(context: Context): Intent = Intent(context, TelemetryService::class.java)

        /** Start the service in the foreground with the connected-device type. */
        fun start(context: Context) {
            val intent = intent(context)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        /** Stop the service, triggering full cleanup in [onDestroy]. */
        fun stop(context: Context) {
            context.stopService(intent(context))
        }
    }
}
