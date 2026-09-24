cmake_minimum_required(VERSION 3.28)
project(osmolink VERSION 0.1.0 DESCRIPTION "DJI Osmo Action live view as a virtual camera" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OSMOLINK_BUILD_TESTS "Build the unit tests" ON)
option(OSMOLINK_BUILD_GUI "Build the Qt desktop app" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
add_subdirectory(cli)

if(OSMOLINK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(OSMOLINK_BUILD_GUI)
    add_subdirectory(media)
    add_subdirectory(gui)
endif()
