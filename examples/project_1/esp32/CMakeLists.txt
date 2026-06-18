# examples/project_1/esp32/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../ports/usmp-esp32"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(project_1_esp32)
