idf_component_register(
    SRCS "src/bno085.c" "src/quat_to_yaw.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_gpio freertos sh2 esp_timer
)
