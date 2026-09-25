cmake_minimum_required(VERSION 3.28)
project(dji-vcam VERSION 0.1.0 DESCRIPTION "DJI Osmo Action live view as a virtual camera" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/DjiVcamVersion.cmake)

option(DJIVCAM_BUILD_TESTS "Build the unit tests" ON)
option(DJIVCAM_BUILD_GUI "Build the Qt desktop app" OFF)
option(DJIVCAM_WITH_BLE "Build Bluetooth LE support (WinRT on Windows, SimpleBLE on Linux)" ${DJIVCAM_BUILD_GUI})

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
if(DJIVCAM_WITH_BLE)
    add_subdirectory(ble)
endif()

if(DJIVCAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(DJIVCAM_BUILD_GUI)
    add_subdirectory(media)
    add_subdirectory(vcam)
    add_subdirectory(gui)
endif()

# After the optional components, so the CLI can use the ones that were built.
add_subdirectory(cli)
