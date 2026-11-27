# Link-time optimisation (UNREAL_LTO), for release packages and benchmarks only. The per-instruction paths of the
# emulator cross translation units (the CPU libraries, the device hooks); with LTO they inline: 4-11 % of a frame on the
# heavy machines (docs/inprogress/2026-10-13-heavy-machines-perf/analysis.md). The price is the link: every binary
# re-optimizes the whole core (unreal-qt 3-16 minutes), so development, gate and test builds and CI go without it
# (owner, 2026-11-26). On: -DUNREAL_LTO=ON (the release workflow's tag and manual runs, deploy bundles asked for with
# UNREAL_LTO=1); a benchmarks build (-DBENCHMARKS=ON) has it on by default, as its numbers are about the shipped code.
# Release configuration only, never with a sanitizer. The MinGW release package goes without it: gcc's whole-program
# stage runs on one thread there and the link of core-tests took over 40 minutes (gcc 15.2, 2026-10-13).
# Included by the root CMakeLists.txt and by unreal-qt/CMakeLists.txt, after Sanitize.cmake and before any target.
include_guard(GLOBAL)

set(UNREAL_LTO_DEFAULT OFF)
if (BENCHMARKS)
    set(UNREAL_LTO_DEFAULT ON)
endif ()
option(UNREAL_LTO "Link-time optimisation in Release builds (release packages, benchmarks)" ${UNREAL_LTO_DEFAULT})
# The option was ENABLE_LTO, on by default, until 2026-11-26: a build directory configured before keeps that entry in
# its cache, which no longer means anything. Dropped, so an old directory builds without LTO like a new one
if (DEFINED CACHE{ENABLE_LTO})
    message(STATUS "LTO: ENABLE_LTO is gone, UNREAL_LTO decides (${UNREAL_LTO})")
    unset(ENABLE_LTO CACHE)
endif ()

if (UNREAL_LTO AND NOT SANITIZE)
    # Third-party subdirectories with an old cmake_minimum_required still get it
    set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT UNREAL_LTO_SUPPORTED OUTPUT UNREAL_LTO_MESSAGE)
    if (UNREAL_LTO_SUPPORTED)
        # The Release configuration only (single- and multi-config generators): Debug stays as it is
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # gcc repeats its flow-based warnings at link time over the whole program and reports false ones there:
            # -Wstringop-overflow in a range-for over std::array (3rdparty/sam2695 statearchive.h) and inside
            # std::string's operator+ (libstdc++ char_traits.h), -Wfree-nonheap-object in the tests' counting
            # operator delete (core/tests/_helpers/heapcounter.cpp). Off for the LTO link only: a build without LTO
            # (every development and CI build) runs them per file, where they stay on and are errors
            add_link_options(-Wno-stringop-overflow -Wno-free-nonheap-object)
            if (UNIX AND NOT APPLE)
                # Every object position independent, as Qt requires (Qt6::Platform gives -fPIC to its users only):
                # lto-wrapper builds the program with the weakest PIC mode among its objects, and the static
                # libraries' PIE then reaches Qt's data directly through copy relocations. With binutils 2.46
                # (Ubuntu 26.04) unreal-qt got a copy of QCoreApplication::self and crashed in QApplication's
                # constructor (qGuiApp null in screenAdded)
                set(CMAKE_POSITION_INDEPENDENT_CODE ON)
            endif ()
            if (MINGW)
                # lto1: "visibility attribute not supported in this configuration" for every object with a
                # visibility attribute (PE has none): nothing to fix on our side
                add_link_options(-Wno-attributes)
            endif ()
        endif ()
        message(STATUS "LTO: on for Release")
    else ()
        message(STATUS "LTO: not supported by this toolchain (${UNREAL_LTO_MESSAGE})")
    endif ()
else ()
    message(STATUS "LTO: off")
endif ()
