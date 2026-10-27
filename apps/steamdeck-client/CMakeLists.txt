# apps/steamdeck-client - Steam Deck client experiments (see README.md).
# Enabled from the root with -DBUILD_STEAMDECK_POC=ON; build.sh does it inside the steamrt4 SDK container.

find_package(SDL3 3.2 CONFIG REQUIRED)

# One aggregate target: build.sh builds it when no target is named
add_custom_target(steamdeck-poc-all)

# Shared helpers: timing log (SDL3 only), emulator host (links the core)
add_library(steamdeck-poc-timing STATIC common/timinglog.cpp)
target_include_directories(steamdeck-poc-timing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(steamdeck-poc-timing PUBLIC SDL3::SDL3)

add_library(steamdeck-poc-common STATIC common/emuhost.cpp common/deckcontrollers.cpp)
target_include_directories(steamdeck-poc-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/core/src
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/src/common
    ${CMAKE_SOURCE_DIR}/core/src/emulator
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(steamdeck-poc-common PUBLIC unrealng::core steamdeck-poc-timing)

# Automation (WebAPI on 0.0.0.0:8090, MCP on :8092; the CLI on loopback): the emulator on the Deck is driven over
# the network. Plain HTTP, no TLS. The definitions must match core/automation/CMakeLists.txt, as in unreal-qt
if (ENABLE_AUTOMATION)
    target_link_libraries(steamdeck-poc-common PUBLIC unrealng::automation)
    target_compile_definitions(steamdeck-poc-common PUBLIC ENABLE_AUTOMATION
        $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:ENABLE_LUA_AUTOMATION>
        $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:ENABLE_PYTHON_AUTOMATION>
        $<$<BOOL:${ENABLE_WEBAPI_AUTOMATION}>:ENABLE_WEBAPI_AUTOMATION>
        $<$<BOOL:${ENABLE_CLI_AUTOMATION}>:ENABLE_CLI_AUTOMATION>
        $<$<BOOL:${ENABLE_GDB_AUTOMATION}>:ENABLE_GDB_AUTOMATION>
        $<$<BOOL:${ENABLE_DEZOG_AUTOMATION}>:ENABLE_DEZOG_AUTOMATION>
        $<$<BOOL:${ENABLE_ZESARUX_AUTOMATION}>:ENABLE_ZESARUX_AUTOMATION>
        $<$<BOOL:${ENABLE_MCP_AUTOMATION}>:ENABLE_MCP_AUTOMATION>
    )
endif()

# Stage the model configs and ROMs next to the binaries (Config::LoadConfig looks there first)
add_custom_target(steamdeck-poc-data
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/configs" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/configs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/rom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/testrom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
)
add_dependencies(steamdeck-poc-data runtime-data)   # never copy the same folders at the same time
add_dependencies(steamdeck-poc-all steamdeck-poc-data)

# Compile GLSL to SPIR-V and embed it as a C array header (<name>.spv.h, symbol <name>_spv)
find_program(GLSLANG glslangValidator)   # needed from P-07 on (shaders)
function(steamdeck_poc_shaders target)
    if (NOT GLSLANG)
        message(FATAL_ERROR "glslangValidator not found: needed to compile the shaders of ${target}")
    endif()
    set(outdir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${outdir})
    set(headers)
    foreach(src ${ARGN})
        get_filename_component(name ${src} NAME)
        string(REPLACE "." "_" sym ${name})
        set(spv ${outdir}/${name}.spv)
        set(hdr ${outdir}/${name}.spv.h)
        add_custom_command(OUTPUT ${hdr}
            COMMAND ${GLSLANG} -V --quiet -o ${spv} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            COMMAND ${CMAKE_COMMAND} -DIN=${spv} -DOUT=${hdr} -DSYM=${sym}_spv
                    -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/common/embed.cmake
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/common/embed.cmake
            VERBATIM)
        list(APPEND headers ${hdr})
    endforeach()
    add_custom_target(${target}-shaders DEPENDS ${headers})
    add_dependencies(${target} ${target}-shaders)
    target_include_directories(${target} PRIVATE ${outdir})
endfunction()

# steamdeck_poc(<target> CORE|NOCORE <sources...>): NOCORE experiments link SDL3 and the timing log only
function(steamdeck_poc name kind)
    add_executable(${name} ${ARGN})
    if (kind STREQUAL "CORE")
        target_link_libraries(${name} PRIVATE steamdeck-poc-common)
    else()
        target_link_libraries(${name} PRIVATE steamdeck-poc-timing)
    endif()
    add_dependencies(steamdeck-poc-all ${name})
endfunction()

add_subdirectory(p01-sdl-gpu-latency)
add_subdirectory(p03-vblank-pacing)
add_subdirectory(p08-input-inspector)
# systemd-logind over D-Bus: Linux only
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(p18-logind-sleep)
endif()
