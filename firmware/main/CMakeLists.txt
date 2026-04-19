idf_component_register(
    SRCS "app.c" "wifi.c"
    INCLUDE_DIRS "."
    REQUIRES dxp
    PRIV_REQUIRES nvs_flash esp_wifi
)