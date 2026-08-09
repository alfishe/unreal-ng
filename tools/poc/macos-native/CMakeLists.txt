# Native macOS (Swift + SwiftUI + Metal) front-end for the unreal-ng core.
# No Qt anywhere in this target.
#
# Layout:
#   unreal-ng-macos-bridge : OBJCXX static library, the only place C++ core is touched
#   unreal-ng-macos        : Swift executable (.app bundle) that imports the bridge
#                            through an Objective-C bridging header
#
# Swift requires the Ninja or Xcode generator; CMake's Makefile generator cannot
# build Swift. Configure with:  cmake -G Ninja -B build

cmake_minimum_required(VERSION 3.26)

# Recording support must be decided BEFORE core is added: ENABLE_RECORDING changes
# the EmulatorContext layout (pRecordingManager member). If the app and libcore
# disagree, every member after that point is read at the wrong offset and the
# process crashes. Core exports the definition PUBLIC, so linking against
# unrealng::core is what keeps this consistent - never define it by hand here.
option(ENABLE_RECORDING "Enable audio/video recording support" ON)

project(unreal-ng-macos VERSION 0.1.0 LANGUAGES CXX OBJCXX Swift)

if(NOT APPLE)
    message(FATAL_ERROR "This POC is macOS only")
endif()

if(CMAKE_GENERATOR MATCHES "Makefiles")
    message(FATAL_ERROR
        "CMake cannot build Swift with the '${CMAKE_GENERATOR}' generator.\n"
        "Re-configure with:  cmake -G Ninja -B build")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS deployment target" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

get_filename_component(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CORE_DIR "${ROOT_DIR}/core/src")

# ---------------------------------------------------------------------------
# Core (added as a subdirectory so it rebuilds automatically)
# ---------------------------------------------------------------------------
if(NOT TARGET unrealng::core)
    if(NOT EXISTS "${CORE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Core library source not found at ${CORE_DIR}/CMakeLists.txt")
    endif()

    if(ENABLE_RECORDING AND NOT TARGET unrealng::recording)
        add_subdirectory("${ROOT_DIR}/core/recording" "${CMAKE_CURRENT_BINARY_DIR}/recording")
    endif()

    add_subdirectory("${CORE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/core")
endif()

# ---------------------------------------------------------------------------
# Objective-C++ bridge
# ---------------------------------------------------------------------------
set(BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/bridge")

add_library(unreal-ng-macos-bridge STATIC
    ${BRIDGE_DIR}/UNEmulatorBridge.mm
    ${BRIDGE_DIR}/UNEmulatorBridge.h
    ${BRIDGE_DIR}/UNAudioOutput.mm
    ${BRIDGE_DIR}/UNAudioOutput.h
    ${BRIDGE_DIR}/UNZXKeys.h
)

set_source_files_properties(
    ${BRIDGE_DIR}/UNEmulatorBridge.mm
    ${BRIDGE_DIR}/UNAudioOutput.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

target_include_directories(unreal-ng-macos-bridge PUBLIC
    ${BRIDGE_DIR}
    ${CORE_DIR}
    ${ROOT_DIR}/core
)

target_link_libraries(unreal-ng-macos-bridge PUBLIC unrealng::core)
if(TARGET unrealng::recording)
    target_link_libraries(unreal-ng-macos-bridge PUBLIC unrealng::recording)
endif()

find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
target_link_libraries(unreal-ng-macos-bridge PUBLIC
    ${FOUNDATION_FRAMEWORK}
    ${AUDIOTOOLBOX_FRAMEWORK}
    iconv               # required by core on macOS
)

# ---------------------------------------------------------------------------
# Swift application
# ---------------------------------------------------------------------------

# The app pulls the C++ runtime in twice: core contributes CXX and the bridge
# contributes OBJCXX, and CMake appends the implicit link libraries of BOTH
# languages, so the link line ends up with two -lc++ and the linker warns.
# Drop the OBJCXX copy only — the CXX one still provides the runtime, so the
# C++ standard library stays linked exactly once.
list(REMOVE_ITEM CMAKE_OBJCXX_IMPLICIT_LINK_LIBRARIES c++)

set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/app")

add_executable(unreal-ng-macos MACOSX_BUNDLE
    ${APP_DIR}/App.swift
    ${APP_DIR}/EmulatorController.swift
    ${APP_DIR}/MetalScreenView.swift
    ${APP_DIR}/WindowChrome.swift
    ${APP_DIR}/ZXKeyboardMap.swift
)

# Swift sees the Objective-C bridge through the bridging header. Nothing C++ may
# be reachable from it - Swift's importer cannot parse C++ headers.
target_compile_options(unreal-ng-macos PRIVATE
    "SHELL:-import-objc-header ${BRIDGE_DIR}/BridgingHeader.h"
    "SHELL:-Xcc -I${BRIDGE_DIR}"
)

target_link_libraries(unreal-ng-macos PRIVATE
    unreal-ng-macos-bridge
)

set_target_properties(unreal-ng-macos PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER org.unreal-ng.macos-native
    MACOSX_BUNDLE_BUNDLE_NAME "unreal-ng"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# ---------------------------------------------------------------------------
# Runtime data: the core loads unreal.ini and ROMs from Contents/Resources
# (FileHelper::GetResourcesPath resolves the bundle path automatically).
# ---------------------------------------------------------------------------
set(BUNDLE_RESOURCES "$<TARGET_BUNDLE_DIR:unreal-ng-macos>/Contents/Resources")
set(CONFIG_FILE "${ROOT_DIR}/data/configs/pentagon512k/unreal.ini")

add_custom_command(TARGET unreal-ng-macos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_RESOURCES}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CONFIG_FILE}" "${BUNDLE_RESOURCES}/unreal.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ROOT_DIR}/data/rom" "${BUNDLE_RESOURCES}/rom"
    COMMENT "Copying unreal.ini and ROMs into the app bundle"
)

install(TARGETS unreal-ng-macos BUNDLE DESTINATION .)
