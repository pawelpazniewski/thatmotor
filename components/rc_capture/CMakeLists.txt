idf_component_register(
    SRCS "src/rc_capture.c" "src/cap_math.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_mcpwm
)
