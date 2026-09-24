add_library(djivcam_core STATIC
    src/datalink.cpp
    src/duml.cpp
    src/h264.cpp
    src/net.cpp
    src/reassembler.cpp
    src/request_tracker.cpp
    src/camera_controller.cpp
    src/camera_protocol.cpp
    src/session.cpp
)
target_include_directories(djivcam_core PUBLIC include)

find_package(Threads REQUIRED)
target_link_libraries(djivcam_core PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(djivcam_core PUBLIC ws2_32)
    target_compile_definitions(djivcam_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
