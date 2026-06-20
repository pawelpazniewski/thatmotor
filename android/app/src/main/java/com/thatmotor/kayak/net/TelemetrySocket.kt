package com.thatmotor.kayak.net

import android.util.Log
import com.thatmotor.kayak.data.TelemetryFrame
import com.thatmotor.kayak.data.parseTelemetryFrame
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.buffer
import kotlinx.coroutines.flow.callbackFlow
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener

/**
 * Subscribes to `WS /ws` and exposes parsed [TelemetryFrame]s as a cold [Flow].
 *
 * Thin HAL adapter over OkHttp's [WebSocket]: each text frame is parsed by the
 * host-tested [parseTelemetryFrame]. A malformed frame is logged and skipped (the
 * stream is lossy ~10 Hz; one bad frame must not kill the link). The socket is
 * cancelled when the collector stops (`awaitClose`), so there are no leaked
 * callbacks (coding-rules pkt 13: cleanup).
 *
 * Backpressure is explicit: the stream is lossy ~10 Hz telemetry, so a slow
 * collector (Phase 3 UI) must drop the OLDEST frame rather than block the WS
 * listener thread or accumulate latency. The [buffer] with [BufferOverflow.DROP_OLDEST]
 * makes that policy visible, and [trySend]'s result is checked + logged instead of
 * being silently discarded (coding-rules pkt 4, pkt 13).
 */
class TelemetrySocket(
    private val client: OkHttpClient,
    private val baseUrl: String = DEFAULT_BASE_URL,
) {
    companion object {
        const val DEFAULT_BASE_URL = "ws://192.168.4.1"
        private const val TAG = "TelemetrySocket"
    }

    fun frames(): Flow<TelemetryFrame> = callbackFlow {
        val request = Request.Builder().url("$baseUrl/ws").build()

        val listener = object : WebSocketListener() {
            override fun onMessage(webSocket: WebSocket, text: String) {
                val frame = runCatching { parseTelemetryFrame(text) }.getOrNull()
                if (frame == null) {
                    Log.w(TAG, "skipping malformed telemetry frame")
                    return
                }
                val sent = trySend(frame)
                if (sent.isFailure && !sent.isClosed) {
                    // Buffer overflow handles drops upstream; a non-closed failure
                    // here is unexpected and must not be swallowed silently.
                    Log.w(TAG, "dropped telemetry frame: channel send failed")
                }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                close(t)
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                close()
            }
        }

        val socket = client.newWebSocket(request, listener)
        awaitClose { socket.cancel() }
        // `cancel()` (not `close()`) is intentional: collector cancellation must
        // tear down immediately without waiting for a graceful close handshake.
    }.buffer(capacity = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)
}
