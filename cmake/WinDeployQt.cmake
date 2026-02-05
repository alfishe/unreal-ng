# WinDeployQt.cmake - Shared Windows Qt DLL deployment logic
# Include this module to add automatic Qt DLL deployment on Windows

# ========================================
# Automatic Qt DLL deployment on Windows
# ========================================
# This module provides a function to set up windeployqt and runtime DLL copying
# for Qt-based executables on Windows. Call this after add_executable().
#
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/WinDeployQt.cmake)
#   setup_windows_qt_deployment(${PROJECT_NAME})

function(setup_windows_qt_deployment TARGET_NAME)
    if(NOT WIN32)
        return()
    endif()
    
    # Find windeployqt executable from Qt installation
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${QT_FOUND_PATH}/bin"
        DOC "Path to windeployqt executable"
    )
    
    if(WINDEPLOYQT_EXECUTABLE)
        message(STATUS "[${TARGET_NAME}] Found windeployqt: ${WINDEPLOYQT_EXECUTABLE}")
        
        # Add post-build step to deploy Qt DLLs automatically
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                "$<TARGET_FILE:${TARGET_NAME}>"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
            COMMENT "[${TARGET_NAME}] Deploying Qt DLLs with windeployqt..."
        )
        
        # MinGW-specific: Copy compiler's runtime DLLs AFTER windeployqt
        # (windeployqt copies Qt's bundled MinGW DLLs which may be incompatible with MSYS2's GCC)
        if(MINGW)
            get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            message(STATUS "[${TARGET_NAME}] MinGW bin directory: ${MINGW_BIN_DIR}")
            
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libstdc++-6.dll"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN_DIR}/libwinpthread-1.dll"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>/"
                COMMENT "[${TARGET_NAME}] Copying MinGW runtime DLLs from compiler..."
            )
        endif()
    else()
        message(WARNING "[${TARGET_NAME}] windeployqt not found - Qt DLLs will not be auto-deployed.")
    endif()
endfunction()

# ========================================
# Get Qt paths based on compiler type
# ========================================
# Returns a list of Qt installation paths to search based on the current compiler

function(get_windows_qt_paths OUT_VAR)
    if(NOT WIN32)
        set(${OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()
    
    if(MINGW)
        message(STATUS "Detected MinGW compiler - searching for MinGW Qt builds")
        set(QT_PATHS
            "C:/Qt/6.9.3/mingw_64"
            "C:/Qt/6.5.3/mingw_64"
            "C:/Qt/6.5.0/mingw_64"
            "$ENV{QTDIR}"
        )
    elseif(MSVC)
        message(STATUS "Detected MSVC compiler - searching for MSVC Qt builds")
        set(QT_PATHS
            "C:/Qt/6.9.3/msvc2022_64"
            "C:/Qt/6.9.3/msvc2019_64"
            "C:/Qt/6.5.3/msvc2019_64"
            "C:/Qt/6.5.0/msvc2019_64"
            "$ENV{QTDIR}"
        )
    else()
        message(STATUS "Unknown Windows compiler - searching for any Qt build")
        set(QT_PATHS
            "C:/Qt/6.9.3/msvc2022_64"
            "C:/Qt/6.9.3/mingw_64"
            "C:/Qt/6.5.3/msvc2019_64"
            "C:/Qt/6.5.3/mingw_64"
            "$ENV{QTDIR}"
        )
    endif()
    
    set(${OUT_VAR} ${QT_PATHS} PARENT_SCOPE)
endfunction()
