package com.thatmotor.kayak.session

/** Whether a telemetry session is running; drives every keep-alive decision. */
enum class SessionState {
    ACTIVE,
    STOPPED,
}

/** Lifecycle events that drive [SessionState] transitions via [SessionPolicy.nextState]. */
enum class SessionEvent {
    STARTED,
    STOPPED,
}
