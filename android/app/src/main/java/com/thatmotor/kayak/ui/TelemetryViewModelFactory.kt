package com.thatmotor.kayak.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import com.thatmotor.kayak.net.CommandApi
import com.thatmotor.kayak.net.EspHttpClient
import com.thatmotor.kayak.net.TelemetrySocket
import com.thatmotor.kayak.repository.TelemetryRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob

/**
 * Manual dependency wiring for [TelemetryViewModel] — no DI framework for v1. Building
 * the chain inside a [ViewModelProvider.Factory] (instead of as Activity fields) lets
 * `viewModels { ... }` retain the ViewModel and its repository across configuration
 * changes; the repository's coroutine scope outlives a single Activity instance and
 * is torn down via [TelemetryRepository.stop] in [TelemetryViewModel.onCleared].
 *
 * Phase 3 builds the OkHttp client with a null network so it relies on
 * `bindProcessToNetwork`; per-socket pinning arrives with the connection layer.
 */
class TelemetryViewModelFactory : ViewModelProvider.Factory {

    override fun <T : ViewModel> create(modelClass: Class<T>): T {
        require(modelClass.isAssignableFrom(TelemetryViewModel::class.java)) {
            "Unknown ViewModel class: ${modelClass.name}"
        }
        val httpClient = EspHttpClient.build(network = null)
        val scope = CoroutineScope(SupervisorJob())
        val repository = TelemetryRepository(scope = scope, socket = TelemetrySocket(httpClient))
        @Suppress("UNCHECKED_CAST")
        return TelemetryViewModel(repository, CommandApi(httpClient)) as T
    }
}
