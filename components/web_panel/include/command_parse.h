#pragma once

#include <stdbool.h>

#include "esc_calibration.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Command keyword -> UI event mapping (Unit 10), pure logic.
 *
 * The panel's POST /api/command carries a tiny JSON like {"cmd":"arm"}. The
 * transport layer (http_server) extracts the "cmd" string with cJSON, then
 * delegates the string -> typed event mapping to this pure core so the command
 * contract (the known keyword set) lives in ONE place and is host-testable.
 *
 * Mapping is EXACT (no substring matching): only a recognised keyword produces
 * the matching event fields; anything else is reported as unknown. It has NO IDF
 * / no cJSON dependency (mirrors the control_loop_ui_events fields rather than
 * including control_loop.h, which would pull in esp_err.h).
 */

/** Outcome of mapping a command keyword to UI event fields. */
typedef struct {
    bool ok;                 /* true: cmd recognised, fields below valid */
    bool arm_request;        /* request a transition to ARMED */
    bool disarm_request;     /* request a transition to DISARMED */
    bool calib_request;      /* request entry to ESC range calibration */
    bool calib_confirm;      /* operator confirmation of the calib warning */
    bool deploy_request;     /* request entry to DEPLOY (from DISARMED) */
    bool stow_request;       /* request exit from DEPLOY -> DISARMED */
    bool trim_left;          /* servo neutral trim: step one click left */
    bool trim_right;         /* servo neutral trim: step one click right */
    bool trim_save;          /* persist the current servo trim to NVS */
    calib_event calib_event; /* discriminated calibration operator event */
} command_parse_result;

/**
 * Map a command keyword to UI event fields (exact match).
 *
 * Recognised keywords: "arm", "disarm", "deploy", "stow", "calib_start",
 * "calib_next", "calib_cancel", "trim_left", "trim_right", "trim_save". Any
 * other (or NULL) keyword yields ok=false with all event fields inert (zeroed).
 *
 * @param cmd  Command keyword (NUL-terminated), or NULL.
 * @return ok + the mapped fields on a known keyword; ok=false otherwise.
 */
command_parse_result command_parse(const char *cmd);

#ifdef __cplusplus
}
#endif
