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
    private val _state = MutableStateFlow<ApConnectionState>(ApConnectionState.Idle)
    val state: StateFlow<ApConnectionState> = _state.asStateFlow()

    /** The bound AP network, available while [state] is [ApConnectionState.Connected]. */
    var boundNetwork: Network? = null
        private set

    private var callback: ConnectivityManager.NetworkCallback? = null

    /**
     * Request and bind to the SoftAP identified by [ssid] / [passphrase].
     * Idempotent-safe: a prior request is torn down first.
     */
    fun connect(ssid: String, passphrase: String) {
        require(ssid.isNotBlank()) { "ssid must not be blank" }
        require(passphrase.length in 8..63) { "WPA2-PSK passphrase must be 8..63 chars" }

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
        connectivityManager.requestNetwork(request, cb)
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
