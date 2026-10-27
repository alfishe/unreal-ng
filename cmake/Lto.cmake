# Link-time optimisation of Release builds (ENABLE_LTO, on by default except on MinGW). The per-instruction paths of the emulator cross
# translation units (the CPU libraries, the device hooks); with LTO they inline: 4-11 % of a frame on the heavy machines
# (docs/inprogress/2026-10-13-heavy-machines-perf/analysis.md). Off: -DENABLE_LTO=OFF (faster links while developing).
# Not with a sanitizer build (the instrumented objects are not worth the link time) and only where the toolchain has it.
# Included by the root CMakeLists.txt and by unreal-qt/CMakeLists.txt, after Sanitize.cmake and before any target.
include_guard(GLOBAL)

# MinGW: off unless asked for. It builds and passes the tests with it, but gcc's whole-program stage runs on one thread
# there and the link of core-tests took over 40 minutes (gcc 15.2, 2026-10-13)
set(UNREAL_LTO_DEFAULT ON)
if (MINGW)
    set(UNREAL_LTO_DEFAULT OFF)
endif ()
option(ENABLE_LTO "Link-time optimisation in Release builds" ${UNREAL_LTO_DEFAULT})

if (ENABLE_LTO AND NOT SANITIZE)
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
            # operator delete (core/tests/_helpers/heapcounter.cpp). None shows in a per-file compile, where the
            # warnings stay on
            add_link_options(-Wno-stringop-overflow -Wno-free-nonheap-object)
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
