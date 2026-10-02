# 04: the op list (common/eve-snap.h) checked on the CPU and evaluated by a Metal kernel.
add_executable(snap-check ${CMAKE_CURRENT_LIST_DIR}/snap-check.cpp)
target_include_directories(snap-check PRIVATE ${EVE_COMMON})
target_link_libraries(snap-check PRIVATE eve-lib-base)

if(APPLE)
    enable_language(OBJCXX)
    add_executable(metal-render ${CMAKE_CURRENT_LIST_DIR}/metal-render.mm)
    target_include_directories(metal-render PRIVATE ${EVE_COMMON})
    target_compile_options(metal-render PRIVATE -fobjc-arc)
    target_compile_definitions(metal-render PRIVATE
        EVE_METAL_SOURCE="${CMAKE_CURRENT_LIST_DIR}/eve-ops.metal" EVE_CORE_SOURCE="${EVE_COMMON}/eve-ops-core.h")
    target_link_libraries(metal-render PRIVATE eve-lib-base "-framework Metal" "-framework Foundation")

    # 04c: eve-emu (the 03 overlay) with the Metal backend for batches of lines
    # (EVE_POC_GPU_MIN lines and more; EVE_POC_GPU=0 turns it off)
    eve_variant(gpu OVERLAY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/03-line-parallel-cpu/overlay
                EXTRA ${CMAKE_CURRENT_LIST_DIR}/eve-gpu-metal.mm)
    target_compile_definitions(eve-lib-gpu PRIVATE EVE_METAL_SOURCE="${CMAKE_CURRENT_LIST_DIR}/eve-ops.metal"
                               EVE_CORE_SOURCE="${EVE_COMMON}/eve-ops-core.h")
    set_source_files_properties(${CMAKE_BINARY_DIR}/src-gpu/eve-gpu-metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(eve-lib-gpu PUBLIC "-framework Metal" "-framework Foundation")
    # The backend registers itself from a static object: keep that object in the executable
    target_link_options(eve-replay-gpu PRIVATE "LINKER:-force_load,$<TARGET_FILE:eve-lib-gpu>")
endif()
