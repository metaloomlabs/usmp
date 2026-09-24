cmake_minimum_required(VERSION 3.16)
project(usmp C)

# Enable C11 standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable compiler warnings and basic security hardening on GCC/Clang
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wconversion
        -fstack-protector-strong
        -D_FORTIFY_SOURCE=2
    )
endif()

# Locate mbedtls
find_package(MbedTLS QUIET)
if(NOT MbedTLS_FOUND)
    # Manual fallback for standard installations
    find_path(MBEDTLS_INCLUDE_DIRS NAMES mbedtls/gcm.h mbedtls/mbedtls_config.h mbedtls/config.h mbedtls/ssl.h HINTS C:/MinGW/include)
    find_library(MBEDTLS_LIBRARY mbedtls HINTS C:/MinGW/lib)
    find_library(MBEDX509_LIBRARY mbedx509 HINTS C:/MinGW/lib)
    find_library(MBEDCRYPTO_LIBRARY mbedcrypto HINTS C:/MinGW/lib)
    
    if(MBEDTLS_LIBRARY AND MBEDCRYPTO_LIBRARY)
        set(MBEDTLS_LIBRARIES ${MBEDTLS_LIBRARY} ${MBEDX509_LIBRARY} ${MBEDCRYPTO_LIBRARY})
        set(MbedTLS_FOUND TRUE)
        set(MbedTLS_FOUND TRUE CACHE BOOL "" FORCE)
    elseif(MBEDCRYPTO_LIBRARY)
        set(MBEDTLS_LIBRARIES ${MBEDCRYPTO_LIBRARY})
        if(MBEDX509_LIBRARY)
            list(APPEND MBEDTLS_LIBRARIES ${MBEDX509_LIBRARY})
        endif()
        set(MbedTLS_FOUND TRUE)
        set(MbedTLS_FOUND TRUE CACHE BOOL "" FORCE)
        if(NOT MBEDTLS_INCLUDE_DIRS)
            find_path(MBED_INCLUDE_DIR mbedtls/config.h)
            set(MBEDTLS_INCLUDE_DIRS ${MBED_INCLUDE_DIR})
        endif()
    endif()
endif()

# Include core library subdirectory
add_subdirectory(core)

# Build C unit tests target
add_executable(usmp_tests
    core/tests/test_main.c
    core/tests/test_golden.c
    core/tests/usmp_port_posix.c
)

target_include_directories(usmp_tests PRIVATE 
    core/include
    core/src
    core/tests
)

target_link_libraries(usmp_tests PRIVATE usmp-core)

# Build C interop client target
add_executable(usmp_interop_client
    core/tests/usmp_interop_client.c
    core/tests/usmp_transport_posix.c
    core/tests/usmp_port_posix.c
)

target_include_directories(usmp_interop_client PRIVATE
    core/include
    core/src
    core/tests
)

target_link_libraries(usmp_interop_client PRIVATE usmp-core)

# If mbedtls include directories and libraries were found, link them explicitly to the tests
if(MbedTLS_FOUND)
    target_include_directories(usmp-core PUBLIC ${MBEDTLS_INCLUDE_DIRS})
    target_link_libraries(usmp-core PUBLIC ${MBEDTLS_LIBRARIES})
    target_include_directories(usmp_tests PRIVATE ${MBEDTLS_INCLUDE_DIRS})
    target_link_libraries(usmp_tests PRIVATE usmp-core ${MBEDTLS_LIBRARIES} ${MBEDTLS_LIBRARIES})
    target_include_directories(usmp_interop_client PRIVATE ${MBEDTLS_INCLUDE_DIRS})
    target_link_libraries(usmp_interop_client PRIVATE usmp-core ${MBEDTLS_LIBRARIES} ${MBEDTLS_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(usmp_tests PRIVATE ws2_32 ssp)
    target_link_libraries(usmp_interop_client PRIVATE ws2_32 ssp)
    target_compile_definitions(usmp_interop_client PRIVATE _WIN32_WINNT=0x0600)
    target_compile_definitions(usmp_tests PRIVATE _WIN32_WINNT=0x0600)
endif()


