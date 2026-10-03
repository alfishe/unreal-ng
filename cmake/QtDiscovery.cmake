# Shared by the suite and standalone GUI builds. Let CMake handle system
# installations (including Debian multiarch and Fedora lib64 directories),
# Qt6_DIR, and CMAKE_PREFIX_PATH.
if(NOT TARGET Qt6::Core)
    # Environment prefixes precede the installer fallbacks, just like explicit
    # CMAKE_PREFIX_PATH entries. TO_CMAKE_PATH handles the host's path separator.
    file(TO_CMAKE_PATH "$ENV{CMAKE_PREFIX_PATH}" _qt_env_prefixes)
    list(APPEND CMAKE_PREFIX_PATH ${_qt_env_prefixes})
    if(NOT QT_INSTALL_PATH AND DEFINED ENV{QT_INSTALL_PATH})
        set(QT_INSTALL_PATH "$ENV{QT_INSTALL_PATH}" CACHE STRING "Qt prefix or Qt6Config.cmake directory")
    endif()
    if(QT_INSTALL_PATH)
        foreach(_qt_path IN LISTS QT_INSTALL_PATH)
            string(STRIP "${_qt_path}" _qt_path)
            list(APPEND CMAKE_PREFIX_PATH "${_qt_path}")
        endforeach()
    endif()
    if(DEFINED ENV{QTDIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
    endif()

    # Keep the release SDK's Qt Online Installer locations as fallbacks.
    foreach(_qt_version 6.9.3)
        if(APPLE)
            list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/${_qt_version}/macos")
        elseif(WIN32 AND MINGW)
            list(APPEND CMAKE_PREFIX_PATH "C:/Qt/${_qt_version}/mingw_64")
        elseif(WIN32)
            list(APPEND CMAKE_PREFIX_PATH "C:/Qt/${_qt_version}/msvc2022_64"
                "C:/Qt/${_qt_version}/msvc2019_64")
        else()
            list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/${_qt_version}/gcc_64")
        endif()
    endforeach()

    # Preserve upstream's complete-install preference without loading a partial
    # Qt config: imported targets from a failed find_package cannot be undone.
    # Check config directories as well as prefixes, including Linux lib layouts.
    function(_unrealng_qt6_config_is_complete config_dir result)
        set(${result} FALSE PARENT_SCOPE)
        get_filename_component(config_dir "${config_dir}" ABSOLUTE)
        if(NOT EXISTS "${config_dir}/Qt6Config.cmake")
            return()
        endif()
        get_filename_component(_qt_cmake_dir "${config_dir}" DIRECTORY)
        foreach(_component Core Gui Widgets Network Multimedia Svg Core5Compat OpenGL OpenGLWidgets)
            if(NOT EXISTS "${_qt_cmake_dir}/Qt6${_component}/Qt6${_component}Config.cmake")
                return()
            endif()
        endforeach()
        set(${result} TRUE PARENT_SCOPE)
    endfunction()
    set(_qt_candidates "${Qt6_DIR}" "$ENV{Qt6_DIR}" ${CMAKE_PREFIX_PATH})
    set(_qt_complete_config "")
    foreach(_qt_candidate IN LISTS _qt_candidates)
        if(_qt_candidate STREQUAL "")
            continue()
        endif()
        foreach(_qt_suffix "" /lib/cmake/Qt6 /lib64/cmake/Qt6 /lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/Qt6)
            _unrealng_qt6_config_is_complete("${_qt_candidate}${_qt_suffix}" _qt_complete)
            if(_qt_complete)
                set(_qt_complete_config "${_qt_candidate}${_qt_suffix}")
                break()
            endif()
        endforeach()
        if(_qt_complete_config)
            set(Qt6_DIR "${_qt_complete_config}" CACHE PATH "Qt6 package configuration directory" FORCE)
            break()
        endif()
    endforeach()

    # An explicitly enabled GUI must fail if Qt is unavailable. Otherwise CI
    # can report a successful build that contains none of the requested apps.
    find_package(Qt6 6.7 REQUIRED CONFIG COMPONENTS Core)
endif()
if(Qt6Core_VERSION VERSION_LESS 6.7)
    message(FATAL_ERROR "The GUI applications require Qt 6.7 or newer (found ${Qt6Core_VERSION})")
endif()

# Packaging needs the installation prefix, not the config directory. qmake
# knows the correct prefix even for lib64 and multiarch installations.
get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
if(NOT _qt_qmake)
    get_target_property(_qt_tool_configs Qt6::qmake IMPORTED_CONFIGURATIONS)
    foreach(_qt_config IN LISTS _qt_tool_configs)
        get_target_property(_qt_qmake Qt6::qmake "IMPORTED_LOCATION_${_qt_config}")
        if(_qt_qmake)
            break()
        endif()
    endforeach()
endif()
execute_process(COMMAND "${_qt_qmake}" -query QT_INSTALL_PREFIX
    OUTPUT_VARIABLE QT_FOUND_PATH OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _qt_query_result)
if(NOT _qt_query_result EQUAL 0 OR NOT QT_FOUND_PATH)
    message(FATAL_ERROR "Cannot query the Qt installation prefix with ${_qt_qmake}")
endif()
message(STATUS "Qt6 config: ${Qt6_DIR}; installation prefix: ${QT_FOUND_PATH}")
