package com.thatmotor.kayak.net

import android.net.Network
import java.util.concurrent.TimeUnit
import okhttp3.OkHttpClient

/**
 * Builds the OkHttp client used for REST + WebSocket against the ESP32 SoftAP.
 *
 * Thin HAL adapter. The socket factory is pinned to the bound AP [Network] so
 * traffic reaches `192.168.4.1` even if `bindProcessToNetwork` is unreliable on a
 * device (plan: per-socket fallback). `pingInterval` keeps the WS alive and lets
 * OkHttp detect a dead peer; a short connect/read timeout suits a LAN-only AP.
 */
object EspHttpClient {
    /** WS keep-alive ping cadence (plan: 5–10 s). */
    private const val PING_INTERVAL_SECONDS = 7L
    private const val CONNECT_TIMEOUT_SECONDS = 5L
    private const val READ_TIMEOUT_SECONDS = 10L

    /**
     * @param network  The bound AP network whose `socketFactory` pins every socket
     *                 to the SoftAP; null falls back to the process-default
     *                 (relies on `bindProcessToNetwork`).
     */
    fun build(network: Network?): OkHttpClient {
        val builder = OkHttpClient.Builder()
            .pingInterval(PING_INTERVAL_SECONDS, TimeUnit.SECONDS)
            .connectTimeout(CONNECT_TIMEOUT_SECONDS, TimeUnit.SECONDS)
            .readTimeout(READ_TIMEOUT_SECONDS, TimeUnit.SECONDS)
        if (network != null) {
            builder.socketFactory(network.socketFactory)
        }
        return builder.build()
    }
}
