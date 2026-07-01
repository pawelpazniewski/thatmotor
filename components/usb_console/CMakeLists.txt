# USB Serial/JTAG esp_console REPL for ground-side calibration: `spotlog dump`
# (CSV) plus `params get`/`params set` (Unit 5). Diagnostic only, entirely
# outside the control loop and failsafe. Depends on esp_console (`console`) and
# the blackbox HAL/codec/CSV for the dump; control_loop + settings for params.
idf_component_register(
    SRCS "src/usb_console.c"
    INCLUDE_DIRS "include"
    REQUIRES console blackbox control_loop settings
)
