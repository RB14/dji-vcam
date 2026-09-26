# FFmpeg: a shared dev build on Windows (FFMPEG_DIR with include/, lib/, bin/, see
# scripts/setup-windows-deps.sh), pkg-config on Linux.
if(WIN32)
    set(FFMPEG_DIR "" CACHE PATH "FFmpeg shared development build (include/, lib/, bin/)")
    if(NOT EXISTS "${FFMPEG_DIR}/include/libavcodec/avcodec.h")
        message(FATAL_ERROR "FFmpeg not found in FFMPEG_DIR='${FFMPEG_DIR}' (run scripts/setup-windows-deps.sh)")
    endif()
    foreach(lib avcodec avformat avutil swscale swresample)
        file(GLOB dll "${FFMPEG_DIR}/bin/${lib}-*.dll")
        add_library(FFmpeg::${lib} SHARED IMPORTED)
        set_target_properties(FFmpeg::${lib} PROPERTIES
            IMPORTED_IMPLIB "${FFMPEG_DIR}/lib/${lib}.lib"
            IMPORTED_LOCATION "${dll}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_DIR}/include")
    endforeach()
    # avcodec loads swresample at runtime; declaring it gets the DLL deployed with the app.
    set_property(TARGET FFmpeg::avcodec APPEND PROPERTY INTERFACE_LINK_LIBRARIES FFmpeg::swresample FFmpeg::avutil)
    set(DJIVCAM_FFMPEG_LIBS FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
    set(DJIVCAM_FFMPEG_LIBS PkgConfig::FFMPEG)
endif()

add_library(djivcam_media STATIC src/decoder.cpp src/network_stream.cpp)
target_include_directories(djivcam_media PUBLIC include)
target_compile_definitions(djivcam_media PUBLIC DJIVCAM_HAVE_MEDIA)
target_link_libraries(djivcam_media PUBLIC ${DJIVCAM_FFMPEG_LIBS} djivcam_core)
if(WIN32)
    # The D3D11VA device's adapter name (decoder.cpp) includes the Windows headers.
    target_compile_definitions(djivcam_media PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
