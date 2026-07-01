idf_component_register(
    SRCS "src/gps_reader.c" "src/nmea_parse.c" "src/sensor_freshness.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_timer
)
