# Bluetooth LE for the camera session (pairing, Wi-Fi wake, credentials), on a small platform
# layer (src/central.h). Windows: Windows.Devices.Bluetooth through C++/WinRT (Windows SDK).
# Linux: SimpleBLE (BlueZ over D-Bus) until our own BlueZ implementation replaces it.
add_library(djivcam_ble STATIC src/camera_ble.cpp)
target_include_directories(djivcam_ble PUBLIC include)
target_link_libraries(djivcam_ble PUBLIC djivcam_core)
target_compile_definitions(djivcam_ble PUBLIC DJIVCAM_HAVE_BLE)

if(WIN32)
    target_sources(djivcam_ble PRIVATE src/central_winrt.cpp)
    target_compile_options(djivcam_ble PRIVATE /bigobj)
    target_link_libraries(djivcam_ble PRIVATE windowsapp ole32)
else()
    include(FetchContent)
    set(SIMPLEBLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(simpleble
        URL https://github.com/simpleble/simpleble/archive/refs/tags/v1.1.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SOURCE_SUBDIR simpleble)
    FetchContent_MakeAvailable(simpleble)
    target_sources(djivcam_ble PRIVATE src/central_simpleble.cpp)
    target_link_libraries(djivcam_ble PRIVATE simpleble::simpleble)
endif()
