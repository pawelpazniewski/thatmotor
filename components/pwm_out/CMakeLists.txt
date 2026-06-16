idf_component_register(
    SRCS "src/pwm_out.c" "src/pwm_us_to_duty.c"
    INCLUDE_DIRS "include"
    REQUIRES safety_clamp esp_driver_ledc
)
