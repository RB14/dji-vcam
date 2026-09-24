include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(osmolink_tests
    datalink_test.cpp
    duml_test.cpp
    h264_test.cpp
    reassembler_test.cpp
)
target_link_libraries(osmolink_tests PRIVATE osmolink_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(osmolink_tests)
