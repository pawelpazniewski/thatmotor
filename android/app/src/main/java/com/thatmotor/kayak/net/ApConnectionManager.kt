package com.thatmotor.kayak.net

import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Binds the app's process traffic to the ESP32 SoftAP using a peer-to-peer WiFi
 * request that explicitly lacks [NetworkCapabilities.NET_CAPABILITY_INTERNET], so
 * Android does not tear it down for "no internet".
 *
 * Thin HAL adapter: it translates [ConnectivityManager.NetworkCallback] callbacks
 * into [ApConnectionEvent]s and delegates the decision to [reduceApConnectionState]
 * (Pure ⊥ HAL). The bound [Network]'s `socketFactory` is exposed so OkHttp can pin
 * sockets to the AP even if `bindProcessToNetwork` is unreliable on a device.
 */
class ApConnectionManager(
    private val connectivityManager: ConnectivityManager,
) {
    companion object {
        /**
         * Default timeout for the SoftAP request. Without it, `requestNetwork`
         * never fires `onUnavailable` on its own (only on explicit framework
         * rejection), so a wrong SSID/passphrase would hang in `Connecting`
         * forever (R7). The timeout guarantees an `Unavailable` event.
         */
        const val DEFAULT_TIMEOUT_MS = 30_000
    }

    private val _state = MutableStateFlow<ApConnectionState>(ApConnectionState.Idle)
    val state: StateFlow<ApConnectionState> = _state.asStateFlow()

    /**
     * The bound AP network, available while [state] is [ApConnectionState.Connected].
     *
     * Written on the ConnectivityManager callback thread and read from the OkHttp
     * layer (`network.socketFactory`) on another thread, so it is `@Volatile` to
     * guarantee cross-thread visibility.
     */
    @Volatile
    var boundNetwork: Network? = null
        private set

    private var callback: ConnectivityManager.NetworkCallback? = null

    /**
     * Request and bind to the SoftAP identified by [ssid] / [passphrase].
     * Idempotent-safe: a prior request is torn down first.
     *
     * [timeoutMs] bounds how long the framework attempts the request before
     * firing `onUnavailable` (→ [ApConnectionState.Failed]), so a bad
     * SSID/passphrase cannot hang in `Connecting` indefinitely.
     */
    fun connect(ssid: String, passphrase: String, timeoutMs: Int = DEFAULT_TIMEOUT_MS) {
        require(ssid.isNotBlank()) { "ssid must not be blank" }
        require(passphrase.length in 8..63) { "WPA2-PSK passphrase must be 8..63 chars" }
        require(timeoutMs > 0) { "timeoutMs must be positive" }

        disconnect()
        dispatch(ApConnectionEvent.Requested)

        val specifier = WifiNetworkSpecifier.Builder()
            .setSsid(ssid)
            .setWpa2Passphrase(passphrase)
            .build()

        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(specifier)
            .build()

        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                boundNetwork = network
                connectivityManager.bindProcessToNetwork(network)
                dispatch(ApConnectionEvent.Available)
            }

            override fun onLost(network: Network) {
                boundNetwork = null
                dispatch(ApConnectionEvent.Lost)
            }

            override fun onUnavailable() {
                boundNetwork = null
                dispatch(ApConnectionEvent.Unavailable)
            }
        }
        callback = cb
        connectivityManager.requestNetwork(request, cb, timeoutMs)
    }

    /** Cleanup: unbind the process and unregister the callback. Safe to call repeatedly. */
    fun disconnect() {
        callback?.let { connectivityManager.unregisterNetworkCallback(it) }
        callback = null
        boundNetwork = null
        connectivityManager.bindProcessToNetwork(null)
    }

    private fun dispatch(event: ApConnectionEvent) {
        _state.value = reduceApConnectionState(_state.value, event)
    }
}
