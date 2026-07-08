cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "../../core/src/usmp_frame.c"
        "../../core/src/usmp_crypto.c"
        "../../core/src/usmp_handshake.c"
        "../../core/src/usmp_session.c"
        "../../core/src/usmp_connect.c"
        "port/usmp_port_esp32.c"
        "transport/usmp_transport_tcp.c"
        "transport/usmp_transport_udp.c"
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

# Enable compiler warnings and basic security hardening on component build
if(TARGET ${COMPONENT_LIB})
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -fstack-protector-strong
        -D_FORTIFY_SOURCE=2
    )
endif()