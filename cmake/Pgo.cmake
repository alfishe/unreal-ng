# Profile-guided optimisation of Release builds (UNREAL_PGO). The release workflow builds three times in one build
# directory: GENERATE (instrumented unreal-qt), the training run tools/pgo/train.py, then USE (the packages). The
# compiler lays out the hot paths from the profile: DOOM on VDAC4 -21 % frame time, the machines' frame benchmarks
# -19..-33 % (MSVC x86_64, Zen 2, 2026-11-19; tools/pgo/README.md).
#   GENERATE  instrument; the training run writes the profile into UNREAL_PGO_DIR
#   USE       optimise with the profile in UNREAL_PGO_DIR
#   (empty)   off: a normal build
# Toolchains:
#   MSVC      unreal-qt only (unreal_pgo_target): /GENPROFILE, /USEPROFILE on the LTCG link. Needs LTO (/GL objects);
#             USE only relinks. The training writes unreal-qt!N.pgc beside unreal-qt.pgd.
#   GCC       every object: -fprofile-generate / -fprofile-use. The .gcda files are named after the object paths,
#             so GENERATE and USE build in the same directory.
#   Clang     every object: -fprofile-generate writes .profraw files; train.py merges them into default.profdata
#             (llvm-profdata, on macOS through xcrun) for -fprofile-use.
# Included by the root CMakeLists.txt and by unreal-qt/CMakeLists.txt, after Lto.cmake and before any target.
include_guard(GLOBAL)

set(UNREAL_PGO "" CACHE STRING "Profile-guided optimisation: GENERATE | USE (empty: off)")
set_property(CACHE UNREAL_PGO PROPERTY STRINGS "" GENERATE USE)
set(UNREAL_PGO_DIR "${CMAKE_BINARY_DIR}/pgo" CACHE PATH "The profile of UNREAL_PGO (written by GENERATE, read by USE)")

if (UNREAL_PGO AND NOT UNREAL_PGO MATCHES "^(GENERATE|USE)$")
    message(FATAL_ERROR "UNREAL_PGO=${UNREAL_PGO}: expected GENERATE, USE or empty")
endif ()
if (UNREAL_PGO AND SANITIZE)
    message(FATAL_ERROR "UNREAL_PGO and SANITIZE do not go together: a build directory of its own for each")
endif ()

if (UNREAL_PGO)
    file(MAKE_DIRECTORY "${UNREAL_PGO_DIR}")
    # Compiled languages only: the profile flags mean nothing to the assembler
    set(_unrealPgoLang "$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>")
    if (MSVC)
        if (NOT CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE)
            message(FATAL_ERROR "UNREAL_PGO with MSVC needs LTO (ENABLE_LTO=ON): the profile applies at the LTCG link")
        endif ()
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # The profile counters (GENERATE) and the profile's inlining (USE) change what gcc's flow analysis sees: it
        # reports false maybe-uninitialized (a scratch array read only up to its filled count, filter_decimator.h).
        # The plain CI builds keep the warning as an error
        add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:-Wno-maybe-uninitialized>")
        if (UNREAL_PGO STREQUAL "GENERATE")
            # Atomic counters: the emulation, sound and UI threads run the same code at once
            set(_unrealPgoFlags "-fprofile-generate=${UNREAL_PGO_DIR}" -fprofile-update=prefer-atomic)
        else ()
            # Code the training never ran keeps the normal optimisation (-fprofile-partial-training), a counter a
            # race still bent is put right (-fprofile-correction), and objects without a profile (the build-time
            # tools, the other applications' own code) build without the warning
            set(_unrealPgoFlags "-fprofile-use=${UNREAL_PGO_DIR}" -fprofile-partial-training -fprofile-correction
                -Wno-missing-profile)
        endif ()
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if (UNREAL_PGO STREQUAL "GENERATE")
            set(_unrealPgoFlags "-fprofile-generate=${UNREAL_PGO_DIR}")
        else ()
            if (NOT EXISTS "${UNREAL_PGO_DIR}/default.profdata")
                message(FATAL_ERROR "UNREAL_PGO=USE: ${UNREAL_PGO_DIR}/default.profdata is missing (tools/pgo/train.py makes it)")
            endif ()
            # Functions the profile does not cover (the other applications, the build-time tools) are not errors
            set(_unrealPgoFlags "-fprofile-use=${UNREAL_PGO_DIR}/default.profdata" -Wno-profile-instr-unprofiled
                -Wno-profile-instr-out-of-date -Wno-profile-instr-missing)
        endif ()
    else ()
        message(FATAL_ERROR "UNREAL_PGO: no profile-guided optimisation for ${CMAKE_CXX_COMPILER_ID}")
    endif ()
    if (_unrealPgoFlags)
        foreach (_flag IN LISTS _unrealPgoFlags)
            add_compile_options("$<$<AND:$<CONFIG:Release>,${_unrealPgoLang}>:${_flag}>")
            add_link_options("$<$<CONFIG:Release>:${_flag}>")
        endforeach ()
    endif ()
    message(STATUS "PGO: ${UNREAL_PGO}, profile in ${UNREAL_PGO_DIR}")
endif ()

# The executable the training runs (unreal-qt). GCC and Clang need nothing more (the flags above cover every object);
# MSVC instruments and optimises at the target's own LTCG link
function(unreal_pgo_target target)
    if (NOT UNREAL_PGO OR NOT MSVC)
        return()
    endif ()
    if (UNREAL_PGO STREQUAL "GENERATE")
        target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:/GENPROFILE:PGD=${UNREAL_PGO_DIR}/${target}.pgd>")
    else ()
        target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:/USEPROFILE:PGD=${UNREAL_PGO_DIR}/${target}.pgd>")
    endif ()
endfunction()
