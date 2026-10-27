# The Stream Deck companion (docs/inprogress/2026-10-19-streamdeck-integration): libsdb (models, HID protocol, images,
# scenario engine, automation), the daemon unreal-streamdeck, the developer CLI sdctl and the tests (no Qt, no link
# to the core), and Stream Deck Studio (studio/, Qt, when Qt is found). Added from the root CMakeLists.txt under
# UNREAL_STREAMDECK.

# Build settings are the project's: the C++ standard, build type, LTO and the macOS deployment target come from the
# root CMakeLists.txt; the warnings are the core's (core/src/CMakeLists.txt), applied to our sources, not to 3rdparty.

set(SD_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SD_3RDPARTY ${SD_DIR}/3rdparty)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(SD_WARNINGS -Wall -Wextra -Werror -Wno-format-security -Werror=unused-result -Werror=format-security
        -Wno-error=deprecated-declarations)
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
        list(APPEND SD_WARNINGS -Wno-free-nonheap-object -Wno-stringop-overflow -Wno-error=array-bounds)
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SD_WARNINGS -Wall -Wextra -Werror)
endif ()

# Windows 10 API level for the whole companion: cpp-httplib refuses anything lower, and MinGW's headers default to
# Windows 7 (0x0601); MSVC's SDK leaves it undefined (= the SDK's own, Windows 10)
if (WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif ()

# ---------------------------------------------------------------- hidapi (vendored, one backend per OS)
add_library(sd-hidapi STATIC)
target_include_directories(sd-hidapi PUBLIC ${SD_3RDPARTY}/hidapi/hidapi)
if(APPLE)
    target_sources(sd-hidapi PRIVATE ${SD_3RDPARTY}/hidapi/mac/hid.c)
    target_include_directories(sd-hidapi PUBLIC ${SD_3RDPARTY}/hidapi/mac)
    target_link_libraries(sd-hidapi PUBLIC "-framework IOKit" "-framework CoreFoundation")
elseif(WIN32)
    # through a wrapper that undoes the root's WIN32_LEAN_AND_MEAN; hid.c includes hidapi_descriptor_reconstruct.c
    target_sources(sd-hidapi PRIVATE ${SD_DIR}/lib/hidapi/windows-hid.c)
    target_include_directories(sd-hidapi PUBLIC ${SD_3RDPARTY}/hidapi/windows)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SD_UDEV REQUIRED IMPORTED_TARGET libudev)
    target_sources(sd-hidapi PRIVATE ${SD_3RDPARTY}/hidapi/linux/hid.c)
    target_link_libraries(sd-hidapi PRIVATE PkgConfig::SD_UDEV)
endif()
if(NOT MSVC)
    target_compile_options(sd-hidapi PRIVATE -w)   # third-party source
endif()

# ---------------------------------------------------------------- libsdb
file(GLOB SD_LIB_SOURCES CONFIGURE_DEPENDS ${SD_DIR}/lib/src/*.cpp)
add_library(sdb STATIC ${SD_LIB_SOURCES})
target_include_directories(sdb PUBLIC ${SD_DIR}/lib/include)
target_include_directories(sdb SYSTEM PUBLIC ${SD_3RDPARTY})   # nlohmann/json is part of the public headers
target_compile_options(sdb PRIVATE ${SD_WARNINGS})
# the source tree's data/: the last place the data directory is looked for (sdb/datadir.h), for a build not installed
get_filename_component(SD_SOURCE_DATA ${SD_DIR}/../../data ABSOLUTE)
set_source_files_properties(${SD_DIR}/lib/src/datadir.cpp PROPERTIES
    COMPILE_DEFINITIONS "SDB_SOURCE_DATA_DIR=\"${SD_SOURCE_DATA}\"")
if(NOT MSVC)
    set_source_files_properties(${SD_DIR}/lib/src/svg.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error")   # nanosvg inside
endif()
if(APPLE)
    target_link_libraries(sdb PRIVATE "-framework ApplicationServices")   # the keys action: CGEvent
elseif(WIN32)
    target_link_libraries(sdb PRIVATE ws2_32)   # sockets (sdb/eventloop.h)
endif()

# ---------------------------------------------------------------- libsdb-hid: decks on USB (hidapi)
# Only what opens a USB deck: the daemon and sdctl link it, Stream Deck Studio (a client, no USB) does not.
file(GLOB SD_HID_SOURCES CONFIGURE_DEPENDS ${SD_DIR}/lib/src/hid/*.cpp)
add_library(sdb-hid STATIC ${SD_HID_SOURCES})
target_compile_options(sdb-hid PRIVATE ${SD_WARNINGS})
target_link_libraries(sdb-hid PUBLIC sdb PRIVATE sd-hidapi)

# ---------------------------------------------------------------- shared CLI helpers
add_library(sdb-cli STATIC ${SD_DIR}/cli/cli.cpp)
target_include_directories(sdb-cli PUBLIC ${SD_DIR}/cli)
target_compile_options(sdb-cli PRIVATE ${SD_WARNINGS})
target_link_libraries(sdb-cli PUBLIC sdb-hid)

# ---------------------------------------------------------------- unreal-streamdeck (the daemon)
file(GLOB SD_DAEMON_SOURCES CONFIGURE_DEPENDS ${SD_DIR}/daemon/*.cpp)
add_executable(unreal-streamdeck ${SD_DAEMON_SOURCES})
target_compile_options(unreal-streamdeck PRIVATE ${SD_WARNINGS})
target_link_libraries(unreal-streamdeck PRIVATE sdb-cli)

# ---------------------------------------------------------------- sdctl (developer CLI)
file(GLOB SD_SDCTL_SOURCES CONFIGURE_DEPENDS ${SD_DIR}/sdctl/*.cpp)
add_executable(sdctl ${SD_SDCTL_SOURCES})
target_compile_options(sdctl PRIVATE ${SD_WARNINGS})
target_link_libraries(sdctl PRIVATE sdb-cli)

# ---------------------------------------------------------------- tests (hardware-free)
file(GLOB SD_TEST_SOURCES CONFIGURE_DEPENDS ${SD_DIR}/tests/*.cpp)
add_executable(streamdeck-tests EXCLUDE_FROM_ALL ${SD_TEST_SOURCES})
target_compile_options(streamdeck-tests PRIVATE ${SD_WARNINGS})
target_link_libraries(streamdeck-tests PRIVATE sdb)
# the source tree's data/ (the shipped catalogue, $data paths; not a copy in bin/) and the generator's fixture
target_compile_definitions(streamdeck-tests PRIVATE "SDB_TEST_SOURCE_DATA=\"${SD_SOURCE_DATA}\""
    "SDB_TEST_TOOLS_DIR=\"${SD_DIR}/tools\"")

# ---------------------------------------------------------------- Stream Deck Studio (Qt; skipped without Qt)
add_subdirectory(studio)
