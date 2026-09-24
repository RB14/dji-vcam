add_library(osmolink_core STATIC
    src/datalink.cpp
    src/duml.cpp
    src/h264.cpp
    src/net.cpp
    src/reassembler.cpp
    src/session.cpp
)
target_include_directories(osmolink_core PUBLIC include)

find_package(Threads REQUIRED)
target_link_libraries(osmolink_core PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(osmolink_core PUBLIC ws2_32)
    target_compile_definitions(osmolink_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
