cmake_minimum_required(VERSION 3.16)

# Core USMP library — platform agnostic
# Used by ports as a subdirectory or included directly

if(ESP_PLATFORM)
    # Building inside ESP-IDF — register as component
    idf_component_register(
        SRCS
            "src/usmp_frame.c"
            "src/usmp_crypto.c"
            "src/usmp_handshake.c"
            "src/usmp_session.c"
            "src/usmp_connect.c"
        INCLUDE_DIRS
            "include"
        PRIV_INCLUDE_DIRS
            "src"
        REQUIRES
            mbedtls
    )
else()
    # Building as standalone cmake library (for testing, other platforms)
    project(usmp-core C)
    add_library(usmp-core STATIC
        src/usmp_frame.c
        src/usmp_crypto.c
        src/usmp_handshake.c
        src/usmp_session.c
        src/usmp_connect.c
    )
    target_include_directories(usmp-core
        PUBLIC  include
        PRIVATE src
    )
    if(NOT MbedTLS_FOUND)
        target_link_libraries(usmp-core PUBLIC mbedtls)
    endif()

    # Enable compile warning and hardening flags on host builds
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(usmp-core PRIVATE
            -Wall
            -Wextra
            -Wconversion
            -fstack-protector-strong
            -D_FORTIFY_SOURCE=2
        )
    endif()
endif()