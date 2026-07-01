idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES pwm_out rc_capture control_loop settings web_panel gps imu blackbox usb_console
)
