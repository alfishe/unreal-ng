# unreal-asm - cross-assembler source library: codecs for ZX Spectrum assembler source formats (decode and encode),
# format conversion, dialect conversion plugins, symbols. Standard C++ only.
# Design: docs/inprogress/2026-10-05-unreal-asm/ (in the unreal-ng repository).
#
# Embedded use: add_subdirectory(3rdparty/unreal-asm) and link `unreal-asm`. Standalone:
#   cmake -S . -B build -G Ninja -DUNREAL_ASM_TESTS=ON && ninja -C build && ./build/unreal-asm-tests
cmake_minimum_required(VERSION 3.16)
project(unreal-asm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tests follow the emulator's TESTS switch when embedded; standalone they are opt-in
if (DEFINED TESTS)
    set(UNREAL_ASM_TESTS_DEFAULT ${TESTS})
else()
    set(UNREAL_ASM_TESTS_DEFAULT OFF)
endif()
option(UNREAL_ASM_TESTS "Build unreal-asm-tests" ${UNREAL_ASM_TESTS_DEFAULT})
set(UNREAL_ASM_GOOGLETEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../lib/googletest" CACHE PATH "GoogleTest sources")

if (MSVC)
    set(UNREAL_ASM_WARNINGS /W4)
else()
    set(UNREAL_ASM_WARNINGS -Wall -Wextra -Wpedantic)
endif()

add_library(unreal-asm STATIC
    src/encoding/codepages.cpp
    src/encoding/codepagetables.cpp
    src/encoding/detectors.cpp
    src/core/document.cpp
    src/core/registry.cpp
    src/codecs/text/textcodec.cpp
    src/codecs/sjasmplus/sjasmpluscodec.cpp
    src/codecs/tasm/tasmtokens.cpp
    src/codecs/tasm/tasmcodec.cpp
    src/codecs/alasm/alasmtokens.cpp
    src/codecs/alasm/alasmcodec.cpp
    src/codecs/zxasm/zxasmcodec.cpp
    src/codecs/storm/stormcodec.cpp
    src/codecs/masm/masmcodec.cpp
    src/codecs/gens/genscodec.cpp
    src/codecs/zeus/zeuscodec.cpp
    src/codecs/xas/xascodec.cpp
    src/ir/expr.cpp
    src/core/dialects.cpp
    src/dialects/common/z80.cpp
    src/dialects/common/macros.cpp
    src/dialects/storm/stormfrontend.cpp
    src/dialects/zxasm/zxasmfrontend.cpp
    src/dialects/xas/xasfrontend.cpp
    src/dialects/tasm/tasmfrontend.cpp
    src/dialects/masm/masmfrontend.cpp
    src/dialects/alasm/alasmfrontend.cpp
    src/dialects/sjasmplus/sjasmplusbackend.cpp
    src/dialects/common/classicbackend.cpp
    src/dialects/pasmo/pasmobackend.cpp
    src/dialects/z88dk/z88dkbackend.cpp
    src/dialects/sjasmplus/sjasmplusfrontend.cpp
    src/dialects/zeus/zeusfrontend.cpp
    src/containers/trdos.cpp
    src/symbols/model/symbol.cpp
    src/symbols/model/symbolindex.cpp
    src/symbols/model/symbolstore.cpp
    src/symbols/io/json.cpp
    src/symbols/codecs/native/nativecodec.cpp
    src/symbols/codecs/text/textlines.cpp
    src/symbols/codecs/labelfiles/labelfilecodecs.cpp
    src/symbols/codecs/text/tokenizer.cpp
    src/symbols/codecs/crossasm/crossasmcodecs.cpp
    src/symbols/codecs/script/scriptcodecs.cpp
    src/symbols/model/namerules.cpp
    src/symbols/registry.cpp
    src/layout/z80size.cpp
    src/layout/layout.cpp
    src/symbols/fromsource/fromsource.cpp
)
target_include_directories(unreal-asm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(unreal-asm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(unreal-asm PRIVATE ${UNREAL_ASM_WARNINGS})

# Command-line tool: zxasm (decision D-12)
add_executable(zxasm tools/zxasm/zxasm.cpp)
target_link_libraries(zxasm PRIVATE unreal-asm)
target_compile_options(zxasm PRIVATE ${UNREAL_ASM_WARNINGS})

# Command-line tool: symconv, symbol files from any format to any format (symbols/tdd.md §10, S4)
add_executable(symconv tools/symconv/symconv.cpp)
target_link_libraries(symconv PRIVATE unreal-asm)
target_compile_options(symconv PRIVATE ${UNREAL_ASM_WARNINGS})

# Examples (decision D-14): small programs on the public API, built with the library
foreach(example detect-encoding decode-encode read-hobeta convert-version convert-dialect)
    string(REPLACE "-" "" exampleTarget "unrealasmexample${example}")
    add_executable(${exampleTarget} examples/${example}/main.cpp)
    target_link_libraries(${exampleTarget} PRIVATE unreal-asm)
    target_compile_options(${exampleTarget} PRIVATE ${UNREAL_ASM_WARNINGS})
endforeach()

set(UNREAL_ASM_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "unreal-asm source directory")
set(UNREAL_ASM_WARNINGS_LIST "${UNREAL_ASM_WARNINGS}" CACHE INTERNAL "unreal-asm warning options")

# unreal-asm-tests needs the gtest target. Embedded in unreal-ng it is created later by core/tests (with that
# directory's flags), so the test target is defined at the end of the top-level directory; standalone this file adds
# GoogleTest itself.
function(unrealasm_add_tests)
    if (NOT TARGET gtest OR TARGET unreal-asm-tests)
        return()
    endif()
    add_executable(unreal-asm-tests
        ${UNREAL_ASM_DIR}/tests/main.cpp
        ${UNREAL_ASM_DIR}/tests/codepages_test.cpp
        ${UNREAL_ASM_DIR}/tests/detectors_test.cpp
        ${UNREAL_ASM_DIR}/tests/document_test.cpp
        ${UNREAL_ASM_DIR}/tests/registry_test.cpp
        ${UNREAL_ASM_DIR}/tests/textcodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/sjasmpluscodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/tasmcodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/alasmcodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/zxasmcodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/stormcodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/masmcodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/genscodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/zeuscodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/xascodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/stormfrontend_test.cpp
        ${UNREAL_ASM_DIR}/tests/zxasmfrontend_test.cpp
        ${UNREAL_ASM_DIR}/tests/xasfrontend_test.cpp
        ${UNREAL_ASM_DIR}/tests/pasmobackend_test.cpp
        ${UNREAL_ASM_DIR}/tests/z88dkbackend_test.cpp
        ${UNREAL_ASM_DIR}/tests/trdos_test.cpp
        ${UNREAL_ASM_DIR}/tests/dialects_test.cpp
        ${UNREAL_ASM_DIR}/tests/tasmfrontend_test.cpp
        ${UNREAL_ASM_DIR}/tests/masmfrontend_test.cpp
        ${UNREAL_ASM_DIR}/tests/zeusfrontend_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/symbol_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/symbolindex_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/symbolstore_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/json_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/nativecodec_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/labelfilecodecs_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/tokenizer_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/crossasmcodecs_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/namerules_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/scriptcodecs_test.cpp
        ${UNREAL_ASM_DIR}/tests/symbols/fromsource_test.cpp
    )
    target_link_libraries(unreal-asm-tests PRIVATE unreal-asm gtest)
    target_include_directories(unreal-asm-tests PRIVATE ${UNREAL_ASM_DIR}/src ${UNREAL_ASM_DIR}/tests)
    target_compile_definitions(unreal-asm-tests PRIVATE UNREAL_ASM_TESTDATA_DIR="${UNREAL_ASM_DIR}/testdata")
    target_compile_options(unreal-asm-tests PRIVATE ${UNREAL_ASM_WARNINGS_LIST})
endfunction()

if (UNREAL_ASM_TESTS)
    if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        if (NOT TARGET gtest)
            set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
            add_subdirectory(${UNREAL_ASM_GOOGLETEST_DIR} ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
        endif()
        unrealasm_add_tests()
        enable_testing()
        add_test(NAME unreal-asm-tests COMMAND unreal-asm-tests)
    elseif (TARGET gtest)
        unrealasm_add_tests()
    else()
        cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL unrealasm_add_tests)
    endif()
endif()
