# The Stream Deck companion (docs/inprogress/2026-10-19-streamdeck-integration): libsdb (models, HID protocol, images,
# scenario engine, automation), the daemon unreal-streamdeck, the developer CLI sdctl and the tests. No Qt, no link
# to the core. Added from the root CMakeLists.txt under UNREAL_STREAMDECK.

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

# ---------------------------------------------------------------- hidapi (vendored, one backend per OS)
add_library(sd-hidapi STATIC)
target_include_directories(sd-hidapi PUBLIC ${SD_3RDPARTY}/hidapi/hidapi)
if(APPLE)
    target_sources(sd-hidapi PRIVATE ${SD_3RDPARTY}/hidapi/mac/hid.c)
    target_include_directories(sd-hidapi PUBLIC ${SD_3RDPARTY}/hidapi/mac)
    target_link_libraries(sd-hidapi PUBLIC "-framework IOKit" "-framework CoreFoundation")
elseif(WIN32)
    target_sources(sd-hidapi PRIVATE ${SD_3RDPARTY}/hidapi/windows/hid.c
        ${SD_3RDPARTY}/hidapi/windows/hidapi_descriptor_reconstruct.c)
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
set_source_files_properties(${SD_DIR}/lib/src/svg.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error")   # nanosvg inside
target_link_libraries(sdb PRIVATE sd-hidapi)
if(APPLE)
    target_link_libraries(sdb PRIVATE "-framework ApplicationServices")   # the keys action: CGEvent
endif()

# ---------------------------------------------------------------- shared CLI helpers
add_library(sdb-cli STATIC ${SD_DIR}/cli/cli.cpp)
target_include_directories(sdb-cli PUBLIC ${SD_DIR}/cli)
target_compile_options(sdb-cli PRIVATE ${SD_WARNINGS})
target_link_libraries(sdb-cli PUBLIC sdb)

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
