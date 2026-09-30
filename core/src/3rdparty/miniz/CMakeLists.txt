# miniz 3.1.2 (Rich Geldreich and contributors, MIT): zlib-compatible deflate /
# inflate in one C file. Vendored for the zlib streams of SZX snapshots (and
# later RZX); the core has no zlib otherwise. Source: the release amalgamation
# https://github.com/richgel999/miniz/releases/tag/3.1.2, unmodified.
cmake_minimum_required(VERSION 3.16)
project(miniz LANGUAGES C)

add_library(miniz STATIC miniz.c)
# Only the raw deflate / inflate API is used: no archive, stdio or time code
target_compile_definitions(miniz PUBLIC MINIZ_NO_ARCHIVE_APIS MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    target_compile_options(miniz PRIVATE /W0)
else()
    target_compile_options(miniz PRIVATE -w)
endif()
# Consumers include it as "3rdparty/miniz/miniz.h"
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
