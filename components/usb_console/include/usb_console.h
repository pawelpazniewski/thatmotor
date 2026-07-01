#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Minimal esp_console REPL on the USB Serial/JTAG port (the native USB-C the
 * operator plugs into for calibration). It is a diagnostic, ground-side console:
 * entirely OUTSIDE the 50 Hz control loop and failsafe. Commands run in the REPL
 * task (prio 2), never in the control task.
 *
 * Console layout (see sdkconfig.defaults): the primary console is USB Serial/JTAG
 * so this REPL and its CSV dump reach the operator's port; ESP_LOG shares it (the
 * standard esp_console pattern re-draws the prompt after a log line). The CSV
 * dump is raw printf (no log prefix), so a parser filters lines that match the
 * blackbox CSV schema. Dump when the loop is quiet (DISARMED / off-water).
 *
 * Registered commands:
 *   spotlog dump  - stream the blackbox flash region as denormalised CSV
 *   params get    - print the active spot-lock regulator settings (Unit 5)
 *   params set    - stage a tuned spot-lock setting via the SI-6 path (Unit 5)
 */

/**
 * Create the USB Serial/JTAG REPL, register the console commands, and start the
 * REPL task. Diagnostic and OPTIONAL: a failure is returned so app_main can log
 * and continue without a console (it never aborts the boot or the control loop).
 *
 * @return ESP_OK on success, otherwise the failing esp_err_t.
 */
esp_err_t usb_console_start(void);

#ifdef __cplusplus
}
#endif
