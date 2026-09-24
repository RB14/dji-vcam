add_executable(dji-vcam-cli main.cpp)
target_link_libraries(dji-vcam-cli PRIVATE djivcam_core)
