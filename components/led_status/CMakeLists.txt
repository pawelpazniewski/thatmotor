idf_component_register(
    SRCS "src/led_pattern.c" "src/led_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES state_machine esp_driver_rmt
)
