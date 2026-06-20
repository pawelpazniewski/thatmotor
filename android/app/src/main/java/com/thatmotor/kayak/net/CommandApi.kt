package com.thatmotor.kayak.net

import com.thatmotor.kayak.data.ApiEnvelope
import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.data.CommandRequest
import com.thatmotor.kayak.data.espJson
import java.io.IOException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.JsonElement
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody

/**
 * Sends an operational command and returns its transport [CommandResult]. The seam
 * the UI depends on, so a test can substitute a fake without an OkHttp stack
 * (coding-rules pkt 2: mock only the external API).
 */
fun interface CommandSender {
    suspend fun send(command: Command): CommandResult
}

/**
 * Sends operational commands to `POST /api/command` and maps the response to a
 * [CommandResult] via the pure [mapCommandResult].
 *
 * Thin HAL adapter: it owns only the OkHttp call + JSON (de)serialisation; the
 * status/envelope → result decision lives in the host-tested pure core.
 */
class CommandApi(
    private val client: OkHttpClient,
    private val baseUrl: String = DEFAULT_BASE_URL,
) : CommandSender {
    companion object {
        const val DEFAULT_BASE_URL = "http://192.168.4.1"
        private val JSON_MEDIA_TYPE = "application/json".toMediaType()
    }

    /** Send [command]; never throws for an HTTP error — failures map to a [CommandResult]. */
    override suspend fun send(command: Command): CommandResult = withContext(Dispatchers.IO) {
        val body = espJson
            .encodeToString(CommandRequest.serializer(), CommandRequest(command))
            .toRequestBody(JSON_MEDIA_TYPE)
        val request = Request.Builder()
            .url("$baseUrl/api/command")
            .post(body)
            .build()

        try {
            client.newCall(request).execute().use { response ->
                val payload = response.body?.string().orEmpty()
                val error = parseEnvelopeError(payload)
                mapCommandResult(response.code, error)
            }
        } catch (e: IOException) {
            CommandResult.TransportError(e.message ?: "I/O error")
        }
    }

    private fun parseEnvelopeError(payload: String) =
        if (payload.isBlank()) {
            null
        } else {
            espJson.decodeFromString(
                ApiEnvelope.serializer(JsonElement.serializer()),
                payload,
            ).error
        }
}
