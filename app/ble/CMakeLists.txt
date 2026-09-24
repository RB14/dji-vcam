# Bluetooth LE (SimpleBLE: WinRT on Windows, BlueZ over D-Bus on Linux).
include(FetchContent)
set(SIMPLEBLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(simpleble
    URL https://github.com/simpleble/simpleble/archive/refs/tags/v1.1.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SOURCE_SUBDIR simpleble)
FetchContent_MakeAvailable(simpleble)

add_library(djivcam_ble STATIC src/camera_ble.cpp)
target_include_directories(djivcam_ble PUBLIC include)
target_link_libraries(djivcam_ble PUBLIC djivcam_core PRIVATE simpleble::simpleble)
target_compile_definitions(djivcam_ble PUBLIC DJIVCAM_HAVE_BLE)
