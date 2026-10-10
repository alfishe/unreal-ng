# Runtime data (ROMs, symbols, configs, fonts, MIDI bank, boot) next to the executables.
# ONE target does the copy and every app depends on it: per-app POST_BUILD copies of the
# same folders into the shared bin/ run in parallel under ninja and race each other
# (copy_directory rewrites files another copy is reading) - an intermittent
# "Error copying file" that a rebuild hides.
# Included from core/src, which both the root build and the standalone unreal-qt build reach.
if (TARGET runtime-data)
    return()
endif ()

get_filename_component(_runtime_data_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_runtime_data "${_runtime_data_root}/data")
set(_runtime_bin "${CMAKE_BINARY_DIR}/bin")

add_custom_target(runtime-data
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_runtime_data}/fonts" "${_runtime_bin}/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_runtime_data}/configs" "${_runtime_bin}/configs"
    COMMAND ${CMAKE_COMMAND} -DDEST="${_runtime_bin}/configs" -P "${CMAKE_CURRENT_LIST_DIR}/ExtractConfigImages.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_runtime_data}/rom" "${_runtime_bin}/rom"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_runtime_data}/testrom" "${_runtime_bin}/rom"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_runtime_data}/symbols" "${_runtime_bin}/symbols"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_runtime_data}/midi" "${_runtime_bin}/midi"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_runtime_data}/boot" "${_runtime_bin}/boot"
    COMMENT "Copying runtime resource files to output directory"
)
