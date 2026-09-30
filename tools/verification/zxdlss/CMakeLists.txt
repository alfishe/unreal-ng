# tools/verification/zxdlss - ZX DLSS algorithm module and tools.
#
#   zxdlss_algo    de-flicker algorithms behind one interface (algo/include/zxdlss/algorithm.h);
#                  no emulator dependency. OBJECT library: every algorithm registers itself
#                  from its own translation unit, which a static archive would drop.
#   zxdlss-render  TTD file or clip -> algorithm -> video / exact RGB dump
#   zxdlss-bench   per-frame cost of an algorithm on a clip
#
# Built from the root with -DBUILD_ZXDLSS_TOOLS=ON (targets excluded from `all`):
#   ninja zxdlss-render zxdlss-bench

add_library(zxdlss_algo OBJECT
    algo/src/palette.cpp
    algo/src/registry.cpp
    algo/src/mod_tpgw.cpp
    algo/src/mod_tpgw_ref.cpp
)
target_include_directories(zxdlss_algo PUBLIC algo/include PRIVATE algo/src)
target_compile_features(zxdlss_algo PUBLIC cxx_std_20)

set(ZXDLSS_COMMON_LIBS)
if (APPLE)
    set(ZXDLSS_COMMON_LIBS pthread iconv)
elseif (UNIX)
    set(ZXDLSS_COMMON_LIBS pthread)
endif ()

add_executable(zxdlss-render EXCLUDE_FROM_ALL
    tool/main.cpp
    tool/frames.cpp
    tool/ttd_source.cpp
    $<TARGET_OBJECTS:zxdlss_algo>
)
target_include_directories(zxdlss-render PRIVATE algo/include)
target_link_libraries(zxdlss-render PRIVATE ${ZXDLSS_COMMON_LIBS})

if (TARGET core)
    # TTD input replays sessions through the emulator core (which brings zstd)
    target_compile_definitions(zxdlss-render PRIVATE ZXDLSS_WITH_CORE)
    target_include_directories(zxdlss-render PRIVATE ${CMAKE_SOURCE_DIR}/core/src
        $<TARGET_PROPERTY:libzstd_static,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(zxdlss-render PRIVATE core recording)
    # the emulator reads its machine configs and ROMs next to the binary
    add_custom_command(TARGET zxdlss-render POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/configs" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/configs"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/rom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
        COMMENT "zxdlss-render: copying machine configs and ROMs")
else ()
    target_link_libraries(zxdlss-render PRIVATE libzstd_static)
endif ()

add_executable(zxdlss-bench EXCLUDE_FROM_ALL
    tool/bench.cpp
    tool/frames.cpp
    tool/ttd_source.cpp
    $<TARGET_OBJECTS:zxdlss_algo>
)
target_include_directories(zxdlss-bench PRIVATE algo/include)
target_link_libraries(zxdlss-bench PRIVATE libzstd_static ${ZXDLSS_COMMON_LIBS})
