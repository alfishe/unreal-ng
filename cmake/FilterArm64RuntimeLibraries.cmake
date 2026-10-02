# An ARM64 redistributable directory can also contain x64 emulation helpers
# (for example vcruntime140_1.dll). A native ARM64 package needs only ARM64 DLLs.
function(unrealng_filter_arm64_runtime_libraries libraries_var)
    set(_native_libraries)
    foreach(_library IN LISTS ${libraries_var})
        # e_lfanew is the little-endian offset of the PE signature. The machine
        # field follows that four-byte signature; IMAGE_FILE_MACHINE_ARM64=AA64.
        file(READ "${_library}" _pe_offset_hex OFFSET 60 LIMIT 4 HEX)
        string(REGEX REPLACE "^(..)(..)(..)(..)$" "0x\\4\\3\\2\\1" _pe_offset "${_pe_offset_hex}")
        math(EXPR _machine_offset "${_pe_offset} + 4")
        file(READ "${_library}" _machine OFFSET ${_machine_offset} LIMIT 2 HEX)
        if(_machine STREQUAL "64aa")
            list(APPEND _native_libraries "${_library}")
        else()
            message(STATUS "Skipping non-ARM64 compiler runtime: ${_library} (PE machine bytes ${_machine})")
        endif()
    endforeach()
    set(${libraries_var} "${_native_libraries}" PARENT_SCOPE)
endfunction()
