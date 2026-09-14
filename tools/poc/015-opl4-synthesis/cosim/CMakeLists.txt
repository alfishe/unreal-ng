#
# Co-simulation harnesses (§12.2/12.3 of the core TDD):
#   cosim-ymfm   differential vs ymfm's ymf278b (automated, pinned ref)
#   cosim-oracle golden self-oracle — pins libopl4's own bit stream
# Opt-in via -DCOSIM=ON from the PoC root; the differential harness needs
# the pinned reference sources, fetched once by ./fetch-refs.sh (refs/ is
# gitignored — no vendored third-party code enters the repo).
if (NOT TARGET opl4)
    message(FATAL_ERROR
        "cosim must be configured from the PoC root: "
        "cmake -S . -B build -G Ninja -DCOSIM=ON")
endif()

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/refs/ymfm/src/ymfm.h)
    message(FATAL_ERROR
        "cosim: ymfm reference sources missing — run cosim/fetch-refs.sh first")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# ymfm reference build: third-party code, warnings deliberately suppressed
# (our zero-warnings policy covers libopl4 and the harnesses, not refs).
# EXCLUDE_FROM_ALL: it is built only when a harness target needs it.
add_library(ymfm_ref STATIC EXCLUDE_FROM_ALL
    refs/ymfm/src/ymfm_opl.cpp
    refs/ymfm/src/ymfm_pcm.cpp
    refs/ymfm/src/ymfm_adpcm.cpp # y8950's adpcm_b engine, referenced by ymfm_opl
)
target_include_directories(ymfm_ref SYSTEM PUBLIC refs/ymfm/src)
if (MSVC)
    target_compile_options(ymfm_ref PRIVATE /w)
else()
    target_compile_options(ymfm_ref PRIVATE -w)
endif()

# Differential harness: libopl4 vs ymfm ymf278b, same SRAM + register scripts.
add_executable(cosim-ymfm cosim-ymfm.cpp)
target_link_libraries(cosim-ymfm PRIVATE opl4 ymfm_ref)
if (NOT MSVC)
    target_compile_options(cosim-ymfm PRIVATE -Wall -Wextra)
    target_link_libraries(cosim-ymfm PRIVATE m)
endif()

# Golden oracle: libopl4 only — builds without the reference sources.
add_executable(cosim-oracle cosim-oracle.cpp)
target_link_libraries(cosim-oracle PRIVATE opl4)
if (NOT MSVC)
    target_compile_options(cosim-oracle PRIVATE -Wall -Wextra)
    target_link_libraries(cosim-oracle PRIVATE m)
endif()
