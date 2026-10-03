cmake_minimum_required(VERSION 3.16)

# windeployqt collects Qt, but not libraries detected by Drogon/Trantor (such
# as c-ares), nor their dependencies. Scan the completed staging tree.
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${MINGW_BIN_DIR}/objdump.exe")
file(GLOB _executables "${STAGING_DIR}/*.exe")
file(GLOB_RECURSE _libraries "${STAGING_DIR}/*.dll")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${_executables}
    LIBRARIES ${_libraries}
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    DIRECTORIES "${STAGING_DIR}" "${MINGW_BIN_DIR}" "${QT_BIN_DIR}" "${BUILD_BIN_DIR}"
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*")
if(_unresolved)
    message(FATAL_ERROR "Unresolved Windows runtime dependencies: ${_unresolved}")
endif()
foreach(_dll IN LISTS _resolved)
    get_filename_component(_directory "${_dll}" DIRECTORY)
    if(NOT _directory STREQUAL STAGING_DIR)
        message(STATUS "Bundling ${_dll}")
        file(COPY "${_dll}" DESTINATION "${STAGING_DIR}")
    endif()
endforeach()
