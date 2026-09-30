# tools/verification/zxdlss - ZX DLSS verification tools.
#
# The algorithms live in the emulator core (core/src/emulator/video/zxdlss, the
# interface in algorithm.h); the tools link the core. Only the literal,
# unoptimized reference implementation (algo/src/mod_tpgw_ref.cpp, "mod-tpgw-ref")
# stays here: it exists to check the optimized one bit for bit, and registers
# itself from the tool binaries.
#
#   zxdlss-render  TTD file or clip -> algorithm -> video / exact RGB dump
#   zxdlss-bench   per-frame cost of an algorithm on a clip
#
# Built from the root with -DBUILD_ZXDLSS_TOOLS=ON (targets excluded from `all`):
#   ninja zxdlss-render zxdlss-bench

set(ZXDLSS_COMMON_LIBS)
if (APPLE)
    set(ZXDLSS_COMMON_LIBS pthread iconv)
elseif (UNIX)
    set(ZXDLSS_COMMON_LIBS pthread)
endif ()

# the emulator reads its machine configs and ROMs next to the binary: one copy
# step both tools depend on (two POST_BUILD copies of the same folders race)
add_custom_target(zxdlss-data
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/configs" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/configs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/rom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
    COMMENT "zxdlss tools: copying machine configs and ROMs")

foreach (tool zxdlss-render zxdlss-bench)
    if (tool STREQUAL "zxdlss-render")
        set(main tool/main.cpp)
    else ()
        set(main tool/bench.cpp)
    endif ()
    add_executable(${tool} EXCLUDE_FROM_ALL
        ${main}
        tool/frames.cpp
        tool/ttd_source.cpp
        algo/src/mod_tpgw_ref.cpp
    )
    target_compile_features(${tool} PRIVATE cxx_std_20)
    # TTD input replays sessions through the emulator core (which brings zstd)
    target_compile_definitions(${tool} PRIVATE ZXDLSS_WITH_CORE)
    target_include_directories(${tool} PRIVATE ${CMAKE_SOURCE_DIR}/core/src
        $<TARGET_PROPERTY:libzstd_static,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(${tool} PRIVATE core recording ${ZXDLSS_COMMON_LIBS})
    add_dependencies(${tool} zxdlss-data)
endforeach ()
