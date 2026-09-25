# Versions (docs/building.md "Versions and releases"). MAJOR.MINOR.PATCH is the project() version,
# the only place it is written. The full version adds whether the build is exactly a release:
# "0.1.0" for the release tag v0.1.0 with no uncommitted changes, else "0.1.0-dev+g<commit>" (plus
# ".dirty" with uncommitted changes). Also runs as a script, printing the version of a checkout:
#   cmake -P app/cmake/DjiVcamVersion.cmake        (what scripts/version.sh does)

# Sets <out_var> to the full version of the git checkout at <source_dir> for version <base>.
function(djivcam_detect_version source_dir base out_var)
    set(version "${base}-dev")  # not a git checkout (e.g. a source archive), or no git
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --short=7 HEAD
                        OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
        if(NOT failed)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" diff --quiet HEAD --
                            RESULT_VARIABLE dirty OUTPUT_QUIET ERROR_QUIET)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" tag --points-at HEAD --list "v${base}"
                            OUTPUT_VARIABLE tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT dirty AND tag STREQUAL "v${base}")
                set(version "${base}")
            else()
                set(version "${base}-dev+g${commit}")
                if(dirty)
                    string(APPEND version ".dirty")
                endif()
            endif()
        endif()
    endif()
    set(${out_var} "${version}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)  # cmake -P: print the version of this checkout
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt" project_line REGEX "^project\\(dji-vcam VERSION")
    string(REGEX REPLACE ".*VERSION ([0-9.]+).*" "\\1" base "${project_line}")
    djivcam_detect_version("${CMAKE_CURRENT_LIST_DIR}" "${base}" version)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${version}")
    return()
endif()

# The build scripts pass the version in (the WSL-driven Windows build compiles a copy of the sources
# without git); otherwise it comes from git here.
set(DJIVCAM_VERSION "" CACHE STRING "Full version, e.g. 0.1.0 or 0.1.0-dev+g1a2b3c4 (default: from git)")
if(DJIVCAM_VERSION)
    set(DJIVCAM_VERSION_FULL "${DJIVCAM_VERSION}")
else()
    djivcam_detect_version("${PROJECT_SOURCE_DIR}" "${PROJECT_VERSION}" DJIVCAM_VERSION_FULL)
endif()
message(STATUS "DJI VCam version ${DJIVCAM_VERSION_FULL}")

# Gives a Windows executable or DLL its version resource (Explorer's Details tab), and optionally
# an icon: djivcam_add_version_resource(<target> "<description>" [ICON <file.ico>]).
function(djivcam_add_version_resource target description)
    if(NOT WIN32)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "ICON" "")
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "SHARED_LIBRARY")
        set(DJIVCAM_RC_FILETYPE VFT_DLL)
        set(DJIVCAM_RC_FILENAME "${target}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    else()
        set(DJIVCAM_RC_FILETYPE VFT_APP)
        set(DJIVCAM_RC_FILENAME "${target}${CMAKE_EXECUTABLE_SUFFIX}")
    endif()
    set(DJIVCAM_RC_DESCRIPTION "${description}")
    set(DJIVCAM_RC_NAME "${target}")
    set(DJIVCAM_RC_ICON "")
    if(arg_ICON)
        set(DJIVCAM_RC_ICON "IDI_ICON1 ICON \"${arg_ICON}\"")
    endif()
    set(DJIVCAM_RC_FLAGS 0)
    if(DJIVCAM_VERSION_FULL MATCHES "-")
        set(DJIVCAM_RC_FLAGS VS_FF_PRERELEASE)  # not a release build
    endif()
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc" @ONLY)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
endfunction()
