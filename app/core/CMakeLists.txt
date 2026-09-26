add_library(djivcam_core STATIC
    src/datalink.cpp
    src/duml.cpp
    src/h264.cpp
    src/live_session.cpp
    src/live_stream.cpp
    src/net.cpp
    src/net_neighbors.cpp
    src/reassembler.cpp
    src/request_tracker.cpp
    src/camera_controller.cpp
    src/camera_model.cpp
    src/camera_protocol.cpp
    src/session.cpp
    src/wifi_channel.cpp
)
target_include_directories(djivcam_core PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file(include/djivcam/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/djivcam/version.h" @ONLY)

find_package(Threads REQUIRED)
target_link_libraries(djivcam_core PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(djivcam_core PUBLIC ws2_32 iphlpapi)
    target_compile_definitions(djivcam_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
