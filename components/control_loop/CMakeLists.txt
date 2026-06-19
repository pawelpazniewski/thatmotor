idf_component_register(
    SRCS "src/loop_step.c" "src/control_loop.c"
    INCLUDE_DIRS "include"
    REQUIRES state_machine signal_chain rc_validity rc_capture settings pwm_out
             safety_clamp led_status gps esp_timer esp_system freertos
)
