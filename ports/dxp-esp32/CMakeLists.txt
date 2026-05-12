cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "../../core/src/dxp_frame.c"
        "../../core/src/dxp_crypto.c"
        "../../core/src/dxp_handshake.c"
        "../../core/src/dxp_session.c"
        "../../core/src/dxp_connect.c"
        "port/dxp_port_esp32.c"
        "transport/dxp_transport_tcp.c"
    INCLUDE_DIRS
        "../../core/include"
    PRIV_INCLUDE_DIRS
        "../../core/src"
    REQUIRES
        mbedtls
        esp_wifi
        lwip
        esp_timer
)