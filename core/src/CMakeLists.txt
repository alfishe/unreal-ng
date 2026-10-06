cmake_minimum_required(VERSION 3.16.0)

message(">> core/src CMakeLists.txt")

# Project named 'core' on C++
project(core LANGUAGES CXX)

# Enable Objective-C++ on Apple platforms (for VideoToolbox encoder)
if(APPLE)
    enable_language(OBJCXX)
endif()

# Note: Shared memory mapping is now controlled at runtime via the 'sharedmemory' feature flag
# (see FeatureManager). The old compile-time options ENABLE_MEMORY_MAPPING, USE_SHAREDMEM_MAPPING,
# and USE_FILE_MAPPING have been removed.

# C++ standard is set by the root CMakeLists.txt (C++20 for MSVC, C++17 for others)
# Do not override here to maintain cross-platform compatibility

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release)
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # GCC-specific flags
    add_compile_options(-Wall -Wextra -Werror -Wno-format-security)

    # Treat specific warnings as errors
    add_compile_options(-Werror=unused-result -Werror=format-security)

    # Disable specific warnings if needed
    add_compile_options(-Wno-error=deprecated-declarations)  # Example for disabling specific warnings

    # gcc 14+ misfires -Wfree-nonheap-object and -Wstringop-overflow on plain
    # std::vector growth (libstdc++ _M_realloc_append internals; the PR108247
    # false-positive family). Seen on the mingw cross build (foldertapebuilder.cpp,
    # portdecoder.cpp); clang, MSVC and CI's gcc 15 compile the same code clean
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
        add_compile_options(-Wno-free-nonheap-object -Wno-stringop-overflow)
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang-specific flags
    add_compile_options(-Wall -Wextra -Werror)
endif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")

# Set paths
get_filename_component(ROOT_DIR ../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set (SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Include CMake helper functions file
include(${ROOT_DIR}/cmake/func.cmake)

# liblzma
add_subdirectory(3rdparty/liblzma)

# zstd v1.5.7 (embedded source — no FetchContent, no find_package).
# The CMake project in 3rdparty/zstd/ compiles the checked-in source
# tree under 3rdparty/zstd/lib/ directly into libzstd_static and
# exposes the `zstd::libzstd` alias target.
#
# Why embed: see 3rdparty/zstd/README.md. The TTD codec depends on zstd
# at the .ttd on-disk format protocol level, so we cannot ship without
# a known-pinned version. The C++ codec PoC (tools/poc/cpp/) confirmed
# zstd-1 remains optimal for the TTD workload in v2 — see
# docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md.
#
# CRITICAL: do NOT use directory-scoped include_directories() here — that
# would pollute every add_subdirectory under core/, including
# core/automation/webapi/lib/jsoncpp, which would then resolve
# `json/reader.h` to the Homebrew-installed jsoncpp
# (/opt/homebrew/include/json/reader.h) instead of the vendored 1.9.5
# headers, breaking the build. The libzstd_static target propagates its
# own include dir via INTERFACE_INCLUDE_DIRECTORIES, so we don't need to
# forward it manually.
add_subdirectory(3rdparty/zstd)

# gif — GIF writer shared by core (ScreenCapture) and the optional recording
# library; its own target so neither layer owns it (see its CMakeLists.txt).
# The guard lets every entry point that builds core declare it exactly once.
if (NOT TARGET gif)
    add_subdirectory(3rdparty/gif)
endif()

# miniz — zlib streams for SZX snapshots (see its CMakeLists.txt); a C
# target of its own, so the C++ source glob below does not see it
if (NOT TARGET miniz)
    add_subdirectory(3rdparty/miniz)
endif()

# libopl4 — MoonSound (YMF278B / OPL4) synthesis library, vendored in-house at
# 3rdparty/opl4 (std-lib only, own CMakeLists.txt). Declared here — not in the
# root file — so every entry point that builds core (root, unreal-qt,
# unreal-videowall, core/tests standalone) picks the target up exactly once.
# EXCLUDE_FROM_ALL keeps its opl4tests binary out of default builds; the opl4
# library itself is still compiled as a dependency of core. The guard lets
# tools/poc (-DBUILD_POC=ON) skip its research copy of the same target.
if (NOT TARGET opl4)
    add_subdirectory(3rdparty/opl4 EXCLUDE_FROM_ALL)
endif()

# libsam2695 — Dream SAM2695 General MIDI synthesizer (serial MIDI receiver, the chip's MIDI
# implementation, SF2 voice model), vendored in-house at 3rdparty/sam2695 like libopl4 (std-lib
# only, own CMakeLists.txt). EXCLUDE_FROM_ALL keeps its sam2695tests / sam2695render binaries out
# of default builds; build them by name. Nothing in the emulator uses the library yet.
if (NOT TARGET sam2695)
    add_subdirectory(3rdparty/sam2695 EXCLUDE_FROM_ALL)
endif()

# unreal-asm - cross-assembler source library (codecs, conversion, symbols), in-house at 3rdparty/unreal-asm, std-lib
# only, own CMakeLists.txt: the library, the zxasm CLI, the examples and (with TESTS) unreal-asm-tests, which the
# emulator's test-parallel runs. Design: docs/inprogress/2026-10-05-unreal-asm/
if (NOT TARGET unreal-asm)
    add_subdirectory(3rdparty/unreal-asm)
endif()

# All C++ sources in /src and sub-folders will be compiled
file (GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "liblzma/" FALSE OUTVAR CPP_FILES) # Has own CMakeLists.txt
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "3rdparty/opl4/" FALSE OUTVAR CPP_FILES) # Has own CMakeLists.txt
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "3rdparty/sam2695/" FALSE OUTVAR CPP_FILES) # Has own CMakeLists.txt
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "3rdparty/unreal-asm/" FALSE OUTVAR CPP_FILES) # Has own CMakeLists.txt
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "3rdparty/gif/" FALSE OUTVAR CPP_FILES) # Has own CMakeLists.txt
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "3rdparty/eve-emu/" FALSE OUTVAR CPP_FILES) # Has own CMakeLists.txt (root ENABLE_VDAC2)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/recording/" FALSE OUTVAR CPP_FILES) # Moved to separate library
# OS-specific implementations (src/platform/<os>/): only the host's own.
# posix/ serves macOS and Linux; macos/ and linux/ hold what differs between them
if (WIN32)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/posix/" FALSE OUTVAR CPP_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/macos/" FALSE OUTVAR CPP_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/linux/" FALSE OUTVAR CPP_FILES)
elseif (APPLE)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/windows/" FALSE OUTVAR CPP_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/linux/" FALSE OUTVAR CPP_FILES)
else()
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/windows/" FALSE OUTVAR CPP_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/platform/macos/" FALSE OUTVAR CPP_FILES)
endif()

# ymfm (YM2203 core for the TurboSound FM device): vendored at 3rdparty/ymfm.
# Only the OPN family and what it links against (SSG, ADPCM) is compiled; the
# OPL/PCM pair in the same directory is not used by core. ymfm_opn.cpp includes
# ymfm_fm.ipp (template implementations), so the .ipp never compiles
# standalone. The vendored .cpp files are not warning-clean under our
# -Wall -Wextra -Werror (upstream keeps unused parameters) and must not be
# force-fed stdafx.h, so skip the PCH and relax that single warning on those
# files only. See 3rdparty/ymfm/PATCHES.md for the local source patch.
file (GLOB YMFM_ALL_CPP ${SOURCE_DIR}/3rdparty/ymfm/*.cpp)
list(REMOVE_ITEM CPP_FILES ${YMFM_ALL_CPP})
set(YMFM_CPP
    ${SOURCE_DIR}/3rdparty/ymfm/ymfm_opn.cpp
    ${SOURCE_DIR}/3rdparty/ymfm/ymfm_ssg.cpp
    ${SOURCE_DIR}/3rdparty/ymfm/ymfm_adpcm.cpp
)
list(APPEND CPP_FILES ${YMFM_CPP})
set_source_files_properties(${YMFM_CPP} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if (NOT CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set_source_files_properties(${YMFM_CPP} PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")
endif()

# unreal-z80 (Z80 coprocessor core for the General Sound device): self-contained
# C++17 library, compile units picked up by the glob above. Kept out of the
# stdafx.h PCH so its private headers never meet the emulator's CPU macros; its
# C-style tables live in namespace Z80Lib (see 3rdparty/unreal-z80/README.md)
file (GLOB UNREAL_Z80_FILES ${SOURCE_DIR}/3rdparty/unreal-z80/*.cpp)
set_source_files_properties(${UNREAL_Z80_FILES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# z84c15 (the Sprinter's Zilog Z84C15 CPU: a fork of unreal-z80 0.5.0 with the
# chip's on-chip block): the same treatment; namespace Z84Lib, C API prefix
# Z84Cpu (see 3rdparty/z84c15/README.md)
file (GLOB Z84C15_FILES ${SOURCE_DIR}/3rdparty/z84c15/*.cpp)
set_source_files_properties(${Z84C15_FILES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# rapidyaml (single-header YAML / JSON parser): its definitions compile in one
# translation unit, ryml_impl.cpp. The vendored code is not warning-clean under
# -Wall -Wextra -Werror and must not be force-fed stdafx.h
set(RYML_IMPL_CPP ${SOURCE_DIR}/3rdparty/rapidyaml/ryml_impl.cpp)
set_source_files_properties(${RYML_IMPL_CPP} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if (MSVC)
    set_source_files_properties(${RYML_IMPL_CPP} PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${RYML_IMPL_CPP} PROPERTIES COMPILE_OPTIONS "-w")
endif()
message("CPP_FILES:${CPP_FILES}")

set (SOURCES ${CPP_FILES})

# Include Objective-C++ files on macOS natively
if (APPLE)
    file (GLOB_RECURSE MM_FILES ${SOURCE_DIR}/*.mm)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/build/" FALSE OUTVAR MM_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/CMakeFiles/" FALSE OUTVAR MM_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/recording/" FALSE OUTVAR MM_FILES) # Moved to separate library
    set_source_files_properties(${MM_FILES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    list(APPEND SOURCES ${MM_FILES})
endif()

message("SOURCES:${SOURCES}")

# Produce static library;  Create unrealng::core alias for the target
add_library(${PROJECT_NAME} STATIC ${SOURCES})
add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Declare _DEBUG symbol for compiler
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE} - defining '_DEBUG'")
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif (CMAKE_BUILD_TYPE MATCHES Debug)

# Recording support - if the option is set by parent CMake, define the symbol, add include path, and link
if (ENABLE_RECORDING)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_RECORDING)
    target_include_directories(${PROJECT_NAME} PUBLIC ${ROOT_DIR}/core/recording/src)
    # Build recording library if not already built by parent
    if (NOT TARGET unrealng::recording)
        add_subdirectory("${ROOT_DIR}/core/recording" "${CMAKE_CURRENT_BINARY_DIR}/recording")
    endif()
    # Link to recording so linker resolves RecordingManager symbols
    target_link_libraries(${PROJECT_NAME} PUBLIC unrealng::recording)
endif()

# Set include paths for compiler

# Set include paths for compiler.
# zstd's include dir is added PUBLIC so:
#   - Direct linkers (target_link_libraries(... core)) inherit it.
#   - Sibling targets that use
#     $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES> (e.g.
#     automation-cli, automation-lua, automation-webapi) also see it.
# This is the only correct way to propagate a system include path without
# polluting the global include search order (which would shadow vendored
# headers like jsoncpp's).
target_include_directories(${PROJECT_NAME} PUBLIC
    ${SOURCE_DIR}/
    ${SOURCE_DIR}/3rdparty
    ${SOURCE_DIR}/common
    ${SOURCE_DIR}/emulator
    # Generated build fingerprint header (buildinfo.h) - configured by the
    # root CMakeLists.txt from git metadata. PUBLIC so every consumer of
    # core (automation-webapi/mcp/cli, tests, Qt apps) can
    # #include "emulator/buildinfo.h".
    ${CMAKE_BINARY_DIR}/generated
)

# ymfm include dir: SYSTEM so warnings originating in the upstream headers
# never surface in our translation units (-Werror). PUBLIC so consumers
# (core-tests, benchmarks, the future TSFM device) inherit it through the
# core link and can #include <ymfm_opn.h> flat
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${SOURCE_DIR}/3rdparty/ymfm)


# 'stdafx.h' is our precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE ${SOURCE_DIR}/stdafx.h)


# Link lzma library
target_link_libraries(${PROJECT_NAME} PRIVATE lzma)

# Link zstd (TTD codec)
# PUBLIC because ttd_compression.h is a public header (included by
# ttd_codec_page_store.h → timetravelmanager.h → consumed by unreal-qt,
# automation/lua, automation/webapi, tests). All consumers need zstd's
# include path available.
target_link_libraries(${PROJECT_NAME} PUBLIC $<TARGET_NAME:zstd::libzstd>)
target_link_libraries(${PROJECT_NAME} PUBLIC miniz)

# Host-side TLS for emulated devices whose firmware speaks TLS itself (the ZiFi ESP32-S3's HTTPS):
# common/network/hosttls.cpp with OpenSSL, the library the WebAPI already uses. Without OpenSSL (or with
# UNREAL_HOST_TLS=OFF, e.g. release packages that drop the dependency) the device's TLS connect fails as on a
# module whose server is unreachable. PRIVATE: only hosttls.cpp includes OpenSSL
option(UNREAL_HOST_TLS "Host-side TLS for emulated TLS clients (needs OpenSSL)" ON)
if (UNREAL_HOST_TLS)
    find_package(OpenSSL QUIET)
    if (NOT OpenSSL_FOUND AND APPLE AND EXISTS "/opt/homebrew/opt/openssl")
        set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl")
        find_package(OpenSSL QUIET)
    endif()
    if (OpenSSL_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(${PROJECT_NAME} PRIVATE UNREAL_HOST_TLS=1)
        message(STATUS "[core] host TLS: OpenSSL ${OPENSSL_VERSION}")
    else()
        message(STATUS "[core] host TLS: off (OpenSSL not found)")
    endif()
endif()

# Link gif (ScreenCapture). PRIVATE: no public core header includes gif.h
target_link_libraries(${PROJECT_NAME} PRIVATE gif)

# Link libopl4 (MoonSound / YMF278B synthesis). PRIVATE: only core sources
# include opl4 headers or call the library — public headers such as
# soundmanager.h only forward-declare the device class. The static-archive
# dependency still reaches final executables (CMake's $<LINK_ONLY:>
# propagation), so unreal-qt / automation binaries resolve opl4 symbols
# without inheriting the library's include path.
target_link_libraries(${PROJECT_NAME} PRIVATE opl4)

# Compile gate for the MoonSound device implementation. PUBLIC: core-tests
# compiles core sources directly into its own binary and must see the same
# gate as the core library build (a mismatch would be an ODR hazard).
target_compile_definitions(${PROJECT_NAME} PUBLIC UNREALNG_HAVE_OPL4=1)

# Link libsam2695 (SAM2695 General MIDI synthesizer). PRIVATE for the same reason as opl4; the
# gate is PUBLIC so core-tests, which compiles core sources itself, sees the same definition.
target_link_libraries(${PROJECT_NAME} PRIVATE sam2695)
target_compile_definitions(${PROJECT_NAME} PUBLIC UNREALNG_HAVE_SAM2695=1)


# TS-Conf VDAC2 card: the FT812 library (root CMakeLists.txt, ENABLE_VDAC2). The
# gate is PUBLIC for the same reason as UNREALNG_HAVE_OPL4: core-tests compiles core
# sources itself and must see the same class layouts
if (ENABLE_VDAC2)
    target_link_libraries(${PROJECT_NAME} PRIVATE eve::emu)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_VDAC2)
endif()


# Platform-specific framework and library linkage
# Note: Recording-specific frameworks (VideoToolbox, AVFoundation, CoreMedia, CoreVideo, AudioToolbox)
# are now linked via the recording library (core/recording/CMakeLists.txt)
if (APPLE)
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)

    target_link_libraries(${PROJECT_NAME} PUBLIC
        ${FOUNDATION_LIB}
        ${COREFOUNDATION_LIB}
    )
endif ()

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE -static-libstdc++)
endif ()

# dlopen for optional host libraries loaded at run time (platform/dynamiclibrary.h: libpcap for the network bridge)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})

# Note: Recording-specific Windows libs (d3d11, dxgi, mfplat, mfuuid, mf, ole32)
# are now linked via the recording library (core/recording/CMakeLists.txt)
if (WIN32 AND MINGW)
    target_link_libraries(${PROJECT_NAME} PRIVATE -static-libstdc++ Shlwapi ws2_32 iphlpapi)
endif()

if (WIN32 AND MSVC)
    # ws2_32: sockets (host network bridge); iphlpapi: IcmpSendEcho (ping through the host)
    target_link_libraries(${PROJECT_NAME} PRIVATE Shlwapi ws2_32 iphlpapi)
endif()
