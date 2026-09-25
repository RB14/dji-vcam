# Virtual webcam. Windows 11: Media Foundation virtual camera (media source DLL + app-side
# registration and frame publishing). Linux (v4l2loopback) is still to come.
if(NOT WIN32)
    return()
endif()

include(FetchContent)
set(WIL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WIL_BUILD_PACKAGING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(wil
    URL https://github.com/microsoft/wil/archive/refs/tags/v1.0.260126.7.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(wil)

# Media source loaded by the Windows Camera Frame Server (adapted from smourier/VCamSample, MIT).
add_library(djivcam-source SHARED
    windows/source/Activator.cpp
    windows/source/EnumNames.cpp
    windows/source/MFTools.cpp
    windows/source/MediaSource.cpp
    windows/source/MediaStream.cpp
    windows/source/SharedFrameReader.cpp
    windows/source/Tools.cpp
    windows/source/WinTrace.cpp
    windows/source/dllmain.cpp
    windows/source/djivcam-source.def
)
target_include_directories(djivcam-source PRIVATE windows/source include)
djivcam_add_version_resource(djivcam-source "DJI VCam webcam (Media Foundation media source)")
target_compile_definitions(djivcam-source PRIVATE UNICODE _UNICODE)
# Loaded by a Windows service on any machine: do not depend on the VC++ runtime being installed.
set_property(TARGET djivcam-source PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(djivcam-source PRIVATE WIL::WIL
    mfplat mfuuid mf mfsensorgroup ole32 oleaut32 propsys advapi32 windowsapp)

# App side: registers the virtual camera and publishes frames to the media source.
add_library(djivcam_vcam STATIC windows/virtual_camera.cpp)
target_include_directories(djivcam_vcam PUBLIC include)
target_compile_definitions(djivcam_vcam PUBLIC DJIVCAM_HAVE_VCAM PRIVATE UNICODE _UNICODE)
target_link_libraries(djivcam_vcam PUBLIC mf mfplat mfsensorgroup ole32 shell32 advapi32)
