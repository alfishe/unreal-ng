# Checks that a static archive defines only Eve* symbols (C API), EveLib:: / EveChip::
# symbols (C++ internals) and inline instantiations of the standard library. Anything
# else - in particular a vendored decoder's symbol - fails.
#
#   cmake -DNM=<nm> -DARCHIVE=<libeve-emu.a> -P CheckSymbols.cmake

execute_process(COMMAND ${NM} -g -C --defined-only ${ARCHIVE}
                OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "nm failed (${rc}): ${err}")
endif()

string(REPLACE "\n" ";" lines "${out}")
set(bad)
set(count 0)
foreach(line ${lines})
    # "<address> <type> <name>"; archive member headers and blank lines have no type.
    if(NOT line MATCHES "^[0-9a-fA-F]* *[A-Za-z] (.+)$")
        continue()
    endif()
    set(name "${CMAKE_MATCH_1}")
    math(EXPR count "${count} + 1")
    # Mach-O prefixes C names with '_'.
    string(REGEX REPLACE "^_" "" plain "${name}")
    if(plain MATCHES "^Eve[A-Z]" OR name MATCHES "^Eve[A-Z]")
        continue()
    endif()
    # Inline functions and templates of the standard library instantiated for our types.
    if(name MATCHES "(^| |<)std::")
        continue()
    endif()
    # Compiler-generated companions of our own symbols.
    if(name MATCHES "^(vtable|typeinfo|typeinfo name|guard variable|thread-local wrapper routine|TLS init function) for Eve")
        continue()
    endif()
    # MinGW reference pointers to imported data (".refptr.<symbol>").
    if(name MATCHES "^\\.refptr\\.")
        continue()
    endif()
    list(APPEND bad "${name}")
endforeach()

if(bad)
    list(JOIN bad "\n  " text)
    message(FATAL_ERROR "eve-emu exports foreign symbols:\n  ${text}")
endif()
message(STATUS "eve-emu symbol check: ${count} symbols, all Eve* / EveLib::")
