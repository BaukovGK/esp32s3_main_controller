idf_component_register(
    SRCS "web_server.c" "web_api_status.c" "web_api_command.c"
         "web_static.c" "web_auth.c"
    INCLUDE_DIRS "include"
    REQUIRES services process drivers board_hal mqtt_app
    PRIV_REQUIRES esp_http_server json esp_timer mbedtls
    EMBED_TXTFILES "www/index.html" "www/style.css" "www/app.js"
)
