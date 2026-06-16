idf_component_register(
    SRCS "src/api_contract.c"
         "src/command_parse.c"
         "src/params_decide.c"
         "src/wifi_ap_config.c"
         "src/wifi_ap.c"
         "src/params_json.c"
         "src/params_api.c"
         "src/ws_telemetry.c"
         "src/http_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_http_server nvs_flash json
             settings control_loop state_machine esp_timer
    EMBED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../web/index.html"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../web/app.js"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../web/style.css"
)
