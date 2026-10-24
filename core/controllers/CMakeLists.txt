cmake_minimum_required(VERSION 3.16)
project(controllers LANGUAGES CXX)

# Objective-C++ on macOS: the GameController companion (platform/macos/gccompanion.mm)
if(APPLE)
    enable_language(OBJCXX)
endif()

# The host game controllers library (docs/inprogress/2026-10-17-controller-manager/architecture.md §3): backends,
# device identity, the canonical control vocabulary, the input router, profiles. No Qt. Built like core/recording:
# core links it PUBLIC under ENABLE_CONTROLLERS; it uses only the core's controller headers (no core symbols).
# Added from core/src/CMakeLists.txt, so it inherits the core's warning flags (zero warnings).

# C++ standard - MUST match the rest of the project (C++20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(">> <${PROJECT_NAME}>")

get_filename_component(ROOT_DIR ../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(${ROOT_DIR}/cmake/func.cmake)

option(ENABLE_CONTROLLERS "Enable host game controllers" ON)

if(NOT ENABLE_CONTROLLERS)
    message(STATUS "Controllers disabled - skipping ${PROJECT_NAME} library")
    return()
endif()

file(GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES)
# Each OS builds its own backends only (architecture.md §4)
if(NOT APPLE)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/macos/" FALSE OUTVAR CPP_FILES)
endif()
if(NOT WIN32)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/windows/" FALSE OUTVAR CPP_FILES)
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/linux/" FALSE OUTVAR CPP_FILES)
endif()

# The device database (device-database.md §2) is compiled in, so a headless build needs no data folder
set(DEVICES_JSON_FILE ${ROOT_DIR}/data/controllers/devices.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DEVICES_JSON_FILE})
file(READ ${DEVICES_JSON_FILE} DEVICES_JSON)
# MSVC takes up to 16380 bytes in one literal (C2026) and about 64 KiB in adjacent ones: pieces cut at line ends, joined
# at run time
set(DEVICES_JSON_PIECES "")
set(DEVICES_JSON_REST "${DEVICES_JSON}")
string(LENGTH "${DEVICES_JSON_REST}" DEVICES_JSON_LENGTH)
while(DEVICES_JSON_LENGTH GREATER 0)
    if(DEVICES_JSON_LENGTH GREATER 16000)
        string(SUBSTRING "${DEVICES_JSON_REST}" 0 16000 DEVICES_JSON_PIECE)
        string(FIND "${DEVICES_JSON_PIECE}" "\n" DEVICES_JSON_CUT REVERSE)
        if(DEVICES_JSON_CUT LESS 1)
            message(FATAL_ERROR "${DEVICES_JSON_FILE}: a line of more than 16000 bytes")
        endif()
        math(EXPR DEVICES_JSON_CUT "${DEVICES_JSON_CUT} + 1")
        string(SUBSTRING "${DEVICES_JSON_REST}" 0 ${DEVICES_JSON_CUT} DEVICES_JSON_PIECE)
        string(SUBSTRING "${DEVICES_JSON_REST}" ${DEVICES_JSON_CUT} -1 DEVICES_JSON_REST)
    else()
        set(DEVICES_JSON_PIECE "${DEVICES_JSON_REST}")
        set(DEVICES_JSON_REST "")
    endif()
    string(APPEND DEVICES_JSON_PIECES "        R\"devicesjson(${DEVICES_JSON_PIECE})devicesjson\",\n")
    string(LENGTH "${DEVICES_JSON_REST}" DEVICES_JSON_LENGTH)
endwhile()
configure_file(${SOURCE_DIR}/devicesjson.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/devicesjson.cpp @ONLY)
list(APPEND CPP_FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/devicesjson.cpp)

# The default profiles and the schemes (mapping-and-profiles.md §7.5) are compiled in the same way, one literal per
# file; the game profiles stay files (runtime data, through games/index.yaml)
set(PROFILES_DIR ${ROOT_DIR}/data/controllers/profiles)
file(GLOB BUILTIN_PROFILE_FILES CONFIGURE_DEPENDS
    ${PROFILES_DIR}/play/*.yaml ${PROFILES_DIR}/debug/*.yaml ${PROFILES_DIR}/schemes/*.yaml)
list(SORT BUILTIN_PROFILE_FILES)
set(BUILTIN_PROFILES "")
foreach(PROFILE_FILE ${BUILTIN_PROFILE_FILES})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROFILE_FILE})
    file(READ ${PROFILE_FILE} PROFILE_TEXT)
    string(LENGTH "${PROFILE_TEXT}" PROFILE_LENGTH)
    file(RELATIVE_PATH PROFILE_NAME ${PROFILES_DIR} ${PROFILE_FILE})
    # MSVC takes up to 16380 bytes in one literal
    if(PROFILE_LENGTH GREATER 16000)
        message(FATAL_ERROR "${PROFILE_NAME}: ${PROFILE_LENGTH} bytes, a compiled-in profile holds at most 16000")
    endif()
    string(APPEND BUILTIN_PROFILES "    {\"${PROFILE_NAME}\", R\"profile(${PROFILE_TEXT})profile\"},\n")
endforeach()
configure_file(${SOURCE_DIR}/profiles/builtinprofiles.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/builtinprofiles.cpp
               @ONLY)
list(APPEND CPP_FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/builtinprofiles.cpp)

if(APPLE)
    file(GLOB_RECURSE MM_FILES ${SOURCE_DIR}/platform/macos/*.mm)
    set_source_files_properties(${MM_FILES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS "-fobjc-arc")
    list(APPEND CPP_FILES ${MM_FILES})
endif()

add_library(${PROJECT_NAME} STATIC ${CPP_FILES})
add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${SOURCE_DIR}/
    ${ROOT_DIR}/core/src/           # the core's controller types (emulator/io/controllers/*.h, header-only use)
    ${ROOT_DIR}/core/swsignature/src/  # match.h: the signature match rules, header-only (the library needs the core)
)

target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_CONTROLLERS)

# rapidyaml for the profile files (profiles/yaml.cpp); its own library, not the core's
if(NOT TARGET rapidyaml)
    add_subdirectory(${ROOT_DIR}/core/src/3rdparty/rapidyaml ${CMAKE_CURRENT_BINARY_DIR}/rapidyaml)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE rapidyaml)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PUBLIC "-framework IOKit" "-framework CoreFoundation" "-framework Foundation"
                          "-weak_framework GameController" "-weak_framework CoreHaptics")
elseif(WIN32)
    # setupapi: SetupDiGetClassDevs (usbpresence.cpp, the Dial's HID interfaces); hid: HidD_* / HidP_* (Raw Input
    # backend). GameInput and XInput are loaded at run time
    target_link_libraries(${PROJECT_NAME} PUBLIC setupapi hid)
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif()

message("<< <${PROJECT_NAME}>")
