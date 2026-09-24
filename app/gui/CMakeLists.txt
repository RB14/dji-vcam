find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OpenGLWidgets SerialPort)
qt_standard_project_setup()

qt_add_executable(dji-vcam WIN32
    main.cpp
    bridge_link.cpp bridge_link.h
    camera_connector.cpp camera_connector.h
    main_window.cpp main_window.h
    pipeline.cpp pipeline.h
    preview_widget.cpp preview_widget.h
)
target_link_libraries(dji-vcam PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::SerialPort djivcam_core djivcam_ble djivcam_media)

if(WIN32)
    # App icon (resources/dji-vcam.svg, rendered by scripts/make-icon.sh) and version info.
    set(DJIVCAM_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/dji-vcam.ico")
    configure_file(resources/dji-vcam.rc.in dji-vcam.rc @ONLY)
    target_sources(dji-vcam PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/dji-vcam.rc")
endif()

if(TARGET djivcam_vcam)
    target_link_libraries(dji-vcam PRIVATE djivcam_vcam)
    # The media source DLL ships next to the exe; the app installs it for Windows on request.
    add_dependencies(dji-vcam djivcam-source)
    add_custom_command(TARGET dji-vcam POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:djivcam-source> $<TARGET_FILE_DIR:dji-vcam>)
endif()

if(WIN32)
    # Make the build directory runnable: Qt runtime (windeployqt) and FFmpeg DLLs next to the exe.
    get_target_property(qmake_path Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin_dir "${qmake_path}" DIRECTORY)
    add_custom_command(TARGET dji-vcam POST_BUILD
        COMMAND "${qt_bin_dir}/windeployqt.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw
                "$<TARGET_FILE:dji-vcam>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:dji-vcam> $<TARGET_FILE_DIR:dji-vcam>
        COMMAND_EXPAND_LISTS
        VERBATIM)
    # App-local Visual C++ runtime (redistributable DLLs of the building toolset), so the app also
    # runs where the Visual C++ Redistributable is not installed.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    add_custom_command(TARGET dji-vcam POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} $<TARGET_FILE_DIR:dji-vcam>
        VERBATIM)
endif()
