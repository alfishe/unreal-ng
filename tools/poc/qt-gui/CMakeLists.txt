cmake_minimum_required(VERSION 3.19)
project(unreal-ng-ui VERSION 0.9.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Resolve paths
get_filename_component(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CORE_DIR "${ROOT_DIR}/core/src")

# Qt path detection (same as unreal-qt)
if(NOT QT_INSTALL_PATH)
    if(APPLE)
        set(QT_POSSIBLE_PATHS
            "$ENV{HOME}/Qt/6.9.3/macos"
            "$ENV{HOME}/Qt/6.5.3/macos"
            "$ENV{HOME}/Qt/6.5.0/macos"
        )
    elseif(UNIX AND NOT APPLE)
        set(QT_POSSIBLE_PATHS
            "$ENV{HOME}/Qt/6.9.3/gcc_64"
            "$ENV{HOME}/Qt/6.5.3/gcc_64"
        )
    elseif(WIN32)
        set(QT_POSSIBLE_PATHS
            "C:/Qt/6.9.3/msvc2022_64"
            "C:/Qt/6.5.3/msvc2019_64"
        )
    endif()

    foreach(POSSIBLE_PATH ${QT_POSSIBLE_PATHS})
        if(EXISTS "${POSSIBLE_PATH}/lib/cmake/Qt6/Qt6Config.cmake")
            set(QT_INSTALL_PATH "${POSSIBLE_PATH}")
            message(STATUS "Found Qt6 at: ${QT_INSTALL_PATH}")
            break()
        endif()
    endforeach()
endif()

if(QT_INSTALL_PATH)
    list(APPEND CMAKE_PREFIX_PATH "${QT_INSTALL_PATH}")
endif()

# Qt components
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Multimedia)
qt_standard_project_setup()

# Build core library if not already available (standalone build)
if(NOT TARGET unrealng::core)
    # For standalone builds, we need the core library
    # Option 1: Build from unified root (recommended)
    # Option 2: Use pre-built library
    message(STATUS "unrealng::core not found - checking for standalone core build")

    # Try to find pre-built core library
    find_library(CORE_LIBRARY
        NAMES core libcore
        PATHS "${ROOT_DIR}/cmake-build-release/lib"
              "${ROOT_DIR}/cmake-build-debug/lib"
              "${ROOT_DIR}/build/lib"
        NO_DEFAULT_PATH
    )

    if(CORE_LIBRARY)
        message(STATUS "Found pre-built core library: ${CORE_LIBRARY}")

        # Also find recording library (core depends on it)
        find_library(RECORDING_LIBRARY
            NAMES recording librecording
            PATHS "${ROOT_DIR}/cmake-build-release/lib"
                  "${ROOT_DIR}/cmake-build-debug/lib"
                  "${ROOT_DIR}/build/lib"
            NO_DEFAULT_PATH
        )

        add_library(unrealng::core STATIC IMPORTED)
        set_target_properties(unrealng::core PROPERTIES
            IMPORTED_LOCATION "${CORE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CORE_DIR};${ROOT_DIR}/core"
        )

        if(RECORDING_LIBRARY)
            message(STATUS "Found pre-built recording library: ${RECORDING_LIBRARY}")
            add_library(unrealng::recording STATIC IMPORTED)
            set_target_properties(unrealng::recording PROPERTIES
                IMPORTED_LOCATION "${RECORDING_LIBRARY}"
            )
        endif()
    else()
        message(WARNING "Core library not found. Build from root CMakeLists.txt or build core first.")
        message(STATUS "Tip: cmake -B build -S ${ROOT_DIR} && cmake --build build --target core")
    endif()
endif()

# Platform-specific icon resources
set(APP_ICON_RESOURCE)
if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE unreal-ng.icns)
    set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app/unreal-ng.icns")
    set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set(APP_ICON_RESOURCE ${APP_ICON_MACOS})
elseif(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
        @ONLY
    )
    set(APP_ICON_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

# Platform-specific sources and libraries
set(PLATFORM_SOURCES)
set(PLATFORM_LIBS)

if(APPLE)
    enable_language(OBJCXX)
    set(MACOS_OBJCXX_SOURCES
        src/platform/macos/FullscreenHelper.mm
        src/platform/macos/MetalScreenWidget.mm
    )
    set_source_files_properties(${MACOS_OBJCXX_SOURCES} PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    list(APPEND PLATFORM_SOURCES
        src/platform/macos/FullscreenHelper.h
        src/platform/macos/MetalScreenWidget.h
        ${MACOS_OBJCXX_SOURCES}
    )
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(VIDEOTOOLBOX_FRAMEWORK VideoToolbox REQUIRED)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    list(APPEND PLATFORM_LIBS
        ${COCOA_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${VIDEOTOOLBOX_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
    )
elseif(WIN32)
    # Windows: TODO - add D3D or OpenGL screen widget
elseif(UNIX)
    # Linux: TODO - add OpenGL screen widget
endif()

# Emulator integration sources
set(EMULATOR_SOURCES
    src/emulator/EmulatorWidget.cpp
    src/emulator/EmulatorWidget.h
    src/emulator/KeyboardManager.cpp
    src/emulator/KeyboardManager.h
    src/emulator/AppSoundManager.cpp
    src/emulator/AppSoundManager.h
    src/emulator/FileManager.cpp
    src/emulator/FileManager.h
    src/dialogs/AudioSettingsDialog.cpp
    src/dialogs/AudioSettingsDialog.h
)

qt_add_executable(unreal-ng-ui
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ScreenWidget.cpp
    src/ScreenWidget.h
    src/StatusIndicator.cpp
    src/StatusIndicator.h
    ${EMULATOR_SOURCES}
    resources/assets.qrc
    ${APP_ICON_RESOURCE}
    ${PLATFORM_SOURCES}
)

target_include_directories(unreal-ng-ui PRIVATE
    src
    ${CORE_DIR}
    ${ROOT_DIR}/core
    ${ROOT_DIR}/core/recording/src  # For recordingmanager.h when ENABLE_RECORDING is defined
)

# Link libraries
target_link_libraries(unreal-ng-ui PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    ${PLATFORM_LIBS}
)

# Link core if available
message(STATUS "Checking for unrealng::core target...")
if(TARGET unrealng::core)
    message(STATUS "unrealng::core target EXISTS - linking and defining HAS_EMULATOR_CORE")
    target_link_libraries(unreal-ng-ui PRIVATE unrealng::core)
    target_compile_definitions(unreal-ng-ui PRIVATE HAS_EMULATOR_CORE)

    # CRITICAL: Must define ENABLE_RECORDING to match how libcore.a was built
    # This affects EmulatorContext class layout (pRecordingManager pointer)
    # Without this, pFeatureManager offset is wrong and we get crashes
    target_compile_definitions(unreal-ng-ui PRIVATE ENABLE_RECORDING)

    # Recording library is a dependency of core
    if(TARGET unrealng::recording)
        target_link_libraries(unreal-ng-ui PRIVATE unrealng::recording)
    endif()
else()
    message(STATUS "unrealng::core target NOT FOUND")
endif()

# macOS: link iconv (required by core)
if(APPLE)
    target_link_directories(unreal-ng-ui PRIVATE /usr/lib)
    target_link_libraries(unreal-ng-ui PRIVATE iconv)
endif()

set_target_properties(unreal-ng-ui PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER org.unreal-ng.ui
    MACOSX_BUNDLE_BUNDLE_NAME "unreal-ng"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE unreal-ng.icns
)

# Copy data files (configs, ROMs) to app bundle/output
set(DATA_DIR "${ROOT_DIR}/data")
set(DATA_CONFIG_SRC "${ROOT_DIR}/cmake-build-release/bin/unreal.ini")

if(APPLE)
    # For macOS app bundle, copy to Resources
    set(BUNDLE_RESOURCES_DIR "$<TARGET_BUNDLE_DIR:unreal-ng-ui>/Contents/Resources")

    # Copy unreal.ini from build output (it's generated during main build)
    add_custom_command(TARGET unreal-ng-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DATA_CONFIG_SRC}" "${BUNDLE_RESOURCES_DIR}/unreal.ini"
        COMMENT "Copying unreal.ini to app bundle"
    )

    # Copy ROM files
    add_custom_command(TARGET unreal-ng-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DATA_DIR}/rom" "${BUNDLE_RESOURCES_DIR}/rom"
        COMMENT "Copying ROM files to app bundle"
    )
else()
    # For Windows/Linux, copy to binary directory
    add_custom_command(TARGET unreal-ng-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DATA_CONFIG_SRC}" "$<TARGET_FILE_DIR:unreal-ng-ui>/unreal.ini"
        COMMENT "Copying unreal.ini"
    )
    add_custom_command(TARGET unreal-ng-ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DATA_DIR}/rom" "$<TARGET_FILE_DIR:unreal-ng-ui>/rom"
        COMMENT "Copying ROM files"
    )
endif()

install(TARGETS unreal-ng-ui
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)
