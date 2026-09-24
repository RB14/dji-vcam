find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OpenGLWidgets)
qt_standard_project_setup()

qt_add_executable(osmolink WIN32
    main.cpp
    main_window.cpp main_window.h
    pipeline.cpp pipeline.h
    preview_widget.cpp preview_widget.h
)
target_link_libraries(osmolink PRIVATE Qt6::Widgets Qt6::OpenGLWidgets osmolink_core osmolink_media)

if(WIN32)
    # Make the build directory runnable: Qt runtime (windeployqt) and FFmpeg DLLs next to the exe.
    get_target_property(qmake_path Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin_dir "${qmake_path}" DIRECTORY)
    add_custom_command(TARGET osmolink POST_BUILD
        COMMAND "${qt_bin_dir}/windeployqt.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw
                "$<TARGET_FILE:osmolink>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:osmolink> $<TARGET_FILE_DIR:osmolink>
        COMMAND_EXPAND_LISTS
        VERBATIM)
endif()
