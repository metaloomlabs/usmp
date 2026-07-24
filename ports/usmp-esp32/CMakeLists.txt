cmake_minimum_required(VERSION 3.16)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/core")
    set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/core")
else()
    set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../core")
endif()

idf_component_register(
    SRCS
        "${CORE_DIR}/src/usmp_frame.c"
        "${CORE_DIR}/src/usmp_crypto.c"
        "${CORE_DIR}/src/usmp_handshake.c"
        "${CORE_DIR}/src/usmp_session.c"
        "${CORE_DIR}/src/usmp_connect.c"
        "port/usmp_port_esp32.c"
        "transport/usmp_transport_tcp.c"
        "transport/usmp_transport_udp.c"
    INCLUDE_DIRS
        "${CORE_DIR}/include"
    PRIV_INCLUDE_DIRS
        "${CORE_DIR}/src"
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