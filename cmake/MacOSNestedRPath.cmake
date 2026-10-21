# Script mode: point a nested app's binary at its container's frameworks.
#   cmake -DBINARY=<Mach-O> -DRPATH=<rpath to add> -P MacOSNestedRPath.cmake
#
# Used by package_suite_macos for Stream Deck Studio nested in UnrealNG.app (Contents/Applications/...): macdeployqt
# deploys Qt once, into UnrealNG.app/Contents/Frameworks, and with -executable= rewrites the studio's Qt references
# (@loader_path/../../../../Frameworks with Qt 6.9). A guard against a macdeployqt that leaves the build tree's rpaths:
# every absolute rpath (the Qt SDK, the build tree) is deleted, so the packaged binary cannot pick up a Qt outside the
# bundle, and RPATH is added when missing. Must run before signing.

if(NOT BINARY OR NOT RPATH)
    message(FATAL_ERROR "MacOSNestedRPath.cmake: pass -DBINARY=<file> -DRPATH=<rpath>")
endif()
if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "MacOSNestedRPath.cmake: ${BINARY} does not exist")
endif()

execute_process(COMMAND otool -l "${BINARY}" OUTPUT_VARIABLE _load RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "otool -l ${BINARY} failed")
endif()

# LC_RPATH entries: "cmd LC_RPATH" / "cmdsize N" / "path <p> (offset N)"
string(REGEX MATCHALL "cmd LC_RPATH[^\n]*\n[^\n]*\n[ \t]*path [^\n]+" _entries "${_load}")
set(_have FALSE)
foreach(_entry IN LISTS _entries)
    string(REGEX REPLACE ".*path (.+) \\(offset [0-9]+\\).*" "\\1" _path "${_entry}")
    if(_path STREQUAL RPATH)
        set(_have TRUE)
    elseif(NOT _path MATCHES "^@")
        message(STATUS "${BINARY}: deleting rpath ${_path}")
        execute_process(COMMAND install_name_tool -delete_rpath "${_path}" "${BINARY}" RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "install_name_tool -delete_rpath ${_path} failed")
        endif()
    endif()
endforeach()
if(NOT _have)
    execute_process(COMMAND install_name_tool -add_rpath "${RPATH}" "${BINARY}" RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "install_name_tool -add_rpath ${RPATH} failed")
    endif()
endif()
