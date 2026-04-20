cmake_minimum_required(VERSION 3.16)

# Core DXP library — platform agnostic
# Used by ports as a subdirectory or included directly

if(ESP_PLATFORM)
    # Building inside ESP-IDF — register as component
    idf_component_register(
        SRCS
            "src/dxp_frame.c"
            "src/dxp_crypto.c"
            "src/dxp_handshake.c"
            "src/dxp_session.c"
            "src/dxp_connect.c"
        INCLUDE_DIRS
            "include"
        PRIV_INCLUDE_DIRS
            "src"
        REQUIRES
            mbedtls
    )
else()
    # Building as standalone cmake library (for testing, other platforms)
    project(dxp-core C)
    add_library(dxp-core STATIC
        src/dxp_frame.c
        src/dxp_crypto.c
        src/dxp_handshake.c
        src/dxp_session.c
        src/dxp_connect.c
    )
    target_include_directories(dxp-core
        PUBLIC  include
        PRIVATE src
    )
    target_link_libraries(dxp-core mbedtls)
endif()