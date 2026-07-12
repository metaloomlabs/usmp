# examples/aws_ec2_test/esp32/main/CMakeLists.txt
idf_component_register(
    SRCS "app.c" "wifi.c"
    INCLUDE_DIRS "."
    REQUIRES usmp-esp32
    PRIV_REQUIRES nvs_flash esp_wifi
)
