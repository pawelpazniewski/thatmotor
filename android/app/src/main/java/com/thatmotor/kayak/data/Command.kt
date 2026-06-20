package com.thatmotor.kayak.data

import kotlinx.serialization.Serializable

/**
 * The operational commands the app may send to `POST /api/command` in v1.
 *
 * The firmware accepts more keywords (`calib_*`, `trim_*`), but those belong to
 * the web panel (configuration/calibration) and are out of scope for the tablet
 * app (see plan "Poza zakresem"). [keyword] is the exact wire string the firmware
 * matches in `command_parse` (`components/web_panel/include/command_parse.h`).
 */
enum class Command(val keyword: String) {
    ARM("arm"),
    DISARM("disarm"),
    DEPLOY("deploy"),
    STOW("stow"),
}

/** Request body for `POST /api/command`: `{"cmd":"arm"}`. */
@Serializable
data class CommandRequest(val cmd: String) {
    constructor(command: Command) : this(command.keyword)
}
